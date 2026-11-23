#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_body_kl.hpp"
#include "native_body_math.hpp"
#include <atomic>

namespace NativeBodyKL {
namespace {
using namespace NativeCameraMath;
struct Context {
    bool valid=false;
    CameraPose eye{};
    std::uint64_t pairId=0;
    float units=100.f;
    bool faceView=true;
    std::uint32_t recenterAck=0;
    DWORD tick=0;
    NativeBodyMath::FollowState follow{};
    NativeBodyMath::FollowState actionFollow{};
    std::uintptr_t actionReference=0;
};
thread_local Context context;
std::atomic<std::uint64_t> frames{0},adjusted{0},rotated{0},rejected{0},lastPair{0};
std::atomic<std::uint64_t> shoulderMeasured{0},shoulderFallback{0};
std::atomic<std::uint64_t> actionAdjusted{0},actionRejected{0};
std::atomic<std::uintptr_t> lastPlayer{0};
std::atomic<DWORD> lastThread{0};
std::atomic<float> lastYaw{0.f},lastCorrection{0.f};
std::atomic<float> lastRawTorsoYaw{0.f},lastAnimationYaw{0.f};
std::atomic<float> lastBindYawOffset{0.f};
}

void SetFrame(const CameraPose& trackedEye,std::uint64_t pairId,float units,bool faceView,std::uint32_t recenterAck) {
    if(!ValidatePose(trackedEye)||!pairId||!std::isfinite(units)||units<1.f||units>1000.f) {
        ClearFrame();return;
    }
    const DWORD now=GetTickCount();
    if(context.recenterAck!=recenterAck||context.units!=units||context.faceView!=faceView||
       (context.tick&&now-context.tick>300)||pairId<context.pairId){context.follow={};context.actionFollow={};context.actionReference=0;}
    context.valid=true;context.eye=trackedEye;context.pairId=pairId;
    context.units=units;context.tick=now;context.recenterAck=recenterAck;context.faceView=faceView;++frames;
}
void ClearFrame(){context.valid=false;}
void ResetFollow(){context.follow={};context.actionFollow={};context.actionReference=0;}
bool Adjust(std::uintptr_t player,const CameraPose& rawRoot,Vec3 rawHeadLocal,Vec3 rawShoulderLine,
            Vec3 bindShoulderLine,CameraPose& correctedRoot,std::uint64_t pairId) {
    if(!NativeBodyMath::DrawContextCurrent(context.valid,context.pairId,pairId))return false;
    CameraPose facing;
    if(!Finite(rawHeadLocal)||Dot(rawHeadLocal,rawHeadLocal)>16.f*context.units*context.units||
       !NativeBodyMath::FacingView(player,context.follow,rawRoot,rawShoulderLine,bindShoulderLine,context.eye,facing)||
       !NativeBodyMath::Adjust(context.follow,player,rawRoot,rawHeadLocal,rawShoulderLine,bindShoulderLine,facing,
                               context.pairId,context.tick,correctedRoot,context.faceView)) {++rejected;return false;}
    const float correction=NativeBodyMath::Wrap(context.follow.yaw-context.follow.rawTorsoYaw);
    lastPlayer=player;lastPair=context.pairId;lastThread=GetCurrentThreadId();
    lastYaw=context.follow.yaw*180.f/NativeBodyMath::Pi;
    lastCorrection=correction*180.f/NativeBodyMath::Pi;
    lastRawTorsoYaw=context.follow.rawTorsoYaw*180.f/NativeBodyMath::Pi;
    lastAnimationYaw=context.follow.animationYaw*180.f/NativeBodyMath::Pi;
    lastBindYawOffset=context.follow.bindYawOffset*180.f/NativeBodyMath::Pi;
    if(context.follow.shoulderMeasured)++shoulderMeasured;else ++shoulderFallback;
    ++adjusted;if(std::fabs(correction)>1e-5f)++rotated;
    return true;
}
bool AdjustAction(std::uintptr_t player,const NativeActionKL::State& action,const CameraPose& rawRoot,Vec3 rawHeadLocal,Vec3 rawShoulderLine,
                  Vec3 bindShoulderLine,CameraPose& correctedRoot,std::uint64_t pairId) {
    if(!ActionFollowEligible(action,player)||!NativeBodyMath::DrawContextCurrent(context.valid,context.pairId,pairId))return false;
    const auto reference=action.action?action.action:action.movement;
    if(context.actionReference!=reference){context.actionFollow={};context.actionReference=reference;}
    CameraPose facing;
    if(!Finite(rawHeadLocal)||Dot(rawHeadLocal,rawHeadLocal)>16.f*context.units*context.units||
       !NativeBodyMath::FacingView(action.player,context.actionFollow,rawRoot,rawShoulderLine,bindShoulderLine,context.eye,facing)||
       !NativeBodyMath::Adjust(context.actionFollow,action.player,rawRoot,rawHeadLocal,rawShoulderLine,bindShoulderLine,facing,
                              pairId,context.tick,correctedRoot,true)) {++actionRejected;return false;}
    ++actionAdjusted;return true;
}
Stats Snapshot() {
    Stats result{};result.frames=frames;result.adjusted=adjusted;result.rotated=rotated;
    result.rejected=rejected;result.player=lastPlayer;result.pairId=lastPair;result.threadId=lastThread;
    result.yawDegrees=lastYaw;result.correctionDegrees=lastCorrection;
    result.shoulderMeasured=shoulderMeasured;result.shoulderFallback=shoulderFallback;
    result.actionAdjusted=actionAdjusted;result.actionRejected=actionRejected;
    result.rawTorsoYawDegrees=lastRawTorsoYaw;result.animationYawDegrees=lastAnimationYaw;
    result.bindYawOffsetDegrees=lastBindYawOffset;return result;
}
}
