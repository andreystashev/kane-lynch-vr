#include "native_action_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
constexpr std::uintptr_t Image=0x400000,Player=0x11000000,Dispatcher=0x12000000,
    Action=0x13000000,Task=0x14000000,Dictionary=0x15000000;
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok){std::printf("FAIL %s\n",message);std::exit(1);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t trigger=0,target=0;unsigned triggerOccurrence=1,seen=0,reads=0;std::uint32_t replacement=0;
    template<class T>void Put(std::uintptr_t at,const T& value){const auto* data=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=data[i];}
    void String(std::uintptr_t at,const char* text){for(unsigned i=0;text[i];++i)bytes[at+i]=static_cast<unsigned char>(text[i]);}
    Memory(){
        Put(Action,std::uint32_t(Image+0x5667F4));Put(Action+8,std::uint32_t(Player));Put(Action+0x14,std::uint32_t(Task));
        Put(Dispatcher+4,std::uint32_t(Action));Put(Task+0x24,std::uint16_t(0x39b));Put(Player+0x80,std::uint32_t(Dictionary));
        Put(Dictionary,std::uint32_t(3));
        // Sorted dictionary, independently packed strings and nonordinal IDs.
        for(unsigned i=0;i<200;++i)bytes[Dictionary+28+i]=0;
        Put(Dictionary+4,std::uint32_t(0));Put(Dictionary+8,std::uint32_t(4));
        Put(Dictionary+12,std::uint32_t(48));Put(Dictionary+16,std::uint32_t(0x39b));
        Put(Dictionary+20,std::uint32_t(104));Put(Dictionary+24,std::uint32_t(7));
        String(Dictionary+28,"/Ambient");String(Dictionary+28+48,NativeBlastActionKL::Name);String(Dictionary+28+104,"/Movement");
    }
    bool Read(std::uintptr_t at,void* output,std::size_t n){
        ++reads;
        for(std::size_t i=0;i<n;++i){auto it=bytes.find(at+i);if(it==bytes.end())return false;static_cast<unsigned char*>(output)[i]=it->second;}
        if(at==trigger&&++seen==triggerOccurrence){trigger=0;Put(target,replacement);}
        return true;
    }
    bool Match(std::uint32_t tick=0){return NativeBlastActionKL::Matches(Image,Player,Dispatcher,Action,[this](auto at,auto* out,auto size){return Read(at,out,size);},tick);}
};
}
int main(){
    Memory good;Check(good.Match(),"exact named MiniAct and active local task are recognized");
    Memory otherGroup;otherGroup.Put(Task+0x24,std::uint16_t(12));Check(!otherGroup.Match(),"another animation task is not the blast clip");
    Memory otherName;otherName.bytes[Dictionary+28+48+1]='X';Check(!otherName.Match(),"a changed dictionary name is rejected");
    Memory wrongId;wrongId.Put(Dictionary+16,std::uint32_t(0x1039b));Check(!wrongId.Match(),"high-bit false match is rejected");
    Memory foreign;foreign.Put(Action+8,std::uint32_t(Player+4));Check(!foreign.Match(),"foreign action owner rejected");
    Memory stale;stale.Put(Dispatcher+4,std::uint32_t(Action+4));Check(!stale.Match(),"replaced dispatcher action rejected");
    Memory wrongClass;wrongClass.Put(Action,std::uint32_t(Image+0x566834));Check(!wrongClass.Match(),"MiniAnim cannot impersonate the verified MiniAct");
    Memory missing;missing.bytes.erase(Dictionary+28+48);Check(!missing.Match(),"unreadable name cannot authorize scene");
    Memory excessive;excessive.Put(Dictionary,std::uint32_t(8193));Check(!excessive.Match(),"unbounded dictionary rejected");
    Memory wrap;wrap.Put(Player+0x80,std::uint32_t(0xfffffffcu));Check(!wrap.Match(),"overflowing dictionary rejected");
    Memory dynamic;dynamic.Put(Task+0x24,std::uint16_t(741));dynamic.Put(Dictionary+16,std::uint32_t(741));
    Check(dynamic.Match(),"the dictionary supplies its current index instead of requiring ordinal 0x39b");
    Memory production;constexpr std::uintptr_t Data=0x16000000,Move=0x17000000;
    production.Put(Image+0x68760C,std::uint32_t(Data));production.Put(Data+0xA2C,std::uint32_t(Player));
    production.Put(Player,std::uint32_t(Image+0x56582C));production.Put(Player+0x67C,std::uint32_t(Move));
    production.Put(Move,std::uint32_t(Image+0x566A44));production.Put(Move+0x38,std::uint32_t(Player));
    production.Put(Move+0x44,std::uint32_t(2));production.Put(Player+0x600,std::uint32_t(Move));
    production.Put(Player+0x194,std::uint32_t(Dispatcher));production.Put(Dispatcher,std::uint32_t(Image+0x54DE4C));
    production.Put(Dispatcher+8,std::uint32_t(Player));
    const auto snapshot=[&]{return NativeActionKL::Snapshot(Image,[&](auto at,auto* out,auto size){return production.Read(at,out,size);});};
    const auto certified=snapshot();Check(certified.valid&&certified.explosionCover&&certified.BodyAlignedInteraction(),
        "production local-action snapshot passes the exact certificate into body alignment");
    production.Put(Move,std::uint32_t(Image+0x566D14));const auto vehicle=snapshot();
    Check(vehicle.valid&&!vehicle.explosionCover&&!vehicle.BodyAlignedInteraction(),"the exact same task cannot alter a passenger seat");
    Memory absent;absent.bytes[Dictionary+28+48+1]='X';
    Check(!absent.Match(100001),"dictionary without the exact name receives a negative result");
    const auto absentFirst=absent.reads;Check(!absent.Match(100001),"same-tick same-identity negative cache fails closed");
    Check(absent.reads-absentFirst<absentFirst,"repeated unrelated MiniAct does fewer reads within the tick");
    absent.bytes[Dictionary+28+48+1]='L';
    Check(!absent.Match(100001),"in-place scene change in the same tick cannot grant a false positive");
    Check(absent.Match(100002),"next tick rechecks changed dictionary and recognizes the exact animation");
    const auto positiveReads=absent.reads;Check(absent.Match(100002)&&absent.reads-positiveReads>0,
        "positive cache re-reads the current dictionary slot and ownership rather than trusting the address");
    absent.Put(Task+0x24,std::uint16_t(77));Check(!absent.Match(100002),"changed task group retires a cached positive immediately");
    absent.Put(Task+0x24,std::uint16_t(0x39b));absent.Put(Dictionary+16,std::uint32_t(77));
    Check(!absent.Match(100002),"changed matching slot id invalidates the positive cache immediately");
    Memory changedTask;changedTask.bytes[Dictionary+28+48+1]='X';
    Check(!changedTask.Match(100003),"negative-cache identity fixture");
    changedTask.bytes[Dictionary+28+48+1]='L';changedTask.Put(Action+0x14,std::uint32_t(Task+0x100));
    changedTask.Put(Task+0x124,std::uint16_t(0x39b));
    Check(changedTask.Match(100003),"replaced action task invalidates a same-tick negative cache");
    struct Race {std::uintptr_t trigger,target;std::uint32_t value;};
    const Race races[]={
        {Dictionary+28+48,Action+0x14,std::uint32_t(Task+0x100)},
        {Dictionary+28+48,Dispatcher+4,std::uint32_t(Action+4)},
        {Dictionary+28+48,Player+0x80,std::uint32_t(Dictionary+0x100)},
        {Dictionary+28+48,Dictionary+16,std::uint32_t(33)},
        {Dictionary+28+48,Dictionary,std::uint32_t(2)},
        {Dictionary+28+48,Action+8,std::uint32_t(Player+4)},
        {Dictionary+28+48,Dictionary+28+48,std::uint32_t(0x78787878)}
    };
    for(const auto& race:races){Memory m;m.trigger=race.trigger;m.target=race.target;m.replacement=race.value;
        Check(!m.Match()&&!m.trigger,"replacement during lookup is re-read and rejected");}
    std::printf("Exact native blast action: %u checks PASS.\n",checks);
}
