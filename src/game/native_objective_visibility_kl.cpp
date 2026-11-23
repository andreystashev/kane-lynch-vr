#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_objective_visibility_kl.hpp"
#include "native_hud_kl.hpp"
#include "native_team_commands_kl_image.hpp"
#include "profile.hpp"
#include <atomic>
namespace NativeObjectiveVisibilityKL {namespace {
static_assert(sizeof(void*)==4,"KL1 objective hooks are native x86");
std::uintptr_t image=0;using Color=std::uint32_t(__thiscall*)(void*,void*);using MapRequest=void(__thiscall*)(void*,int,int);
bool selectionMarkerVerified=false;
Color originalColor=nullptr;MapRequest originalMap=nullptr;
using Count=unsigned(__thiscall*)(void*);using Index=void*(__thiscall*)(void*,unsigned);
using Constructor=void*(__thiscall*)(void*,void*,unsigned,float,int);using PositionUpdate=void(__thiscall*)(void*);
Count originalCount=nullptr;Index originalIndex=nullptr;Constructor constructor=nullptr;PositionUpdate positionUpdate=nullptr;
std::atomic<bool> installed{false};std::atomic<std::uint64_t> colorCalls{0},forced{0},rejected{0},mapCalls{0},mapSuppressed{0},goalsObserved{0};
std::atomic<unsigned> available{0};std::atomic<std::uint64_t> heldAdded{0},heldViews{0},viewRejected{0};
std::atomic<std::uint64_t> filteredLive{0},copiedLive{0},recoveredStyle{0};
std::atomic<std::uint64_t> selectionPrepared{0};
struct View {
    std::uintptr_t player=0,list=0;std::uint32_t tick=0,context=0;bool valid=false;
    unsigned count=0;NativeObjectiveGoalKL::Available goals;
    std::array<NativeObjectiveGoalKL::Record,NativeObjectiveGoalKL::Maximum> records{};
    std::array<std::uintptr_t,257+NativeObjectiveGoalKL::Maximum> entries{};
    NativeObjectiveGoalKL::Record selection{};
    std::uint32_t selectedActor=0;
    std::array<std::uintptr_t,NativeObjectiveGoalKL::Maximum> borrowed{};
    std::array<NativeObjectiveGoalKL::Record,NativeObjectiveGoalKL::Maximum> borrowedBytes{};
};
thread_local std::array<View,8> views;thread_local unsigned depth=0;
struct Style {bool valid=false;std::uint32_t manager=0,slot=0,entity=0,geometry=0;NativeObjectiveGoalKL::Record source{};};
thread_local std::array<Style,NativeObjectiveGoalKL::Maximum> styles;
// If Reset cancels a draw AFTER Count returned an augmented sequence, its
// remaining Index requests must not reach the original assert on index>=count.
// Native filter6531C0 rejects filter2 for BOTH local-player indices0/1, before
// following an entity pointer; ColorHook also returns0 for this exact sentinel.
thread_local NativeObjectiveGoalKL::Record cancelledRecord=[] {NativeObjectiveGoalKL::Record out{};const std::uint32_t neither=2;std::memcpy(out.data()+0x44,&neither,4);return out;}();
SRWLOCK contextLock=SRWLOCK_INIT;Context context;
bool Read(std::uintptr_t at,void* out,std::size_t size){SIZE_T got=0;return at>=0x10000u&&at<=0xffffffffu-size&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&got)&&got==size;}
std::uint32_t SelectedActor(std::uintptr_t player){
    namespace Team=NativeTeamCommandsImage;
    if(!selectionMarkerVerified)return 0;
    const auto current=Team::Receiver(image,GetTickCount(),0,Read,true);
    std::uint32_t actor=0,vt=0,lifeQuery=0,geometry=0,owner=0;
    std::uint8_t life=0;
    if(!current.valid||current.player!=player||!current.receiver||
       !Team::Read(current.receiver+0x40,actor,Read)||actor==player||
       !Team::Read(actor,vt,Read)||vt<image+0x530000||vt>=image+0x650000||
       !Team::Read(vt+0x3F0,lifeQuery,Read)||lifeQuery!=image+0x1167F0||
       !Team::Read(actor+0x536,life,Read)||life!=1||!Team::Read(actor+4,geometry,Read)||
       !Team::Read(geometry+0x60,owner,Read)||owner!=actor)return 0;
    return actor;
}
Context Current(){Context out;AcquireSRWLockShared(&contextLock);out=context;ReleaseSRWLockShared(&contextLock);return out;}
View* CurrentView(){return depth&&depth<=views.size()?&views[depth-1]:nullptr;}
void Observe(NativeHudKL::NativeMarkerEvent event,std::uintptr_t player){
    if(event==NativeHudKL::NativeMarkerEvent::Reset){depth=0;styles={};return;}
    if(event==NativeHudKL::NativeMarkerEvent::Begin){if(depth<views.size()){views[depth]={};views[depth].player=player;}++depth;}
    else if(depth)--depth;
}
const NativeObjectiveGoalKL::Goal* PrivateRecord(View* view,std::uintptr_t record){
    if(!view||!view->valid)return nullptr;
    for(unsigned i=0;i<view->goals.count;++i){const auto& goal=view->goals.goals[i];if(record==reinterpret_cast<std::uintptr_t>(view->records[goal.index].data()))return &goal;}
    return nullptr;
}
unsigned __fastcall CountHook(void* manager,void*){
    const auto count=originalCount(manager);auto* view=CurrentView();
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));if(!view||caller!=image+0x253BCF)return count;
    view->valid=false;const auto state=Current();if(!Fresh(state,GetTickCount())||count>256)return count;
    const auto local=CaptureLocal(image,Read,state.held);std::uint32_t list=0,records=0,nativeCount=0;
    if(!local.valid||local.player!=view->player||!Read(local.data+0x1550,&list,4)||list!=reinterpret_cast<std::uintptr_t>(manager)||
       !Read(list+8,&nativeCount,4)||nativeCount!=count||!Read(list+0x14,&records,4)||
       (count&&(records<0x10000u||records>0xffffffffu-std::size_t(count)*0x64))){++viewRejected;return count;}
    const auto goals=NativeObjectiveGoalKL::Capture(image,view->player,local.index,Read);available=goals.valid?goals.count:0;
    if(!goals.valid){++viewRejected;return count;}
    if(!goals.count)styles={};
    const auto selected=state.held?SelectedActor(view->player):0;
    if(!goals.count&&!selected)return count;
    std::array<NativeObjectiveGoalKL::Record,256> native{};
    if(count&&!Read(records,native.data(),std::size_t(count)*0x64)){++viewRejected;return count;}
    // Remember only authored native style for the EXACT still-active goal.
    // A later reveal re-runs native positionUpdate; no WORLD point is frozen.
    std::array<std::uintptr_t,NativeObjectiveGoalKL::Maximum> matched{};
    for(unsigned i=0;i<goals.count;++i){const auto& goal=goals.goals[i];auto& style=styles[goal.index];
        if(!style.valid||style.manager!=goals.manager||style.slot!=goal.slot||style.entity!=goal.entity||style.geometry!=goal.geometry||
           !NativeObjectiveGoalKL::AttachedSource(style.source,goal.entity,unsigned(local.index)))style={};
        unsigned found=0;for(unsigned j=0;j<count;++j)if(NativeObjectiveGoalKL::AttachedSource(native[j],goal.entity,unsigned(local.index))){matched[goal.index]=records+j*0x64;++found;}
        if(found!=1)matched[goal.index]=0;
        if(matched[goal.index]){const auto index=unsigned((matched[goal.index]-records)/0x64);style={true,goals.manager,goal.slot,goal.entity,goal.geometry,native[index]};}
    }
    if(!state.held)return count;
    unsigned filtered=0,copied=0,recovered=0;
    view->goals=goals;view->list=list;view->tick=GetTickCount();view->context=state.epoch;view->count=0;view->selectedActor=0;
    for(unsigned i=0;i<count;++i){std::uint32_t type=0,filter=0;std::memcpy(&type,native[i].data()+0x1C,4);
        std::memcpy(&filter,native[i].data()+0x44,4);
        if(selected&&type==NativeTeamMarkerKL::SelectionType&&filter==static_cast<unsigned>(local.index))continue;
        // The active mission-goal manager is authoritative while the panel is held. Keeping
        // another fading/scripted Objective26 alongside its one active goal
        // produced observed2:1 goal draws. Other native marker types remain.
        if(!NativeObjectiveGoalKL::Objective(type))view->entries[view->count++]=records+i*0x64;else ++filtered;
    }
    for(unsigned i=0;i<goals.count;++i){const auto& goal=goals.goals[i];auto& record=view->records[goal.index];
        const auto& style=styles[goal.index];
        if(style.valid){record=style.source;
            if(matched[goal.index]){view->borrowed[goal.index]=matched[goal.index];view->borrowedBytes[goal.index]=style.source;++copied;}
            else {positionUpdate(record.data());++recovered;}
        }else {constructor(record.data(),reinterpret_cast<void*>(static_cast<std::uintptr_t>(goal.entity)),NativeObjectiveGoalKL::Type,-1.f,static_cast<int>(goal.filter));positionUpdate(record.data());}
        // These are the exact native Show665A70 type26 fields. Only the
        // presentation copy is initialized, never the live marker manager.
        const auto colour=NativeObjectiveGoalKL::Colour;std::memcpy(record.data()+0x48,&colour,4);
        record[0x4C]=0;NativeCameraMath::Vec3 point;std::memcpy(&point,record.data()+4,sizeof(point));
        if(!NativeCameraMath::Finite(point)){++viewRejected;return count;}
        record[0x4F]=0;const float zero=0;std::memcpy(record.data()+0x40,&zero,4);
        view->entries[view->count++]=reinterpret_cast<std::uintptr_t>(record.data());
    }
    if(selected&&SelectedActor(view->player)==selected){
        // Original ZSquadControl selection calls this constructor with type
        // 2. Keep a render-only copy live while the wrist roster is open.
        constructor(view->selection.data(),reinterpret_cast<void*>(std::uintptr_t(selected)),NativeTeamMarkerKL::SelectionType,-1.f,local.index);
        positionUpdate(view->selection.data());
        NativeCameraMath::Vec3 point;
        std::memcpy(&point,view->selection.data()+4,sizeof(point));
        if(NativeCameraMath::Finite(point)){
            view->selectedActor=selected;
            view->entries[view->count++]=reinterpret_cast<std::uintptr_t>(view->selection.data());
            ++selectionPrepared;
        }
    }
    const auto after=NativeObjectiveGoalKL::Capture(image,view->player,local.index,Read);const auto checked=Current();std::uint32_t afterCount=0,afterRecords=0;
    for(unsigned i=0;i<goals.count;++i){const auto index=goals.goals[i].index;if(view->borrowed[index]){NativeObjectiveGoalKL::Record current{};
        if(!Read(view->borrowed[index],current.data(),current.size())||current!=view->borrowedBytes[index]){++viewRejected;return count;}}}
    if(!after.valid||after.count!=goals.count||after.manager!=goals.manager||std::memcmp(after.goals.data(),goals.goals.data(),sizeof(goals.goals))||
       !Read(list+8,&afterCount,4)||afterCount!=count||!Read(list+0x14,&afterRecords,4)||afterRecords!=records||
       checked.epoch!=state.epoch||!checked.held||!Fresh(checked,GetTickCount())){++viewRejected;return count;}
    view->valid=true;++heldViews;heldAdded+=goals.count;filteredLive+=filtered;copiedLive+=copied;recoveredStyle+=recovered;return view->count;
}
void* __fastcall IndexHook(void* manager,void*,unsigned index){
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));auto* view=CurrentView();
    if(caller==image+0x253C27){
        if(view&&view->valid&&view->list==reinterpret_cast<std::uintptr_t>(manager)&&index<view->count)
            return reinterpret_cast<void*>(view->entries[index]);
        if(index>=originalCount(manager))return cancelledRecord.data();
    }
    return originalIndex(manager,index);
}
std::uint32_t __fastcall ColorHook(void* record,void*,void* camera){
    if(record==cancelledRecord.data())return 0;
    ++colorCalls;const auto state=Current();
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));std::uint32_t type=0;
    if(caller==image+ColorCallerRva&&Read(reinterpret_cast<std::uintptr_t>(record)+0x1C,&type,4)&&NativeObjectiveGoalKL::Objective(type))++goalsObserved;
    auto* view=CurrentView();
    if(view&&view->valid&&record==view->selection.data()){
        const auto local=CaptureLocal(image,Read,state.held);std::uint32_t ownedCamera=0;
        if(caller==image+ColorCallerRva&&state.held&&state.epoch==view->context&&Fresh(state,GetTickCount())&&
           local.valid&&local.player==view->player&&view->selectedActor&&SelectedActor(local.player)==view->selectedActor&&
           Read(local.player+0x9A0,&ownedCamera,4)&&ownedCamera==reinterpret_cast<std::uintptr_t>(camera)){
            return originalColor(record,camera);
        }
        const std::uint32_t none=0;std::memcpy(record,&none,4);return 0;
    }
    const auto* goal=PrivateRecord(view,reinterpret_cast<std::uintptr_t>(record));
    if(!goal)return originalColor(record,camera);
    const auto local=CaptureLocal(image,Read,state.held);std::uint32_t ownerCamera=0;bool sourceCurrent=false;
    if(local.valid&&local.player==view->player){const auto active=NativeObjectiveGoalKL::Capture(image,local.player,local.index,Read);
        if(active.valid&&active.manager==view->goals.manager)for(unsigned i=0;i<active.count;++i){const auto& current=active.goals[i];
            if(current.slot==goal->slot&&current.entity==goal->entity&&current.geometry==goal->geometry&&current.source==goal->source)sourceCurrent=true;}}
    if(caller!=image+ColorCallerRva||!state.held||state.epoch!=view->context||!Fresh(state,GetTickCount())||!local.valid||local.player!=view->player||
       !Read(local.player+0x9A0,&ownerCamera,4)||ownerCamera!=reinterpret_cast<std::uintptr_t>(camera)||
       !sourceCurrent){
        // The native filter still runs after Color, even with alpha0. Retire
        // only this private entity pointer so it cannot follow a dead owner.
        const std::uint32_t none=0;std::memcpy(record,&none,4);++rejected;return 0;}
    ++forced;return originalColor(record,camera);
}
void __fastcall MapHook(void* manager,void*,int pressed,int slot){
    ++mapCalls;const auto state=Current();
    if(Fresh(state,GetTickCount())&&SuppressMap(image,reinterpret_cast<std::uintptr_t>(manager),slot,Read)){
        const auto checked=Current();if(checked.tick==state.tick&&Fresh(checked,GetTickCount())){++mapSuppressed;return;}}
    originalMap(manager,pressed,slot);
}
}
bool Install(LogCallback log){
    if(installed)return true;wchar_t path[MAX_PATH]{};char hash[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,hash)||!IsSupportedExecutableHash(hash))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!Verify(image,Read)){if(log)log("KL1 objective/map signatures differ; native behavior retained.");return false;}
    selectionMarkerVerified=VerifySelectionMarker(image,Read);
    constructor=reinterpret_cast<Constructor>(image+0x252F50);positionUpdate=reinterpret_cast<PositionUpdate>(image+0x253210);
    void* targets[]={reinterpret_cast<void*>(image+ColorRva),reinterpret_cast<void*>(image+MapRequestRva),reinterpret_cast<void*>(image+0x12F500),reinterpret_cast<void*>(image+0x254180)};
    void* hooks[]={reinterpret_cast<void*>(ColorHook),reinterpret_cast<void*>(MapHook),reinterpret_cast<void*>(CountHook),reinterpret_cast<void*>(IndexHook)};
    void** originals[]={reinterpret_cast<void**>(&originalColor),reinterpret_cast<void**>(&originalMap),reinterpret_cast<void**>(&originalCount),reinterpret_cast<void**>(&originalIndex)};
    unsigned created=0;bool okay=true;
    for(;created<4;++created)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK){okay=false;break;}
    if(okay)for(unsigned i=0;i<4;++i)if(MH_QueueEnableHook(targets[i])!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay){for(unsigned i=0;i<created;++i){MH_QueueDisableHook(targets[i]);MH_DisableHook(targets[i]);}MH_ApplyQueued();
        for(unsigned i=0;i<created;++i)MH_RemoveHook(targets[i]);return false;}
    NativeHudKL::SetNativeMarkerObserver(Observe);installed=true;if(log)log("KL1 native Objective26: held Y rebuilds standard markers from active native goal slots; map modal entry suppressed.");return true;
}
void SetContext(bool gameplay,bool teamHudHeld){AcquireSRWLockExclusive(&contextLock);const bool held=gameplay&&teamHudHeld;
    const auto epoch=context.epoch+unsigned(context.gameplay!=gameplay||context.held!=held);
    context={gameplay?GetTickCount():0,gameplay,held,epoch};ReleaseSRWLockExclusive(&contextLock);}
Stats Snapshot(){const auto state=Current();return {installed,state.held&&Fresh(state,GetTickCount()),colorCalls,forced,rejected,mapCalls,mapSuppressed,goalsObserved,available,heldAdded,heldViews,viewRejected,filteredLive,copiedLive,recoveredStyle,selectionPrepared};}
}
