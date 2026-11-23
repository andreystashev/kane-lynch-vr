#pragma once
#include "native_aim_kl.hpp"
#include "native_aim_kl_math.hpp"

// One contact choice for an entire immutable canonical gun/eye pair. A native
// query arriving between eyes must not move one reticle from contact to20m.
// No contact is invented or kept after its original strict proof expires.
namespace NativeAimTargetPolicy {
using namespace NativeCameraMath;
struct Selection {bool hit=false;Vec3 target{};};
struct Stats {std::uint64_t resolved=0,reused=0,hits=0,misses=0,retired=0;};
class PairTarget {
    bool decided_=false,hit_=false,retired_=false;
    NativeWeaponKL::AimFrame pose_{};
    NativeAimKL::Endpoint contact_{};
    Stats stats_{};
public:
    void Reset(){decided_=hit_=retired_=false;pose_={};contact_={};}
    Stats Snapshot()const{return stats_;}
    bool Select(const NativeWeaponKL::AimFrame& pose,const NativeAimKL::Endpoint* contact,
                std::uint32_t now,Selection& output) {
        Vec3 direction;
        if(!pose.player||!pose.weapon||!pose.root||!pose.model||!pose.pairId||!pose.tick||
           std::int32_t(now-pose.tick)<0||now-pose.tick>150||pose.hand<0||pose.hand>1||
           !std::isfinite(pose.unitsPerMetre)||pose.unitsPerMetre<1||pose.unitsPerMetre>1000||
           !Finite(pose.origin)||!NativeAimKLMath::Unit(pose.direction,direction))return false;
        if(!decided_||pose.pairId!=pose_.pairId) {
            decided_=true;retired_=false;pose_=pose;contact_={};
            hit_=contact&&contact->valid&&NativeAimKLMath::MatchVisual(contact->pose,contact->tick,contact->point,pose,now);
            if(hit_)contact_=*contact;
            ++stats_.resolved;if(hit_)++stats_.hits;else ++stats_.misses;
        } else {
            ++stats_.reused;
            Vec3 previousDirection;NativeAimKLMath::Unit(pose_.direction,previousDirection);
            const auto shift=Subtract(pose.origin,pose_.origin);const float limit=2.f*pose.unitsPerMetre;
            if((!NativeAimKLMath::SameSource(pose_,pose)||Dot(shift,shift)>limit*limit||
                Dot(previousDirection,direction)<.98f)&&!retired_){retired_=true;++stats_.retired;}
        }
        if(retired_)return false;
        // Once a current pair selected its real contact, keep that same
        // image choice for both eyes. Sensor age is checked at selection;
        // re-expiring it between eyes made only the right dot/beam vanish.
        Selection selected;selected.hit=hit_;
        selected.target=hit_?contact_.point:Add(pose.origin,Scale(direction,20.f*pose.unitsPerMetre));
        output=selected;return true;
    }
};
} // namespace NativeAimTargetPolicy
