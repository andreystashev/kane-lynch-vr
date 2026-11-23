#pragma once
#include <d3d9.h>
#include "native_effects_math_kl.hpp"
namespace NativeCinemaViewport {
// The camera-applied callback owns the current stereo cinema scene. Native
// meshes normally bind combined WVP, so a standalone P at three billboard-only
// registers is NOT a draw ownership test. Match the scene target, world depth
// and authored letterbox instead. The existing per-eye WVP stays untouched;
// its complete SDK vertical FOV must map to the complete eye target.
class Scope {
    IDirect3DDevice9* device_=nullptr;D3DVIEWPORT9 original_{};
    RECT originalScissor_{};bool scissorChanged_=false,rollbackFailed_=false;
public:
    unsigned stage=0;
    D3DVIEWPORT9 observed{};
    bool Begin(IDirect3DDevice9* device,bool cinematic,unsigned width,unsigned height,const float* projection) {
        stage=1;
        if(device_||!device||!cinematic||width<32||height<32||!projection||
           !NativeEffectsKLMath::ProjectionMatches(projection,projection))return false;
        rollbackFailed_=false;
        D3DVIEWPORT9 viewport{};DWORD z=0,function=0,colour=0,scissor=0;
        IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
        stage=2;if(FAILED(device->GetViewport(&viewport)))return false;observed=viewport;
        stage=3;if(viewport.X||viewport.Width!=width||
           viewport.Height<height/3||viewport.Height>=height||viewport.Y>height-viewport.Height||
           (viewport.Y&&std::abs(int(2*viewport.Y+viewport.Height)-int(height))>2))return false;
        stage=4;if(FAILED(device->GetRenderState(D3DRS_ZENABLE,&z))||!z||
           FAILED(device->GetRenderState(D3DRS_ZFUNC,&function))||function==D3DCMP_ALWAYS||
           FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&colour))||!(colour&7u))return false;
        stage=5;if(FAILED(device->GetRenderTarget(0,&target))||!target)return false;
        const auto result=target->GetDesc(&desc);target->Release();
        if(FAILED(result)||desc.Width!=width||desc.Height!=height)return false;
        // An enabled matching native scissor would otherwise retain exactly
        // the same vertical crop after viewport repair. Other clip rectangles
        // belong to a material/subpass and must never be enlarged.
        RECT rect{};stage=6;
        if(FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))||
           (scissor&&FAILED(device->GetScissorRect(&rect))))return false;
        const bool matchingScissor=scissor&&rect.left==0&&rect.right==LONG(width)&&
            rect.top==LONG(viewport.Y)&&rect.bottom==LONG(viewport.Y+viewport.Height);
        auto full=viewport;full.Y=0;full.Height=height;
        stage=7;if(FAILED(device->SetViewport(&full)))return false;
        if(matchingScissor){
            const RECT expanded{0,0,LONG(width),LONG(height)};stage=8;
            if(FAILED(device->SetScissorRect(&expanded))){
                rollbackFailed_=FAILED(device->SetViewport(&viewport));return false;
            }
        }
        originalScissor_=rect;scissorChanged_=matchingScissor;
        device_=device;original_=viewport;stage=100;return true;
    }
    bool Finish(){
        if(!device_)return !rollbackFailed_;
        auto* device=device_;device_=nullptr;
        const bool viewport=SUCCEEDED(device->SetViewport(&original_));
        const bool scissor=!scissorChanged_||SUCCEEDED(device->SetScissorRect(&originalScissor_));
        scissorChanged_=false;return viewport&&scissor&&!rollbackFailed_;
    }
    ~Scope(){Finish();}
};
}
