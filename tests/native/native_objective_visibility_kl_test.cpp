#include "native_objective_visibility_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {using namespace NativeObjectiveVisibilityKL;using namespace NativeHudFixture;
unsigned checks=0;void Check(bool okay,const char* label){++checks;if(!okay){std::printf("FAIL %s\n",label);std::exit(1);}}
Memory Ready(unsigned type=26){Memory m;m.Markers();m.Set(Player+0x805,std::int8_t(0));m.Set(Records+0x1C,std::uint32_t(type));m.Set(Records+0x48,std::uint32_t(0xE055AA11));return m;}
}
int main(int argc,char** argv){
    if(argc>1){Image image;Check(image.Load(argv[1]),"read supported EXE without executing the game");Check(Verify(Base,image.Reader()),"exact native map thunk, mode2/name, marker caller, stride and objective resources verify");
        for(auto rva:{ColorRva,MapRequestRva,std::uintptr_t(0x253C36),std::uintptr_t(0x270490),std::uintptr_t(0x25418E),std::uintptr_t(0x647B70),std::uintptr_t(0x576DA8),std::uintptr_t(0x1E8950),std::uintptr_t(0x56582C+0x668)}){
            image.mapped[rva]^=1;Check(!Verify(Base,image.Reader()),"changed native objective/map proof fails closed");image.mapped[rva]^=1;}}
    Check(Fresh({1000,true,true},1250)&&!Fresh({1000,true,true},1251)&&!Fresh({1000,true,true},999)&&!Fresh({0,true,true},1)&&!Fresh({1000,false,true},1000),"context expires, cancels on scene stop and rejects reversed clocks");
    std::uint8_t alpha=0;for(unsigned type=0;type<32;++type){auto m=Ready(type);const bool accepted=GoalAlpha(Base,Records,Camera,Base+ColorCallerRva,m.Reader(),alpha);
        Check(accepted==(type==26),"only native standard Objective26 can receive held-X visibility");if(accepted)Check(alpha==0xE0,"authored base alpha retained");}
    auto good=Ready();Check(SuppressMap(Base,Base+0x888390,0,good.Reader()),"only exact local native map request is suppressed");
    Check(!SuppressMap(Base,Base+0x888390,1,good.Reader())&&!SuppressMap(Base,Base+0x888390+4,0,good.Reader())&&!SuppressMap(Base,Base+0x888390,-1,good.Reader()),"foreign input slot, manager and invalid slot retain native map");
    for(unsigned i=0;i<14;++i){auto m=Ready();std::uintptr_t record=Records,caller=Base+ColorCallerRva;
        switch(i){case 0:m.Set(Base+0x8883A4,std::uint8_t(1));break;case 1:m.Set(Base+0x8883A4,std::uint8_t(2));break;case 2:m.Set(Base+0x8883A4,std::uint8_t(3));break;
        case 3:m.Set(Base+0x8883A5,std::uint8_t(1));break;case 4:m.Set(Player,Base+0x56582C+4);break;case 5:m.Set(Player+0x804,std::int8_t(-1));break;
        case 6:m.Set(Player+0x805,std::int8_t(2));break;case 7:m.Set(List+8,std::uint32_t(257));break;case 8:m.Set(List+8,std::uint32_t(0));break;
        case 9:m.Set(Records+0x44,std::uint32_t(1));break;case 10:m.Set(Records+0x4B,std::uint8_t(0));break;case 11:record+=4;break;case 12:record+=0x64*3;break;default:++caller;break;}
        Check(!GoalAlpha(Base,record,Camera,caller,m.Reader(),alpha),"pause/map/inventory/foreign/hidden/out-of-list/wrong-producer records retain original visibility");}
    auto all=Ready();all.Set(Records+0x44,std::uint32_t(-1));Check(GoalAlpha(Base,Records,Camera,Base+ColorCallerRva,all.Reader(),alpha),"native all-players filter is retained");
    Check(!GoalAlpha(Base,Records,Camera+4,Base+ColorCallerRva,good.Reader(),alpha),"foreign display camera cannot force current player goals");
    auto bytes=good.bytes;Check(GoalAlpha(Base,Records,Camera,Base+ColorCallerRva,good.Reader(),alpha)&&good.bytes==bytes,"held objective capture never writes deadline, fade, position, type or entity fields");
    unsigned reads=0;const auto stale=[&](auto at,void* out,auto n){if(at==List+0x14&&++reads==2){const std::uint32_t moved=Records+0x64;std::memcpy(out,&moved,n);return true;}return good.Read(at,out,n);};
    Check(!GoalAlpha(Base,Records,Camera,Base+ColorCallerRva,stale,alpha),"native marker-list replacement between reads cancels force");
    std::printf("Objective visibility/map policy: %u checks PASS.\n",checks);
}
