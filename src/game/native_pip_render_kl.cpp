#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_pip_render_kl.hpp"
#include "native_camera_kl.hpp"
#include <atomic>

namespace NativePipRenderKL {
namespace {
// RVA BF600 is owned by NativeCameraKL. This module supplies a predicate to
// that single detour, preserving all six native arguments on the pass path.
std::uintptr_t image=0;
std::atomic<bool> installed{false},enabled{false},verified{false};
std::atomic<std::uint64_t> calls{0},suppressed{0},rejected{0};
std::atomic<std::uint64_t> activePipSeen{0},parentRejected{0},viewRejected{0},entryRejected{0};
std::atomic<std::uint64_t> fullRejected{0},localRejected{0},insetRejected{0},compositeSuppressed{0};
std::atomic<std::uint32_t> lastMode{0};
std::atomic<std::int32_t> lastRectangle[4]{};
std::atomic<std::uintptr_t> lastParent{0},lastView{0},lastOwnerView{0},lastCamera{0};
bool Read(std::uintptr_t at,void* output,std::size_t count) {
    SIZE_T got=0;
    return NativePipKL::Address(at,count)&&ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(at),output,count,&got)&&got==count;
}
bool Get(std::uintptr_t at,std::uint32_t& value){return Read(at,&value,4);}
struct Observed {NativePipKL::State state;Entry overlay;};
const Observed& PipState() {
    struct Cache {
        std::uint32_t system=0,tick=0;
        bool initialized=false;
        Observed observed{};
    };
    static thread_local Cache cache;
    std::uint32_t system=0,tick=0;
    if(!Get(image+0x6875F0,system)||!NativePipKL::Address(system,0x48)||!Get(system+0x44,tick)) {
        cache={};return cache.observed;
    }
    // The native frame counter also drives the PIP controller's lifecycle.
    // Keep the ownership walk out of each camera/scene-object render call.
    if(!cache.initialized||cache.system!=system||cache.tick!=tick) {
        cache.system=system;cache.tick=tick;cache.initialized=true;
        cache.observed.state=NativePipKL::Snapshot(image,Read);
        cache.observed.overlay=OverlayCamera(image,cache.observed.state,Read);
    }
    return cache.observed;
}
std::uintptr_t LocalCamera() {
    std::uint32_t data=0,player=0,camera=0;
    return Get(image+NativePipKL::GameDataRva,data)&&NativePipKL::Address(data,0xA30)&&
        Get(data+0xA2C,player)&&NativePipKL::Address(player,0x9A4)&&Get(player+0x9A0,camera)?camera:0;
}
bool SuppressProducer(void* self,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,
                      std::uintptr_t d,std::uintptr_t e,std::uintptr_t f) {
    (void)a;(void)c;(void)e;
    ++calls;
    // BF600 never reads its sixth (first-camera) argument. In particular,
    // an auxiliary camera sorted before the main camera is still auxiliary.
    // The outer dispatcher owns its marker/scratch and advances normally.
    if(enabled.load(std::memory_order_relaxed)&&b){++parentRejected;lastParent=b;}
    if(enabled.load(std::memory_order_relaxed)&&!b) {
        const auto at=reinterpret_cast<std::uintptr_t>(self);
        std::uint32_t vt=0;
        if(Get(at,vt)&&vt==image+CameraEntryVtableRva) {
            const auto& observed=PipState();const auto& pip=observed.state;
            if(pip.valid&&pip.active) {
                ++activePipSeen;lastMode=pip.mode;lastRectangle[0]=pip.left;lastRectangle[1]=pip.top;
                lastRectangle[2]=pip.right;lastRectangle[3]=pip.bottom;lastParent=b;lastView=d;
                std::uint32_t geometry=0,camera=0,view=0,viewVt=0;
                if(Get(pip.controller+0x51C,view)&&view==d&&Get(view,viewVt)&&
                   (viewVt==image+0x53DAC4||viewVt==image+0x540CCC)) {
                    lastOwnerView=view;
                    const bool sourceRead=Get(at+0xF4,geometry)&&Get(geometry+0x60,camera);
                    const bool primary=sourceRead&&geometry==pip.geometry&&camera==pip.camera;
                    const bool composite=sourceRead&&observed.overlay.valid&&
                        geometry==observed.overlay.geometry&&camera==observed.overlay.camera;
                    if(primary||composite) {
                        lastCamera=camera;
                        const auto entry=CameraEntry(image,at,Read);
                        const auto cinematic=NativeCinematicKL::Snapshot(image,Read);
                        std::uint8_t nativeMode=0xff;Read(image+0x8883A4,&nativeMode,1);
                        const auto local=LocalCamera();auto target=pip;target.camera=camera;target.geometry=geometry;
                        if(!entry.valid)++entryRejected;
                        else if(!cinematic.valid||cinematic.full||nativeMode!=0)++fullRejected;
                        else if(!local||camera==local)++localRejected;
                        else if(ShouldSuppress(true,target,cinematic,local,entry,d,view,f!=0,b,nativeMode)) {
                            ++suppressed;if(composite)++compositeSuppressed;return true;
                        } else ++insetRejected;
                    } else ++entryRejected;
                } else {lastOwnerView=view;++viewRejected;}
            }
        }
        ++rejected;
    }
    return false;
}
}
bool InitializeVerifiedImage(std::uintptr_t next,LogCallback log){
    if(installed.load())return next==image;
    if(!next||NativeCameraKL::Installed()){
        if(log)log("Native auxiliary PIP requires pristine producer proof before the camera detour.");return false;
    }
    image=next;verified.store(VerifyImage(image,Read));
    if(!verified.load()&&log)log("Native auxiliary PIP pristine producer signatures differ; original rendering retained.");
    return verified.load();
}
bool Install(bool hideAuxiliaryPip,LogCallback log) {
    if(installed.load())return true;
    enabled.store(hideAuxiliaryPip);
    if(!hideAuxiliaryPip)return true;
    if(!image||!verified.load()) {
        if(log)log("Native auxiliary PIP pristine producer proof unavailable; original rendering retained.");
        return false;
    }
    if(!NativeCameraKL::RegisterCameraProducerFilter(image,SuppressProducer)){
        if(log)log("Native auxiliary PIP camera-producer owner differs; original rendering retained.");
        return false;
    }
    installed.store(true);
    if(log)log("Native auxiliary inset PIP suppression uses the verified shared camera producer; controller updates retained.");
    return true;
}
void Shutdown() {
    enabled.store(false);
    // The shared camera callback remains registered until process exit. Any
    // producer already in flight now retains its original native path.
}
Status Snapshot() {
    Status result;
    result.installed=installed.load();result.enabled=enabled.load();result.verified=verified.load();
    result.calls=calls.load();result.suppressed=suppressed.load();result.rejected=rejected.load();
    result.activePipSeen=activePipSeen.load();result.parentRejected=parentRejected.load();
    result.viewRejected=viewRejected.load();result.entryRejected=entryRejected.load();
    result.fullRejected=fullRejected.load();result.localRejected=localRejected.load();
    result.insetRejected=insetRejected.load();result.compositeSuppressed=compositeSuppressed.load();
    result.mode=lastMode.load();for(unsigned i=0;i<4;++i)result.rectangle[i]=lastRectangle[i].load();
    result.parent=lastParent.load();result.view=lastView.load();result.ownerView=lastOwnerView.load();
    result.camera=lastCamera.load();return result;
}
}
