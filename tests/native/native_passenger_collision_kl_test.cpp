#include "native_passenger_collision_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){using namespace NativePassengerCollisionKL;using namespace NativeHudFixture;unsigned checks=0;
    const auto check=[&](bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}};
    const auto near=[](Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<.0001f;};
    if(argc>1){Image exe;check(exe.Load(argv[1])&&NativeAimKLImage::Verify(Base,exe.Reader()),"actual supported ROOM/backend/ray schema verifies without execution");}
    Memory memory;Query query;query.request.units=100;query.identity.carrierGeometry=0x18000000;query.originalOffset={3,4,5};query.desiredActorLocalMetres={.1f,0,.05f};
    const Basis quarter{{1,0,0},{0,1,0},{0,0,-1}};query.entry.child=Player;query.entry.localBasis=query.entry.boneBasis=quarter;query.entry.bonePosition={100,200,300};
    NativePlayerPose::Geometry geometry;geometry.pose.basis={{0,0,1},{0,1,0},{1,0,0}};geometry.pose.position={1000,2000,3000};memory.Set(query.identity.carrierGeometry,geometry);Path path;
    check(Prepare(query,memory.Reader(),path)&&near(path.previous,{103,204,305})&&near(path.desired,{93,204,300}),"unparented native cached WORLD bones never receive geometry transform a second time");
    geometry.pose.position={10,20,30};geometry.parent=0x19000000;memory.Set(query.identity.carrierGeometry,geometry);NativePlayerPose::Geometry parent;parent.pose={quarter,{1000,2000,3000}};memory.Set(geometry.parent,parent);
    check(Prepare(query,memory.Reader(),path)&&near(path.desired,{1330,2224,2897}),"parented native cached model bones traverse actual geometry ancestry");
    query.desiredActorLocalMetres={.5f,0,0};check(Prepare(query,memory.Reader(),path)&&near(path.acceptedLocal,{.25f,0,0})&&std::fabs(Dot(Subtract(path.desired,path.previous),Subtract(path.desired,path.previous))-625)<.01f,"long requested movement advances only a checked25cm step instead of deadlocking new context");query.desiredActorLocalMetres={.1f,0,.05f};
    parent.parent=geometry.parent;memory.Set(geometry.parent,parent);check(!Prepare(query,memory.Reader(),path),"cyclic parent source fails closed");
    constexpr std::uint32_t manager=0x20000000,pool=0x21000000,data=0x22000000,generations=0x23000000;
    memory.Set(Base+0x6C99B8,manager);memory.Set(manager+0xC,pool);memory.Set(pool+4,std::uint32_t(0x70));memory.Set(pool+8,std::uint32_t(4));memory.Set(pool+0xC,data);memory.Set(pool+0x10,generations);
    for(unsigned i=0;i<4;++i){memory.Set(data+i*0x70,NativePlayerPose::Geometry{});memory.Set(data+i*0x70+0x60,std::uint32_t(0));memory.Set(generations+i*2,std::uint16_t(0));}
    query.identity.player=Player;query.identity.child=Player;query.identity.geometry=data;query.identity.carrier=0x24000000;query.identity.carrierGeometry=data+0x70;
    memory.Set(data+0x60,Player);memory.Set(data+0xD0,std::uint32_t(query.identity.carrier));std::uintptr_t hit=0,owner=0;
    check(Classify(Base,1,query,memory.Reader(),hit,owner)==Contact::Self&&hit==data&&owner==Player,"normal generation-checked native reference identifies actor self contact");
    check(Classify(Base,2,query,memory.Reader(),hit,owner)==Contact::Carrier,"native carrier floor contact has explicit ownership");
    check(Classify(Base,3,query,memory.Reader(),hit,owner)==Contact::Other,"world/foreign floor does not masquerade as carrier support");
    memory.Set(data+0xE0+0x50,std::uint32_t(data+0x70));check(Classify(Base,3,query,memory.Reader(),hit,owner)==Contact::Carrier,"mesh child ancestry identifies current moving carrier");
    memory.Set(data+0xE0+0x50,std::uint32_t(data+0xE0));check(Classify(Base,3,query,memory.Reader(),hit,owner)==Contact::Unknown,"cyclic contact graph fails closed");
    check(Classify(Base,0,query,memory.Reader(),hit,owner)==Contact::Unknown&&Classify(Base,0x80000001,query,memory.Reader(),hit,owner)==Contact::Unknown,"absent/tagged/raw pointers are never accepted as native contacts");
    memory.Set(generations,std::uint16_t(1));check(Classify(Base,1,query,memory.Reader(),hit,owner)==Contact::Unknown,"retired native reference generation cannot prove floor or self");
    std::printf("Passenger collision native-space/contact policy: %u checks PASS.\n",checks);
}
