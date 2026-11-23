#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_passenger_collision_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include <algorithm>
#include <atomic>
namespace NativePassengerCollisionKL {namespace {
using Ray=std::uintptr_t(__thiscall*)(void*,NativeAimKLMath::Query*,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
std::uintptr_t image=0;std::atomic<bool> installed{false};std::atomic<DWORD> simulationThread{0};
SRWLOCK statsLock=SRWLOCK_INIT;Stats stats;thread_local bool querying=false;
bool Read(std::uintptr_t at,void* out,std::size_t size){SIZE_T got=0;return NativeActionKL::Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;}
void Reject(unsigned stage){AcquireSRWLockExclusive(&statsLock);stats.accepted=false;stats.stage=stage;++stats.rejected;ReleaseSRWLockExclusive(&statsLock);}
bool Gameplay(const Query& query){const auto& request=query.request;const auto now=GetTickCount();const auto action=NativeActionKL::Current();
    std::uint8_t pause[2]{1,1},blocked=1;std::uint32_t controller=0,camera=0;
    return NativePassengerActorKL::Valid(request,now)&&request.recenter==static_cast<std::uint32_t>(VRBridge::RecenterAck())&&
        VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        VRBridge::ReadSecondaryFire().valid&&action.valid&&action.player==request.player&&action.movement==request.movement&&
        action.movementKind==NativeActionKL::Movement::Vehicle&&(!action.action||action.TrackedReload()||action.TrackedWeaponGesture())&&
        Read(image+0x8883A4,pause,2)&&!pause[0]&&!pause[1]&&Read(request.player+0x9B8,&controller,4)&&
        Read(controller+0x18,&blocked,1)&&!blocked&&Read(request.player+0x9A0,&camera,4)&&camera==request.camera;
}
bool Source(const Query& query){NativePassengerAttachmentKL::Identity identity;NativePassengerAttachmentKL::Entry entry;
    return NativePassengerAttachmentKL::Snapshot(image,query.request.player,Read,identity,entry)&&
        NativePassengerAttachmentKL::Same(identity,query.identity)&&!std::memcmp(&entry,&query.entry,sizeof(entry));}
enum class Trace {Unavailable,Miss,Hit};
Trace Cast(const Query& query,const World& world,Vec3 start,Vec3 delta,Contact& contact,Vec3& point){
    NativeAimKLMath::Query ray;ray.start=start;ray.delta=delta;const auto before=ray;
    const auto result=reinterpret_cast<Ray>(image+NativeAimKLImage::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
    AcquireSRWLockExclusive(&statsLock);++stats.rays;ReleaseSRWLockExclusive(&statsLock);
    if(std::memcmp(&before.start,&ray.start,sizeof(Vec3))||std::memcmp(&before.delta,&ray.delta,sizeof(Vec3)))return Trace::Unavailable;
    if(!(result&0xFF))return Trace::Miss;
    if(!NativeAimKLMath::Hit(before,ray,result,point))return Trace::Unavailable;
    std::uintptr_t geometry=0,owner=0;contact=Classify(image,ray.reference,query,Read,geometry,owner);
    AcquireSRWLockExclusive(&statsLock);stats.reference=ray.reference;stats.geometry=geometry;stats.owner=owner;stats.contact=point;
    if(contact==Contact::Carrier)++stats.carrierContacts;else if(contact==Contact::Self)++stats.selfContacts;
    else if(contact==Contact::Other)++stats.otherContacts;else ++stats.unknownContacts;ReleaseSRWLockExclusive(&statsLock);
    return Trace::Hit;
}
}
bool Install(LogCallback log){if(installed)return true;wchar_t path[MAX_PATH]{};char hash[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,hash)||!IsSupportedExecutableHash(hash))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));if(!NativeAimKLImage::Verify(image,Read))return false;
    installed=true;if(log)log("KL1 cargo collision: native ROOM carrier-floor and sampled body-path rays on owned simulation callback; unavailable contacts hold movement.");return true;}
