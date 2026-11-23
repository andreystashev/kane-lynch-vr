#include "native_ledge_hint_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {using namespace NativeLedgeHintKL;using namespace NativeHudFixture;
constexpr std::uint32_t Move=0x16000000,Climb=0x17000000,Geometry=0x18000000,Dispatch=0x19000000,Backend=0x1A000000;
unsigned checks=0;void Check(bool okay,const char* text){++checks;if(!okay){std::printf("FAIL %s\n",text);std::exit(1);}}
Memory Ready(){Memory m;m.Set(Base+0x68760C,Data);m.Set(Data+0xA2C,Player);m.Set(Player,Base+0x56582C);m.Set(Player+0x67C,Move);m.Set(Move,Base+0x566A44);m.Set(Move+0x38,Player);m.Set(Move+0x44,std::uint32_t(3));m.Set(Player+0x604,Move);m.Set(Move+0x137C,std::uint8_t(0));
    m.Set(Player+0x194,Dispatch);m.Set(Dispatch,Base+0x54DE4C);m.Set(Dispatch+8,Player);m.Set(Dispatch+4,std::uint32_t(0));
    m.Set(Player+0x5F8+11*4,Climb);m.Set(Climb,std::array<unsigned char,ClimbBytes>{});m.Set(Climb,Base+0x566C44);m.Set(Climb+0x38,Player);m.Set(Climb+0x44,std::uint32_t(11));
    m.Set(Player+4,Geometry);m.Set(Geometry,NativePlayerPose::Geometry{});m.Set(Geometry+0x60,Player);m.Set(Player+0x5A8,NativeCameraMath::Vec3{});m.Set(Player+0x5C4,0.f);m.Set(Player+0x238,std::uint8_t(0));m.Set(Player+0xC68,std::uint32_t(0));m.Set(Player+0x132C,std::uint32_t(0));m.Set(Player+0x11E4,std::uint32_t(0));m.Set(Base+0x8883A4,std::uint16_t(0));
    m.Set(Base+0x7C1D34,Backend);m.Set(Backend,Base+0x541274);m.Set(Base+0x541274+0x2C,Base+0xC3B20);return m;}
}
int main(int argc,char** argv){if(argc>1){Image exe;Check(exe.Load(argv[1]),"read pinned EXE without running it");Check(Verify(Base,exe.Reader()),"native Climb allocation/query/getters and collision-image proof match actual executable");}
    auto m=Ready();const auto good=Capture(Base,m.Reader());Check(good.valid&&good.climb==Climb&&good.action.movement==Move,"capture exact registered Climb ID11 separate from current Stand");
    const auto before=m.bytes;auto scratch=good.native;scratch[0xE8]=1;Check(scratch[0xE8]==1&&m.bytes==before&&good.native[0xE8]==0,"input arm changes only private Climb copy");
    for(unsigned n=0;n<13;++n){auto bad=m;switch(n){case 0:bad.Set(Climb,Base+0x566A44);break;case 1:bad.Set(Climb+0x38,std::uint32_t(Player+4));break;case 2:bad.Set(Climb+0x44,std::uint32_t(3));break;case 3:bad.Set(Climb+0x54,std::uint8_t(1));break;
        case 4:bad.Set(Dispatch+4,Climb);break;case 5:bad.Set(Move,Base+0x566EAC);break;case 6:bad.Set(Player+0x238,std::uint8_t(1));break;case 7:bad.Set(Player+0xC68,std::uint32_t(1));break;case 8:bad.Set(Player+0x132C,std::uint32_t(1));break;
        case 9:bad.Set(Move+0x137C,std::uint8_t(1));break;case 10:bad.Set(Base+0x8883A4,std::uint8_t(3));break;case 11:bad.Set(Geometry+0x60,std::uint32_t(Player+4));break;default:bad.Set(Backend,Base+0x541274+4);break;}
        Check(!Capture(Base,bad.Reader()).valid,"foreign class/registry/actor/action/blocked/special movement cannot authorize a passive native query");}
    auto special=m;special.Set(Player+0x11E4,std::uint32_t(Climb+0x1000));special.Set(Climb+0x144C,std::uint16_t(11^0xDEAD));Check(!Capture(Base,special.Reader()).valid,"state11 excluded before native animation lookup branch");
    special.Set(Climb+0x144C,std::uint16_t(7^0xDEAD));Check(Capture(Base,special.Reader()).valid,"other readable authored state uses ordinary CanStart branch");
    unsigned count=0;const auto stale=[&](auto at,void* out,auto n){if(at==Climb&&n==ClimbBytes&&++count==2)return false;return m.Read(at,out,n);};Check(!Capture(Base,stale).valid,"retired registered Climb copy is rejected before native execution");
    std::printf("Passive native Climb source: %u checks PASS.\n",checks);
}
