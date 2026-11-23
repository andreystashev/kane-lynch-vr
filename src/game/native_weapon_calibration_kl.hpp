#pragma once
#include "native_rig_math.hpp"
#include <cstdint>

// Heuristic settling gate for the FIRST grip certificate after a known
// scene/loading transition. A moving/animated item must not freeze one
// arbitrary transition pose into the long-lived tracked weapon calibration.
// This is not native ownership proof: the caller must establish ownership,
// current actor/action, and same-draw wrist/item inputs before invoking it.
namespace NativeWeaponCalibrationKL {
struct Key {
    std::uint64_t epoch=0;
    std::uintptr_t player=0,item=0,model=0;
    std::uint32_t reference=0,recenter=0,side=0;
    std::uintptr_t root=0;
};
inline bool Same(const Key& a,const Key& b){return a.epoch==b.epoch&&a.player==b.player&&a.item==b.item&&
    a.model==b.model&&a.reference==b.reference&&a.recenter==b.recenter&&a.side==b.side&&a.root==b.root;}
class SettledGrip {
public:
    void Reset(){*this={};}
    bool Select(bool eligible,Key key,std::uint64_t pair,const NativeCameraMath::CameraPose& rawWrist,
                const NativeCameraMath::CameraPose& item,float units) {
        using namespace NativeCameraMath;
        if(!eligible||!key.player||!key.item||!key.model||!key.reference||key.side>1||!pair||
           !std::isfinite(units)||units<1||units>1000||!ValidatePose(rawWrist,.05f)||!ValidatePose(item,.05f)){
            Reset();return false;
        }
        const auto local=NativeRigMath::ToModel(item,rawWrist);
        if(!ValidatePose(local,.1f)){Reset();return false;}
        if(!samples_||!Same(key,key_)||units!=units_||pair<lastPair_){Start(key,pair,local,units);return false;}
        if(pair==lastPair_)return samples_>=3;
        // Local wrist in item space avoids rejecting ordinary world movement
        // or headset rotation. These small thresholds are conservative
        // heuristics (1.5 cm and 3 degrees), not reverse-engineered constants.
        const auto delta=Subtract(local.position,previous_.position);
        const float radius=units*.015f;
        const auto alignment=[](Vec3 a,Vec3 b){return Dot(a,b)/std::sqrt(Dot(a,a)*Dot(b,b));};
        const float trace=alignment(local.basis.forward,previous_.basis.forward)+
            alignment(local.basis.up,previous_.basis.up)+alignment(local.basis.right,previous_.basis.right);
        if(!std::isfinite(trace)||Dot(delta,delta)>radius*radius||trace<2.99725907f){
            Start(key,pair,local,units);return false;
        }
        lastPair_=pair;previous_=local;if(samples_<3)++samples_;return samples_>=3;
    }
private:
    void Start(Key key,std::uint64_t pair,const NativeCameraMath::CameraPose& pose,float units){
        key_=key;lastPair_=pair;previous_=pose;units_=units;samples_=1;
    }
    Key key_{};std::uint64_t lastPair_=0;NativeCameraMath::CameraPose previous_{};
    float units_=0;unsigned samples_=0;
};
}
