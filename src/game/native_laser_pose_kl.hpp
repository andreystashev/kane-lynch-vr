#pragma once
#include "native_aim_kl.hpp"
#include "native_aim_kl_math.hpp"
#include <algorithm>

// The beam is geometry attached to the currently rendered barrel. A delayed
// simulation hit can bound its length, but cannot rotate it toward an old
// world-space point. Only an on-ray real hit authorizes the surface dot.
namespace NativeLaserPoseKL {
using namespace NativeCameraMath;
constexpr float PendingLengthMetres=8.f,ContactToleranceMetres=.01f;
struct Beam {Vec3 end{};float distance=0;bool surfaceContact=false;};
namespace Detail {
inline bool ValidRendered(const NativeWeaponKL::AimFrame& rendered,std::uint32_t now,Vec3& direction) {
    if(!rendered.player||!rendered.weapon||!rendered.root||!rendered.model||!rendered.pairId||!rendered.tick||
       rendered.hand<0||rendered.hand>1||!std::isfinite(rendered.unitsPerMetre)||
       rendered.unitsPerMetre<1||rendered.unitsPerMetre>1000||!Finite(rendered.origin)||
       std::int32_t(now-rendered.tick)<0||now-rendered.tick>150||
       !NativeAimKLMath::Unit(rendered.direction,direction))return false;
    return true;
}
inline bool Geometry(const NativeWeaponKL::AimFrame& rendered,const Vec3* admittedPoint,bool allowSurface,
                     std::uint32_t now,Beam& output) {
    Vec3 direction;if(!ValidRendered(rendered,now,direction))return false;
    const float pending=PendingLengthMetres*rendered.unitsPerMetre;
    Beam beam;beam.distance=pending;
    if(admittedPoint) {
        if(!Finite(*admittedPoint))return false;
        const auto delta=Subtract(*admittedPoint,rendered.origin);
        const float distance=Dot(delta,direction);
        const auto error=Subtract(delta,Scale(direction,distance));
        const float tolerance=ContactToleranceMetres*rendered.unitsPerMetre;
        if(!std::isfinite(distance))return false;
        if(distance>0) {
            beam.surfaceContact=allowSurface&&Dot(error,error)<=tolerance*tolerance;
            // A pending beam is still a useful rigid directional guide. Only
            // the genuine on-ray hit gets a contact dot; a nearby previous hit
            // bounds the guide's length without rotating it toward that point.
            beam.distance=beam.surfaceContact?distance:std::min(pending,distance);
        }
    }
    if(!std::isfinite(beam.distance)||beam.distance<.01f*rendered.unitsPerMetre)return false;
    beam.end=Add(rendered.origin,Scale(direction,beam.distance));
    if(!Finite(beam.end))return false;output=beam;return true;
}
}
inline bool Build(const NativeWeaponKL::AimFrame& rendered,const NativeAimKL::Endpoint* endpoint,
                  std::uint32_t now,Beam& output) {
    const bool trusted=endpoint&&endpoint->valid&&NativeAimKLMath::MatchVisual(endpoint->pose,endpoint->tick,endpoint->point,rendered,now);
    return Detail::Geometry(rendered,trusted?&endpoint->point:nullptr,true,now,output);
}

// Like the gun/eye pair, collision-choice admission is immutable across its
// image passes and eyes. A query arriving or aging between eyes cannot create
// a one-eye dot. Current render pose validity is checked against current time;
// the admitted collision is never reused by simulation or a different pair.
class PairBeam {
    bool decided_=false,retired_=false,point_=false,surface_=false;
    NativeWeaponKL::AimFrame source_{};
    Vec3 contact_{};
public:
    void Reset(){decided_=retired_=point_=surface_=false;source_={};contact_={};}
    bool Select(const NativeWeaponKL::AimFrame& rendered,const NativeAimKL::Endpoint* endpoint,
                std::uint32_t now,Beam& output) {
        if(decided_&&source_.pairId==rendered.pairId&&!NativeAimKLMath::SameSource(source_,rendered)){retired_=true;return false;}
        Vec3 unused;if(!Detail::ValidRendered(rendered,now,unused))return false;
        if(!decided_||source_.pairId!=rendered.pairId) {
            Reset();decided_=true;source_=rendered;
            point_=endpoint&&endpoint->valid&&NativeAimKLMath::MatchVisual(endpoint->pose,endpoint->tick,endpoint->point,rendered,now);
            if(point_)contact_=endpoint->point;
            Beam first;if(!Detail::Geometry(rendered,point_?&contact_:nullptr,true,now,first)){retired_=true;return false;}
            surface_=first.surfaceContact;output=first;return true;
        }
        if(retired_)return false;
        if(!NativeAimKLMath::SameSource(source_,rendered)){retired_=true;return false;}
        Beam next;if(!Detail::Geometry(rendered,point_?&contact_:nullptr,surface_,now,next)){retired_=true;return false;}
        // A genuine carrier/muzzle transform may change even within a pair.
        // In that case keep the beam rigid and retire the unsupported dot;
        // do not bend it or resurrect it when that old WORLD point realigns.
        if(surface_&&!next.surfaceContact)surface_=false;
        output=next;return true;
    }
};
}
