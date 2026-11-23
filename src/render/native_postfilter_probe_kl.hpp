#pragma once
#include <cstdint>
#include <cstring>

// Opt-in, read-only evidence for the exact native 0x4B00B0 postfilter stage.
// Resource identities are ephemeral addresses, not COM ownership or proof of
// which image was visible. This probe never changes postfilter state.
namespace NativePostfilterProbeKL {
using LogCallback=void(*)(const char*);
struct Frame {std::uint64_t id=0;std::uint32_t eye=2,tick=0,thread=0;};
using FrameProvider=bool(*)(Frame&);
struct StageState {
    std::uintptr_t postFilter=0,renderer=0;
    float lod=0,amount=0;
    std::uint32_t sequence=0,viewport[4]{},resources[13]{},shader[2]{};
    std::uint32_t maskVariant=0,resolveFlag=0,auxRenderer=0,maskTexture=0,maskSurface=0,resolveSurface=0;
    bool auxValid=false;
};
struct Event {std::uint64_t sequence=0;Frame frame{};StageState before{},after{};bool frameKnown=false,beforeValid=false,afterValid=false;};
constexpr unsigned Capacity=256;
struct Stats {bool installed=false;std::uint64_t calls=0,sampled=0,rejected=0,frameUnavailable=0,written=0,overwritten=0;unsigned count=0;Event events[Capacity]{};};

// Used only when the user explicitly enabled the diagnostic before startup.
// Hash/signature checks ensure the owned thiscall function and callsite are
// the verified build. Game state, settings, shaders and textures are untouched.
bool Install(FrameProvider provider,LogCallback log);
Stats Snapshot();

template<class Reader> bool VerifyStage(std::uintptr_t image,Reader read){
    const unsigned char stage[]={0x83,0xec,0x38,0x56,0x8b,0xf1,0x8b,0x8e,0xd0,0x32,0x00,0x00};
    const unsigned char caller[]={0x8b,0xce,0xe8,0x11,0xf6,0xff,0xff,0x8b,0xce};
    unsigned char actual[sizeof(stage)]{},callActual[sizeof(caller)]{};
    return read(image+0xb00b0,actual,sizeof(actual))&&!std::memcmp(actual,stage,sizeof(stage))&&
        read(image+0xb0a98,callActual,sizeof(callActual))&&!std::memcmp(callActual,caller,sizeof(caller));
}

template<class Reader> bool Capture(std::uintptr_t image,std::uintptr_t object,Reader read,StageState& result){
    if(image<0x10000u||object<0x10000u||object>0xffffcc00u)return false;
    const auto get=[&](std::uintptr_t at,auto& value){return at>=0x10000u&&read(at,&value,sizeof(value));};
    std::uint32_t owner=0,renderer=0;
    StageState next;next.postFilter=object;
    if(!get(image+0x7c1b0c,owner)||owner!=object||!get(image+0x6977e8,renderer)||renderer<0x10000u||
       !get(renderer+0x88,next.lod)||!get(object+0x10,next.amount)||!get(object+0xe0,next.sequence))return false;
    next.renderer=renderer;
    const unsigned viewOffsets[]={0x32c4,0x32c8,0x32cc,0x32d0};
    const unsigned resourceOffsets[]={0xe4,0xe8,0xec,0xf0,0x100,0x104,0x108,0x210,0x3254,0x3258,0x3260,0x3268,0x326c};
    for(unsigned i=0;i<4;++i)if(!get(object+viewOffsets[i],next.viewport[i]))return false;
    for(unsigned i=0;i<13;++i)if(!get(object+resourceOffsets[i],next.resources[i]))return false;
    if(!get(object+0x3288,next.shader[0])||!get(object+0x328c,next.shader[1])||
       !get(object+0x32ac,next.maskVariant)||!get(renderer+0xa4,next.resolveFlag)||
       !get(object+0x3270,next.auxRenderer))return false;
    if(next.auxRenderer>=0x10000u){
        std::uint32_t texture=0,surface=0,resolve=0;
        next.auxValid=get(next.auxRenderer+0x16b4,texture)&&get(next.auxRenderer+0x16b8,surface)&&get(next.auxRenderer+0x16bc,resolve);
        if(next.auxValid){next.maskTexture=texture;next.maskSurface=surface;next.resolveSurface=resolve;}
    }
    std::uint32_t ownerAfter=0,rendererAfter=0,auxAfter=0;
    if(!get(image+0x7c1b0c,ownerAfter)||ownerAfter!=owner||
       !get(image+0x6977e8,rendererAfter)||rendererAfter!=renderer||
       !get(object+0x3270,auxAfter)||auxAfter!=next.auxRenderer)return false;
    result=next;return true;
}
}
