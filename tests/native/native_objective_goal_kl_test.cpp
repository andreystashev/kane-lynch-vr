#include "native_objective_goal_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {using namespace NativeHudFixture;using namespace NativeObjectiveGoalKL;
constexpr std::uint32_t Manager=0x15000000,Entity=0x16000000,Geometry=0x17000000,Slot=Manager+4;
unsigned checks=0;void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
Memory Ready(){Memory m;m.Markers();m.Set(Data+0x1554,Manager);m.Set(Manager+0x1384,std::uint32_t(1));m.Set(Manager+0x12C4,Slot);m.Set(Slot,Record{});m.Set(Slot,Entity);m.Set(Slot+0x58,std::uint32_t(Type));m.Set(Slot+0x18,std::uint32_t(0));m.Set(Slot+0x2C,std::uint8_t(1));
    m.Set(Entity,Base+0x540000);m.Set(Base+0x540030,Base+0x2000);m.Set(Entity+4,Geometry);m.Set(Geometry,NativePlayerPose::Geometry{});m.Set(Geometry+0x60,Entity);return m;}
}
int main(int argc,char** argv){if(argc>1){Image exe;Check(exe.Load(argv[1]),"read supported EXE without executing it");Check(Verify(Base,exe.Reader()),"actual Objective26 atlas path, persistent slot list and native constructor/position/index boundaries verify");
    for(auto rva:{0x2704A0u,0x12F500u,0x254180u,0x26645Du,0x263440u,0x252F50u,0x253210u,0x647B70u,0x648004u,0x574CCCu}){exe.mapped[rva]^=1;Check(!Verify(Base,exe.Reader()),"changed native goal producer/resource proof rejects presentation");exe.mapped[rva]^=1;}}
    auto m=Ready();const auto before=m.bytes;auto good=Capture(Base,Player,0,m.Reader());Check(good.valid&&good.count==1&&good.goals[0].entity==Entity&&good.goals[0].slot==Slot,"persistent active native Objective slot is available independently of render-marker lifetime");
    m.Set(List+8,std::uint32_t(0));Check(Capture(Base,Player,0,m.Reader()).count==1,"faded/removed render records do not erase active mission goal availability");m=Ready();
    for(unsigned i=0;i<12;++i){auto bad=m;bool invalid=false;switch(i){case 0:bad.Set(Slot+0x58,std::uint32_t(18));break;case 1:bad.Set(Slot+0x58,std::uint32_t(19));break;case 2:bad.Set(Slot+0x2C,std::uint8_t(0));break;case 3:bad.Set(Slot,std::uint32_t(0));break;case 4:bad.Set(Slot+0x18,std::uint32_t(1));break;
        case 5:bad.Set(Manager+0x1384,std::uint32_t(49));invalid=true;break;case 6:bad.Set(Manager+0x12C4,std::uint32_t(Slot+4));invalid=true;break;case 7:bad.Set(Manager+0x1384,std::uint32_t(2));bad.Set(Manager+0x12C8,Slot);invalid=true;break;
        case 8:bad.Set(Geometry+0x60,std::uint32_t(Entity+4));invalid=true;break;case 9:bad.Set(Geometry+0x50,Geometry);invalid=true;break;case 10:bad.Set(Entity+4,std::uint32_t(0));invalid=true;break;default:bad.Set(Data+0xA2C,std::uint32_t(Player+4));invalid=true;break;}
        const auto result=Capture(Base,Player,0,bad.Reader());Check(invalid?!result.valid:result.valid&&!result.count,"only active owned mission objectives are eligible; Loot/Money/completed/foreign/corrupt sources never reappear");}
    m.Set(Slot+0x18,std::uint32_t(2));good=Capture(Base,Player,0,m.Reader());Check(good.count==1&&good.goals[0].filter==0xffffffffu,"native both-player filter is normalized in copy without mutating mission slot");
    m=Ready();Capture(Base,Player,0,m.Reader());Check(m.bytes==before,"availability query never changes mission, marker lifetime, map or actor state");
    unsigned reads=0;const auto stale=[&](auto at,void* out,auto n){if(at==Slot&&n==Stride&&++reads==2)return false;return m.Read(at,out,n);};Check(!Capture(Base,Player,0,stale).valid,"goal retirement during snapshot cancels a held presentation");
    Record authored{};const auto put=[&](unsigned at,const auto& value){std::memcpy(authored.data()+at,&value,sizeof(value));};
    put(0,Entity);put(0x1C,Type);put(0x44,std::uint32_t(0));put(4,NativeCameraMath::Vec3{100,95,300});put(0x3C,2.5f);put(0x50,75.f);
    Check(AttachedSource(authored,Entity,0),"exact native attached goal source retains authored height and scale");
    Check(!AttachedSource(authored,Entity+4,0)&&!AttachedSource(authored,Entity,1),"foreign lifetime owner or filter cannot provide another goal's authored style");
    authored[0x4D]=1;Check(!AttachedSource(authored,Entity,0),"fixed-point lifetime owner is not an attached mission-goal source");authored[0x4D]=0;
    put(0x3C,0.f);Check(!AttachedSource(authored,Entity,0),"invalid native scale cannot enter reusable goal style");
    std::printf("Native objective availability: %u checks PASS.\n",checks);
}
