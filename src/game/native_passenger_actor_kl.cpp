#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_passenger_actor_kl.hpp"
#include "native_mounted_aim_kl.hpp"
#include "native_cover_kl.hpp"
#include "vr_bridge.hpp"
#include <atomic>

namespace NativePassengerActorKL {namespace {
namespace Attachment=NativePassengerAttachmentKL;
using QueueIdentity=Attachment::Identity;using Entry=Attachment::Entry;
std::uintptr_t image=0;
std::atomic<bool> installed{false};std::atomic<DWORD> simulationThread{0};
std::atomic<bool> seatedOnly{false};
std::atomic<CollisionProvider> collisionProvider{nullptr};
std::atomic<bool> collisionRequired{false};
SRWLOCK targetLock=SRWLOCK_INIT,statsLock=SRWLOCK_INIT;
Request target;Stats stats;Motion motion;
std::uint64_t motionRevision=0; // protected by statsLock; never reset with a lease
struct Applied {bool valid=false;QueueIdentity identity{};Vec3 original{},modified{};Basis localBasis{};std::uint32_t epoch=0;Vec3 actorLocal{};float units=100;};
thread_local Applied applied;thread_local unsigned depth=0,ownedDepth=0;
thread_local bool ownedFrames[32]{};
thread_local std::uintptr_t quarantineChild=0;thread_local std::uint32_t quarantineEpoch=0;
struct Certificate {bool valid=false;Request request{};QueueIdentity identity{};Vec3 accepted{};};
thread_local Certificate certificate;
bool SameContext(const Request& a,const Request& b){return a.player==b.player&&a.movement==b.movement&&a.camera==b.camera&&
    a.epoch==b.epoch&&a.recenter==b.recenter&&a.units==b.units;}
bool Read(std::uintptr_t at,void* output,std::size_t size){SIZE_T got=0;return NativeActionKL::Address(at,size)&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),output,size,&got)&&got==size;}
bool WritePosition(std::uintptr_t entry,Vec3 position){SIZE_T put=0;
    return NativeActionKL::Address(entry+8,sizeof(position))&&WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(entry+8),&position,sizeof(position),&put)&&put==sizeof(position);}