bool Check(const Query& query,Vec3& accepted){
    AcquireSRWLockExclusive(&statsLock);++stats.calls;stats.accepted=false;stats.stage=0;stats.tick=GetTickCount();stats.thread=GetCurrentThreadId();ReleaseSRWLockExclusive(&statsLock);
    if(!installed||querying||!NativeCoverKL::SimulationCallbackActive(query.request.player)){AcquireSRWLockExclusive(&statsLock);++stats.threadRejected;ReleaseSRWLockExclusive(&statsLock);Reject(1);return false;}
    DWORD expected=0;if(!simulationThread.compare_exchange_strong(expected,GetCurrentThreadId())&&expected!=GetCurrentThreadId()){Reject(2);return false;}
    if(!Gameplay(query)||!Source(query)){Reject(3);return false;}
    struct Timer {LARGE_INTEGER begin{},frequency{};Timer(){QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);}~Timer(){LARGE_INTEGER end{};QueryPerformanceCounter(&end);
        const auto us=frequency.QuadPart>0&&end.QuadPart>=begin.QuadPart?std::uint64_t((end.QuadPart-begin.QuadPart)*1000000/frequency.QuadPart):0;
        AcquireSRWLockExclusive(&statsLock);stats.lastUs=us;stats.totalUs+=us;stats.maxUs=std::max(stats.maxUs,us);ReleaseSRWLockExclusive(&statsLock);}} timer;
    World world;Path path;if(!CurrentWorld(image,Read,world)){Reject(4);return false;}if(!Prepare(query,Read,path)){Reject(5);return false;}
    AcquireSRWLockExclusive(&statsLock);stats.room=world.room;stats.carrier=query.identity.carrier;stats.previous=path.previous;stats.desired=path.desired;ReleaseSRWLockExclusive(&statsLock);
    struct Guard {Guard(){querying=true;}~Guard(){querying=false;}} guard;
    const auto u=path.units;float lo=1e30f,hi=-1e30f;
    const Vec3 footprint[]={{0,0,0},{.18f,0,0},{-.18f,0,0},{0,0,.18f},{0,0,-.18f}};
    for(const auto offset:footprint){Contact contact=Contact::Unknown;Vec3 point;
        const auto state=Cast(query,world,Add(Add(path.desired,Scale(offset,u)),{0,.45f*u,0}),{0,-.8f*u,0},contact,point);
        if(state!=Trace::Hit){AcquireSRWLockExclusive(&statsLock);++stats.floorMisses;ReleaseSRWLockExclusive(&statsLock);Reject(state==Trace::Miss?10:11);return false;}
        AcquireSRWLockExclusive(&statsLock);++stats.floorHits;ReleaseSRWLockExclusive(&statsLock);
        if(contact!=Contact::Carrier){Reject(contact==Contact::Self?12:13);return false;}
        const auto y=point.y-path.desired.y;lo=std::min(lo,y);hi=std::max(hi,y);
        if(y<-.25f*u||y>.15f*u||hi-lo>.18f*u){Reject(14);return false;}
    }
    // Native rays around a crouched body envelope. Paths sample its boundary
    // at three heights, then check radial target clearance. Never slide, change
    // collision flags or step past an obscuring self contact.
    const Vec3 cardinal[]={{.24f,0,0},{-.24f,0,0},{0,0,.24f},{0,0,-.24f}};
    const auto travel=Subtract(path.desired,path.previous);
    for(float height:{.24f,.95f,1.22f})for(auto offset:cardinal){offset.y=height;
        if(Dot(travel,travel)<.00000001f*u*u)continue;Contact contact=Contact::Unknown;Vec3 point;
        const auto state=Cast(query,world,Add(path.previous,Scale(offset,u)),travel,contact,point);
        if(state!=Trace::Miss){AcquireSRWLockExclusive(&statsLock);++stats.wallHits;ReleaseSRWLockExclusive(&statsLock);Reject(state==Trace::Unavailable?15:contact==Contact::Self?16:17);return false;}}
    constexpr float diagonal=.169705627f;
    const Vec3 perimeter[]={{.24f,0,0},{-.24f,0,0},{0,0,.24f},{0,0,-.24f},{diagonal,0,diagonal},{diagonal,0,-diagonal},{-diagonal,0,diagonal},{-diagonal,0,-diagonal}};
    for(float height:{.24f,.95f,1.22f})for(auto offset:perimeter){Contact contact=Contact::Unknown;Vec3 point;
        const auto state=Cast(query,world,Add(path.desired,{0,height*u,0}),Scale(offset,u),contact,point);
        if(state!=Trace::Miss){AcquireSRWLockExclusive(&statsLock);++stats.wallHits;ReleaseSRWLockExclusive(&statsLock);Reject(state==Trace::Unavailable?18:contact==Contact::Self?19:20);return false;}}
    for(const auto offset:footprint){Contact contact=Contact::Unknown;Vec3 point;
        const auto state=Cast(query,world,Add(Add(path.desired,Scale(offset,u)),{0,.95f*u,0}),{0,.42f*u,0},contact,point);
        if(state!=Trace::Miss){AcquireSRWLockExclusive(&statsLock);++stats.wallHits;ReleaseSRWLockExclusive(&statsLock);Reject(state==Trace::Unavailable?22:contact==Contact::Self?23:24);return false;}}
    World after;if(!NativeCoverKL::SimulationCallbackActive(query.request.player)||!Gameplay(query)||!Source(query)||!CurrentWorld(image,Read,after)||std::memcmp(&world,&after,sizeof(world))){Reject(21);return false;}
    accepted=path.acceptedLocal;AcquireSRWLockExclusive(&statsLock);stats.accepted=true;++stats.acceptedSteps;ReleaseSRWLockExclusive(&statsLock);return true;
}
Stats Snapshot(){AcquireSRWLockShared(&statsLock);auto out=stats;ReleaseSRWLockShared(&statsLock);out.installed=installed;return out;}
}
