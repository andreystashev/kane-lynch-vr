// Actual visibility adapter with private process memory only. No game,
// graphics device, input or runtime is created by this fixture.
#include "native_rope_kl.cpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
namespace {
unsigned checks=0;bool scene=true,firstPerson=true;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t address,const T& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
struct PE {
    struct Section {std::uint32_t size,rva,rawSize,raw;};
    std::vector<unsigned char> bytes;std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& value)const{if(at>bytes.size()||sizeof(value)>bytes.size()-at)return false;std::memcpy(&value,bytes.data()+at,sizeof(value));return true;}
    bool Load(const char* file){std::ifstream f(file,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);auto n=f.tellg();if(n<=0||n>64*1024*1024)return false;bytes.resize(std::size_t(n));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),n);std::uint32_t off=0,sig=0;std::uint16_t count=0,opt=0;if(!Get(0x3c,off)||!Get(off,sig)||sig!=0x4550||!Get(off+6,count)||!Get(off+20,opt)||count>96)return false;for(unsigned i=0;i<count;++i){Section s{};const auto at=off+24+opt+i*40;if(!Get(at+8,s.size)||!Get(at+12,s.rva)||!Get(at+16,s.rawSize)||!Get(at+20,s.raw))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t size)const{if(at<0x400000)return false;const auto rva=at-0x400000;for(auto s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize){const auto offset=s.raw+(rva-s.rva);if(size>s.rawSize-(rva-s.rva)||offset>bytes.size()||size>bytes.size()-offset)return false;std::memcpy(out,bytes.data()+offset,size);return true;}return false;}
};
}
namespace NativeCameraKL {
bool DrawStateActive(std::uintptr_t,bool* fps,bool){if(fps)*fps=firstPerson;return scene;}
}
int main(int argc,char** argv) {
    using namespace NativeRopeKL;
    constexpr std::uintptr_t base=0x28000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private low-address fixture allocation");
    image=base;installed=true;
    const std::uint32_t data=base+0x10000,player=base+0x20000,rope=base+0x30000,geometry=base+0x31000,child=base+0x32000,entry=base+0x40000,state=base+0x50000;
    Put(base+0x68760C,data);Put(data+0xA2C,player);Put(player,std::uint32_t(base+0x56582C));
    Put(data+0xACC,std::uint32_t(1));Put(data+0xAD0,rope);Put(rope,std::uint32_t(base+I::RopeVtable));Put(rope+4,geometry);Put(rope+0x7F4,player);Put(geometry+0x60,rope);Put(child+0x50,geometry);
    Put(entry,std::uint32_t(base+0x540FF8));Put(entry+0xF4,child);Put(base+0x7678A4,std::uint32_t(0));
    NativeCameraMath::CameraPose pose;pose.basis={{0,0,1},{0,1,0},{1,0,0}};pose.position={12,34,56};
    Put(entry+0x10,pose);Put(entry+0x40,pose);Put(state+0x8C,pose);
    const auto source=reinterpret_cast<const NativeCameraMath::CameraPose*>(entry+0x10);
    const auto bind=[&]{ObserveWorldBind(reinterpret_cast<void*>(state),source,0x72F6A);};
    const auto pool=I::Capture(base,player,Read);
    Check(pool.count==1&&pool.ropes[0].object==rope,"pool requires exact local native rope owner");
    Check(I::OwnEntry(base,entry,pool,Read),"deformed child geometry resolves to verified owned rope");
    SetFrame(player,1,true);bind();Check(SuppressDraw(),"owned rope draw is hidden in active first-person view");
    Check(SuppressDraw()&&Snapshot().multiPassSuppressed==1,"second material primitive in the same proven rope entry is also hidden");
    const std::uint32_t foreignEntry=base+0x41000;
    Put(foreignEntry,std::uint32_t(base+0x540FF8));Put(foreignEntry+0xF4,std::uint32_t(base+0x60000));Put(foreignEntry+0x10,pose);
    ObserveWorldBind(reinterpret_cast<void*>(state),reinterpret_cast<const NativeCameraMath::CameraPose*>(foreignEntry+0x10),0x72F6A);
    Check(!SuppressDraw(),"foreign entry bind retires rope scope even when its WORLD is byte-identical");
    bind();ObserveWorldBind(reinterpret_cast<void*>(state),nullptr,0x739E0);
    Check(!SuppressDraw()&&Snapshot().rigidBoundaries==1,"inline rigid entry boundary retires the prior material scope");
    bind();bool bounded=true;for(unsigned i=0;i<ScopeDrawLimit;++i)bounded=bounded&&SuppressDraw();
    Check(bounded&&!SuppressDraw()&&Snapshot().scopeBudgetRetired==1,"material scope is bounded without reauthorizing another primitive");
    Put(entry,std::uint32_t(base+0x541070));auto rigidWorld=pose;rigidWorld.position.x+=20;Put(state+0x8C,rigidWorld);
    ObserveWorldBind(reinterpret_cast<void*>(state),source,0x739E0);
    Check(SuppressDraw()&&Snapshot().rigidCandidates==1,"proven owned inline rigid child uses its derived WORLD rather than its entry root");
    Check(SuppressDraw(),"rigid child material passes retain the same exact owned entry scope");
    Put(entry,std::uint32_t(base+0x540FF8));Put(state+0x8C,pose);ObserveWorldBind(reinterpret_cast<void*>(state),source,0x739E0);
    Check(!SuppressDraw(),"post-rigid authorization requires the verified rigid entry class");
    bind();ClearFrame();Check(!SuppressDraw(),"VR overlay/capture boundary retires rope ownership");
    SetFrame(player,2,true);scene=false;bind();Check(!SuppressDraw(),"unknown view retains original rope");scene=true;
    firstPerson=false;bind();Check(!SuppressDraw(),"shadow or external view retains native rope");firstPerson=true;
    SetFrame(player,3,false);bind();Check(!SuppressDraw(),"cinema frame does not hide ropes");
    SetFrame(player,4,true);bind();auto changed=pose;changed.position.x+=1;Put(state+0x8C,changed);Check(!SuppressDraw(),"changed native WORLD cannot inherit rope draw tag");Put(state+0x8C,pose);
    bind();Put(rope+0x7F4,std::uint32_t(player+4));Check(!SuppressDraw(),"late ownership change prevents hiding another character rope");
    SetFrame(player,5,true);bind();Check(!SuppressDraw(),"foreign rope never becomes local by proximity");Put(rope+0x7F4,player);
    SetFrame(player,6,true);Put(entry+0xF4,std::uint32_t(base+0x60000));bind();Check(!SuppressDraw(),"recycled former rope entry revalidates geometry");Put(entry+0xF4,child);
    bool recovered=false;for(unsigned i=7;i<=14;++i){SetFrame(player,i,true);bind();recovered=recovered||SuppressDraw();}
    Check(recovered,"recycled entry ownership is rediscovered within a bounded frame interval");
    Put(base+0x7678A4,std::uint32_t(1));SetFrame(player,15,true);ObserveWorldBind(reinterpret_cast<void*>(state),reinterpret_cast<const NativeCameraMath::CameraPose*>(entry+0x40),0);Check(SuppressDraw(),"both native GPU indices identify the actual rope entry");
    Put(data+0xACC,std::uint32_t(9));SetFrame(player,16,true);bind();Check(!SuppressDraw(),"corrupt native pool count fails open");
    if(argc>1){PE pe;Check(pe.Load(argv[1]),"read supported EXE without opening game");Check(I::Verify(0x400000,[&](auto a,auto* o,auto n){return pe.Read(a,o,n);}),"pool/owner/rappel creation/WorldBind proof comes from actual PE");}
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("Native owned rope visibility: %u checks PASS; private CPU memory only.\n",checks);return 0;
}
