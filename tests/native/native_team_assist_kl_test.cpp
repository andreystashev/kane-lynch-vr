#include "native_team_assist_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}}
int main(int argc,char** argv){using namespace NativeTeamAssistKL;using namespace NativeHudFixture;
    if(argc>1){Image actual;Check(actual.Load(argv[1]),"load actual supported EXE read-only");Check(Verify(Base,actual.Reader()),"actual native picker/point/origin caller and24/5degree thresholds");
        actual.mapped[0x231EDB]^=1;Check(!Verify(Base,actual.Reader()),"different native origin call cannot authorize scoped hook");}
    Memory memory;const std::uint32_t player=Player,mover=0x15000000;
    memory.Set(Base+0x68760C,Data);memory.Set(Data+4,std::uint32_t(1));memory.Set(Data+0x2D88,std::uint32_t(0));
    memory.Set(player+0x9AC,mover);memory.Set(mover,std::array<unsigned char,MoverBytes>{});memory.Set(mover,std::uint32_t(Base+NativeCameraMoverKL::MouseVtable));
    memory.Set(mover+8,player);memory.Set(mover+0xC,std::uint32_t(0x16000000));
    memory.Set(Base+0x56AC7C,.418879032f);memory.Set(Base+0x56AC84,.0872664601f);memory.Set(Base+0x646288,625000000.f);
    Snapshot snapshot;const NativeCameraMath::Vec3 origin{10,20,30},forward{0,0,1};
    Check(Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"known native mover copied for original picker");
    Check(snapshot.angle==CommandCone&&snapshot.rangeSquared==625000000.f,"36 degree command cone preserves the native radius");
    Check(Current(snapshot,memory.Reader())&&snapshot.original!=snapshot.bytes,"patched private camera leaves entire native source unchanged");
    NativeCameraMath::Vec3 stored;std::memcpy(&stored,snapshot.bytes.data()+0x22C,sizeof(stored));Check(!std::memcmp(&stored,&origin,sizeof(stored)),"private picker origin follows actual rendered ray");
    std::memcpy(&stored,snapshot.bytes.data()+0x34,sizeof(stored));Check(!std::memcmp(&stored,&forward,sizeof(stored)),"private picker forward follows actual rendered ray");
    memory.Set(mover+0x9C,std::uint8_t(1));Check(!Current(snapshot,memory.Reader()),"native precision/context change invalidates old assist snapshot");
    Check(Prepare(Base,player,origin,forward,memory.Reader(),snapshot)&&snapshot.angle==PrecisionCommandCone,"precision command uses a narrower 18 degree cone");
    memory.Set(mover+0xC,std::uint32_t(0));Check(!Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"missing native camera controller rejects private picker");memory.Set(mover+0xC,std::uint32_t(0x16000000));
    memory.Set(Data+4,std::uint32_t(2049));Check(!Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"invalid native NPC array bound cannot enter picker");memory.Set(Data+4,std::uint32_t(1));
    memory.Set(Data+0x2D88,std::uint32_t(2049));Check(!Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"invalid native interactive array bound cannot enter picker");memory.Set(Data+0x2D88,std::uint32_t(0));
    memory.Set(mover+8,std::uint32_t(player+4));Check(!Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"foreign mover owner cannot borrow local command certificate");memory.Set(mover+8,player);
    memory.Set(mover,std::uint32_t(0));Check(!Prepare(Base,player,origin,forward,memory.Reader(),snapshot),"unknown mover class rejected");
    std::printf("Native team assist snapshot: %u checks PASS.\n",checks);return 0;
}
