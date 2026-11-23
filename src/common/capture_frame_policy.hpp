#pragma once
#include <cstdint>
namespace CaptureFramePolicy {
enum class Action {Scene,Hold,Panel};
// Shared GPU textures only need a new panel image at most this often. The
// headset may resubmit the last panel independently on every display frame.
struct PanelCopyCadence {
    static constexpr unsigned RateHz=144;
    struct Key {
        unsigned width=0,height=0,epoch=0,frameClass=0;
        bool operator==(const Key& other) const {
            return width==other.width&&height==other.height&&epoch==other.epoch&&frameClass==other.frameClass;
        }
    };
    Key current{};
    std::uint64_t next=0;
    void Reset(){*this={};}
    bool Skip(Key key,std::uint64_t now){
        if(!(current==key)){current=key;next=0;}
        return now&&now<next;
    }
    void Copied(std::uint64_t now,std::uint64_t period){
        // Carry the phase across a slightly late Present so a cadence just
        // above RateHz does not drop to half speed. Discard debt after a gap.
        if(!now||!period){next=0;return;}
        if(!next||now<next||now-next>=period)next=now;
        next+=period;
    }
};
// Before the first successful publication, the normal stereoEstablished flag
// is still false. Remember an actually observed native scene across missing
// camera callbacks and Present retries; only an explicit lifecycle boundary
// can return that transition to the startup/panel fallback.
struct SceneIntent {
    bool observed=false;
    void Reset(){observed=false;}
    bool Observe(bool nativeScene,bool stereoEnabled,bool explicitPanel,bool hostReady){
        if(!stereoEnabled||explicitPanel||!hostReady)Reset();
        else observed=observed||nativeScene;
        return observed;
    }
};
struct ReflectionWorld {
    std::uintptr_t data=0,player=0,camera=0;
    long recenter=0;
    bool Valid() const {return data&&player&&camera;}
    bool operator==(const ReflectionWorld& other) const {
        return data==other.data&&player==other.player&&camera==other.camera&&recenter==other.recenter;
    }
};
// History belongs to an ordinary world view. A single missing camera
// admission does not change worlds, but it must not extend the lease.
struct ReflectionContinuity {
    static constexpr std::uint32_t GapLimitMs=120;
    ReflectionWorld world{};
    std::uint32_t lastViewTick=0;
    bool skipped=false;
    void Reset(){*this={};}
    bool Qualified(const ReflectionWorld& next,std::uint32_t now) {
        if(!next.Valid()){Reset();return false;}
        const bool same=!world.Valid()||world==next;
        world=next;lastViewTick=now;skipped=false;
        return same;
    }
    bool CanSkip(std::uint32_t now) const {
        return world.Valid()&&!skipped&&now-lastViewTick<=GapLimitMs;
    }
    bool Skip(const ReflectionWorld& next,std::uint32_t now) {
        if(!CanSkip(now)||!(world==next)){Reset();return false;}
        skipped=true;
        return true;
    }
};
inline Action Select(bool prepared,bool stereoEnabled,bool stereoEstablished,bool explicitPanel,bool nativeSceneIntent=false){
    if(prepared)return Action::Scene;
    // Losing a tracked frame does not establish a menu/movie. A previously
    // established stereo scene is held until an explicit mono reason exists.
    // This also applies before its FIRST publication once a native tracked
    // camera has begun. Calling that incomplete image UI would retire the
    // four-texture scene generation that is still awaiting the host's ack,
    // recreate the compact panel, and repeat forever on the next scene try.
    if(stereoEnabled&&(stereoEstablished||nativeSceneIntent)&&!explicitPanel)return Action::Hold;
    return Action::Panel;
}
// A held image is a dropped capture, not an unsupported GPU transport. -1
// would allocate/read back native pixels which cannot pass the same gate.
inline int HeldGPUResult(){return 0;}
}