bool Equal(Vec3 a,Vec3 b){return !std::memcmp(&a,&b,sizeof(a));}
void Reject(unsigned stage,const Attachment::Probe* probe=nullptr){AcquireSRWLockExclusive(&statsLock);++stats.rejected;stats.rejectionStage=stage;if(probe)stats.identity=*probe;ReleaseSRWLockExclusive(&statsLock);}
void ClearApplied(){applied={};certificate={};AcquireSRWLockExclusive(&statsLock);stats.applied=false;motion={};ReleaseSRWLockExclusive(&statsLock);}
bool Restore(){
    if(!applied.valid){ClearApplied();return true;}const auto saved=applied;ClearApplied();
    std::uint32_t data=0,player=0,geometry=0,owner=0,carrierGeometry=0;
    if(!Read(image+0x68760C,&data,4)||!Read(data+0xA2C,&player,4)||player!=saved.identity.player||
       !Read(player+4,&geometry,4)||geometry!=saved.identity.geometry||!Read(geometry+0x60,&owner,4)||owner!=saved.identity.geometryOwner||
       !Read(saved.identity.carrier+4,&carrierGeometry,4)||carrierGeometry!=saved.identity.carrierGeometry)return false;
    auto current=saved.identity;Entry entry;
    if(!Attachment::Find(current.carrier,player,geometry,owner,Read,current,entry))return true;
    if(current.child!=saved.identity.child||current.slot!=saved.identity.slot||
       std::memcmp(&entry.localBasis,&saved.localBasis,sizeof(saved.localBasis))||!Equal(entry.localPosition,saved.modified)){
        quarantineChild=saved.identity.child;quarantineEpoch=saved.epoch;
        AcquireSRWLockExclusive(&statsLock);++stats.diverged;stats.rejectionStage=71;ReleaseSRWLockExclusive(&statsLock);return false;
    }
    if(!WritePosition(current.entry,saved.original)){Reject(72);return false;}
    Vec3 after;if(!Read(current.entry+8,&after,sizeof(after))||!Equal(after,saved.original)){Reject(73);return false;}
    AcquireSRWLockExclusive(&statsLock);++stats.restored;ReleaseSRWLockExclusive(&statsLock);return true;
}
bool Hold(){
    if(!applied.valid)return false;QueueIdentity identity;Entry entry;Attachment::Probe probe;
    if(!Attachment::Snapshot(image,applied.identity.player,Read,identity,entry,&probe)||!Attachment::Same(applied.identity,identity)||
       !Equal(entry.localPosition,applied.modified)||std::memcmp(&entry.localBasis,&applied.localBasis,sizeof(entry.localBasis)))return false;
    Vec3 carried;
    if(!Attachment::Target(entry,applied.original,applied.actorLocal,applied.units,carried))return false;
    // Keep the already accepted ACTOR-local location through a tracking/menu
    // gap. A WORLD cached-bone yaw can change while the van moves: recompose
    // this same target, never integrate new navigation or cast fresh rays.
    Entry checked;if(!Read(identity.entry,&checked,sizeof(checked))||std::memcmp(&checked,&entry,sizeof(entry)))return false;
    if(!Equal(carried,entry.localPosition)&&!WritePosition(identity.entry,carried))return false;
    Entry after,expected=entry;expected.localPosition=carried;
    if(!Read(identity.entry,&after,sizeof(after))||std::memcmp(&after,&expected,sizeof(after)))return false;
    applied.identity=identity;applied.modified=carried;certificate={};
    AcquireSRWLockExclusive(&statsLock);motion={};stats.applied=true;++stats.held;ReleaseSRWLockExclusive(&statsLock);return true;
}
void StopNavigation(){if(!Hold())Restore();}
void IdlePose(const Request& request,const QueueIdentity& identity,std::uint32_t now){
    // Fresh exact native attachment is enough to display its OWN idle VR
    // crouch, not to move the actor. This keeps initial/blocked floor probing
    // from exposing the rejected seated camera/body while granting no path.
    Motion idle;idle.active=idle.poseOnly=true;idle.player=request.player;idle.movement=identity.movement;idle.carrier=identity.carrier;
    idle.tick=now;idle.epoch=request.epoch;idle.recenter=request.recenter;idle.requestTick=request.tick;idle.pair=request.pair;
    idle.crouchMetres=std::fmax(.45f,std::fmin(.6f,-request.actorLocalMetres.y));
    AcquireSRWLockExclusive(&statsLock);idle.revision=++motionRevision;motion=idle;ReleaseSRWLockExclusive(&statsLock);
}
void Apply(std::uintptr_t player,bool simulationQuery=false){
    if(simulationThread&&simulationThread!=GetCurrentThreadId()){Reject(53);return;}
    Request request;AcquireSRWLockShared(&targetLock);request=target;ReleaseSRWLockShared(&targetLock);
    const auto now=static_cast<std::uint32_t>(GetTickCount());
    if(!installed||request.player!=player||!Valid(request,now)||request.recenter!=static_cast<std::uint32_t>(VRBridge::RecenterAck())){StopNavigation();return;}
    QueueIdentity identity;Entry entry;Attachment::Probe probe;std::uint32_t camera=0;
    AcquireSRWLockExclusive(&statsLock);stats.requestCamera=static_cast<std::uint32_t>(request.camera);ReleaseSRWLockExclusive(&statsLock);
    if(!Attachment::Snapshot(image,player,Read,identity,entry,&probe)){Restore();Reject(probe.stage,&probe);return;}
    const bool cameraRead=Read(player+0x9A0,&camera,4);
    AcquireSRWLockExclusive(&statsLock);stats.currentCamera=camera;stats.identity=probe;ReleaseSRWLockExclusive(&statsLock);
    if(!cameraRead||camera!=request.camera){Restore();Reject(50,&probe);return;}
    if(identity.movement!=request.movement){Restore();Reject(51,&probe);return;}
    if(applied.valid&&!Attachment::Same(applied.identity,identity)){
        Restore();if(!Attachment::Snapshot(image,player,Read,identity,entry,&probe)){Reject(probe.stage,&probe);return;}
    }
    const bool keep=applied.valid&&Attachment::Same(applied.identity,identity);
    if(keep&&(!Equal(entry.localPosition,applied.modified)||std::memcmp(&entry.localBasis,&applied.localBasis,sizeof(entry.localBasis)))){
        quarantineChild=identity.child;quarantineEpoch=request.epoch;ClearApplied();Reject(60,&probe);return;
    }
    if(quarantineChild==identity.child&&quarantineEpoch==request.epoch){Reject(61,&probe);return;}
    quarantineChild=0;quarantineEpoch=0;
    const auto original=keep?applied.original:entry.localPosition;Vec3 next;
    // Y is avatar crouch/eye pose, never a second whole-character drop
    // through the native van floor. Floor correction is separately queried.
    Vec3 planar{request.actorLocalMetres.x,0,request.actorLocalMetres.z};bool collisionHeld=false;
    if(collisionRequired.load(std::memory_order_acquire)&&!collisionProvider.load(std::memory_order_acquire)){
        Restore();IdlePose(request,identity,now);Reject(81,&probe);return;}
    if(const auto provider=collisionProvider.load(std::memory_order_acquire)){
        const bool certified=certificate.valid&&SameContext(certificate.request,request)&&Attachment::Same(certificate.identity,identity);
        if(!certified)certificate={};
        if(simulationQuery){
            CollisionQuery query;query.request=request;query.identity=identity;query.entry=entry;query.originalOffset=original;
            query.previousActorLocalMetres=certified?certificate.accepted:keep?applied.actorLocal:Vec3{};query.desiredActorLocalMetres=planar;Vec3 accepted;
            if(provider(query,accepted)&&Finite(accepted)&&std::fabs(accepted.x)<=1.101f&&std::fabs(accepted.z)<=2.001f&&std::fabs(accepted.y)<.0001f)
                certificate={true,request,identity,accepted};
            else if(keep){
                // This input pair was processed but cannot advance. Keep the
                // already owned location, stop its gait and acknowledge that
                // held target so render prediction cannot pile up at a wall.
                // This grants no new location/floor/collision authority.
                certificate={true,request,identity,applied.actorLocal};
                collisionHeld=true;
                AcquireSRWLockExclusive(&statsLock);++stats.held;ReleaseSRWLockExclusive(&statsLock);
            }
        }
        if(!certificate.valid||!Valid(certificate.request,now)||!SameContext(certificate.request,request)||
           !Attachment::Same(certificate.identity,identity)){
            StopNavigation();if(!applied.valid)IdlePose(request,identity,now);Reject(80,&probe);return;}
        // Movement/gait and navigation feedback identify the ACCEPTED pair,
        // never a newer uncrossed render request waiting for collision proof.
        request=certificate.request;planar=certificate.accepted;
    }
    if(!Attachment::Target(entry,original,planar,request.units,next)){Reject(52,&probe);return;}
    auto expectedThread=DWORD{0};if(!simulationThread.compare_exchange_strong(expectedThread,GetCurrentThreadId())&&expectedThread!=GetCurrentThreadId()){Reject(53,&probe);return;}
    Entry checked;if(!Read(identity.entry,&checked,sizeof(checked))||std::memcmp(&checked,&entry,sizeof(entry))){Reject(54,&probe);return;}
    if(!Equal(next,entry.localPosition)&&!WritePosition(identity.entry,next)){Reject(55,&probe);return;}
    Entry after;if(!Read(identity.entry,&after,sizeof(after))||after.child!=entry.child||after.slot!=entry.slot||
       std::memcmp(&after.localBasis,&entry.localBasis,sizeof(entry.localBasis))||!Equal(after.localPosition,next)){Reject(56,&probe);return;}
    applied={true,identity,original,next,entry.localBasis,request.epoch,planar,request.units};
    AcquireSRWLockExclusive(&statsLock);stats.applied=true;stats.rejectionStage=0;stats.player=player;stats.geometry=identity.geometry;
    stats.parent=identity.parent;stats.carrier=identity.carrier;stats.entry=identity.entry;stats.child=identity.child;stats.pair=request.pair;
    stats.simulationThread=GetCurrentThreadId();stats.localMetres={planar.x,request.actorLocalMetres.y,planar.z};stats.nativePosition=original;stats.shiftedPosition=next;
    const bool previousMotion=motion.active&&motion.player==player&&motion.movement==identity.movement&&motion.carrier==identity.carrier&&
        motion.epoch==request.epoch&&motion.recenter==request.recenter;
    if(!previousMotion)motion={};
    if(!previousMotion||motion.requestTick!=request.tick||motion.pair!=request.pair||!Equal(motion.offsetMetres,planar)){
        const auto difference=Subtract(planar,motion.offsetMetres);const auto dt=request.tick-motion.requestTick;
        const float distance=std::hypot(difference.x,difference.z);
        if(previousMotion&&dt==0){
            // Coarse GetTickCount may cover more than one stereo pair.
            // Keep the gait sample until time advances instead of alternating
            // moving/idle feet every other pair at high native frame rates.
            // Native simulation may also accept another bounded part of one
            // render request in this same tick/pair. Its actual location must
            // immediately reach camera/body/navigation feedback.
            if(distance<=.35f)motion.phase=std::remainder(motion.phase+distance*(6.283185307179586f/.65f),6.283185307179586f);
            motion.offsetMetres=planar;motion.pair=request.pair;
        }else if(previousMotion&&std::int32_t(dt)>0&&dt<=250&&distance<=.35f){
            motion.phase=std::remainder(motion.phase+distance*(6.283185307179586f/.65f),6.283185307179586f);
            motion.velocityMetres=Scale(difference,1000.f/dt);motion.speed=std::fmin(distance*1000.f/dt,1.5f);
            motion.offsetMetres=planar;motion.requestTick=request.tick;motion.pair=request.pair;
        }else {motion.velocityMetres={};motion.speed=0;motion.offsetMetres=planar;motion.requestTick=request.tick;motion.pair=request.pair;}
    }
    motion.active=true;motion.poseOnly=false;motion.player=player;motion.movement=identity.movement;motion.carrier=identity.carrier;motion.revision=++motionRevision;
    if(collisionHeld){motion.speed=0;motion.velocityMetres={};}
    motion.tick=now;motion.epoch=request.epoch;motion.recenter=request.recenter;
    // Cargo is deliberately low-profile. Physical/B crouch may lower it a
    // little farther, within anatomically bounded lower-body IK reach.
    motion.crouchMetres=std::fmax(.45f,std::fmin(.6f,-request.actorLocalMetres.y));
    if(!Equal(next,entry.localPosition))++stats.translated;ReleaseSRWLockExclusive(&statsLock);
    QueueIdentity current;Entry currentEntry;
    if(!Attachment::Snapshot(image,player,Read,current,currentEntry,&probe)||!Attachment::Same(identity,current)){Reject(57,&probe);return;}
}
void Observe(NativeMountedAimKL::UpdatePhase phase,std::uintptr_t player){using Phase=NativeMountedAimKL::UpdatePhase;
    if(phase==Phase::Before){
        Request request;AcquireSRWLockShared(&targetLock);request=target;ReleaseSRWLockShared(&targetLock);
        const bool owned=player&&((applied.valid&&applied.identity.player==player)||request.player==player);
        const auto index=depth++;if(index>=32)return;ownedFrames[index]=owned;
        if(!owned||++ownedDepth!=1)return;
        if(simulationThread&&simulationThread!=GetCurrentThreadId())return;
        if(applied.valid&&applied.identity.player!=player)ClearApplied();
        AcquireSRWLockExclusive(&statsLock);++stats.updates;ReleaseSRWLockExclusive(&statsLock);
        Apply(player);return;
    }
    if(!depth)return;const auto index=--depth;if(index>=32)return;
    const bool owned=ownedFrames[index];ownedFrames[index]=false;if(!owned||!ownedDepth||--ownedDepth)return;
    if(simulationThread&&simulationThread!=GetCurrentThreadId())return;
    if(phase==Phase::Aborted){Restore();AcquireSRWLockExclusive(&statsLock);++stats.aborted;ReleaseSRWLockExclusive(&statsLock);return;}
    Apply(player);
}
}
bool Install(LogCallback log){
    if(installed)return !seatedOnly;image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!NativeMountedAimKL::Install(log)||!Verify(image,Read)){
        if(log)log("KL1 passenger actor: native attachment consumer proof missing; cargo displacement disabled.");return false;
    }
    if(!NativeMountedAimKL::SetUpdateObserver(Observe))return false;collisionRequired=true;installed=true;
    if(log)log("KL1 passenger actor: owned native carrier queue local offset; native carry/child setter and entry/exit retained.");return true;
}
bool Publish(const Request& request){
    if(!installed||seatedOnly||!Valid(request,GetTickCount())||request.recenter!=static_cast<std::uint32_t>(VRBridge::RecenterAck())){Suspend();return false;}
    AcquireSRWLockExclusive(&targetLock);target=request;ReleaseSRWLockExclusive(&targetLock);
    AcquireSRWLockExclusive(&statsLock);++stats.published;ReleaseSRWLockExclusive(&statsLock);return true;
}
void SetCollisionProvider(CollisionProvider provider){collisionProvider.store(provider,std::memory_order_release);}
void SimulationTick(std::uintptr_t player){if(installed&&!seatedOnly&&player&&NativeCoverKL::SimulationCallbackActive(player))Apply(player,true);}
void Suspend(){AcquireSRWLockExclusive(&targetLock);target={};ReleaseSRWLockExclusive(&targetLock);
    if(seatedOnly){AcquireSRWLockExclusive(&statsLock);motion={};ReleaseSRWLockExclusive(&statsLock);}}
