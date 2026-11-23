#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_rope_kl.hpp"
#include "native_rope_kl_image.hpp"
#include "native_camera_kl.hpp"
#include "profile.hpp"
#include <atomic>
#include <cmath>
namespace NativeRopeKL { namespace {
namespace I=NativeRopeKLImage;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> frames{0},owned{0},candidates{0},suppressed{0},rejected{0},cacheHits{0};
std::atomic<std::uint64_t> multiPassSuppressed{0},bindRetired{0},scopeBudgetRetired{0},rigidBoundaries{0},rigidCandidates{0};
constexpr unsigned ScopeDrawLimit=64;
bool Read(std::uintptr_t at,void* out,std::size_t bytes){SIZE_T got=0;return NativeActionKL::Address(at,bytes)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;}
struct Entry {std::uintptr_t address=0;std::uint64_t epoch=0,serial=0;bool own=false;};
struct Frame {
    I::Pool pool{};std::array<Entry,4096> memo{};
    std::uint64_t epoch=1,serial=0;std::uint32_t gpuIndex=2;
    std::uintptr_t state=0,entry=0;NativeCameraMath::CameraPose world{};
    bool active=false,tag=false;unsigned draws=0;
};
thread_local Frame frame;
}
bool Install(bool enabled,LogCallback log) {
    if(installed)return true;
    if(!enabled)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!I::Verify(image,Read)){if(log)log("KL1 local rope visibility: native ownership signatures differ; ropes retained.");return false;}
    installed=true;if(log)log("KL1 local rope visibility: first-person owned rope geometry only, no native physics/lifecycle edits.");return true;
}
void SetFrame(std::uintptr_t player,std::uint64_t serial,bool firstPerson) {
    frame.tag=false;frame.active=false;frame.draws=0;
    if(!installed||!firstPerson||!player)return;
    const auto pool=I::Capture(image,player,Read);
    if(!I::Same(pool,frame.pool)){frame.pool=pool;if(++frame.epoch==0){frame.memo={};frame.epoch=1;}}
    if(!pool.count)return;
    std::uint32_t index=2;if(!Read(image+0x7678A4,&index,4)||index>1)return;
    frame.gpuIndex=index;frame.serial=serial;frame.active=true;++frames;owned+=pool.count;
}
void ClearFrame(){frame.tag=false;frame.active=false;frame.draws=0;}
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva) {
    if(frame.tag)++bindRetired;
    frame.tag=false;frame.draws=0;
    if(installed&&frame.active&&!source)++rigidBoundaries;
    if(!installed||!frame.active||!source)return;
    const auto at=reinterpret_cast<std::uintptr_t>(source);
    const auto offset=std::uintptr_t(0x10+frame.gpuIndex*0x30);
    if(at<0x10000+offset)return;
    const auto entry=at-offset;auto& memo=frame.memo[(entry>>4)&(frame.memo.size()-1)];
    const bool rigid=callerRva==0x739E0;
    if(rigid) {
        std::uint32_t vt=0;
        if(!Read(entry,&vt,4)||vt!=image+0x541070)return;
    }
    bool own=false;
    if(memo.address==entry&&memo.epoch==frame.epoch&&(memo.own||frame.serial-memo.serial<8)){own=memo.own;++cacheHits;}
    else {own=I::OwnEntry(image,entry,frame.pool,Read);memo={entry,frame.epoch,frame.serial,own};}
    if(!own)return;
    ++candidates;if(rigid)++rigidCandidates;bool firstPerson=false;
    const auto worldAt=rigid?reinterpret_cast<std::uintptr_t>(state)+0x8C:at;
    if(!NativeCameraKL::DrawStateActive(reinterpret_cast<std::uintptr_t>(state),&firstPerson)||!firstPerson||
       !I::OwnEntry(image,entry,frame.pool,Read)||!Read(worldAt,&frame.world,sizeof(frame.world))||!NativeCameraMath::ValidatePose(frame.world)){++rejected;return;}
    frame.state=reinterpret_cast<std::uintptr_t>(state);frame.entry=entry;frame.tag=true;
}
bool SuppressDraw() {
    if(!frame.tag)return false;
    // A native geometry entry can submit several material passes after ONE
    // WORLD bind. Keep this exact owned-entry scope until its next native
    // WORLD/rigid bind or an eye/view/lifetime transition. Every primitive
    // re-proves ownership and the unmodified current VIEW/P/WORLD.
    bool firstPerson=false;NativeCameraMath::CameraPose world;
    if(frame.draws>=ScopeDrawLimit){frame.tag=false;++scopeBudgetRetired;return false;}
    if(!frame.active||!NativeCameraKL::DrawStateActive(frame.state,&firstPerson)||!firstPerson||
       !I::OwnEntry(image,frame.entry,frame.pool,Read)||!Read(frame.state+0x8C,&world,sizeof(world))||
       std::memcmp(&world,&frame.world,sizeof(world))){frame.tag=false;++rejected;return false;}
    if(frame.draws++)++multiPassSuppressed;
    ++suppressed;return true;
}
Stats Snapshot(){return {installed.load(),frames.load(),owned.load(),candidates.load(),suppressed.load(),rejected.load(),cacheHits.load(),
    multiPassSuppressed.load(),bindRetired.load(),scopeBudgetRetired.load(),rigidBoundaries.load(),rigidCandidates.load()};}
}
