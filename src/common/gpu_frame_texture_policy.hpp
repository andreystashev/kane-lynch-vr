#pragma once
#include "vr_ipc.hpp"

namespace GPUFrameTexturePolicy {
// A native context/UI publication already uses one mono image and the left
// two-slot ring. A scope also has mask3, but remains a full four-texture
// generation so it can never inherit a UI-only aliasing assumption.
inline bool CompactPanel(unsigned mask,const VRIPC::EyeMetadata& image){
    return mask==3&&(image.poseValid==VRIPC::kUiFrame||image.poseValid==VRIPC::kContextFrame)&&
        image.frameId&&VRIPC::ValidImageDimensions(image.width,image.height,VRIPC::kGPU)&&
        image.stride==image.width*4;
}
inline unsigned PhysicalSlot(unsigned slot,bool compact){return compact&&slot>=2?slot-2:slot;}
// Loading keeps its bounded GPU retirement wait without allocating CPU pixels.
// A resumed scene/scope must still recover if a driver's old fence never
// completes: restore the existing CPU route after continuous scene attempts.
struct SceneRetirementWait {
    bool pending=false;DWORD started=0,last=0;
    void Reset(){pending=false;started=last=0;}
    bool Fallback(bool scene,DWORD now){
        if(!scene){Reset();return false;}
        if(!pending||DWORD(now-last)>500){pending=true;started=now;}
        last=now;return DWORD(now-started)>=2000;
    }
};
}
