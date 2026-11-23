#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <cstdint>

// Navigation intent in the carried look reference. The consumer moves the
// actual owned actor through the native attachment queue after collision
// proof; this helper does not add another camera translation.
namespace NativePassengerRoomscale {
using namespace NativeCameraMath;
struct Key {
    std::uintptr_t player=0,camera=0,movement=0;
    std::uint32_t recenter=0;
};
inline bool Same(const Key& a,const Key& b) {
    return a.player==b.player&&a.camera==b.camera&&a.movement==b.movement&&a.recenter==b.recenter;
}
struct Intent {
    bool valid=false,crouched=false;
    std::uint32_t tick=0;
    float leftX=0,leftY=0;
};
struct Sample {
    Key key{};
    std::uint64_t pair=0;
    std::uint32_t tick=0;
    bool tracked=false;
    Vec3 headPosition{}; // raw tracking-space SDK head position
    Quat trackingOrigin{},relativeHead{}; // carriedLook.origin and SelectFree result
    Intent intent{};
};
inline bool FreshIntent(const Intent& intent,std::uint32_t now) {
    return intent.valid&&intent.tick&&std::int32_t(now-intent.tick)>=0&&now-intent.tick<=100&&
        std::isfinite(intent.leftX)&&std::isfinite(intent.leftY)&&std::fabs(intent.leftX)<=1&&std::fabs(intent.leftY)<=1;
}
inline bool Bound(Vec3 value,Vec3& output) {
    if(!Finite(value))return false;
    // A generous cargo safety envelope, not a measured van collider. Native
    // ray proof supplies the actual usable compartment boundary.
    value.x=std::clamp(value.x,-1.1f,1.1f);value.z=std::clamp(value.z,-2.f,2.f);
    value.y=std::clamp(value.y,-.65f,.20f);output=value;return true;
}
struct State {
    bool valid=false,suspended=false,crouched=false;
    Key key{};
    std::uint64_t pair=0;
    std::uint32_t tick=0;
    Vec3 referencePosition{},virtualOffset{},lastOffset{};
    Quat origin{};
    float lastDt=0;
    struct Prediction {std::uint64_t pair=0;Vec3 offset{};};
    Prediction predictions[16]{};std::uint64_t acceptedPair=0,acceptedRevision=0;
    void Reset(){*this={};}
    void Suspend(){if(valid)suspended=true;tick=0;lastDt=0;}
    bool Accept(const Key& acceptedKey,std::uint64_t consumedPair,Vec3 accepted,std::uint64_t revision=0) {
        if(!valid||!Same(key,acceptedKey)||!consumedPair||consumedPair<acceptedPair||consumedPair>pair||!Finite(accepted)||
           (acceptedRevision&&!revision)||(revision&&revision<=acceptedRevision)||(consumedPair==acceptedPair&&(!revision||revision<=acceptedRevision)))return false;
        auto& prediction=predictions[consumedPair%16];
        if(prediction.pair!=consumedPair)return false;
        const Vec3 correction{accepted.x-prediction.offset.x,0,accepted.z-prediction.offset.z};
        virtualOffset=Add(virtualOffset,correction);lastOffset=Add(lastOffset,correction);
        for(auto& value:predictions)if(value.pair>=consumedPair)value.offset=Add(value.offset,correction);
        acceptedPair=consumedPair;acceptedRevision=revision;return true;
    }
    void Remember(){predictions[pair%16]={pair,lastOffset};}
    bool Select(const Sample& sample,Vec3& output) {
        Quat selectedOrigin,look,fullOrigin;
        if(!sample.key.player||!sample.key.camera||!sample.key.movement){Reset();return false;}
        if(!sample.tracked||!sample.tick||!Finite(sample.headPosition)||
           !NormalizeQuaternion(sample.trackingOrigin,fullOrigin)||
           !NormalizeQuaternion(sample.relativeHead,look)) {
            Suspend();return false;
        }
        // Entry pitch/roll must never tilt gravity. At an exactly vertical
        // entry gaze yaw is undefined: keep the previous yaw (or neutral on
        // first entry) without blocking the passenger's stereo camera.
        if(!MakeYawReference(fullOrigin,selectedOrigin))selectedOrigin=valid&&Same(key,sample.key)?origin:Quat{};
        const bool changed=!valid||!Same(key,sample.key);
        if(changed) {
            const bool recentered=valid&&key.player==sample.key.player&&key.camera==sample.key.camera&&key.movement==sample.key.movement;
            const auto retained=recentered?lastOffset:Vec3{};
            const bool retainedCrouch=crouched;
            Reset();valid=true;key=sample.key;pair=sample.pair;tick=sample.tick;
            referencePosition=sample.headPosition;origin=selectedOrigin;
            // Recentring chooses a new physical tracking reference; it must
            // not return an actually walking actor to its original seat.
            virtualOffset=lastOffset=retained;crouched=recentered&&retainedCrouch;
            if(FreshIntent(sample.intent,sample.tick))crouched=sample.intent.crouched;
            output=lastOffset;Remember();return true;
        }
        // Both eyes and all draw passes share one local offset; carrier
        // movement is composed by the camera separately for each current eye.
        if(!suspended&&pair==sample.pair){output=lastOffset;return true;}
        const float originDot=std::fabs(origin.x*selectedOrigin.x+origin.y*selectedOrigin.y+
            origin.z*selectedOrigin.z+origin.w*selectedOrigin.w);
        const auto elapsed=sample.tick-tick;
        const bool resume=suspended||!tick||std::int32_t(elapsed)<0||elapsed>250||sample.pair<pair||originDot<.99999f;
        if(resume) {
            // Rebase physical tracking without teleporting the current local
            // eye or integrating a menu/tracking gap into virtual movement.
            origin=selectedOrigin;
            referencePosition=Subtract(sample.headPosition,Rotate(origin,Subtract(lastOffset,virtualOffset)));
        }
        lastDt=resume?0.f:std::min(elapsed*.001f,.05f);
        const bool input=FreshIntent(sample.intent,sample.tick);
        if(input) {
            crouched=sample.intent.crouched;
            const float magnitude=std::sqrt(sample.intent.leftX*sample.intent.leftX+sample.intent.leftY*sample.intent.leftY);
            if(magnitude>.25f) {
                Vec3 forward=Rotate(look,{0,0,-1});const float length=std::sqrt(forward.x*forward.x+forward.z*forward.z);
                if(length>.001f) {
                    forward={forward.x/length,0,forward.z/length};const Vec3 right{-forward.z,0,forward.x};
                    const float gain=std::clamp((magnitude-.25f)/.75f,0.f,1.f)/magnitude;
                    const auto velocity=Add(Scale(right,sample.intent.leftX*gain),Scale(forward,sample.intent.leftY*gain));
                    virtualOffset=Add(virtualOffset,Scale(velocity,.75f*lastDt));
                }
            }
            const float target=crouched?-.45f:0.f;
            virtualOffset.y+=std::clamp(target-virtualOffset.y,-1.5f*lastDt,1.5f*lastDt);
            Vec3 bounded;if(!Bound(virtualOffset,bounded)){Suspend();return false;}virtualOffset=bounded;
        }
        const auto physical=Rotate(Conjugate(origin),Subtract(sample.headPosition,referencePosition));
        Vec3 result;if(!Bound(Add(physical,virtualOffset),result)){Suspend();return false;}
        pair=sample.pair;tick=sample.tick;suspended=false;lastOffset=result;output=result;Remember();return true;
    }
};
}
