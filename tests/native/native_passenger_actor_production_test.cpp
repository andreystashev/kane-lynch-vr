// Actual native carrier refresh/child transform consumer, private CPU arena.
// Only the external child class/setter and game update are substituted.
#define main MountedFixtureMain
#include "native_mounted_aim_production_test.cpp"
#undef main
#include "native_action_kl.cpp"
#include "native_passenger_actor_kl.cpp"
#include "native_player_anchor.cpp"

namespace PassengerFixture {
using namespace NativeCameraMath;namespace A=NativePassengerAttachmentKL;
using Consume=void(__thiscall*)(void*);using Refresh=void(__thiscall*)(void*,const CameraPose*);
Consume consume=nullptr;Refresh refresh=nullptr;
std::uintptr_t carrier=0,carrierGeometry=0,actorGeometry=0,foreignGeometry=0,boneRows=0,alias=0;
unsigned writes=0,nesting=0;bool nested=false,foreignCallsLocal=false,throws=false;
unsigned collisionCalls=0;bool collisionAllowed=true;float collisionFraction=1.f;Vec3 collisionPrevious{};
bool Collision(const NativePassengerActorKL::CollisionQuery& query,Vec3& accepted){
    ++collisionCalls;Fixture::Check(NativeCoverKL::SimulationCallbackActive(query.request.player),"provider invoked only inside owned simulation callback");
    collisionPrevious=query.previousActorLocalMetres;
    accepted=Add(query.previousActorLocalMetres,Scale(Subtract(query.desiredActorLocalMetres,query.previousActorLocalMetres),collisionFraction));accepted.y=0;
    return collisionAllowed;
}
constexpr Basis Identity{{0,0,1},{0,1,0},{1,0,0}};
constexpr Basis Quarter{{1,0,0},{0,1,0},{0,0,-1}};
Vec3 originalOffset{3,4,5};
template<class T>T Get(std::uintptr_t at){T value{};std::memcpy(&value,reinterpret_cast<void*>(at),sizeof(value));return value;}
bool Near(Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.0001f;}
std::uint32_t __thiscall Class(void*){return 0;}
void __thiscall ChildPose(void* child,const Basis* basis,const Vec3* position){
    const auto geometry=Get<std::uint32_t>(reinterpret_cast<std::uintptr_t>(child)+4);
    Fixture::Put(geometry,CameraPose{*basis,*position});++writes;
}
void ConsumeNow(){refresh(reinterpret_cast<void*>(carrier),reinterpret_cast<const CameraPose*>(boneRows));consume(reinterpret_cast<void*>(carrier));}
void __thiscall Update(void* object){
    if(nesting)return;const auto player=reinterpret_cast<std::uintptr_t>(object);
    if(player!=Fixture::player){if(foreignCallsLocal)NativeMountedAimKL::UpdateHook(reinterpret_cast<void*>(Fixture::player),nullptr);return;}
    if(nested){++nesting;NativeMountedAimKL::UpdateHook(reinterpret_cast<void*>(Fixture::otherPlayer),nullptr);--nesting;}
    ConsumeNow();if(throws)throw 7;
}
void Copy(const NativeHudFixture::Image& source,std::uint32_t rva,std::size_t bytes){
    Fixture::Check(source.Read(NativeHudFixture::Base+rva,reinterpret_cast<void*>(NativeMountedAimKL::image+rva),bytes),"copy bounded actual native function bytes");
}
void Step(){NativeMountedAimKL::UpdateHook(reinterpret_cast<void*>(Fixture::player),nullptr);ConsumeNow();}
void Simulate(){Fixture::observer=true;NativePassengerActorKL::SimulationTick(Fixture::player);Fixture::observer=false;ConsumeNow();}
CameraPose Actor(){return Get<CameraPose>(actorGeometry);}
A::Entry Entry(unsigned index){return Get<A::Entry>(carrier+0x128+index*sizeof(A::Entry));}
void PutEntry(unsigned index,const A::Entry& entry){Fixture::Put(carrier+0x128+index*sizeof(A::Entry),entry);}
}
#ifndef KL_PASSENGER_FIXTURE_MAIN
#define KL_PASSENGER_FIXTURE_MAIN main
#endif
int KL_PASSENGER_FIXTURE_MAIN(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto prior=MountedFixtureMain(argc,argv);if(prior)return prior;
    using namespace Fixture;using namespace PassengerFixture;namespace N=NativePassengerActorKL;
    if(argc<2){std::puts("SKIP provide supported EXE for native carrier CPU proof.");return 0;}
    const int pathChars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,nullptr,0);
    Check(pathChars>1,"read-only proof path is valid UTF-8");std::vector<wchar_t> path(static_cast<std::size_t>(pathChars));
    Check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path.data(),pathChars)==pathChars,"decode supported EXE path");
    char digest[65]{};Check(HashFile(path.data(),digest)&&IsSupportedExecutableHash(digest),"hash pin before executing selected copied native routines");
    NativeHudFixture::Image actual;Check(actual.Load(argv[1]),"load supported PE without starting game");
    Check(N::Verify(NativeHudFixture::Base,actual.Reader()),"actual native attachment consumer/append layout signatures");
    Reset();tick=1000;ack=7;team=false;inventory=false;ready=true;cinema=false;
    N::image=NativeMountedAimKL::image;N::installed=true;N::stats={};N::applied={};N::motion={};N::simulationThread=0;
    N::quarantineChild=0;N::quarantineEpoch=0;N::depth=N::ownedDepth=0;
    Check(NativeMountedAimKL::SetUpdateObserver(N::Observe),"native player-update observer installs exact carrier adapter");
    NativeMountedAimKL::update=PassengerFixture::Update;
    for(const auto part:{std::pair<std::uint32_t,unsigned>{0x1A18C0,0x77},{0x1A1940,0xD3},{0x2ED20,0x23},{0x20860,0x100},
                         {0x1F980,0x50},{0x2DFF0,0x50},{0xF83A0,8},{0x2BBE0,0x3B},{0x2A1F0,0x41},{0x2A240,0x39},{0x1A1B40,0x100},
                         {0x137BD0,7},{0x137B6D,7},{0x11C2CE,10},{0x11C3BB,7}})Copy(actual,part.first,part.second);
    Check(NativeActionKL::InitializeVerifiedImage(NativeMountedAimKL::image),"real action reader verifies original native state/getter prologues");
    unsigned relocations=0;const auto consumer=NativeMountedAimKL::image+0x1A1940;
    for(unsigned i=0;i+4<=0xD3;++i){const auto value=Get<std::uint32_t>(consumer+i);
        if(value==0xC73A30||value==0xC73A34){Put(consumer+i,std::uint32_t(NativeMountedAimKL::image+value-NativeHudFixture::Base));++relocations;}}
    Check(relocations==2,"only two exact class-mask operands rebased; native math/calls remain actual code");
    Put(NativeMountedAimKL::image+0x873A30,std::uint32_t(1));Put(NativeMountedAimKL::image+0x873A34,std::uint32_t(0xFFFFFFFF));
    consume=reinterpret_cast<Consume>(consumer);refresh=reinterpret_cast<Refresh>(NativeMountedAimKL::image+0x1A18C0);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(NativeMountedAimKL::image),0x900000);
    carrier=Allocate(0x1000);carrierGeometry=Allocate(0x100);actorGeometry=Allocate(0x100);foreignGeometry=Allocate(0x100);boneRows=Allocate(0x100);alias=Allocate(0x200);
    Put(player+4,std::uint32_t(actorGeometry));NativePlayerPose::Geometry geometryPose;geometryPose.pose.basis=Identity;
    Put(actorGeometry,geometryPose);Put(actorGeometry+0x60,std::uint32_t(player));Put(carrier+4,std::uint32_t(carrierGeometry));
    Put(carrierGeometry,geometryPose);Put(carrierGeometry+0x60,std::uint32_t(carrier));
    Put(otherPlayer+4,std::uint32_t(foreignGeometry));Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+0x56582C));Put(foreignGeometry,geometryPose);Put(foreignGeometry+0x60,std::uint32_t(otherPlayer));
    Put(NativeMountedAimKL::image+0x56582C+0x30,reinterpret_cast<std::uintptr_t>(Class));Put(NativeMountedAimKL::image+0x56582C+0x98,reinterpret_cast<std::uintptr_t>(ChildPose));
    Put(movement,std::uint32_t(NativeMountedAimKL::image+0x566D14));Put(movement+0x70,std::uint32_t(player));Put(dispatcher+4,std::uint32_t(0));
    Put(record+0x18,std::uint32_t(carrier));Put(record+0x30,std::uint32_t(0xFFFFFFFF));Put(player+0x564,std::uint32_t(0));
    const auto camera=Allocate(0x500),model=Allocate(0x500),lookup=Allocate(0x100),poses=Allocate(0x100);
    Put(player+0x9A0,std::uint32_t(camera));Put(camera,std::uint32_t(NativeMountedAimKL::image+0x545A3C));Put(player+0x70,std::uint32_t(model));
    Put(model+0xC,std::uint32_t(lookup));Put(model+0x4A0,std::uint32_t(1));Put(model+0x4D0,std::uint32_t(poses));Put(lookup+64,std::uint8_t(0));Put(poses,CameraPose{Identity,{0,120,0}});
    const unsigned char getter[]={0x8b,0x44,0x24,4,0x85,0xc0,0x7c,5,0x83,0xf8,0x10,0x7c,1,0xcc,0x8b,0x84,0x81,0x2c,0x0a,0,0,0xc2,4,0};
    std::memcpy(reinterpret_cast<void*>(NativeMountedAimKL::image+0x26FBD0),getter,sizeof(getter));
    Check(NativePlayerAnchor::InitializeVerifiedImage(NativeMountedAimKL::image),"actual player anchor getter proof usable in private arena");
    A::Entry foreign;foreign.child=otherPlayer;foreign.slot=0;foreign.localBasis=Identity;foreign.boneBasis=Identity;foreign.localPosition={50,60,70};
    A::Entry local;local.child=player;local.slot=1;local.localBasis=Quarter;local.boneBasis=Quarter;local.localPosition=originalOffset;
    Put(carrier+0x124,std::uint32_t(2));PutEntry(0,foreign);PutEntry(1,local);
    Put(boneRows,CameraPose{Identity,{400,500,600}});Put(boneRows+sizeof(CameraPose),CameraPose{Quarter,{100,200,300}});
    ConsumeNow();const auto baseline=Actor();const auto before=NativePlayerAnchor::Current(100,1700,true);
    Check(before.valid&&before.headBone,"actual anatomy anchored before carrier movement");const auto untouchedForeign=Entry(0);
    N::Request request{player,movement,ack,tick,10,{.2f,-.3f,.1f},100,1,camera};Check(N::Publish(request),"fresh request despite recordSlotFFFFFFFF/player564zero");Step();
    Check(N::Snapshot().applied&&N::Snapshot().translated>0&&N::Snapshot().identity.stage==100,"actual van queue identity applies instead of all938 requests rejected");
    Check(Near(Subtract(Actor().position,baseline.position),{-20,0,-10}),"actual native basis composition carries requested actor-local XZ; Y does not sink actor below floor");
    const auto moved=NativePlayerAnchor::Current(100,1700,true);Check(moved.valid&&Near(Subtract(moved.position,before.position),{-20,0,-10}),"full native child geometry and actual camera anchor move together without camera-only offset");
    const auto foreignAfter=Entry(0);Check(!std::memcmp(&untouchedForeign,&foreignAfter,sizeof(foreignAfter)),"owned displacement leaves foreign attachment bytes unchanged");
    const auto shifted=Entry(1).localPosition;
    for(unsigned i=0;i<20;++i){++tick;request.tick=tick;++request.pair;Check(N::Publish(request),"refresh same native cargo request");Step();Check(Near(Entry(1).localPosition,shifted)&&Near(Subtract(Actor().position,baseline.position),{-20,0,-10}),"repeated before/after updates do not accumulate position");}
    N::Motion motion;Check(N::ReadMotion(player,ack,motion)&&motion.active&&motion.crouchMetres>=.45f,"same owned carrier motion drives crouch pose without rootY translation");
    Put(boneRows+sizeof(CameraPose),CameraPose{Identity,{1000,400,2000}});Step();
    Check(Near(Actor().position,{1013,404,1985}),"actual refresh/consumer moves actor with translated and turned unparented carrier bone frame");
    const Basis pitch{{0,-1,0},{0,0,1},{1,0,0}};
    Put(boneRows+sizeof(CameraPose),CameraPose{pitch,{1000,400,2000}});Step();
    const auto noncommuting=Actor();
    Check(Near(noncommuting.basis.forward,{0,-1,0})&&Near(noncommuting.basis.up,{1,0,0})&&Near(noncommuting.basis.right,{0,0,-1}),
        "actual native pitch/yaw composition order is noncommuting and matches child orientation");
    Check(Near(noncommuting.position,{1003,394,1985}),"actor-local displacement uses that same noncommuting native composition");
    Put(boneRows+sizeof(CameraPose),CameraPose{Identity,{1000,400,2000}});Step();
    // The native conversion applies geometry itself only when it has a parent.
    // This case uses cached local bones and a moving nontrivial ancestry.
    const auto parent=Allocate(0x100);NativePlayerPose::Geometry parentPose;parentPose.pose={Quarter,{2000,300,4000}};Put(parent,parentPose);
    geometryPose.pose={Identity,{10,20,30}};geometryPose.parent=parent;Put(carrierGeometry,geometryPose);Put(carrierGeometry+0x60,std::uint32_t(carrier));
    Step();CameraPose expectedFrame;Check(NativePlayerPose::World(carrierGeometry,N::Read,expectedFrame),"real carrier ancestry readable");
    const auto expected=Add(expectedFrame.position,NativePlayerPose::Transform(expectedFrame.basis,Vec3{1013,404,1985}));
    Check(Near(Actor().position,expected),"actual4F83A0/42BBE0 path carries model child through changed parent world pose");
    const auto compact=Entry(1);PutEntry(0,compact);Put(carrier+0x124,std::uint32_t(1));Step();
    Check(N::Snapshot().applied&&Near(Entry(0).localPosition,compact.localPosition)&&N::Snapshot().entry==carrier+0x128,"native queue compaction preserves semantic child ownership and does not double offset");
    const auto stableActor=Actor();NativeMountedAimKL::UpdateHook(reinterpret_cast<void*>(otherPlayer),nullptr);Check(Near(Actor().position,stableActor.position),"foreign native update cannot retire/move local cargo child");
    nested=true;Step();nested=false;Check(N::Snapshot().applied&&!NativeMountedAimKL::active.player,"nested foreign update preserves local attachment capability");
    foreignCallsLocal=true;NativeMountedAimKL::UpdateHook(reinterpret_cast<void*>(otherPlayer),nullptr);foreignCallsLocal=false;ConsumeNow();Check(N::Snapshot().applied,"local update nested inside foreign update retains correct queue ownership");
    const auto heldOnPause=Entry(0).localPosition;N::Suspend();Step();
    Check(N::Snapshot().applied&&Near(Entry(0).localPosition,heldOnPause)&&!N::Snapshot().motion.active,"pause holds owned cargo location instead of teleporting back to seat");
    Check(N::Publish(request),"rearm before native action");Step();Put(dispatcher+4,std::uint32_t(otherPlayer));Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+0x5667F4));Step();
    Check(!N::Snapshot().applied&&Near(Entry(0).localPosition,originalOffset),"authored action transition restores exact native queue field");Put(dispatcher+4,std::uint32_t(0));Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+0x56582C));
    request.camera=camera+4;Check(N::Publish(request),"request camera rechecked at simulation");Step();Check(!N::Snapshot().applied&&N::Snapshot().rejectionStage==50,"foreign camera rejected with useful stage");request.camera=camera;
    Check(N::Publish(request),"rearm before stale source");Step();const auto stalePosition=Entry(0).localPosition;tick+=151;Step();
    Check(N::Snapshot().applied&&Near(Entry(0).localPosition,stalePosition)&&!N::Snapshot().motion.active,"stale input holds last accepted position and retires gait input");
    request.tick=tick;Check(N::Publish(request),"rearm before C++ unwind");throws=true;bool caught=false;try{Step();}catch(int){caught=true;}throws=false;ConsumeNow();
    Check(caught&&!N::Snapshot().applied&&Near(Entry(0).localPosition,originalOffset)&&N::Snapshot().aborted==1,"native unwind restores owned attachment and clears active motion");
    Check(N::Publish(request),"rearm for detach");Step();Put(carrier+0x124,std::uint32_t(0));const CameraPose exitPose{Identity,{700,800,900}};Put(actorGeometry,exitPose);Step();
    Check(!N::Snapshot().applied&&Near(Actor().position,exitPose.position),"native detach/exit keeps new actor pose; removed queue is not resurrected");
    local=compact;local.localPosition=originalOffset;PutEntry(0,local);Put(carrier+0x124,std::uint32_t(1));++request.epoch;Check(N::Publish(request),"new cargo lifetime after native reentry");Step();
    auto changed=Entry(0);changed.localPosition={999,998,997};PutEntry(0,changed);Step();Check(Near(Entry(0).localPosition,changed.localPosition)&&!N::Snapshot().applied,"foreign script local-position change quarantined and preserved");Step();Check(Near(Entry(0).localPosition,changed.localPosition),"quarantined epoch cannot repeatedly accrue offset");
    // A native geometry owner alias is accepted only by its reciprocal link.
    N::Suspend();Step();Put(alias,std::uint32_t(NativeMountedAimKL::image+0x56582C));Put(alias+4,std::uint32_t(actorGeometry));Put(actorGeometry+0x60,std::uint32_t(alias));
    local.child=alias;local.localPosition=originalOffset;PutEntry(0,local);++request.epoch;Check(N::Publish(request),"fresh reciprocal child alias request");Step();
    Check(N::Snapshot().applied&&N::Snapshot().child==alias&&N::Snapshot().identity.geometryOwner==alias,"real queue child alias supported without assuming geometryOwner equals logical player");
    N::Suspend();Step();Put(alias+4,std::uint32_t(actorGeometry+4));Check(N::Publish(request),"invalid alias still checked inside simulation");Step();
    Check(!N::Snapshot().applied&&N::Snapshot().rejectionStage==24,"nonreciprocal geometry alias cannot receive passenger writes");
    // The registered production collision provider grants movement only from
    // the owned simulation callback. Pre/post updates cannot query physics.
    N::Restore();N::ClearApplied();N::quarantineChild=0;N::quarantineEpoch=0;
    Put(actorGeometry+0x60,std::uint32_t(player));Put(alias+4,std::uint32_t(actorGeometry));local.child=player;local.localPosition=originalOffset;PutEntry(0,local);
    tick=2000;request.tick=tick;request.recenter=ack;request.camera=camera;request.epoch=100;request.pair=500;request.actorLocalMetres={.2f,-.3f,.1f};
    N::collisionRequired=true;N::SetCollisionProvider(nullptr);Check(N::Publish(request),"publish production-required collision request");Step();
    Check(!N::Snapshot().applied&&N::Snapshot().rejectionStage==81&&Near(Entry(0).localPosition,originalOffset),"missing collision module cannot use expanded cargo envelope");
    Check(N::Snapshot().motion.active&&N::Snapshot().motion.poseOnly,"first verified owner gets idle crouch without floor/navigation authority");
    N::SetCollisionProvider(Collision);Check(N::Publish(request),"register bounded physics provider");Step();
    Check(collisionCalls==0&&!N::Snapshot().applied,"pre/post actor updates do not call collision or invent a certificate");
    N::SimulationTick(player);Check(collisionCalls==0,"direct call outside owned callback cannot query physics");Simulate();
    Check(collisionCalls==1&&N::Snapshot().applied&&!N::Snapshot().motion.poseOnly&&N::Snapshot().motion.pair==request.pair,"owned simulation grants actual queue movement for accepted source pair");
    const auto firstAccepted=Entry(0).localPosition;tick+=16;request.tick=tick;++request.pair;request.actorLocalMetres={.6f,-.3f,.4f};
    Check(N::Publish(request),"publish expanded cargo prediction beyond old55cm bound");Step();
    Check(collisionCalls==1&&Near(Entry(0).localPosition,firstAccepted)&&N::Snapshot().motion.pair==request.pair-1,"pending render request keeps previous accepted pair and actual position");
    collisionAllowed=false;Simulate();
    Check(N::Snapshot().applied&&Near(Entry(0).localPosition,firstAccepted)&&N::Snapshot().motion.pair==request.pair&&N::Snapshot().motion.speed==0,"blocked input holds crouched avatar and acknowledges same-position feedback");
    const auto heldRevision=N::Snapshot().motion.revision;
    collisionAllowed=true;collisionFraction=.5f;Simulate();
    Check(Near(N::Snapshot().motion.offsetMetres,{.4f,0,.25f})&&N::Snapshot().motion.pair==request.pair,"only geometry-checked partial target drives body and feedback");
    Check(N::Snapshot().motion.revision>heldRevision,"same-pair accepted refinement has a newer body feedback revision");
    const auto retained=N::Snapshot().motion.offsetMetres,retainedOffset=Entry(0).localPosition;
    ++request.epoch;tick+=16;request.tick=tick;++request.pair;request.actorLocalMetres=retained;request.actorLocalMetres.y=-.3f;
    Check(N::Publish(request),"new navigation epoch keeps same owned cargo lifetime");Step();
    Check(Near(Entry(0).localPosition,retainedOffset),"epoch-only transition cannot jump back to original vehicle seat");Simulate();
    Check(Near(collisionPrevious,retained)&&Near(N::Snapshot().motion.offsetMetres,retained),"resume seeds collision from held position rather than zero-relative stale origin");
    ++ack;request.recenter=ack;++request.epoch;tick+=16;request.tick=tick;++request.pair;Check(N::Publish(request),"recenter reauthorizes at same body location");Step();Simulate();
    Check(Near(collisionPrevious,retained)&&Near(Entry(0).localPosition,retainedOffset),"new HMD origin changes view authority without teleporting body");
    const auto callsBeforeStale=collisionCalls;tick+=151;Put(boneRows+sizeof(CameraPose),CameraPose{Quarter,{1100,450,2100}});Step();
    Check(collisionCalls==callsBeforeStale&&N::Snapshot().applied&&!N::Snapshot().motion.active,"stale input does not query rays or authorize a walking pose");
    Check(Near(Entry(0).localPosition,{3-retained.x*100,4,5-retained.z*100}),"held offset recomposes with fresh moving carrier basis without new input");
    Put(dispatcher+4,std::uint32_t(otherPlayer));Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+0x5667F4));Step();
    Check(!N::Snapshot().applied&&Near(Entry(0).localPosition,originalOffset),"authored native exit/action restores original attachment rather than holding stale navigation");
    Put(dispatcher+4,std::uint32_t(0));N::SetCollisionProvider(nullptr);N::collisionRequired=false;request.tick=tick;
    request.tick=tick+1;Check(!N::Publish(request),"future request rejected");request.tick=tick;request.recenter=ack+1;Check(!N::Publish(request),"recenter mismatch rejected");
    request.recenter=ack;request.epoch=0;Check(!N::Publish(request),"missing lifetime rejected");
    Check(NativeMountedAimKL::SetUpdateObserver(nullptr),"private fixture retires observer");
    // .44: accepted .43 poseOnly presentation remains, while production no
    // longer installs the experimental actor/navigation/collision observer.
    N::installed=false;N::seatedOnly=false;N::SetCollisionProvider(Collision);
    const auto seatBefore=N::Snapshot();const auto seatQueue=Entry(0);const auto seatActor=Actor();const auto seatRays=collisionCalls;
    Check(N::BeginSeated(N::image,nullptr)&&N::Snapshot().seatedOnly&&NativeMountedAimKL::updateObserver.load()==nullptr,
        "actual verified seated initializer installs no native player-update observer");
    Check(N::SetSeatedContext(player,camera,movement,800,ack,tick,100)&&N::ReadMotion(player,ack,motion)&&motion.poseOnly&&
        Near(motion.offsetMetres,{})&&motion.speed==0&&motion.phase==0&&motion.crouchMetres==.45f,
        "read-only current attachment emits precisely the accepted .43 idle crouch and no locomotion target");
    request.tick=tick;request.recenter=ack;request.epoch=200;request.actorLocalMetres={1,-.6f,1};
    Check(!N::Publish(request)&&!N::Snapshot().motion.active,"seated mode rejects and retires former navigation publication");
    Check(N::SetSeatedContext(player,camera,movement,801,ack,tick,100),"fresh seat pose may resume after rejected navigation");
    Simulate();const auto seatAfter=Entry(0);
    Check(collisionCalls==seatRays&&N::Snapshot().translated==seatBefore.translated&&N::Snapshot().published==seatBefore.published&&
        !N::Snapshot().applied&&N::Snapshot().motion.active&&N::Snapshot().motion.poseOnly&&
        !std::memcmp(&seatQueue,&seatAfter,sizeof(seatQueue))&&Near(Actor().position,seatActor.position),
        "even owned simulation callback writes no queue/actor navigation or native rays in seated mode");
    const auto movedSeat=CameraPose{Identity,{5000,700,8000}};Put(boneRows+sizeof(CameraPose),movedSeat);ConsumeNow();
    Check(N::SetSeatedContext(player,camera,movement,802,ack,tick,100)&&N::ReadMotion(player,ack,motion)&&Near(motion.offsetMetres,{}),
        "native carrier continues transporting seated actor without virtual walking offset");
    Check(!Near(Actor().position,seatActor.position),"unchanged original native carrier consumer still moves actual seated actor");
    for(auto gesture:{0x566944u,0x566690u,0x5666C4u,0x5666F8u}){
        Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+gesture));Put(dispatcher+4,std::uint32_t(otherPlayer));
        Check(N::SetSeatedContext(player,camera,movement,803,ack,tick,100)&&N::ReadMotion(player,ack,motion),
            "accepted native reload/item/command gestures retain seated idle body and eye");
    }
    Put(otherPlayer,std::uint32_t(NativeMountedAimKL::image+0x5667F4));
    Check(!N::SetSeatedContext(player,camera,movement,804,ack,tick,100)&&!N::Snapshot().motion.active,
        "authored vehicle action retires read-only seat pose without extending its authority");Put(dispatcher+4,std::uint32_t(0));
    Check(!N::SetSeatedContext(player,camera,movement,805,ack,tick-151,100),"stale seat context fails closed");
    Check(!N::SetSeatedContext(player,camera,movement,805,ack,tick+1,100),"future seat context fails closed");
    Check(!N::SetSeatedContext(player,camera,movement,805,ack+1,tick,100),"foreign tracking origin cannot authorize seated eye");
    Check(!N::SetSeatedContext(player,camera+4,movement,805,ack,tick,100),"foreign native camera cannot authorize seated eye");
    Check(!N::SetSeatedContext(player,camera,movement+4,805,ack,tick,100),"foreign native movement cannot authorize seated eye");
    Check(!N::SetSeatedContext(otherPlayer,camera,movement,805,ack,tick,100),"foreign actor cannot authorize seated eye");
    Check(N::SetSeatedContext(player,camera,movement,806,ack,tick,100),"valid read-only seat recovers after rejected contexts");N::Suspend();
    Check(!N::ReadMotion(player,ack,motion)&&!N::Snapshot().motion.active,"scene/menu suspension immediately retires seated presentation without native update");
    std::printf("Passenger actual native carrier consumer: %u total checks PASS.\n",checks);return 0;
}
