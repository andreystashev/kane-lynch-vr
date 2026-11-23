#pragma once
#include "native_passenger_actor_kl.hpp"
#include "native_aim_kl_image.hpp"
#include "native_aim_kl_math.hpp"
#include "native_weapon_ownership.hpp"
namespace NativePassengerCollisionKL {
using namespace NativeCameraMath;
using Query=NativePassengerActorKL::CollisionQuery;
struct World {std::uint32_t app=0,engine=0,room=0,roomVtable=0,backend=0,backendVtable=0;};
template<class Reader>bool CurrentWorld(std::uintptr_t image,Reader read,World& world){
    const auto get=[&](std::uintptr_t at,auto& out){return NativeWeaponOwnership::Read(at,out,read);};
    std::uint32_t method=0,geometry=0,flags=0;
    return get(image+0x6875F0,world.app)&&get(world.app+0xA8,world.engine)&&get(world.engine+0x8C,world.room)&&
        get(world.room,world.roomVtable)&&NativeAimKLImage::RoomType(image,world.roomVtable)&&
        get(world.roomVtable+0x26C,method)&&method==image+NativeAimKLImage::RoomRay&&
        get(world.room+4,geometry)&&get(geometry+0x3C,flags)&&!(flags&0x400)&&
        get(image+0x7C1D34,world.backend)&&get(world.backend,world.backendVtable)&&
        (world.backendVtable==image+0x5411C4||world.backendVtable==image+0x541274)&&
        get(world.backendVtable+0x2C,method)&&method==image+NativeAimKLImage::BackendRay;
}
struct Path {Vec3 previous{},desired{};float units=100;Vec3 acceptedLocal{};};
template<class Reader>bool Prepare(const Query& query,Reader read,Path& output){
    const auto units=query.request.units;Vec3 before,after;
    if(!NativePassengerAttachmentKL::Target(query.entry,query.originalOffset,query.previousActorLocalMetres,units,before)||
       !NativePassengerAttachmentKL::Target(query.entry,query.originalOffset,query.desiredActorLocalMetres,units,after))return false;
    Path path{Add(query.entry.bonePosition,before),Add(query.entry.bonePosition,after),units,query.desiredActorLocalMetres};
    std::uint32_t parent=0;if(!NativeWeaponOwnership::Read(query.identity.carrierGeometry+0x50,parent,read))return false;
    // Exact4F83A0 ->42BBE0: an unparented carrier's cached bones are
    // already WORLD. Applying its geometry again would double-translate.
    if(parent){CameraPose world;if(!NativePlayerPose::World(query.identity.carrierGeometry,read,world))return false;
        path.previous=Add(world.position,NativePlayerPose::Transform(world.basis,path.previous));
        path.desired=Add(world.position,NativePlayerPose::Transform(world.basis,path.desired));}
    if(!Finite(path.previous)||!Finite(path.desired))return false;
    const auto delta=Subtract(path.desired,path.previous);const auto distance2=Dot(delta,delta);
    if(!std::isfinite(distance2))return false;
    if(distance2>.25f*.25f*units*units){const auto ratio=.25f*units/std::sqrt(distance2);
        path.desired=Add(path.previous,Scale(delta,ratio));path.acceptedLocal=Add(query.previousActorLocalMetres,Scale(Subtract(query.desiredActorLocalMetres,query.previousActorLocalMetres),ratio));}
    path.acceptedLocal.y=0;
    output=path;return true;
}
enum class Contact {Unknown,Self,Carrier,Other};
template<class Reader>Contact Classify(std::uintptr_t image,std::uint32_t reference,const Query& query,Reader read,
                                     std::uintptr_t& geometry,std::uintptr_t& owner){
    geometry=owner=0;std::uintptr_t current=0;
    if(!NativeWeaponOwnership::ResolveGeometry(image,reference,read,current))return Contact::Unknown;
    geometry=current;Contact kind=Contact::Other;std::array<std::uintptr_t,16> seen{};
    for(unsigned depth=0;current&&depth<seen.size();++depth){
        for(unsigned i=0;i<depth;++i)if(seen[i]==current)return Contact::Unknown;seen[depth]=current;
        std::uint32_t parent=0,atOwner=0;
        if(!NativeWeaponOwnership::Read(current+0x50,parent,read)||!NativeWeaponOwnership::Read(current+0x60,atOwner,read))return Contact::Unknown;
        if(!depth)owner=atOwner;
        if(current==query.identity.geometry||atOwner==query.identity.player||atOwner==query.identity.child)kind=Contact::Self;
        else if(kind!=Contact::Self&&(current==query.identity.carrierGeometry||atOwner==query.identity.carrier))kind=Contact::Carrier;
        current=parent;
    }
    if(current)return Contact::Unknown;std::uintptr_t after=0;
    if(!NativeWeaponOwnership::ResolveGeometry(image,reference,read,after)||after!=geometry)return Contact::Unknown;
    return kind;
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
// Exact owned native simulation callback only. Stop the complete step on
// missing carrier support, unknown/self contacts or blocked body-path probes.
// This sampled native-ray envelope is NOT a claimed Havok convex capsule cast.
bool Check(const Query& query,Vec3& acceptedActorLocalMetres);
struct Stats {bool installed=false,accepted=false;std::uint64_t calls=0,acceptedSteps=0,rejected=0,rays=0,floorHits=0,floorMisses=0,
    carrierContacts=0,selfContacts=0,otherContacts=0,unknownContacts=0,wallHits=0,threadRejected=0,totalUs=0,lastUs=0,maxUs=0;
    unsigned stage=0;std::uint32_t tick=0,thread=0,reference=0;std::uintptr_t geometry=0,owner=0,room=0,carrier=0;
    Vec3 previous{},desired{},contact{};};
Stats Snapshot();
}
