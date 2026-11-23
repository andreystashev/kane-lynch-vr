#pragma once
#include "vr_ipc.hpp"
#include <algorithm>
#include <cmath>

// Host policy only: a native zoom image is one opaque head-locked image,
// never a tracked stereo scene with the headset's unzoomed projection.
namespace MonoFrame {
inline LONG InputContext(bool ready,bool menu,bool loading,bool cinematic,bool steering){
    if(!ready)return VRIPC::kInputSuspended;
    if(menu)return VRIPC::kInputMenu;
    if(loading)return VRIPC::kInputLoading;
    if(cinematic)return VRIPC::kInputCinematic;
    return steering?VRIPC::kInputGame:VRIPC::kInputUnknown;
}
inline bool Kind(unsigned kind){return kind==VRIPC::kUiFrame||kind==VRIPC::kScopeFrame||kind==VRIPC::kContextFrame;}
inline bool ValidMetadata(const VRIPC::EyeMetadata& m,bool stereo,unsigned transport){
    if(!VRIPC::ValidImageDimensions(m.width,m.height,transport)||m.stride!=m.width*4||!m.frameId)return false;
    if(Kind(m.poseValid))return !stereo;
    if(m.poseValid!=VRIPC::kSceneFrame)return false;
    for(float tangent:m.fov)if(!std::isfinite(tangent)||tangent<=.01f||tangent>10)return false;
    double length=0;for(float q:m.headOrientation)length+=double(q)*q;
    return length>.9&&length<1.1;
}
inline bool ReadGameplay(const VRIPC::SharedFrames& frames,DWORD now,LONG& output){
    const auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&frames.gameplaySequence),0,0);
    const auto tick=static_cast<DWORD>(frames.gameplayTick);const auto flags=frames.gameplayFlags;MemoryBarrier();
    if(frames.version!=VRIPC::kVersion||!before||(before&1)||!tick||static_cast<LONG>(now-tick)<0||now-tick>=250||
       before!=InterlockedCompareExchange(const_cast<volatile LONG*>(&frames.gameplaySequence),0,0))return false;
    output=flags;return true;
}
inline bool ScopeProof(LONG inputContext,bool fresh,LONG flags,bool nativeMenu=false){
    return inputContext==VRIPC::kInputGame&&!nativeMenu&&fresh&&(flags&16)&&!(flags&8);
}
inline bool LoadingProof(LONG inputContext,unsigned kind) {
    return inputContext==VRIPC::kInputLoading&&kind==VRIPC::kContextFrame;
}
inline bool HoldLoading(bool committed,LONG inputContext,bool captureEnabled) {
    // A game camera or new input context can arrive before the corresponding
    // texture. Keep the committed loading panel until its replacement uploads.
    return committed&&captureEnabled&&inputContext!=VRIPC::kInputSuspended;
}
inline bool HoldScene(bool committed,LONG inputContext,bool captureEnabled) {
    // The first loading panel may still be pending. A committed stereo layer
    // remains safe to submit in its original projection during that gap.
    return committed&&captureEnabled&&inputContext!=VRIPC::kInputMenu&&inputContext!=VRIPC::kInputSuspended;
}
inline bool HoldScope(bool wasScope,bool proof,DWORD imageTick,DWORD now){
    return wasScope&&proof&&imageTick&&static_cast<LONG>(now-imageTick)>=0&&now-imageTick<=1000;
}
inline bool ScopeExitProof(LONG inputContext,bool fresh,LONG flags,bool nativeMenu,unsigned latestKind){
    return inputContext==VRIPC::kInputGame&&fresh&&!nativeMenu&&!(flags&(8|16))&&
        (latestKind==VRIPC::kScopeFrame||latestKind==VRIPC::kSceneFrame);
}
class ScopeExitGrace {
    DWORD began=0;
    bool active=false;
public:
    void Reset(){active=false;}
    bool Hold(bool eligible,DWORD committedImageTick,DWORD now) {
        if(!eligible||!committedImageTick||now-committedImageTick>250){Reset();return false;}
        if(!active){began=now;active=true;}
        // The two unzoomed eyes may become ready on different game frames.
        return now-began<48;
    }
};
struct Extent {float width=0,height=0,distance=0;};
inline bool PanelExtent(unsigned width,unsigned height,float panelWidth,float distance,Extent& output){
    if(!width||!height||!std::isfinite(panelWidth)||panelWidth<.1f||panelWidth>20||
       !std::isfinite(distance)||distance<.1f||distance>20)return false;
    const Extent result{panelWidth,panelWidth*float(height)/float(width),distance};
    if(!std::isfinite(result.height)||result.height<=0)return false;
    output=result;return true;
}
inline bool ScopeExtent(const float (&fov)[2][4],unsigned width,unsigned height,float distance,Extent& output){
    if(!width||!height||!std::isfinite(distance)||distance<.1f||distance>20)return false;
    float horizontal=0,vertical=0;
    for(const auto& eye:fov)for(unsigned i=0;i<4;++i){
        const auto value=eye[i];if(!std::isfinite(value)||value<=.01f||value>10)return false;
        if(i<2)horizontal=std::max(horizontal,value);else vertical=std::max(vertical,value);
    }
    // Cover both eyes' asymmetric FOV while preserving native image aspect.
    // Centred opaque overscan may crop image edges; it never stretches the
    // native optic or assigns its zoom image an unrelated EyeFov projection.
    const float aspect=float(width)/height;
    const float span=std::max(2*horizontal*distance,2*vertical*distance*aspect);
    Extent result{span,span/aspect,distance};
    if(!std::isfinite(result.width)||!std::isfinite(result.height))return false;
    output=result;return true;
}
}
