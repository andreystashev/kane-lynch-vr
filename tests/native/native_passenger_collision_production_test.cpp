// Real ROOM510830 wrapper, selected native carrier consumer and production
// query guards in private CPU arenas. Physics intersection is a fixture;
// this does not claim a live van floor or a native convex capsule sweep.
#define main ExistingMountedFixtureMain
#include "native_mounted_aim_production_test.cpp"
#undef main
#include "native_action_kl.cpp"
#include "native_passenger_collision_kl.cpp"
namespace CollisionFixture {
namespace C=NativePassengerCollisionKL;using namespace NativeCameraMath;
std::uintptr_t app=0,engine=0,room=0,backend=0,poolData=0;
std::uintptr_t carrier=0,carrierGeometry=0,actorGeometry=0;
const Vec3 originalOffset{3,4,5};
template<class T>T Get(std::uintptr_t at){T value{};std::memcpy(&value,reinterpret_cast<void*>(at),sizeof(value));return value;}
bool Near(Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<.0001f;}
NativePassengerAttachmentKL::Entry Entry(){return Get<NativePassengerAttachmentKL::Entry>(carrier+0x128);}
unsigned backendCalls=0;float floorY=0;enum class CollisionMode {Clear,NoFloor,WorldFloor,Self,Wall,Corrupt,Retire,HeadWall,Ceiling};CollisionMode collisionMode=CollisionMode::Clear;
std::uintptr_t __thiscall Reference(void*,void* geometry){const auto at=reinterpret_cast<std::uintptr_t>(geometry);if(!at)return 0;return (at-poolData)/0x70+1;}
bool __thiscall Backend(void*,void* output,unsigned zero,const Vec3* start,const Vec3* delta,unsigned flags,unsigned mask,unsigned one,unsigned another,unsigned layer){
    ++backendCalls;Fixture::Check(zero==0&&flags==1&&mask==6&&one==1&&another==1&&layer==10,"real ROOM wrapper forwards original mask/flags/layer to physics ABI");
    if(collisionMode==CollisionMode::Retire)Fixture::Put(carrier+0x124,std::uint32_t(0));
    const bool floor=delta->y<0;float fraction=0;std::uintptr_t geometry=poolData+0x70;
    if(floor){if(collisionMode==CollisionMode::NoFloor)return false;fraction=(floorY-start->y)/delta->y;if(fraction<0||fraction>1)return false;
        if(collisionMode==CollisionMode::WorldFloor)geometry=poolData+0xE0;if(collisionMode==CollisionMode::Self)geometry=poolData;
    }else {const bool head=collisionMode==CollisionMode::HeadWall&&start->y>floorY+105;
        const bool ceiling=collisionMode==CollisionMode::Ceiling&&delta->y>0;
        if(collisionMode!=CollisionMode::Wall&&collisionMode!=CollisionMode::Corrupt&&!head&&!ceiling)return false;fraction=collisionMode==CollisionMode::Corrupt?2.f:.5f;geometry=poolData+0xE0;}
    auto* bytes=static_cast<unsigned char*>(output);std::memset(bytes,0,0x54);std::memcpy(bytes,&fraction,4);
    const auto point=Add(*start,Scale(*delta,fraction));std::memcpy(bytes+4,&point,sizeof(point));const auto raw=std::uint32_t(geometry);std::memcpy(bytes+0x10,&raw,4);return true;
}
void Jump(std::uintptr_t at,std::uintptr_t destination){Fixture::Put(at,std::uint8_t(0xE9));Fixture::Put(at+1,std::int32_t(destination-at-5));}
}
int main(int argc,char** argv){const auto before=ExistingMountedFixtureMain(argc,argv);if(before)return before;
    using namespace Fixture;using namespace CollisionFixture;namespace C=NativePassengerCollisionKL;
    const int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,nullptr,0);Check(chars>1,"read-only supported path has valid UTF-8");std::vector<wchar_t> pathName(static_cast<std::size_t>(chars));
    Check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,pathName.data(),chars)==chars,"decode supported native proof path");char digest[65]{};
    Check(HashFile(pathName.data(),digest)&&IsSupportedExecutableHash(digest),"whole supported EXE hash before executing copied ROOM wrapper");
    NativeHudFixture::Image actual;Check(actual.Load(argv[1])&&NativeAimKLImage::Verify(NativeHudFixture::Base,actual.Reader()),"supported original ROOM/backend/full ray schema proofs");
    const auto image=NativeMountedAimKL::image;C::image=image;C::installed=true;C::stats={};C::simulationThread=0;Reset();
    const auto clock=Fixture::tick;contextTick=clock;contextValid=ready=true;team=inventory=cinema=false;observer=true;
    for(const auto span:{std::pair<std::uint32_t,unsigned>{0x137BD0,7},{0x137B6D,7},{0x11C2CE,10},{0x11C3BB,7}})Check(actual.Read(NativeHudFixture::Base+span.first,reinterpret_cast<void*>(image+span.first),span.second),"copy actual action-reader proof");
    Check(NativeActionKL::InitializeVerifiedImage(image),"actual Current action reader enabled in private memory");
    carrier=Allocate(0x1000);carrierGeometry=Allocate(0x100);actorGeometry=Allocate(0x100);const auto camera=Allocate(0x100);
    NativePlayerPose::Geometry geometry;geometry.pose.basis={{0,0,1},{0,1,0},{1,0,0}};Put(actorGeometry,geometry);Put(carrierGeometry,geometry);
    Put(player+4,std::uint32_t(actorGeometry));Put(actorGeometry+0x60,std::uint32_t(player));Put(carrier+4,std::uint32_t(carrierGeometry));Put(carrierGeometry+0x60,std::uint32_t(carrier));
    Put(player+0x9A0,std::uint32_t(camera));Put(movement,std::uint32_t(image+0x566D14));Put(movement+0x70,std::uint32_t(player));Put(record+0x18,std::uint32_t(carrier));
    NativePassengerAttachmentKL::Entry local;local.child=player;local.localBasis=local.boneBasis=geometry.pose.basis;local.bonePosition={100,200,300};local.localPosition=originalOffset;Put(carrier+0x128,local);Put(carrier+0x124,std::uint32_t(1));
    C::Query query;query.request={player,movement,ack,tick,999,{.1f,0,.05f},100,5,Get<std::uint32_t>(player+0x9A0)};
    Check(NativePassengerAttachmentKL::Snapshot(image,player,C::Read,query.identity,query.entry),"collision provider starts from exact current Vehicle queue identity");query.originalOffset=originalOffset;query.desiredActorLocalMetres={.1f,0,.05f};
    C::Path path;Check(C::Prepare(query,C::Read,path),"native working-space point preparation");floorY=path.desired.y;
    app=Allocate(0x100);engine=Allocate(0x100);room=Allocate(0x100);backend=Allocate(0x100);const auto roomGeometry=Allocate(0x100);
    Put(image+0x6875F0,std::uint32_t(app));Put(app+0xA8,std::uint32_t(engine));Put(engine+0x8C,std::uint32_t(room));Put(room,std::uint32_t(image+0x54A72C));Put(room+4,std::uint32_t(roomGeometry));
    Put(image+0x54A72C+0x26C,std::uint32_t(image+NativeAimKLImage::RoomRay));Put(image+0x7C1D34,std::uint32_t(backend));Put(backend,std::uint32_t(image+0x5411C4));Put(image+0x5411C4+0x2C,std::uint32_t(image+NativeAimKLImage::BackendRay));
    const auto manager=Allocate(0x20),pool=Allocate(0x20),generations=Allocate(0x10);poolData=Allocate(4*0x70);
    Put(image+0x6C99B8,std::uint32_t(manager));Put(manager+0xC,std::uint32_t(pool));Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(4));Put(pool+0xC,std::uint32_t(poolData));Put(pool+0x10,std::uint32_t(generations));
    for(unsigned i=0;i<3;++i){NativePlayerPose::Geometry geometry;Put(poolData+i*0x70,geometry);Put(poolData+i*0x70+0x60,std::uint32_t(i==0?player:i==1?carrier:otherPlayer));}
    Check(actual.Read(0x510830,reinterpret_cast<void*>(image+NativeAimKLImage::RoomRay),0x165),"complete actual ROOM wrapper copied with its native reference conversion");
    unsigned relocated=0;for(unsigned i=0;i+4<=0x165;++i){const auto at=image+NativeAimKLImage::RoomRay+i;const auto value=Get<std::uint32_t>(at);
        if(value==0xA875F0||value==0xBC1D34||value==0xAC99B8){Put(at,std::uint32_t(image+value-0x400000));++relocated;}}
    Check(relocated==6,"only original app/backend/reference-manager absolute operands rebased");
    Jump(image+NativeAimKLImage::BackendRay,reinterpret_cast<std::uintptr_t>(Backend));Jump(image+0x5C450,reinterpret_cast<std::uintptr_t>(Reference));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x900000);
    Vec3 accepted{9,8,7};const auto initial=accepted;collisionMode=CollisionMode::Clear;
    Check(C::Check(query,accepted)&&Near(accepted,query.desiredActorLocalMetres)&&C::Snapshot().floorHits==5&&C::Snapshot().carrierContacts==5,"actual ROOM wrapper proves carrier floor and clear native body path before accepting desired offset");
    Check(C::Snapshot().rays==46,"one movement request has bounded floor/travel/body-head/ceiling native rays");
    const auto gesture=Allocate(0x100);Put(dispatcher+4,std::uint32_t(gesture));
    for(auto rva:{0x566690u,0x5666C4u,0x5666F8u}){Put(gesture,std::uint32_t(image+rva));
        Check(C::Check(query,accepted),"actual Current Vehicle PutBack/GetBack/Command retains native collision-certified locomotion");}
    const auto gestureCalls=backendCalls;
    for(auto rva:{0x5667F4u,0x56665Cu,0x56672Cu}){Put(gesture,std::uint32_t(image+rva));
        Check(!C::Check(query,accepted)&&backendCalls==gestureCalls,"MiniAct/Throw/Vehicle exit actions cannot borrow safe weapon-gesture eligibility");}
    Put(dispatcher+4,std::uint32_t(0));
    const auto savedEntry=Entry();const auto savedActor=Get<CameraPose>(actorGeometry);
    for(auto rejected:{CollisionMode::NoFloor,CollisionMode::WorldFloor,CollisionMode::Self,CollisionMode::Wall,CollisionMode::Corrupt,CollisionMode::HeadWall,CollisionMode::Ceiling}){collisionMode=rejected;accepted=initial;
        Check(!C::Check(query,accepted)&&Near(accepted,initial),"missing carrier floor, self/world floor, wall or invalid physics fraction holds complete step without changing output");}
    const auto afterEntry=Entry();Check(!std::memcmp(&savedEntry,&afterEntry,sizeof(savedEntry))&&Near(savedActor.position,Get<CameraPose>(actorGeometry).position),"collision provider writes neither native queue nor actor geometry");
    collisionMode=CollisionMode::Clear;const auto calls=backendCalls;observer=false;Check(!C::Check(query,accepted)&&backendCalls==calls,"outside real simulation callback never invokes native ROOM");observer=true;
    team=true;Check(!C::Check(query,accepted)&&backendCalls==calls,"team UI cannot perform physics queries");team=false;
    inventory=true;Check(!C::Check(query,accepted)&&backendCalls==calls,"inventory cannot perform physics queries");inventory=false;
    cinema=true;Check(!C::Check(query,accepted)&&backendCalls==calls,"cinematic context cannot perform physics queries");cinema=false;
    Put(image+0x8883A4,std::uint8_t(1));Check(!C::Check(query,accepted)&&backendCalls==calls,"native pause cannot perform physics queries");Put(image+0x8883A4,std::uint8_t(0));
    Put(input+0x18,std::uint8_t(1));Check(!C::Check(query,accepted)&&backendCalls==calls,"native blocked controller cannot perform physics queries");Put(input+0x18,std::uint8_t(0));
    ++ack;Check(!C::Check(query,accepted)&&backendCalls==calls,"recenter mismatch cannot perform physics queries");--ack;
    contextValid=false;Check(!C::Check(query,accepted)&&backendCalls==calls,"stale/missing host Game context cannot perform physics queries");contextValid=true;
    collisionMode=CollisionMode::Retire;accepted=initial;Check(!C::Check(query,accepted)&&Near(accepted,initial),"queue retirement during physics cannot publish a collision certificate");Put(carrier+0x124,std::uint32_t(1));collisionMode=CollisionMode::Clear;
    query.request.actorLocalMetres=query.desiredActorLocalMetres={.8f,0,0};accepted=initial;
    Check(C::Check(query,accepted)&&Near(accepted,{.25f,0,0}),"actual ROOM-certified long move publishes only the checked intermediate offset");
    query.request.tick=tick-151;Check(!C::Check(query,accepted),"stale passenger intent fails before native geometry use");
    std::printf("Passenger collision actual ROOM producer: %u total checks PASS.\n",checks);return 0;
}
