// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include "rom/sha1.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
using namespace psmodern::replay;
void check(bool ok) {if(!ok)throw std::runtime_error("Replay assertion failed");}
int main() {
    auto folder=std::filesystem::temp_directory_path()/("psmodern-replay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Recorder rec(std::string(40,'a'),sha1(Memory::CartridgeRam{}));
        FrameInput input{};rec.add(input);input.pads[0].one=true;input.pads[1].up=true;input.pause=true;input.reset=true;
        rec.add(input);rec.add(input);rec.add({});
        check(rec.recording().changes.size()==2&&rec.recording().frames==4);
        const auto text=write(rec.recording());check(write(parse(text))==text);
        check(parse(text).input_at(2).pads[1].up&&!parse(text).input_at(3).pause);
        auto refuse=[](std::string s){try{parse(s);}catch(const std::exception& e){check(std::string(e.what()).find("line ")!=std::string::npos);return;}throw std::runtime_error("Accepted invalid recording");};
        for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"psmodern-recording 1","psmodern-recording 2"},{"rom ","unknown "},{"p1=1","p1=Z"},{"input 3","input 1"},{"end 4","end 1"},{"pause=1","pause=2"},{"p2=U","p2=UU"}}){auto s=text;s.replace(s.find(pair.first),pair.first.size(),pair.second);refuse(s);}
        refuse("");refuse(text.substr(text.find('\n')+1));refuse(text.substr(0,text.rfind("end ")));
        refuse(text+"input 5 p1=1 p2=- pause=0 reset=0\n");refuse(text+"junk\n");refuse(text+"\n");
        refuse(text.substr(0,text.size()-1));refuse(text.substr(0,10)+"\r"+text.substr(10));
        auto s=text;s.replace(s.find("input 1"),7,"input 01");refuse(s);
        std::filesystem::create_directory(folder);auto path=folder/"test.psrec";
        write_atomic_new(path,rec.recording());check(write(read(path))==text);
        bool refused=false;try{write_atomic_new(path,rec.recording());}catch(...){refused=true;}check(refused&&write(read(path))==text);
        refused=false;try{write_atomic_new(folder/"absent"/"x",rec.recording());}catch(...){refused=true;}check(refused&&!std::filesystem::exists(folder/"absent"/"x"));
        std::filesystem::remove_all(folder);std::cout<<"Format refusals, canonical round trip, change detection and atomic publication passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
