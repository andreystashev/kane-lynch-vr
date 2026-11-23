#include "native_ledge_kl.hpp"
#include <cstdio>
#include <fstream>
#include <vector>

namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL: %s\n",message);}}
void Policy() {
    using namespace NativeActionKL;
    constexpr std::uintptr_t player=0x11000;
    State state;state.valid=true;state.player=player;state.movement=0x22000;
    state.movementKind=Movement::Stand;
    Check(NativeLedgeKL::EligiblePosture(state,player),"ordinary standing can receive hold-B Climb");
    state.movementKind=Movement::BaseMove;
    Check(NativeLedgeKL::EligiblePosture(state,player),"ordinary movement can receive hold-B Climb");
    Check(!NativeLedgeKL::EligiblePosture(state,player+4),"foreign actor cannot receive hold-B Climb");
    state.movementKind=Movement::Rappel;
    Check(!NativeLedgeKL::EligiblePosture(state,player),"rope movement owns its input");
    state.movementKind=Movement::Climb;
    Check(!NativeLedgeKL::EligiblePosture(state,player),"an ongoing climb owns its input");
    state.movementKind=Movement::BaseMove;state.action=0x33000;state.actionKind=Action::Reload;
    Check(!NativeLedgeKL::EligiblePosture(state,player),"a native action owns its input");
}
struct FileImage {
    std::vector<unsigned char> bytes;
    struct Section{std::uint32_t virtualSize,rva,rawSize,raw;};std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& out)const{if(at>bytes.size()||sizeof(T)>bytes.size()-at)return false;std::memcpy(&out,bytes.data()+at,sizeof(T));return true;}
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);auto size=f.tellg();
        if(size<=0||size>64*1024*1024)return false;bytes.resize(static_cast<std::size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),size);
        std::uint32_t pe=0,sig=0;std::uint16_t count=0,optional=0,magic=0;
        if(!f||!Get(0x3c,pe)||!Get(pe,sig)||sig!=0x4550||!Get(pe+6,count)||count>96||!Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10b)return false;
        for(unsigned i=0;i<count;++i){Section s{};if(!Get(pe+24+optional+40*i+8,s))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t count)const{if(at<0x400000)return false;auto rva=at-0x400000;
        for(const auto& s:sections){if(rva>=s.rva&&rva-s.rva<s.rawSize){auto o=rva-s.rva;
            if(count>s.rawSize-o||s.raw+o>bytes.size()||count>bytes.size()-s.raw-o)return false;
            std::memcpy(out,bytes.data()+s.raw+o,count);return true;}}
        return false;}
};
void Image(const char* path){FileImage image;Check(image.Load(path),"actual supported PE32 loads read-only");
    const auto read=[&](auto at,void* out,auto count){return image.Read(at,out,count);};
    Check(NativeLedgeKL::VerifyImage(0x400000,read),"actual image proves blocker gate, exact caller, TapUse pipeline and native class slot");
    for(std::uintptr_t rva:{0x1E8A90u,0x205E27u,0x205E71u,0x1E7F90u,0x1EF980u,0x238222u,0x1F1FEEu,0x23908Fu,0x2386C5u,
        0x566C7Cu,0x565DF0u,0x53EAD0u,0x646474u,0x56B6B0u,0x56BED0u}){
        const auto corrupt=[&](auto at,void* out,auto count){if(!read(at,out,count))return false;if(at==0x400000+rva)static_cast<unsigned char*>(out)[0]^=1;return true;};
        Check(!NativeLedgeKL::VerifyImage(0x400000,corrupt),"changed native ledge or action evidence cannot enable the hook");}
    Check(!NativeLedgeKL::VerifyImage(0x400000,[](auto,void*,auto){return false;}),"unreadable image cannot enable action assistance");
}
}
int main(int argc,char** argv){Policy();if(argc>1)Image(argv[1]);std::printf("Native ledge intent: %u checks, %u failures; no game/input opened.\n",checks,failures);return failures?1:0;}
