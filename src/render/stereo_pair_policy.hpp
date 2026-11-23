#pragma once
#include "vr_ipc.hpp"
#include "stereo_render_math.hpp"
#include <cmath>
#include <cstring>

// A native render stall must not turn a complete, matching tracked pair into
// seconds of compositor hold. Capture age and tracking-at-render age are
// different: steering still requires a fresh pose when the view is built.
namespace StereoPairPolicy {
inline constexpr DWORD TrackingStartMaxAgeMs=250,CompletedImageMaxAgeMs=1000;
// A render-pose admission failure is sticky within its image/pair. Retrying
// that same first eye would never acquire a fresh pair, so retire the ID at
// the boundary even when no image was published. Ordinary capture backpressure
// still retries the current eye and does not discard a valid tracking sample.
inline void FinishImage(StereoRenderMath::PairSequence& pair,bool published,bool scope,bool rejectedRenderPair) {
    if(rejectedRenderPair||(published&&scope))pair.Restart();
    else if(published)pair.CompleteEye();
}
// Sample at the first native camera use, after any engine/driver work before
// the camera packet. The previous Present is too early when that work stalls.
// Keep the LEFT sample for a valid RIGHT; an expired half-pair must restart
// with a new ID before any camera/projection for this image has been applied.
// Refresh reads the production pose and eye configuration; the existing
// camera/publication recenter checks still reject a concurrent origin change.
template<class Tracking,class Refresh>
inline bool LatchAtCamera(StereoRenderMath::PairSequence& pair,bool eyeStarted,
                          const Tracking& sample,DWORD now,long sampleAck,long currentAck,Refresh refresh) {
    if(eyeStarted)return false;
    const bool usable=sample.valid&&sample.mounted&&sample.tick&&
        static_cast<DWORD>(now-sample.tick)<=TrackingStartMaxAgeMs;
    if(pair.Eye()==1&&usable&&sampleAck==currentAck)return false;
    if(pair.Eye()==1)pair.Restart();
    refresh();return true;
}
inline bool Fresh(DWORD now,DWORD tick) {
    return tick&&static_cast<LONG>(now-tick)>=0&&now-tick<=CompletedImageMaxAgeMs;
}
inline bool Compatible(const VRIPC::EyeMetadata& left,const VRIPC::EyeMetadata& right) {
    if(left.poseValid!=VRIPC::kSceneFrame||right.poseValid!=VRIPC::kSceneFrame||
       !left.frameId||(left.frameId&1)||right.frameId!=left.frameId+1||
       !left.width||left.width!=right.width||left.height!=right.height||left.stride!=right.stride||
       !std::isfinite(left.sensorTime)||left.sensorTime!=right.sensorTime||
       std::memcmp(left.headOrientation,right.headOrientation,sizeof(left.headOrientation))||
       std::memcmp(left.headPosition,right.headPosition,sizeof(left.headPosition)))return false;
    const auto gap=static_cast<LONG>(left.tick-right.tick);
    return gap<=static_cast<LONG>(CompletedImageMaxAgeMs)&&gap>=-static_cast<LONG>(CompletedImageMaxAgeMs);
}
}