namespace {bool BeginSeated(std::uintptr_t candidate,LogCallback log){
    if(installed)return seatedOnly;
    if(!Verify(candidate,Read)||!NativeActionKL::InitializeVerifiedImage(candidate))return false;
    image=candidate;seatedOnly=true;installed=true;Suspend();
    if(log)log("KL1 seated passenger: read-only exact attachment context; accepted idle crouch/eye, no actor navigation, queue writes or collision queries.");return true;
}}
bool InstallSeated(LogCallback log){return BeginSeated(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),log);}
bool SetSeatedContext(std::uintptr_t player,std::uintptr_t camera,std::uintptr_t movement,
                      std::uint64_t pair,std::uint32_t recenter,std::uint32_t tick,float units){
    Request request;request.player=player;request.camera=camera;request.movement=movement;request.pair=pair;request.recenter=recenter;
    request.tick=tick;request.units=units;request.epoch=1;const auto now=GetTickCount();QueueIdentity identity;Entry entry;Attachment::Probe probe;
    const auto reject=[&](unsigned stage){Suspend();Reject(stage,&probe);AcquireSRWLockExclusive(&statsLock);++stats.seatedRejected;ReleaseSRWLockExclusive(&statsLock);return false;};
    if(!installed||!seatedOnly||!Valid(request,now)||recenter!=static_cast<std::uint32_t>(VRBridge::RecenterAck())||!VRBridge::Ready())return reject(90);
    if(!Attachment::Snapshot(image,player,Read,identity,entry,&probe))return reject(probe.stage);
    std::uint32_t currentCamera=0;const bool cameraRead=Read(player+0x9A0,&currentCamera,4);
    AcquireSRWLockExclusive(&statsLock);stats.requestCamera=static_cast<std::uint32_t>(camera);stats.currentCamera=currentCamera;stats.identity=probe;ReleaseSRWLockExclusive(&statsLock);
    if(identity.movement!=movement||!cameraRead||currentCamera!=camera)return reject(91);
    IdlePose(request,identity,now);
    AcquireSRWLockExclusive(&statsLock);++stats.seatedFrames;stats.rejectionStage=0;stats.player=player;stats.geometry=identity.geometry;stats.parent=identity.parent;
    stats.carrier=identity.carrier;stats.entry=identity.entry;stats.child=identity.child;stats.pair=pair;ReleaseSRWLockExclusive(&statsLock);return true;
}
bool ReadMotion(std::uintptr_t player,std::uint32_t recenter,Motion& output){
    Motion copy;AcquireSRWLockShared(&statsLock);copy=motion;ReleaseSRWLockShared(&statsLock);
    const auto now=GetTickCount();const auto action=NativeActionKL::Current();
    if(recenter==0xFFFFFFFFu)recenter=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    if(!copy.active||copy.player!=player||copy.recenter!=recenter||!copy.tick||std::int32_t(now-copy.tick)<0||now-copy.tick>250||
       !VRBridge::Ready()||!action.valid||action.player!=player||action.movement!=copy.movement||
       action.movementKind!=NativeActionKL::Movement::Vehicle||(action.action&&!action.TrackedReload()&&!action.TrackedWeaponGesture()))return false;
    output=copy;return true;
}
Stats Snapshot(){AcquireSRWLockShared(&statsLock);auto out=stats;out.motion=motion;ReleaseSRWLockShared(&statsLock);out.installed=installed;out.seatedOnly=seatedOnly;return out;}
}
