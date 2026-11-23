#pragma once
#include "native_player_pose.hpp"
#include "native_action_kl.hpp"
#include "native_passenger_attachment_kl.hpp"
#include <array>
#include <cstring>

// Simulation-side local locomotion in the owned native Vehicle attachment.
// The unchanged native carrier consumer drives the actual actor transform.
namespace NativePassengerActorKL {
using namespace NativeCameraMath;
constexpr std::uintptr_t QueueConsumer=0x1A1940;
struct Request {
    std::uintptr_t player=0,movement=0;
    std::uint32_t recenter=0,tick=0;
    std::uint64_t pair=0;
    Vec3 actorLocalMetres{};
    float units=100;
    std::uint32_t epoch=0;
    std::uintptr_t camera=0;
};
inline bool Valid(const Request& request,std::uint32_t now) {
    return request.player&&request.movement&&request.camera&&request.epoch&&request.pair&&request.tick&&
        std::int32_t(now-request.tick)>=0&&now-request.tick<=150&&
        Finite(request.actorLocalMetres)&&std::isfinite(request.units)&&request.units>=1&&request.units<=1000&&
        // Safety envelope; native collision proof bounds the actual cargo.
        std::fabs(request.actorLocalMetres.x)<=1.101f&&std::fabs(request.actorLocalMetres.z)<=2.001f&&
        request.actorLocalMetres.y>=-.651f&&request.actorLocalMetres.y<=.201f;
}
template<class Reader>bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t at,const auto& expected){std::array<unsigned char,sizeof(expected)> bytes{};
        return read(image+at,bytes.data(),bytes.size())&&!std::memcmp(bytes.data(),expected,bytes.size());};
    const unsigned char consumer[]={0x83,0xEC,0x30,0x53,0x55,0x8B,0xD9,0x33,0xED,0x39,0xAB,0x24,1,0,0};
    const unsigned char fields[]={0x8B,0x7E,0xC0,0x8D,0x56,4,0x8D,0x4C,0x24,0x1C};
    const unsigned char childSetter[]={0x8B,7,0x8B,0x80,0x98,0,0,0,0x8D,0x4C,0x24,0x10};
    const unsigned char append[]={0x83,0xEC,0x6C,0x53,0x55,0x56,0x57,0x8B,0xBC,0x24,0x84,0,0,0};
    return match(QueueConsumer,consumer)&&match(0x1A1960,fields)&&match(0x1A19E3,childSetter)&&match(0x1A1B40,append);
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
bool Publish(const Request& request);
void Suspend();
// .44 accepted seated avatar: immutable owned attachment proof only. These
// entry points install no simulation observer and never write a native queue.
bool InstallSeated(LogCallback log);
bool SetSeatedContext(std::uintptr_t player,std::uintptr_t camera,std::uintptr_t movement,
                      std::uint64_t pair,std::uint32_t recenter,std::uint32_t tick,float units);
struct CollisionQuery {
    Request request{};
    NativePassengerAttachmentKL::Identity identity{};
    NativePassengerAttachmentKL::Entry entry{};
    Vec3 originalOffset{},previousActorLocalMetres{},desiredActorLocalMetres{};
};
using CollisionProvider=bool(*)(const CollisionQuery&,Vec3& acceptedActorLocalMetres);
void SetCollisionProvider(CollisionProvider provider);
// Called only inside the owned native simulation callback. Native room/physics
// queries are never made by the rendering thread or PlayerUpdate observers.
void SimulationTick(std::uintptr_t player);
struct Motion {
    bool active=false,poseOnly=false;
    std::uintptr_t player=0,movement=0,carrier=0;
    std::uint32_t tick=0,epoch=0,recenter=0,requestTick=0;
    std::uint64_t pair=0,revision=0;
    Vec3 offsetMetres{},velocityMetres{};
    float phase=0.f,speed=0.f,crouchMetres=.45f;
};
// UINT32_MAX requests the current acknowledged origin (used by anatomical
// GPU source before the new camera frame exists); it never accepts old ack.
bool ReadMotion(std::uintptr_t player,std::uint32_t recenter,Motion& output);
struct Stats {
    bool installed=false,applied=false,seatedOnly=false;
    std::uint64_t seatedFrames=0,seatedRejected=0;
    std::uint64_t published=0,updates=0,translated=0,restored=0,rejected=0,diverged=0,aborted=0,held=0;
    std::uintptr_t player=0,geometry=0,parent=0;
    std::uint32_t simulationThread=0;
    unsigned rejectionStage=0;
    NativePassengerAttachmentKL::Probe identity{};
    std::uint32_t requestCamera=0,currentCamera=0;
    std::uintptr_t carrier=0,entry=0,child=0;
    Motion motion{};
    std::uint64_t pair=0;
    Vec3 localMetres{},nativePosition{},shiftedPosition{};
};
Stats Snapshot();
}
