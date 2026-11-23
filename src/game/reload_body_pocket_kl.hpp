// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <cstdint>

// Inventory reach volumes, never attachment/bone frames. With only an HMD and
// two controllers a neck turn cannot be distinguished from a torso turn. Use
// a generous belt band instead of inventing a moving left-hip heading. Its
// centre is the same fresh physical optical centre used by ControllerTarget;
// native actor rotation and virtual turn cannot rotate a circular target away
// from the hand. No inventory volume changes the accepted magazine hand pose.
namespace ReloadBodyPocketKL {
using namespace NativeCameraMath;
struct Input {
    std::uintptr_t player=0;std::uint32_t scene=0,reference=UINT32_MAX,tick=0,sample=0;std::uint64_t pairId=0;
    float units=100;CameraPose head{};
    // Only a plausible foot-plane Y can limit a crouched belt's height. Native
    // root X/Z, authored model axes, shoulders and actor yaw never locate it.
    Vec3 rootAnchor{};bool rootAnchorValid=false;
};
struct Volume {
    CameraPose pose{};Vec3 shoulder{};float units=100;bool floorBounded=false;
};
inline bool Band(const Volume& volume,Vec3 centre,Vec3 point,float radius,float radialHalf,float heightHalf,float extraMetres){
    if(!ValidatePose(volume.pose,.05f)||!Finite(centre)||!Finite(point)||!std::isfinite(volume.units)||volume.units<1||volume.units>1000||
       !std::isfinite(extraMetres)||extraMetres<0||extraMetres>.08f)return false;
    const auto d=Scale(Subtract(point,centre),1.f/volume.units);
    const float radial=(std::sqrt(d.x*d.x+d.z*d.z)-radius)/(radialHalf+extraMetres);
    const float vertical=d.y/(heightHalf+extraMetres);
    return radial*radial+vertical*vertical<=1.f;
}
inline bool Contains(const Volume& volume,Vec3 point,float extraMetres=0){
    // 10..48 cm from the body's estimated centre, with a 24 cm vertical half
    // extent. Left hip remains reachable after physical as well as stick turns.
    return Band(volume,volume.pose.position,point,.29f,.19f,.24f,extraMetres);
}
inline bool ShoulderContains(const Volume& volume,Vec3 point){
    // Wide reach around either ear/shoulder, including a hand lifted above
    // eye level. Circular axes remain independent of head and stick yaw.
    return Band(volume,volume.shoulder,point,.35f,.25f,.32f,0);
}
inline bool CanSupply(const Volume& volume,Vec3 point,Vec3 gun){
    if(Contains(volume,point))return true;
    // Reaching for a slide close to the face must not become a magazine grab.
    // The inventory source must stay clear of face-held weapon mechanisms.
    if(!Finite(gun)||!ShoulderContains(volume,point))return false;
    const auto d=Scale(Subtract(point,gun),1.f/volume.units);
    return Dot(d,d)>.28f*.28f;
}
struct State {
    bool valid=false;std::uintptr_t player=0;std::uint32_t scene=0,reference=UINT32_MAX,lastTick=0;
    std::uint64_t pairId=0;float units=100;Volume volume{};
    void Reset(){*this={};}
    bool Step(const Input& in,Volume& output){
        if(!in.player||!in.scene||in.reference==UINT32_MAX||!in.pairId||!in.sample||in.tick-in.sample>150||
           !ValidatePose(in.head,.05f)||!std::isfinite(in.units)||in.units<1||in.units>1000)return false;
        const bool reset=!valid||player!=in.player||scene!=in.scene||reference!=in.reference||
            std::fabs(units-in.units)>in.units*.01f||in.tick-lastTick>300||in.pairId<pairId;
        if(!reset&&in.pairId==pairId){output=volume;return true;}
        Volume next;next.units=in.units;next.pose.position=in.head.position;next.shoulder=in.head.position;
        next.pose.position.y-=.72f*in.units;next.shoulder.y-=.14f*in.units;
        const float height=(in.head.position.y-in.rootAnchor.y)/in.units;
        next.floorBounded=in.rootAnchorValid&&Finite(in.rootAnchor)&&height>=.5f&&height<=2.4f;
        if(next.floorBounded)next.pose.position.y=std::clamp(next.pose.position.y,in.rootAnchor.y+.35f*in.units,in.rootAnchor.y+1.04f*in.units);
        valid=true;player=in.player;scene=in.scene;reference=in.reference;pairId=in.pairId;lastTick=in.tick;units=in.units;volume=next;
        output=volume;return true;
    }
};
}
