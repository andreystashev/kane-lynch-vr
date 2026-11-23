#pragma once
#include "native_action_kl.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace NativeTutorialKL {
constexpr std::uintptr_t MonitorVtable=0x57FF54,Dispatch=0x2B8C90;
constexpr unsigned StageCount=6;
constexpr std::uint32_t FallbackMs=3000;
struct Definition {const char* name;unsigned event;};
inline constexpr Definition Definitions[StageCount]={
    {"KaneMonitor_CoverStanding",3},{"KaneMonitor_CoverStanding_Aim",5},{"KaneMonitor_BlindFire_Stand",9},
    {"KaneMonitor_CoverCrouching",4},{"KaneMonitor_CoverCrouching_Aim",6},{"KaneMonitor_BlindFire_Crouch",10}};
inline constexpr char Prefix[]="ROOT\\AI_Players!025_CoverPractise\\";
struct Monitor {
    std::uint32_t vtable=0;std::uint8_t base[0x2C-4]{};std::uint32_t info=0,selected=0;
    std::uint8_t enabled=0,padding[3]{};std::uint32_t outputs[22]{};std::uint8_t notified[22]{},tail[2]{};
};
static_assert(sizeof(Monitor)==0xA8&&offsetof(Monitor,outputs)==0x38&&offsetof(Monitor,notified)==0x90,"Native PlayerMonitor layout changed");
struct Stage {
    bool valid=false;unsigned index=StageCount,event=0;
    std::uint32_t data=0,player=0,monitor=0,owner=0,geometry=0,root=0,name=0,output=0,slot=0,outputVtable=0;
};
inline bool Same(const Stage& a,const Stage& b){return a.valid&&b.valid&&a.index==b.index&&a.event==b.event&&a.data==b.data&&a.player==b.player&&
    a.monitor==b.monitor&&a.owner==b.owner&&a.geometry==b.geometry&&a.root==b.root&&a.name==b.name&&a.output==b.output&&a.slot==b.slot&&a.outputVtable==b.outputVtable;}
