#include "native_tutorial_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace NativeTutorialKL;using namespace NativeHudFixture;
unsigned checks=0;void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
constexpr std::uint32_t Object=0x15000000,Owner=0x16000000,Node=0x17000000,Text=0x18000000,Output=0x19000000,OutputVT=Base+0x540000;
void String(Memory& m,std::uintptr_t at,const char* value){std::array<char,192> text{};std::memcpy(text.data(),value,std::strlen(value)+1);m.Set(at,text);}
Memory Ready(unsigned index=0,bool full=true){Memory m;m.Set(Base+0x68760C,Data);m.Set(Data+0xA2C,Player);m.Set(Player,Base+0x56582C);m.Set(Data+0xA6C,std::int8_t(0));
    m.Set(Data+0xC7C,std::uint32_t(1));m.Set(Data+0xC80,Object);Monitor monitor;monitor.vtable=Base+MonitorVtable;monitor.enabled=1;monitor.outputs[Definitions[index].event]=Output;m.Set(Object,monitor);
    m.Set(Object+0x24,Owner);m.Set(Owner+4,Node);m.Set(Node+0x60,Owner);m.Set(Node+0x50,std::uint32_t(0));m.Set(Node+0x6C,Text);
    m.Set(Output,OutputVT);m.Set(OutputVT+0x14,std::uint32_t(Base+0x1200));m.Set(OutputVT+0x34,std::uint32_t(Base+0x1300));
    char name[192]{};if(full)std::strcpy(name,Prefix);std::strcat(name,Definitions[index].name);String(m,Text,name);return m;}
}
int main(int argc,char** argv){using namespace NativeTutorialKL;
    if(argc>1){Image exe;Check(exe.Load(argv[1]),"read pinned EXE without executing it");Check(Verify(Base,exe.Reader()),"actual monitor dispatch, dedup, player selection, geometry name and event table proof");
        for(auto at:{Dispatch,std::uintptr_t(0x2B8CA9),std::uintptr_t(0x5CF6F),std::uintptr_t(0x29DD7),std::uintptr_t(0x2B8FAD),MonitorVtable+0x68,std::uintptr_t(0x2B8DDC+12)}){
            exe.mapped[at]^=1;Check(!Verify(Base,exe.Reader()),"changed native script or identity bytes cannot enable tutorial assist");exe.mapped[at]^=1;}}
    for(unsigned i=0;i<StageCount;++i){auto m=Ready(i);Probe probe;const auto s=Snapshot(Base,Player,m.Reader(),&probe);
        Check(s.valid&&s.index==i&&s.event==Definitions[i].event&&probe.active==1&&probe.named==1,"each exact M02 Kane teaching monitor selects only its legitimate output event");
        Check(Same(s,Snapshot(Base,Player,m.Reader())),"unchanged exact native stage identity repeats");}
    auto memory=Ready();const auto stage=Snapshot(Base,Player,memory.Reader());
    for(unsigned i=0;i<18;++i){auto bad=memory;switch(i){case 0:bad.Set(Data+0xA2C,std::uint32_t(Player+4));break;case 1:bad.Set(Player,Base+0x552684);break;
        case 2:bad.Set(Data+0xA6C,std::int8_t(-1));break;case 3:bad.Set(Data+0xC7C,std::uint32_t(33));break;case 4:bad.Set(Object,Base+MonitorVtable+4);break;
        case 5:bad.Set(Object+0x34,std::uint8_t(0));break;case 6:bad.Set(Object+0x30,std::uint32_t(1));break;case 7:bad.Set(Object+0x38+3*4,std::uint32_t(0));break;
        case 8:bad.Set(Object+0x90+3,std::uint8_t(1));break;case 9:bad.Set(Node+0x60,std::uint32_t(Owner+4));break;
        case 10:String(bad,Text,"ROOT\\OtherMission!025_CoverPractise\\KaneMonitor_CoverStanding");break;
        case 11:String(bad,Text,"ROOT\\AI_Players!025_CoverPractise\\LynchMonitor_CoverStanding");break;
        case 12:bad.Set(Node+0x50,Node);break;case 13:bad.Set(Object+0x24,std::uint32_t(Owner+4));break;
        case 14:bad.Set(Output,std::uint32_t(0));break;case 15:bad.Set(OutputVT+0x14,std::uint32_t(0));break;
        case 16:bad.Set(Object+0x30,std::uint32_t(3));break;default:bad.bytes.erase(Data+0xC80);break;}
        Check(!Snapshot(Base,Player,bad.Reader()).valid,"foreign, completed, unlinked, unowned or corrupted script target cannot advance story");}
    auto hierarchy=Ready(0,false);constexpr std::uint32_t Cover=Node+0x1000,AI=Node+0x2000,Root=Node+0x3000;
    hierarchy.Set(Node+0x50,Cover);hierarchy.Set(Cover+0x50,AI);hierarchy.Set(AI+0x50,Root);hierarchy.Set(Root+0x50,std::uint32_t(0));
    hierarchy.Set(Cover+0x6C,std::uint32_t(Text+0x1000));hierarchy.Set(AI+0x6C,std::uint32_t(Text+0x2000));hierarchy.Set(Root+0x6C,std::uint32_t(Text+0x3000));
    String(hierarchy,Text+0x1000,"025_CoverPractise");String(hierarchy,Text+0x2000,"AI_Players");String(hierarchy,Text+0x3000,"ROOT");
    Check(Snapshot(Base,Player,hierarchy.Reader()).valid,"leaf instance name requires the exact authored parent hierarchy");
    String(hierarchy,Text+0x2000,"OtherScript");Check(!Snapshot(Base,Player,hierarchy.Reader()).valid,"same leaf in another script branch is excluded");
    auto both=memory;both.Set(Object+0x30,std::uint32_t(2));Check(Snapshot(Base,Player,both.Reader()).valid,"native both-player selection is still scoped to the proved local Kane slot");
    auto duplicate=memory;duplicate.Set(Data+0xC7C,std::uint32_t(2));duplicate.Set(Data+0xC84,Object);Probe ambiguous;
    Check(!Snapshot(Base,Player,duplicate.Reader(),&ambiguous).valid&&ambiguous.active==2,"ambiguous outstanding script targets never invent a demonstration order");
    unsigned reads=0;const auto retired=[&](auto at,void* out,auto bytes){if(at==Object&&bytes==sizeof(Monitor)&&++reads==2)return false;return memory.Read(at,out,bytes);};
    Check(!Snapshot(Base,Player,retired).valid,"monitor retirement during full graph read cancels continuation");
    Policy policy;Check(policy.Update(stage,1000,false,true)==Reason::None,"enabled stage arms at its own first active tick");
    for(unsigned now=1050;now<4000;now+=50)Check(policy.Update(stage,now,false,true)==Reason::None,"no tutorial timeout before three eligible seconds");
    Check(policy.Update(stage,4000,false,true)==Reason::Timeout&&policy.Update(stage,4050,true,true)==Reason::None,"three-second native demonstration fires once per outstanding stage");
    policy.Reset();Check(policy.Update(stage,2000,true,true)==Reason::Shot&&policy.Update(stage,2010,true,true)==Reason::None,"a real stage-bound shot advances promptly and cannot replay");
    policy.Reset();policy.Update(stage,1000,false,true);for(unsigned now=1050;now<=2500;now+=50)policy.Update(stage,now,false,true);
    Check(policy.Update(stage,2700,false,false)==Reason::None&&!policy.stage.valid,"pause, loading and scene suspension reset the teaching timer");
    Check(policy.Update(stage,30000,false,true)==Reason::None&&!policy.elapsed,"time spent in menus does not satisfy the demonstration");
    auto next=stage;++next.monitor;Check(policy.Update(next,30050,false,true)==Reason::None&&!policy.elapsed,"different script stage starts its own three seconds");
    Check(policy.Update(next,31000,false,true)==Reason::None&&!policy.elapsed,"a long missing simulation interval cannot manufacture elapsed gameplay");
    std::printf("M02 tutorial stage policy: %u checks PASS.\n",checks);
}
