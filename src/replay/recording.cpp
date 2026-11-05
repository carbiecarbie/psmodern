// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace psmodern::replay {
namespace {
bool equal(const FrameInput& a,const FrameInput& b) { return a.pads==b.pads && a.pause==b.pause && a.reset==b.reset; }
[[noreturn]] void bad(std::size_t line,const std::string& why) { throw std::runtime_error("Recording line "+std::to_string(line)+": "+why); }
bool digest(std::string_view s) {
    if (s.size()!=40) return false;
    for (char c:s) if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
    return true;
}
std::uint64_t number(std::string_view s,std::size_t line) {
    std::uint64_t value=0;
    if (s.empty() || (s.size()>1 && s[0]=='0')) bad(line,"invalid frame number");
    auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),value);
    if (ec!=std::errc{} || end!=s.data()+s.size()) bad(line,"invalid frame number");
    return value;
}
std::string_view field(std::string_view s,std::string_view prefix,std::size_t line) {
    if (!s.starts_with(prefix)) bad(line,"unknown or missing key");
    return s.substr(prefix.size());
}
Pad pad(std::string_view s,std::size_t line) {
    Pad p;
    if (s=="-") return p;
    int previous=-1;
    for (char c:s) {
        const auto at=std::string_view("UDLR12").find(c);
        if (at==std::string_view::npos || static_cast<int>(at)<=previous) bad(line,"unknown or duplicate button, or noncanonical order");
        previous=static_cast<int>(at);
        switch(c) { case 'U':p.up=true;break;case 'D':p.down=true;break;case 'L':p.left=true;break;
                    case 'R':p.right=true;break;case '1':p.one=true;break;case '2':p.two=true;break; }
    }
    if (s.empty()) bad(line,"empty pad");
    return p;
}
std::string pad_text(const Pad& p) {
    std::string s;
    if(p.up)s+='U';if(p.down)s+='D';if(p.left)s+='L';if(p.right)s+='R';if(p.one)s+='1';if(p.two)s+='2';
    return s.empty()?"-":s;
}
bool bit(std::string_view s,std::size_t line) {
    if(s=="0")return false;if(s=="1")return true;bad(line,"expected 0 or 1");
}
void validate(const Recording& r) {
    if (!digest(r.rom_sha1)||!digest(r.cart_ram_sha1)) bad(2,"invalid SHA-1");
    FrameInput prior{}; std::uint64_t last=0;bool first=true;
    for (std::size_t i=0;i<r.changes.size();++i) {
        const auto& c=r.changes[i];
        if(c.frame>=r.frames || (!first && c.frame<=last)) bad(i+4,"change frame out of order or after end");
        if(equal(c.input,prior)) bad(i+4,"unchanged input");
        prior=c.input;last=c.frame;first=false;
    }
}
}
FrameInput Recording::input_at(std::uint64_t frame) const {
    FrameInput result{};
    for(const auto& c:changes) {if(c.frame>frame)break;result=c.input;}
    return result;
}
Recording parse(const std::string& text) {
    if(text.find('\r')!=std::string::npos) {
        const auto pos=text.find('\r');bad(1+static_cast<std::size_t>(std::count(text.begin(),text.begin()+pos,'\n')),"CR character");
    }
    if(text.empty()||text.back()!='\n') bad(1+static_cast<std::size_t>(std::count(text.begin(),text.end(),'\n')),"missing LF");
    Recording r;std::size_t line=0,pos=0;bool ended=false;
    while(pos<text.size()) {
        const auto next=text.find('\n',pos);auto s=std::string_view(text).substr(pos,next-pos);++line;pos=next+1;
        if(line==1) {if(s!="psmodern-recording 1")bad(line,"wrong format or version");continue;}
        if(line==2) {r.rom_sha1=field(s,"rom ",line);if(!digest(r.rom_sha1))bad(line,"invalid ROM SHA-1");continue;}
        if(line==3) {r.cart_ram_sha1=field(s,"cart-ram ",line);if(!digest(r.cart_ram_sha1))bad(line,"invalid cartridge RAM SHA-1");continue;}
        if(ended)bad(line,"trailing junk after end");
        if(s.starts_with("end ")) {r.frames=number(s.substr(4),line);ended=true;continue;}
        if(!s.starts_with("input "))bad(line,"unknown key or missing end");
        auto rest=s.substr(6);auto split=rest.find(' ');
        if(split==std::string_view::npos)bad(line,"missing input fields");
        Change c{number(rest.substr(0,split),line),{}};rest=rest.substr(split+1);
        const auto p1=rest.find(' ');if(p1==std::string_view::npos)bad(line,"missing p2");
        c.input.pads[0]=pad(field(rest.substr(0,p1),"p1=",line),line);rest=rest.substr(p1+1);
        const auto p2=rest.find(' ');if(p2==std::string_view::npos)bad(line,"missing pause");
        c.input.pads[1]=pad(field(rest.substr(0,p2),"p2=",line),line);rest=rest.substr(p2+1);
        const auto p3=rest.find(' ');if(p3==std::string_view::npos)bad(line,"missing reset");
        c.input.pause=bit(field(rest.substr(0,p3),"pause=",line),line);
        c.input.reset=bit(field(rest.substr(p3+1),"reset=",line),line);
        if(!r.changes.empty() && c.frame<=r.changes.back().frame)bad(line,"frame numbers not strictly increasing");
        if(equal(c.input,r.changes.empty()?FrameInput{}:r.changes.back().input))bad(line,"unchanged input");
        r.changes.push_back(c);
    }
    if(line<3)bad(line+1,"missing header");
    if(!ended)bad(line+1,"missing end");
    validate(r);return r;
}
Recording read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot open recording: "+path.string());
    std::string s(std::istreambuf_iterator<char>(f),{});if(f.bad())throw std::runtime_error("Cannot read recording: "+path.string());
    return parse(s);
}
std::string write(const Recording& r) {
    validate(r);
    std::string s="psmodern-recording 1\nrom "+r.rom_sha1+"\ncart-ram "+r.cart_ram_sha1+"\n";
    for(const auto& c:r.changes)s+="input "+std::to_string(c.frame)+" p1="+pad_text(c.input.pads[0])+" p2="+pad_text(c.input.pads[1])+" pause="+(c.input.pause?"1":"0")+" reset="+(c.input.reset?"1":"0")+"\n";
    return s+"end "+std::to_string(r.frames)+"\n";
}
Recorder::Recorder(std::string rom,std::string ram) {recording_.rom_sha1=std::move(rom);recording_.cart_ram_sha1=std::move(ram);}
void Recorder::add(const FrameInput& input) {
    if(!equal(input,previous_)) {recording_.changes.push_back({recording_.frames,input});previous_=input;}
    ++recording_.frames;
}
void write_atomic_new(const std::filesystem::path& path,const Recording& r) {
    const auto data=write(r);
    if(std::filesystem::exists(path))throw std::runtime_error("Recording output already exists: "+path.string());
    const auto parent=path.has_parent_path()?path.parent_path():std::filesystem::current_path();
    if(!std::filesystem::is_directory(parent))throw std::runtime_error("Recording parent folder does not exist.");
#ifdef _WIN32
    auto temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::system_error(GetLastError(),std::system_category(),"Creating temporary recording");
    try {
        DWORD n=0;if(!WriteFile(h,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)||n!=data.size())throw std::system_error(GetLastError(),std::system_category(),"Writing recording");
        if(!FlushFileBuffers(h))throw std::system_error(GetLastError(),std::system_category(),"Flushing recording");
        if(!CloseHandle(h))throw std::system_error(GetLastError(),std::system_category(),"Closing recording");h=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))throw std::system_error(GetLastError(),std::system_category(),"Publishing recording");
    }catch(...){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);DeleteFileW(temp.c_str());throw;}
#else
    auto temp=path.string()+".tmp-XXXXXX";int fd=mkstemp(temp.data());
    if(fd<0)throw std::system_error(errno,std::generic_category(),"Creating temporary recording");
    try {std::size_t offset=0;while(offset<data.size()){auto n=::write(fd,data.data()+offset,data.size()-offset);if(n<=0)throw std::system_error(errno,std::generic_category(),"Writing recording");offset+=n;}
        if(fsync(fd))throw std::system_error(errno,std::generic_category(),"Flushing recording");if(close(fd))throw std::system_error(errno,std::generic_category(),"Closing recording");fd=-1;
        // link publishes without replacing an output created by a concurrent writer.
        if(link(temp.c_str(),path.c_str()))throw std::system_error(errno,std::generic_category(),"Publishing recording");
        unlink(temp.c_str());
    }catch(...){if(fd>=0)close(fd);unlink(temp.c_str());throw;}
#endif
}
}