struct Probe {unsigned listed=0,named=0,active=0,rejected=0;};
template<class Reader>bool Name(std::uintptr_t pointer,Reader read,std::array<char,192>& output){
    if(!NativeActionKL::Address(pointer,output.size()))return false;
    // Most names share the native string arena: one bounded read. An arena
    // boundary may need byte reads, still ending at its first NUL.
    std::array<char,192> next{};
    if(!read(pointer,next.data(),next.size())){
        for(unsigned i=0;i<next.size();++i){if(!read(pointer+i,&next[i],1))return false;if(!next[i])break;}
    }
    for(unsigned i=0;i<next.size();++i){if(!next[i]){if(!i)return false;output=next;return true;}
        if(static_cast<unsigned char>(next[i])<32||static_cast<unsigned char>(next[i])>126)return false;}
    return false;
}
template<class Reader>bool NodeName(std::uint32_t node,Reader read,std::array<char,192>& name,std::uint32_t* pointer=nullptr){
    std::uint32_t p=0;if(!NativeActionKL::Address(node,0x70)||!read(node+0x6C,&p,4)||!Name(p,read,name))return false;
    if(pointer)*pointer=p;return true;
}
template<class Reader>bool Scope(std::uint32_t node,const char* name,unsigned index,Reader read,std::uint32_t& root){
    char full[192]{};const auto prefix=sizeof(Prefix)-1,leaf=std::strlen(Definitions[index].name);
    if(prefix+leaf+1>sizeof(full))return false;std::memcpy(full,Prefix,prefix);std::memcpy(full+prefix,Definitions[index].name,leaf+1);
    const bool complete=!std::strcmp(name,full);bool scoped=complete;
    std::uint32_t visited[32]{};unsigned depth=0;std::array<char,192> parentName;
    if(!complete){
        if(std::strcmp(name,Definitions[index].name))return false;
        std::uint32_t parent=0;if(!read(node+0x50,&parent,4)||!parent||!NodeName(parent,read,parentName))return false;
        if(!std::strcmp(parentName.data(),"ROOT\\AI_Players!025_CoverPractise"))scoped=true;
        else if(!std::strcmp(parentName.data(),"AI_Players!025_CoverPractise")){
            if(!read(parent+0x50,&parent,4)||!NodeName(parent,read,parentName)||std::strcmp(parentName.data(),"ROOT"))return false;scoped=true;
        }else if(!std::strcmp(parentName.data(),"025_CoverPractise")){
            if(!read(parent+0x50,&parent,4)||!NodeName(parent,read,parentName)||std::strcmp(parentName.data(),"AI_Players")||
               !read(parent+0x50,&parent,4)||!NodeName(parent,read,parentName)||std::strcmp(parentName.data(),"ROOT"))return false;
            scoped=true;
        }
    }
    if(!scoped)return false;
    // A bounded root identity makes stage lifetime independent of just a
    // recycled monitor address. No transforms or script flags are written.
    while(node&&depth<32){for(unsigned i=0;i<depth;++i)if(visited[i]==node)return false;visited[depth++]=node;
        std::uint32_t parent=0;if(!read(node+0x50,&parent,4))return false;if(!parent){root=node;return true;}node=parent;}
    return false;
}
template<class Reader>Stage Snapshot(std::uintptr_t image,std::uintptr_t player,Reader read,Probe* probe=nullptr){
    if(probe)*probe={};Stage found;std::uint32_t data=0,local=0,vt=0,count=0,kane=0;std::int8_t selected=-1;
    const auto get=[&](std::uintptr_t at,auto& out){return NativeActionKL::Address(at,sizeof(out))&&read(at,&out,sizeof(out));};
    if(!player||!get(image+0x68760C,data)||!get(data+0xA2C,local)||local!=player||!get(player,vt)||vt!=image+0x56582C||
       !get(data+0xA6C,selected)||selected<0||selected>=16||!get(data+0xA2C+unsigned(selected)*4,kane)||kane!=player||
       !get(data+0xC7C,count)||count>32)return {};
    unsigned active=0;
    for(unsigned slot=0;slot<count;++slot){
        if(probe)++probe->listed;std::uint32_t object=0;if(!get(data+0xC80+slot*4,object)||!get(object,vt)||vt!=image+MonitorVtable)continue;
        Monitor monitor;if(!get(object,monitor)||!monitor.enabled||(monitor.selected!=0&&monitor.selected!=2))continue;
        std::uint32_t owner=0,node=0,back=0,namePointer=0;if(!get(object+0x24,owner)||!get(owner+4,node)||!get(node+0x60,back)||back!=owner)continue;
        std::array<char,192> name;if(!NodeName(node,read,name,&namePointer))continue;
        for(unsigned index=0;index<StageCount;++index){const auto event=Definitions[index].event;
            if(!NativeActionKL::Address(monitor.outputs[event],4)||monitor.notified[event])continue;std::uint32_t root=0;
            if(!Scope(node,name.data(),index,read,root))continue;
            std::uint32_t outputVtable=0,countMethod=0,indexMethod=0;
            if(!get(monitor.outputs[event],outputVtable)||outputVtable<image||outputVtable>=image+0x900000||
               !get(outputVtable+0x14,countMethod)||!get(outputVtable+0x34,indexMethod)||
               countMethod<image+0x1000||countMethod>=image+0x4C0000||indexMethod<image+0x1000||indexMethod>=image+0x4C0000)continue;
            if(probe)++probe->named;
            Stage candidate{true,index,event,data,static_cast<std::uint32_t>(player),object,owner,node,root,namePointer,monitor.outputs[event],slot,outputVtable};
            // All consumed links and the enabled/dedup output are re-read.
            std::uint32_t currentData=0,currentCount=0,currentSlot=0,currentOwner=0,currentNode=0,currentName=0;
            Monitor after;
            if(!get(image+0x68760C,currentData)||currentData!=data||!get(data+0xC7C,currentCount)||currentCount!=count||
               !get(data+0xC80+slot*4,currentSlot)||currentSlot!=object||!get(object,after)||after.vtable!=vt||
               after.enabled!=monitor.enabled||after.selected!=monitor.selected||after.outputs[event]!=candidate.output||after.notified[event]||
               !get(object+0x24,currentOwner)||currentOwner!=owner||!get(owner+4,currentNode)||currentNode!=node||
               !get(node+0x60,back)||back!=owner||!get(node+0x6C,currentName)||currentName!=namePointer||
               !get(candidate.output,currentSlot)||currentSlot!=outputVtable){if(probe)++probe->rejected;continue;}
            found=candidate;++active;
        }
    }
    if(probe)probe->active=active;
    // Do not invent a stage order or skip several demonstration blocks when
    // the native script has more than one outstanding teaching monitor.
    return active==1?found:Stage{};
}
enum class Reason {None,Shot,Timeout};
struct Policy {
    Stage stage{};std::uint32_t last=0,elapsed=0;bool emitted=false;
    void Reset(){*this={};}
    Reason Update(const Stage& current,std::uint32_t now,bool shot,bool eligible){
        if(!eligible||!current.valid||!now){Reset();return Reason::None;}
        if(!Same(stage,current)){Reset();stage=current;last=now;}
        else {const auto dt=static_cast<std::int32_t>(now-last);if(dt<0||dt>250){Reset();stage=current;}else if(!emitted)elapsed+=static_cast<std::uint32_t>(dt);last=now;}
        if(emitted)return Reason::None;
        if(shot){emitted=true;return Reason::Shot;}
        if(elapsed>=FallbackMs){emitted=true;return Reason::Timeout;}return Reason::None;
    }
};
template<class Reader>bool Verify(std::uintptr_t image,Reader read){
    const auto match=[&](std::uintptr_t rva,const auto& bytes){std::array<unsigned char,sizeof(bytes)> actual{};return read(image+rva,actual.data(),actual.size())&&!std::memcmp(actual.data(),bytes,actual.size());};
    const unsigned char dispatch[]={0x8B,0x44,0x24,4,0x83,0xF8,0x13,0x57,0x8B,0xF9};
    const unsigned char dedup[]={0x80,0xBC,0x38,0x90,0,0,0,0};
    const unsigned char getKane[]={0x8A,0x84,0x08,0x6C,0x0A,0,0};
    const unsigned char nameStore[]={0x89,0x47,0x6C,0x8B,0xCE,0x8B,0xD0};
    const unsigned char enabled[]={0x83,0x7E,0x30,0,0x75,0x11};
    std::uint32_t handler=0,table=0;
    return match(Dispatch,dispatch)&&match(0x2B8CA9,dedup)&&match(0x5CF6F,getKane)&&match(0x29DD7,nameStore)&&match(0x2B8FAD,enabled)&&
        read(image+MonitorVtable+0x68,&handler,4)&&handler==image+0x2B8C40&&
        read(image+0x2B8DDC+3*4,&table,4)&&table==image+0x2B8D3B;
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
void SetGameplayContext(bool gameplay);
void SimulationTick(std::uintptr_t player);
// Call only after an owned player's real native firearm shot, never on a
// trigger intent. This records a token; script continuation runs next tick.
void NotifyShot(std::uintptr_t player);
struct Stats {bool installed=false;std::uint64_t ticks=0,shotTokens=0,shotCredits=0,timeCredits=0,rejected=0,ambiguous=0;
    unsigned stage=StageCount,event=0,elapsedMs=0,listed=0,named=0,active=0;std::uintptr_t player=0,monitor=0,root=0;};
Stats Snapshot();
}
