#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "native_team_hud_kl.hpp"
#include "native_hud_kl.hpp"
#include "background_diagnostic.hpp"
#include "profile.hpp"

namespace NativeTeamHudKL {
namespace {
using Draw=void(__thiscall*)(void*,void*,void*);
Draw original=nullptr;
using Update=void(__thiscall*)(void*);
Update originalUpdate=nullptr;
SRWLOCK rosterLock=SRWLOCK_INIT;
std::uintptr_t image=0;
SceneProvider sceneProvider=nullptr;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> calls{0},hidden{0},captured{0},rejected{0};
std::atomic<std::uint64_t> markerCalls{0},markerSnapshots{0},markerObserved{0},markerRejected{0};
std::atomic<std::uint64_t> markerFixedObserved{0};
std::atomic<unsigned> members{0};
void TraceSelection(const Roster& roster) {
    if(!BackgroundDiagnostic::TeamPanelTest()||!roster.count)return;
    int selected=-1;
    for(unsigned i=0;i<roster.count;++i)if(roster.members[i].selected)selected=static_cast<int>(i);
    static std::atomic<int> last{-2};
    if(last.exchange(selected)==selected)return;
    wchar_t path[MAX_PATH]{};
    constexpr wchar_t suffix[]=L"\\team-selection.jsonl";
    const auto length=GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",path,_countof(path));
    if(!length||length+_countof(suffix)>_countof(path))return;
    std::wcscat(path,suffix);
    if(auto* file=_wfopen(path,L"ab")){
        std::fprintf(file,"{\"ageMs\":%lu,\"count\":%u,\"slot\":%d,\"all\":%s}\n",
                     BackgroundDiagnostic::Age(),roster.count,selected,roster.allSelected?"true":"false");
        std::fclose(file);
    }
}
bool Read(std::uintptr_t at,void* output,std::size_t bytes) {
    SIZE_T got=0;return Address(at,bytes)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,bytes,&got)&&got==bytes;
}
bool Anchor(std::uintptr_t display,NativeCameraMath::Vec3& centre) {
    std::uint32_t layout=0;
    float origin[3]{},basis[7]{};
    if(!Read(display+0xC,&layout,sizeof(layout))||!Read(layout,basis,sizeof(basis))||
       !Read(display+0x578,origin,sizeof(origin)))return false;
    // Draw65D813 multiplies the authored XY offsets by the HUD layout's
    // scales and adds its origin before submitting the row's sprites.
    centre={basis[0]+origin[0]*basis[5],basis[1]+origin[1]*basis[6],basis[2]};
    return NativeCameraMath::Finite(centre)&&centre.z>=5.5f&&centre.z<=6.5f&&
        std::fabs(centre.x)<10.f&&std::fabs(centre.y)<10.f;
}
void __fastcall DrawHook(void* display,void*,void* renderer,void* parameter) {
    ++calls;std::uintptr_t player=0;
    if(sceneProvider&&sceneProvider()&&OwnedDisplay(image,reinterpret_cast<std::uintptr_t>(display),Read,player,true)) {
        ++hidden;return;
    }
    original(display,renderer,parameter);
}
void __fastcall UpdateHook(void* display,void*){
    AcquireSRWLockExclusive(&rosterLock);
    originalUpdate(display);
    ReleaseSRWLockExclusive(&rosterLock);
}
struct WristCircleColours {
    std::uintptr_t slots[MaxMembers]{};
    std::uint32_t original[MaxMembers]{};
    WristCircleColours(std::uintptr_t display,const Roster& roster){
        std::uint32_t records=0,count=0;
        if(!Read(display+0x6C,&records,sizeof(records))||records!=display+0x118||
           !Read(display+0x64,&count,sizeof(count))||count!=roster.count)return;
        for(unsigned row=0;row<count;++row){
            const auto slot=std::uintptr_t(records)+row*0x28+0x24;
            if(!Read(slot,&original[row],sizeof(original[row])))continue;
            const auto stable=WristCircleColour(original[row],roster.members[row].nativeStatus);
            if(stable==original[row])continue;
            slots[row]=slot;
            std::memcpy(reinterpret_cast<void*>(slot),&stable,sizeof(stable));
        }
    }
    ~WristCircleColours(){
        for(unsigned row=0;row<MaxMembers;++row)
            if(slots[row])std::memcpy(reinterpret_cast<void*>(slots[row]),&original[row],sizeof(original[row]));
    }
};
bool DrawRoster(std::uintptr_t display,std::uintptr_t player,void* renderer,void* parameter,Roster& next){
    struct ReadRoster {
        ReadRoster(){AcquireSRWLockExclusive(&rosterLock);}
        ~ReadRoster(){ReleaseSRWLockExclusive(&rosterLock);}
    } readRoster;
    // Native Update clears the display count and repopulates the sprite
    // rows in place. Keep its entire transaction outside the native draw.
    if(!CaptureRoster(image,display,player,GetTickCount(),Read,next,true))return false;
    if(!next.count)return true;
    NativeCameraMath::Vec3 anchor;
    if(!Anchor(display,anchor))return false;
    // The native producer copies these quad records at submission. Change
    // only the wrist circles' alpha for that call, then restore the widget.
    const WristCircleColours circles(display,next);
    const auto previous=NativeHudKL::ReplaceWristHudSource({renderer,player,display,
        NativeHudKL::WristHudElement::Team,anchor,next});
    original(reinterpret_cast<void*>(display),renderer,parameter);
    NativeHudKL::ReplaceWristHudSource(previous);
    return true;
}
}
void DrawWrist(std::uintptr_t display,std::uintptr_t player,void* renderer,void* parameter){
    if(!installed)return;
    Roster next;
    if(!DrawRoster(display,player,renderer,parameter,next)){++rejected;return;}
    members=next.count;++captured;
    TraceSelection(next);
}
bool Install(SceneProvider scene,LogCallback log) {
    if(installed.load())return true;
    if(!scene||!NativeHudKL::WristHudReady())return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!VerifyImage(image,Read)){if(log)log("Native team HUD signatures differ; original roster retained.");return false;}
    sceneProvider=scene;
    auto* target=reinterpret_cast<void*>(image+0x25D770);
    auto* update=reinterpret_cast<void*>(image+0x25D8A0);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&DrawHook),reinterpret_cast<void**>(&original))!=MH_OK){sceneProvider=nullptr;return false;}
    if(MH_CreateHook(update,reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&originalUpdate))!=MH_OK){
        MH_RemoveHook(target);original=nullptr;sceneProvider=nullptr;return false;
    }
    if(MH_QueueEnableHook(target)!=MH_OK||MH_QueueEnableHook(update)!=MH_OK||MH_ApplyQueued()!=MH_OK){
        MH_QueueDisableHook(target);MH_QueueDisableHook(update);MH_ApplyQueued();
        MH_RemoveHook(update);MH_RemoveHook(target);originalUpdate=nullptr;original=nullptr;sceneProvider=nullptr;return false;
    }
    installed=true;
    if(log)log("Native team roster follows the left hand while Y is held. The right grip advances the native receiver.");
    return true;
}
NativeTeamMarkerKL::Snapshot SnapshotMarkers(std::uintptr_t player,std::uint32_t now) {
    if(!installed||!sceneProvider||!sceneProvider())return {};
    ++markerCalls;auto snapshot=NativeTeamMarkerKL::Capture(image,player,now,Read);
    if(snapshot.valid){++markerSnapshots;markerObserved+=snapshot.count;
        for(unsigned i=0;i<snapshot.count;++i)if(snapshot.markers[i].fixed)++markerFixedObserved;
    }else ++markerRejected;
    return snapshot;
}
Stats SnapshotStats(){return {installed.load(),calls.load(),hidden.load(),captured.load(),rejected.load(),markerCalls.load(),markerSnapshots.load(),markerObserved.load(),markerRejected.load(),markerFixedObserved.load(),members.load()};}
}
