// SPDX-License-Identifier: GPL-3.0-or-later
#include "oracle/lockstep.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace psmodern::oracle {
namespace {
std::atomic<std::uint64_t> checks{0};
std::shared_ptr<const Memory::Rom> load_image(const char* path) {
    if (!path || !*path) return nullptr;
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error(std::string("Opcode check: cannot read the configured ROM ")+path);
    return std::make_shared<const Memory::Rom>(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
const std::vector<std::shared_ptr<const Memory::Rom>>& images() {
    static const auto list=[] {
        std::vector<std::shared_ptr<const Memory::Rom>> out;
#if defined(PSMODERN_CHECK_ROM_US) && defined(PSMODERN_CHECK_ROM_BR)
        for (const char* path:{PSMODERN_CHECK_ROM_US,PSMODERN_CHECK_ROM_BR}) if (auto image=load_image(path)) out.push_back(image);
#endif
        return out;
    }();
    return list;
}
std::string hex(unsigned value, int width) {
    std::ostringstream text; text<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(width)<<value; return text.str();
}
}
void check_opcode(const Machine& engine, std::uint16_t address, OpcodePattern pattern) {
    ++checks;
    const auto mapping=engine.memory().mapping(address);
    const auto where=hex(mapping.bank,2)+":"+hex(address,4);
    if (mapping.kind!=Memory::Kind::rom) throw std::runtime_error("Opcode check: rendered instruction at "+where+" is not in ROM");
    auto matches=[&](auto byte_at) {
        const auto second=static_cast<std::uint16_t>(address+1);
        if (pattern.prefix) return byte_at(address)==pattern.prefix && (byte_at(second)&pattern.mask)==pattern.value;
        return (byte_at(address)&pattern.mask)==pattern.value;
    };
    if (!matches([&](std::uint16_t a) { return engine.peek(a); }))
        throw std::runtime_error("Opcode check: the renderer at "+where+" declares opcode "+
            (pattern.prefix ? hex(pattern.prefix,2)+" " : std::string())+hex(pattern.value,2)+"/"+hex(pattern.mask,2)+
            ", which is not the instruction in the image");
    // The other game image is compared only when the engine runs one of the
    // two (an authored test program is checked against itself).
    static thread_local const void* last_image=nullptr;
    static thread_local bool game_image=false;
    if (engine.memory().image().get()!=last_image) {
        last_image=engine.memory().image().get();
        game_image=false;
        for (const auto& image:images()) game_image=game_image || *image==*engine.memory().image();
    }
    if (game_image) for (const auto& image:images()) {
        auto byte_at=[&](std::uint16_t a) -> std::uint8_t {
            const auto m=engine.memory().mapping(a);
            const std::size_t offset=static_cast<std::size_t>(m.bank)*0x4000+(a&0x3fff);
            return offset<image->size() ? (*image)[offset] : 0;
        };
        if (!matches(byte_at))
            throw std::runtime_error("Opcode check: the renderer at "+where+" does not match the instruction of the other game image");
    }
}
std::uint64_t opcode_checks() { return checks; }
Coverage& coverage() { static Coverage instance; return instance; }
// The instrumentation of one run: write and port traces on both machines
// and the engine's boundary observer, removed on every exit.
class Lockstep::Session {
public:
    Session(Lockstep& lockstep, Machine& engine)
        : engine_(engine), engine_ports(engine), oracle_ports(lockstep.oracle_),
          engine_writes(engine), oracle_writes(lockstep.oracle_) {
        observer=[this,&lockstep](const Machine::Boundary& boundary) {
            last=boundary;
            if (auto& c=coverage(); c.enabled) {
                const auto mapping=engine_.memory().mapping(boundary.pc);
                if (mapping.kind==Memory::Kind::rom) {
                    if (c.native.empty()) { c.native.resize(32*0x4000); c.rendered.resize(32*0x4000); }
                    const auto index=(mapping.bank&31)*0x4000u+(boundary.pc&0x3fffu);
                    (boundary.in_body ? c.rendered : c.native)[index]=1;
                }
            }
            if (lockstep.mode_==Mode::instructions || (boundary.depth==0 && !boundary.in_body))
                lockstep.compare(engine_,boundary,Scope::boundary,*this);
        };
        engine.set_boundary_observer(&observer);
        engine.set_opcode_check(&opcode_check);
    }
    ~Session() { engine_.set_boundary_observer(nullptr); engine_.set_opcode_check(nullptr); }
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    Machine& engine_;
    Console::PortTrace engine_ports, oracle_ports;
    Console::WriteTrace engine_writes, oracle_writes;
    Machine::BoundaryObserver observer;
    const Machine::OpcodeCheck opcode_check=check_opcode;
    Machine::Boundary last{};
};

Lockstep::Lockstep(const Machine& engine, std::string label, Mode mode)
    : oracle_(static_cast<const Console&>(engine)), label_(std::move(label)), mode_(mode) {
    if (engine.depth()) throw std::logic_error("Lockstep must start at a top-level boundary");
}
void Lockstep::fail(const Machine& engine, const Machine::Boundary& boundary, const std::string& what) const {
    std::ostringstream report;
    report<<"Lockstep divergence: recording "<<label_<<", frame "<<engine.cycles()/Machine::frame_cycles+1
          <<", cycle "<<engine.cycles()<<", segment "<<std::hex<<std::uppercase<<std::setfill('0')
          <<std::setw(2)<<segment_bank_<<':'<<std::setw(4)<<segment_pc_<<", instruction "
          <<std::setw(2)<<engine.memory().mapping(boundary.pc).bank<<':'<<std::setw(4)<<boundary.pc<<std::dec
          <<", depth "<<boundary.depth<<(boundary.in_body ? ", inside a body" : "")<<'\n'<<what;
    throw std::runtime_error(report.str());
}
void Lockstep::compare(Machine& engine, const Machine::Boundary& boundary, Scope scope, Session& session) {
    ++comparisons_;
    if (boundary.in_body) ++body_comparisons_;
    if (boundary.depth==0 && !boundary.in_body) {
        segment_pc_=boundary.pc;
        segment_bank_=engine.memory().mapping(boundary.pc).bank;
    }
    while (oracle_.cycles()<engine.cycles()) oracle_.step();
    // The engine has synced its hardware to this boundary (a step starts so).
    oracle_.settle();
    if (oracle_.cycles()!=engine.cycles() || oracle_.cpu().resume)
        fail(engine,boundary,"the oracle is not at an instruction boundary at this cycle (oracle cycle "+
             std::to_string(oracle_.cycles())+")\n");
    std::ostringstream report;
    auto& ew=session.engine_writes.writes(); auto& ow=session.oracle_writes.writes();
    if (ew!=ow) {
        const auto at=std::mismatch(ow.begin(),ow.end(),ew.begin(),ew.end());
        report<<"memory writes differ at write "<<(at.first-ow.begin()+1)<<" (oracle "<<ow.size()<<", engine "<<ew.size()<<")";
        if (at.first!=ow.end()) report<<std::hex<<", oracle address "<<at.first->address;
        if (at.second!=ew.end()) report<<std::hex<<", engine address "<<at.second->address;
        report<<std::dec<<'\n';
    }
    auto& ep=session.engine_ports.accesses(); auto& op=session.oracle_ports.accesses();
    if (ep!=op) {
        const auto at=std::mismatch(op.begin(),op.end(),ep.begin(),ep.end());
        report<<"port accesses differ at access "<<(at.first-op.begin()+1)<<" (oracle "<<op.size()<<", engine "<<ep.size()<<")\n";
    }
    if (!equal(engine,oracle_,scope))
        for (const auto& line:differences(engine,oracle_,scope)) report<<line<<'\n';
    if (scope==Scope::whole)
        for (const auto& line:queue_differences(engine,oracle_)) report<<line<<'\n';
    if (!report.str().empty()) fail(engine,boundary,report.str());
    ew.clear(); ow.clear(); ep.clear(); op.clear();
}
std::uint64_t Lockstep::run_frame(Machine& engine, const FrameInput& input, RoutineRegistry* routines,
                                  const FrameCallback& at_frame, const Machine::Hook& hook,
                                  const Machine::WriteObserver& writes) {
    Session session(*this,engine);
    oracle_.set_input(input);
    const Machine::FrameObserver frame=[&] {
        ++frame_comparisons_;
        compare(engine,session.last,Scope::whole,session);
        FrameSample sample{engine.take_psg_writes(),engine.take_samples()};
        (void)oracle_.take_psg_writes(); (void)oracle_.take_samples();
        if (at_frame) at_frame(engine,sample);
    };
    return engine.run_frame(input,routines,hook,writes,frame);
}
void Lockstep::synchronize(Machine& engine) {
    Session session(*this,engine);
    ++frame_comparisons_;
    compare(engine,{engine.cpu().pc.uint16_value,0,false,false},Scope::whole,session);
}
std::uint64_t Lockstep::step(Machine& engine, RoutineRegistry* routines, const Machine::Hook& hook) {
    Session session(*this,engine);
    const auto elapsed=engine.step(hook,routines);
    ++frame_comparisons_;
    compare(engine,{engine.cpu().pc.uint16_value,0,false,false},Scope::whole,session);
    return elapsed;
}
}
