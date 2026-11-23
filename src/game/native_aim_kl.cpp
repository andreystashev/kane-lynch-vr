#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_aim_kl.hpp"
#include "native_aim_kl_math.hpp"
#include "native_aim_kl_image.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_grenade_kl_math.hpp"
#include "native_command_ray_kl.hpp"
#include "native_team_commands_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include <atomic>

namespace NativeAimKL {namespace {
namespace M=NativeAimKLMath;
namespace I=NativeAimKLImage;
namespace W=NativeWeaponOwnership;
using Vec3=NativeCameraMath::Vec3;
using RoomRay=std::uintptr_t(__thiscall*)(void*,M::Query*,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
std::uintptr_t image=0;
std::atomic<bool> installed{false},enabled{false};
std::atomic<std::uint64_t> epoch{1},queries{0},hits{0},misses{0},rejected{0},selfRejected{0},threadRejected{0},rateLimited{0};
std::atomic<DWORD> latestTick{0},latestThread{0};
SRWLOCK endpointLock=SRWLOCK_INIT;
Endpoint endpoint;
thread_local DWORD lastQuery=0;
thread_local bool querying=false;
bool Read(std::uintptr_t at,void* out,std::size_t size) {
    SIZE_T copied=0;return W::Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&copied)&&copied==size;
}
template<class T>bool Value(std::uintptr_t at,T& out) {return W::Read(at,out,Read);}
struct World {std::uint32_t app=0,engine=0,room=0,roomVtable=0,backend=0,backendVtable=0;};
bool CurrentWorld(World& world) {
    std::uint32_t method=0,geometry=0,flags=0;
    return Value(image+0x6875f0,world.app)&&Value(world.app+0xa8,world.engine)&&Value(world.engine+0x8c,world.room)&&
        Value(world.room,world.roomVtable)&&I::RoomType(image,world.roomVtable)&&
        Value(world.roomVtable+0x26c,method)&&method==image+I::RoomRay&&
        Value(world.room+4,geometry)&&Value(geometry+0x3c,flags)&&!(flags&0x400)&&
        Value(image+0x7c1d34,world.backend)&&Value(world.backend,world.backendVtable)&&
        (world.backendVtable==image+0x5411c4||world.backendVtable==image+0x541274)&&
        Value(world.backendVtable+0x2c,method)&&method==image+I::BackendRay;
}
bool Contact(std::uint32_t reference,std::uintptr_t player,std::uintptr_t root,std::uintptr_t weapon,bool& self) {
    self=false;if(!reference)return true;
    std::uintptr_t geometry=0;
    if(!W::ResolveGeometry(image,reference,Read,geometry))return false;
    const auto contactGeometry=geometry;
    std::uint32_t playerRoot=0;
    if(!Value(player+4,playerRoot))return false;
    for(unsigned depth=0;geometry&&depth<16;++depth) {
        std::uint32_t owner=0,parent=0;
        if(!Value(geometry+0x60,owner)||!Value(geometry+0x50,parent))return false;
        if(geometry==playerRoot||(root&&geometry==root)||owner==player||(weapon&&owner==weapon))self=true;
        if(parent==geometry)return false;
        geometry=parent;
    }
    if(geometry)return false;
    std::uintptr_t after=0;
    return W::ResolveGeometry(image,reference,Read,after)&&after==contactGeometry;
}
void Clear() {AcquireSRWLockExclusive(&endpointLock);endpoint={};ReleaseSRWLockExclusive(&endpointLock);}
}
bool Install(LogCallback log) {
    if(installed)return true;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!I::Verify(image,Read)) {
        if(log)log("KL1 aim collision preview: native ROOM/backend signatures differ; laser endpoint disabled.");return false;
    }
    installed=true;
    if(log)log("KL1 aim collision preview: verified ROOM ray, local native detector callback only; 16ms rate limit, 50ms/1cm endpoint, no geometry flag writes.");
    return true;
}
void SetEnabled(bool value) {
    if(enabled.exchange(value)==value)return;
    ++epoch;Clear();
}
void SimulationTick(std::uintptr_t player) {
    if(!installed||!enabled)return;
    if(!NativeCoverKL::SimulationCallbackActive(player)||querying){++threadRejected;return;}
    if(!VRBridge::Ready()||NativeCameraKL::CinematicActive()){Clear();return;}
    const auto action=NativeActionKL::Current();
    if(!action.WeaponTrackingAllowed()||(action.valid&&action.player!=player)){Clear();return;}
    const DWORD now=GetTickCount();
    if(lastQuery&&now-lastQuery<M::MinimumQueryIntervalMs){++rateLimited;return;}
    lastQuery=now;latestTick=now;latestThread=GetCurrentThreadId();
    struct Scope {Scope(){querying=true;}~Scope(){querying=false;}} scope;
    const auto generation=epoch.load();
    NativeWeaponKL::AimFrame pose;M::Query ray;World world;
    if(!NativeWeaponKL::SnapshotAim(pose)||pose.player!=player||!M::Prepare(pose,now,ray)||!CurrentWorld(world)){++rejected;return;}
    const auto before=ray;++queries;
    const auto result=reinterpret_cast<RoomRay>(image+I::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
    Vec3 point;
    if(!M::Hit(before,ray,result,point)){++misses;Clear();return;}
    bool self=false;
    if(!Contact(ray.reference,pose.player,pose.root,pose.weapon,self)){++rejected;Clear();return;}
    if(self){++selfRejected;Clear();return;}
    NativeWeaponKL::AimFrame after;World afterWorld;
    if(!enabled||epoch!=generation||!NativeCoverKL::SimulationCallbackActive(player)||NativeCameraKL::CinematicActive()||
       !NativeWeaponKL::SnapshotAim(after)||!M::Same(pose,after)||!CurrentWorld(afterWorld)||
       std::memcmp(&world,&afterWorld,sizeof(world))){++rejected;return;}
    Endpoint value;value.valid=true;value.pose=pose;value.point=point;value.tick=GetTickCount();value.reference=ray.reference;value.room=world.room;
    if(!M::Match(pose,value.tick,point,pose,value.tick)){++rejected;return;}
    AcquireSRWLockExclusive(&endpointLock);
    if(enabled&&epoch==generation){endpoint=value;++hits;}
    ReleaseSRWLockExclusive(&endpointLock);
}
bool ReadEndpoint(const NativeWeaponKL::AimFrame& current,Endpoint& output) {
    if(!installed||!enabled||NativeCameraKL::CinematicActive())return false;
    const auto action=NativeActionKL::Current();
    if(!action.WeaponTrackingAllowed()||(action.valid&&action.player!=current.player))return false;
    Endpoint value;AcquireSRWLockShared(&endpointLock);value=endpoint;ReleaseSRWLockShared(&endpointLock);
    World world;
    if(!value.valid||!CurrentWorld(world)||world.room!=value.room||
       !M::MatchVisual(value.pose,value.tick,value.point,current,GetTickCount()))return false;
    output=value;return true;
}
GazeTrace TraceGaze(const NativeWeaponKL::HeadGaze& gaze,const NativeWeaponKL::AimFrame& weapon,Vec3& output) {
    if(!installed||querying||!NativeWeaponKL::ScopeQueryActive(weapon.player,weapon.weapon)||
       gaze.player!=weapon.player||!NativeGrenadeKLMath::ValidGaze(gaze,GetTickCount())||
       !VRBridge::Ready()||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=gaze.recenterAck||NativeCameraKL::CinematicActive())return GazeTrace::Unavailable;
    World world;if(!CurrentWorld(world))return GazeTrace::Unavailable;
    M::Query ray;Vec3 direction;if(!M::Unit(gaze.direction,direction))return GazeTrace::Unavailable;
    ray.start=gaze.origin;ray.delta=NativeCameraMath::Scale(direction,NativeScopeKL::MaximumAimRangeMetres*gaze.unitsPerMetre);
    if(!NativeCameraMath::Finite(ray.delta))return GazeTrace::Unavailable;
    struct Scope {Scope(){querying=true;}~Scope(){querying=false;}} scope;
    const auto before=ray;
    const auto result=reinterpret_cast<RoomRay>(image+I::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
    World after;
    if(!NativeWeaponKL::ScopeQueryActive(weapon.player,weapon.weapon)||!CurrentWorld(after)||std::memcmp(&world,&after,sizeof(world))||
       !NativeGrenadeKLMath::ValidGaze(gaze,GetTickCount())||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=gaze.recenterAck||
       std::memcmp(&before.start,&ray.start,sizeof(Vec3))||std::memcmp(&before.delta,&ray.delta,sizeof(Vec3)))return GazeTrace::Unavailable;
    if(!(result&0xff))return GazeTrace::Miss;
    Vec3 point;bool self=false;
    if(!M::Hit(before,ray,result,point)||!Contact(ray.reference,weapon.player,weapon.root,weapon.weapon,self)||self)return GazeTrace::Unavailable;
    output=point;return GazeTrace::Hit;
}
Stats Snapshot() {
    Stats value;value.installed=installed;value.enabled=enabled;value.queries=queries;value.hits=hits;value.misses=misses;value.rejected=rejected;
    value.selfRejected=selfRejected;value.threadRejected=threadRejected;value.rateLimited=rateLimited;value.tick=latestTick;value.thread=latestThread;return value;
}
bool TraceReloadFloor(std::uintptr_t player,Vec3 point,float units,float& height){
 if(!installed||querying||!NativeCoverKL::SimulationCallbackActive(player)||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||
    !NativeCameraMath::Finite(point)||!std::isfinite(units)||units<1||units>1000)return false;
 NativeWeaponKL::AimFrame pose;World world;if(!NativeWeaponKL::SnapshotAim(pose)||pose.player!=player||!CurrentWorld(world))return false;
 M::Query ray;ray.start=NativeCameraMath::Add(point,{0,.10f*units,0});ray.delta={0,-3.1f*units,0};const auto before=ray;
 struct Scope {Scope(){querying=true;}~Scope(){querying=false;}} scope;
 const auto result=reinterpret_cast<RoomRay>(image+I::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
 World after;Vec3 hit;bool self=false;
 if(!NativeCoverKL::SimulationCallbackActive(player)||!CurrentWorld(after)||std::memcmp(&world,&after,sizeof(world))||!M::Hit(before,ray,result,hit)||
    !Contact(ray.reference,player,pose.root,pose.weapon,self)||self)return false;
 height=hit.y;return true;
}
GazeTrace TraceCommand(const NativeWeaponKL::AimFrame& pose,Vec3 direction,Endpoint& output,float rangeUnits) {
    if(!installed||querying||!NativeCoverKL::SimulationCallbackActive(pose.player)||
       !M::Valid(pose,GetTickCount())||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||
       static_cast<std::uint32_t>(VRBridge::RecenterAck())!=pose.recenterAck)return GazeTrace::Unavailable;
    Vec3 unit;if(!M::Unit(direction,unit))return GazeTrace::Unavailable;
    World world;if(!CurrentWorld(world))return GazeTrace::Unavailable;
    const bool toActor=rangeUnits!=0;if(!toActor)rangeUnits=M::MaximumRangeMetres*pose.unitsPerMetre;
    if(!std::isfinite(rangeUnits)||rangeUnits<=0||(toActor&&rangeUnits>25000.f))return GazeTrace::Unavailable;
    M::Query ray;ray.start=pose.origin;ray.delta=NativeCameraMath::Scale(unit,rangeUnits);
    struct Scope {Scope(){querying=true;}~Scope(){querying=false;}} scope;
    const auto before=ray;
    const auto result=reinterpret_cast<RoomRay>(image+I::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
    World afterWorld;NativeWeaponKL::AimFrame after;
    if(!NativeCoverKL::SimulationCallbackActive(pose.player)||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||
       static_cast<std::uint32_t>(VRBridge::RecenterAck())!=pose.recenterAck||
       !NativeWeaponKL::SnapshotAim(after)||!M::Same(pose,after)||!CurrentWorld(afterWorld)||
       std::memcmp(&world,&afterWorld,sizeof(world)))return GazeTrace::Unavailable;
    if(!(result&0xff))return GazeTrace::Miss;
    Vec3 point;bool self=false;
    if(!M::Hit(before,ray,result,point)||!Contact(ray.reference,pose.player,pose.root,pose.weapon,self)||self)return GazeTrace::Unavailable;
    Endpoint next;next.valid=true;next.pose=pose;next.point=point;next.tick=GetTickCount();
    next.reference=ray.reference;next.room=world.room;output=next;return GazeTrace::Hit;
}
GazeTrace TraceHeadCommand(const NativeWeaponKL::HeadGaze& head,Vec3 direction,Endpoint& output,float rangeUnits) {
    NativeWeaponKL::HeadGaze current;
    if(!installed||querying||!NativeCoverKL::SimulationCallbackActive(head.player)||!NativeTeamCommandsKL::ReadHeadRay(head.player,current)||
       !NativeCommandRayKL::Same(head,current))return GazeTrace::Unavailable;
    Vec3 unit;if(!M::Unit(direction,unit))return GazeTrace::Unavailable;
    World world;if(!CurrentWorld(world))return GazeTrace::Unavailable;
    const bool toActor=rangeUnits!=0;if(!toActor)rangeUnits=M::MaximumRangeMetres*head.unitsPerMetre;
    if(!std::isfinite(rangeUnits)||rangeUnits<=0||(toActor&&rangeUnits>25000.f))return GazeTrace::Unavailable;
    M::Query ray;ray.start=head.origin;ray.delta=NativeCameraMath::Scale(unit,rangeUnits);
    if(!NativeCameraMath::Finite(ray.delta))return GazeTrace::Unavailable;
    struct Scope {Scope(){querying=true;}~Scope(){querying=false;}} scope;
    const auto before=ray;
    const auto result=reinterpret_cast<RoomRay>(image+I::RoomRay)(reinterpret_cast<void*>(world.room),&ray,0,6,UINT32_MAX,1,1,UINT32_MAX);
    World afterWorld;NativeWeaponKL::HeadGaze after;
    if(!NativeCoverKL::SimulationCallbackActive(head.player)||!NativeTeamCommandsKL::ReadHeadRay(head.player,after)||
       !NativeCommandRayKL::Same(head,after)||!CurrentWorld(afterWorld)||std::memcmp(&world,&afterWorld,sizeof(world)))return GazeTrace::Unavailable;
    if(!(result&0xff))return GazeTrace::Miss;
    Vec3 point;bool self=false;
    if(!M::Hit(before,ray,result,point)||!Contact(ray.reference,head.player,0,0,self)||self)return GazeTrace::Unavailable;
    Endpoint next;next.valid=true;next.point=point;next.tick=GetTickCount();next.reference=ray.reference;next.room=world.room;
    output=next;return GazeTrace::Hit;
}
}
