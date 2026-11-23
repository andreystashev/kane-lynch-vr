#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_effects_kl.hpp"
#include "native_effects_math_kl.hpp"
#include "native_effects_shader_kl.hpp"
#include "native_effects_image_kl.hpp"
#include "native_effects_producer_kl.hpp"
#include "native_particle_normal_call_kl.hpp"
#include "native_particle_deferred_call_kl.hpp"
#include "native_camera_kl_math.hpp"
#include "native_cinema_viewport.hpp"
#include "MinHook.h"
#include "native_hud_kl.hpp"
#include "native_tracer_visual_kl.hpp"
#include "profile.hpp"
#include "profile_timing.hpp"
#include <atomic>
#include <array>
#include <optional>
#include <cstdio>
#include <algorithm>
namespace NativeEffectsKL {
namespace {
std::atomic<ReadView> reader{nullptr};
std::atomic<bool> cinematicViewport{false};
std::atomic<unsigned> cinematicWidth{0},cinematicHeight{0};
std::atomic<DWORD> cinematicThread{0};
SRWLOCK cinematicProjectionLock=SRWLOCK_INIT;
float cinematicProjection[16]{};
std::atomic<std::uint64_t> cinematicViewportExpanded{0},cinematicViewportRestoreFailed{0};
std::atomic<std::uint64_t> cinematicViewportAttempts{0};
std::atomic<unsigned> cinematicViewportStage{0};
std::array<std::atomic<unsigned>,4> cinematicLastViewport{};
std::atomic<bool> installed{false};
std::atomic<bool> geometryInstalled{false};
std::atomic<std::uint64_t> billboardCalls{0},billboardEntered{0},billboardBound{0},billboardRejected{0},billboardUnexpected{0};
std::atomic<std::uint64_t> reflectedBillboardBound{0};
std::atomic<std::uint64_t> reflectedScatterDraws{0};
std::atomic<unsigned> lastBillboardMode{0};
std::atomic<std::uintptr_t> lastBillboardDescriptor{0};
std::uintptr_t nativeImage=0;
using BillboardBind=void(__thiscall*)(void*,void*,void*);
BillboardBind originalBillboardBind=nullptr;
std::atomic<unsigned> epoch{1};
std::atomic<std::uint64_t> shaderSeen{0},shaderRecognized{0},shaderDraws{0},shaderRestored{0},shaderFailed{0};
std::atomic<std::uint64_t> viewRejected{0},spriteWrites{0},spriteQuads{0},spriteRejected{0},restoreFailed{0};
std::atomic<std::uint64_t> projectionRejected{0};
std::array<std::atomic<std::uint64_t>,10> familySeen{},familyCalls{},familyDraws{},familyViewRejected{},familyProjectionRejected{};
std::atomic<std::uint64_t> manualWrites{0},manualQuads{0},manualRejected{0},particleWrites{0},particleQuadsObserved{0};
std::atomic<std::uint64_t> particleQuads{0},particleRejected{0},particleAlignedPreserved{0},particlePrefixRejected{0},sourceStateRejected{0};
std::atomic<std::uint64_t> radialFadeDraws{0},radialFogDraws{0},fogSourceRejected{0};
std::atomic<std::uint64_t> worldSprite5Writes{0},worldSprite6Writes{0},worldSpriteQuads{0},worldSpriteRejected{0};
using ParticleNormal=void(__fastcall*)(NativeCameraMath::Vec3*,const NativeCameraMath::Vec3*,const NativeCameraMath::Basis*);
ParticleNormal originalParticleNormal=nullptr;
std::atomic<bool> particleNormalInstalled{false};
std::atomic<std::uint64_t> particleNormalCalls{0},particleNormalApplied{0},particleNormalAlignedPreserved{0},particleNormalRejected{0};
SRWLOCK particleNormalLock=SRWLOCK_INIT;
ParticleNormalEvidence lastParticleNormalApplied,lastParticleNormalRejected;
std::uint64_t particleNormalSequence=0;
using ParticleDeferred=std::uintptr_t(__thiscall*)(void*,void*,const float*,unsigned,unsigned,unsigned,unsigned,unsigned,float);
ParticleDeferred originalParticleDeferred=nullptr;
std::atomic<bool> particleDeferredInstalled{false};
std::atomic<std::uint64_t> particleDeferredCalls{0},particleDeferredQuads{0},particleDeferredAlignedPreserved{0},particleDeferredRejected{0};
std::atomic<VisualTraceActive> visualTraceActive{nullptr};
thread_local VisualSourceDiagnostic mappedVisualSource;
SRWLOCK visualSourceLock=SRWLOCK_INIT;
std::array<VisualSourceDiagnostic,128> billboardVisualSources;
std::uint64_t visualSourceSequence=0,billboardVisualCursor=0;
bool VisualTraceEnabled(){const auto active=visualTraceActive.load(std::memory_order_relaxed);return active&&active();}
bool VisualRead(std::uintptr_t at,void* out,std::size_t size){SIZE_T done=0;return at>=0x10000u&&size&&at<=0xffffffffu-size&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;}
void VisualView(VisualSourceDiagnostic& out,const NativeCameraKL::EffectsView& view){
    out.viewKnown=true;out.frame=view.frame;
    std::memcpy(out.eye,&view.eye,sizeof(out.eye));std::memcpy(out.reference,&view.reference,sizeof(out.reference));
    if(view.worldKnown)std::memcpy(out.world,view.world,sizeof(out.world));
}
LogCallback logger=nullptr;
std::atomic<unsigned> unknownLogged{0};
struct BillboardScope;
thread_local BillboardScope* billboardScope=nullptr;
struct BillboardScope {
    bool reflected=false;
    void* state=nullptr;
    NativeCameraMath::Basis trackedBasis{};
    bool changed=false,observed=false;
    std::optional<VisualSourceDiagnostic> visual;
    void Restore(){if(changed){std::memcpy(static_cast<unsigned char*>(state)+0xBC,&trackedBasis,sizeof(trackedBasis));changed=false;}}
    ~BillboardScope(){Restore();if(billboardScope==this)billboardScope=nullptr;}
};
// All pointers below are borrowed native arguments/data that 72E00 is about to
// read itself. No arbitrary object scan, retained source pointer or per-mesh RPM.
template<class T> bool Borrowed(std::uintptr_t at,T& output){if(at<0x10000u||at>0xFFFFFFFFu-sizeof(T))return false;std::memcpy(&output,reinterpret_cast<const void*>(at),sizeof(T));return true;}
void __fastcall ParticleNormalHook(NativeCameraMath::Vec3* output,const NativeCameraMath::Vec3* source,const NativeCameraMath::Basis* matrix){
    // Reproduce the exact original native point/normal transform first.
    originalParticleNormal(output,source,matrix);
    ProfileTiming::Scope own(ProfileTiming::EffectsOwn);++particleNormalCalls;
    const auto tracked=*output;NativeCameraMath::Vec3 corrected{};
    NativeHudKL::MappedVertexContext context;
    const bool known=NativeHudKL::ReadParticleDrawContext(context)&&context.spriteType==7&&context.spriteDrawId;
    struct Cached {std::uint64_t scope=0;std::uintptr_t state=0;unsigned generation=0;bool valid=false,recorded=false;NativeCameraKL::EffectsView view;};
    static thread_local Cached cached;
    static thread_local DWORD unknownRecorded=0;
    unsigned reason=0;
    if(!known)reason=1;
    else if(context.particleMode!=0){reason=2;++particleNormalAlignedPreserved;}
    else {
        const auto generation=epoch.load(std::memory_order_relaxed);
        if(cached.scope!=context.spriteDrawId||cached.state!=context.sourceState||cached.generation!=generation){
            cached={};cached.scope=context.spriteDrawId;cached.state=context.sourceState;cached.generation=generation;
            const auto read=reader.load(std::memory_order_relaxed);
            cached.valid=read&&read(cached.view)&&cached.view.state&&cached.view.state==context.sourceState;
        }
        if(!cached.valid)reason=3;
        else if(!NativeEffectsKLMath::ReferenceNormal(tracked,cached.view.eye,cached.view.reference,corrected))reason=4;
        else {*output=corrected;++particleNormalApplied;}
    }
    if(reason&&reason!=2)++particleNormalRejected;
    const DWORD now=GetTickCount();
    // One coherent sample per native draw scope; no per-particle logging,
    // allocations, GPU queries or repeated main-view kernel reads.
    const bool record=known?(cached.scope!=context.spriteDrawId||!cached.recorded):(now-unknownRecorded>=100);
    if(record){
        ParticleNormalEvidence evidence;evidence.valid=true;evidence.tick=now;evidence.reason=reason;
        evidence.drawScopeId=context.spriteDrawId;evidence.mode=context.particleMode;evidence.sourceState=context.sourceState;
        evidence.sourceEntry=context.sourceEntry;evidence.sourceGeometry=context.sourceGeometry;
        evidence.normalData=reinterpret_cast<std::uintptr_t>(source)-4;
        const float authored[3]={source->x,source->y,source->z},native[3]={tracked.x,tracked.y,tracked.z};
        std::memcpy(evidence.authored,authored,sizeof(authored));std::memcpy(evidence.tracked,native,sizeof(native));
        if(!reason){const float ref[3]={corrected.x,corrected.y,corrected.z};std::memcpy(evidence.reference,ref,sizeof(ref));
            evidence.eye=cached.view.eyeIndex;std::memcpy(evidence.eyeBasis,&cached.view.eye.basis,sizeof(evidence.eyeBasis));
            std::memcpy(evidence.referenceBasis,&cached.view.reference.basis,sizeof(evidence.referenceBasis));}
        AcquireSRWLockExclusive(&particleNormalLock);evidence.sequence=++particleNormalSequence;
        if(reason)lastParticleNormalRejected=evidence;else lastParticleNormalApplied=evidence;
        ReleaseSRWLockExclusive(&particleNormalLock);
        if(known){cached.scope=context.spriteDrawId;cached.recorded=true;}else unknownRecorded=now;
    }
}
bool InstallParticleNormal(std::uintptr_t image){
    if(particleNormalInstalled)return true;
    if(sizeof(void*)!=4||!NativeEffectsKLProducer::VerifyParticleNormalCall(image,[](auto at,void* out,std::size_t bytes){SIZE_T got=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;}))return false;
    originalParticleNormal=reinterpret_cast<ParticleNormal>(image+0x2E780);
    KL1ParticleNormalReturn=reinterpret_cast<void*>(image+0x99A21);KL1ParticleNormalTarget=reinterpret_cast<void*>(&ParticleNormalHook);
    void* target=reinterpret_cast<void*>(image+0x99A1C);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&KL1ParticleNormalCallHook),nullptr)!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return false;}
    particleNormalInstalled=true;return true;
}
std::uintptr_t __fastcall ParticleDeferredHook(void* manager,void*,void* vertices,const float* depths,
    unsigned material,unsigned technique,unsigned flags,unsigned mode,unsigned count,float order){
    ProfileTiming::Scope own(ProfileTiming::EffectsOwn);++particleDeferredCalls;
    NativeHudKL::MappedVertexContext context;NativeCameraKL::EffectsView view;
    const auto read=reader.load(std::memory_order_relaxed);
    const bool source=vertices==reinterpret_cast<void*>(nativeImage+0x791400)&&
        depths==reinterpret_cast<const float*>(nativeImage+0x789400)&&count&&count<=2048&&
        NativeHudKL::ReadParticleDrawContext(context)&&context.spriteType==7&&context.spriteDrawId&&
        context.sourceEntry&&context.sourceGeometry&&context.sourceState;
    if(source&&context.particleMode!=0)++particleDeferredAlignedPreserved;
    else if(!source||!read||!read(view)||!view.state||view.state!=context.sourceState||
        !NativeEffectsKLMath::MappedFacingParticleQuads(vertices,count,view.eye,view.reference,view.centre))++particleDeferredRejected;
    else particleDeferredQuads+=count;
    // The native queue copies these bytes now, before this exact Type7 scope
    // ends. It later renders through generic98C00 without a SpriteDraw scope.
    // Keep all queue sort/material/depth arguments exactly as authored.
    own.Stop();return originalParticleDeferred(manager,vertices,depths,material,technique,flags,mode,count,order);
}
bool InstallParticleDeferred(std::uintptr_t image){
    if(particleDeferredInstalled)return true;
    if(sizeof(void*)!=4||!NativeEffectsKLProducer::VerifyParticleDeferredCall(image,[](auto at,void* out,std::size_t bytes){SIZE_T got=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;}))return false;
    originalParticleDeferred=reinterpret_cast<ParticleDeferred>(image+0x7A310);
    KL1ParticleDeferredReturn=reinterpret_cast<void*>(image+0x9A189);KL1ParticleDeferredTarget=reinterpret_cast<void*>(&ParticleDeferredHook);
    void* target=reinterpret_cast<void*>(image+0x9A184);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&KL1ParticleDeferredCallHook),nullptr)!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return false;}
    particleDeferredInstalled=true;return true;
}
void __fastcall BillboardBindHook(void* entry,void*,void* state,void* instance){
    ProfileTiming::Scope own(ProfileTiming::EffectsOwn);
    const auto native=[&]{own.Stop();originalBillboardBind(entry,state,instance);};
    ++billboardCalls;
    if(billboardScope){billboardScope->Restore();++billboardUnexpected;native();return;}
    std::uintptr_t descriptor=0;unsigned mode=0;
    if(!NativeEffectsKLProducer::BillboardDescriptor(nativeImage,reinterpret_cast<std::uintptr_t>(instance),
        [](auto at,auto& out){return Borrowed(at,out);},descriptor,mode)||!mode){native();return;}
    const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;
    NativeCameraMath::CameraPose referenceView;
    const auto stateId=reinterpret_cast<std::uintptr_t>(state);
    const bool main=read&&read(view)&&view.state==stateId&&
        NativeCameraKLMath::ViewFromWorld(view.reference,referenceView);
    const bool reflected=!main&&NativeCameraKL::ReadReflectionBillboardReference(stateId,referenceView);
    if(!main&&!reflected){
        ++billboardRejected;native();return;
    }
    BillboardScope scope;scope.state=state;scope.reflected=reflected;
    if(main&&VisualTraceEnabled()){
        scope.visual.emplace();auto& visual=*scope.visual;visual.valid=true;visual.kind=2;visual.state=reinterpret_cast<std::uintptr_t>(state);
        visual.entry=reinterpret_cast<std::uintptr_t>(entry);visual.instance=reinterpret_cast<std::uintptr_t>(instance);
        visual.descriptor=descriptor;visual.mode=mode;visual.tick=GetTickCount();VisualView(visual,view);
        std::uint32_t slot=2,object=0,handle=0;
        std::uint32_t vt=0;if(Borrowed(visual.entry,vt))visual.entryVtable=vt;
        if(Borrowed(nativeImage+0x7678A4,slot)&&slot<2){visual.slot=slot;
            visual.rootKnown=VisualRead(visual.entry+0x10+slot*0x30,visual.rootOriginal,sizeof(visual.rootOriginal));}
        if(Borrowed(visual.instance+4,object)){visual.object=object;if(Borrowed(std::uintptr_t(object)+0x14,handle))visual.descriptorHandle=handle;}
        VisualRead(visual.state+0xBC,visual.nativeView,sizeof(visual.nativeView));VisualRead(visual.state+0x3F8,visual.projection,sizeof(visual.projection));
    }
    std::memcpy(&scope.trackedBasis,static_cast<unsigned char*>(state)+0xBC,sizeof(scope.trackedBasis));
    billboardScope=&scope;scope.changed=true;++billboardEntered;lastBillboardMode=mode;lastBillboardDescriptor=descriptor;
    // Only the native camera-facing BASIS builder sees reference axes. Its
    // authored centre is copied verbatim. ObserveWorldBind restores tracked
    // axes before original 71770 derives WV, projection or material binding.
    std::memcpy(static_cast<unsigned char*>(state)+0xBC,&referenceView.basis,sizeof(referenceView.basis));
    native();
    {ProfileTiming::Scope post(ProfileTiming::EffectsOwn);if(!scope.observed)++billboardUnexpected;scope.Restore();billboardScope=nullptr;}
}
SRWLOCK probeLock=SRWLOCK_INIT;
Stats probe;
struct Entry {
    IDirect3DVertexShader9* source=nullptr,*patched=nullptr;
    unsigned family=0;
    std::uint64_t used=0;
    void Clear(){if(patched)patched->Release();if(source)source->Release();*this={};}
};
struct Cache {
    IDirect3DDevice9* device=nullptr;
    std::array<Entry,256> entries{};
    unsigned count=0,observedEpoch=0,last=0;
    std::uint64_t clock=0;
    ~Cache(){Clear();}
    void Clear(){for(auto& e:entries)e.Clear();count=0;clock=0;last=0;}
    Entry* Get(IDirect3DDevice9* candidate) {
        const auto generation=epoch.load(std::memory_order_relaxed);
        if(device!=candidate||observedEpoch!=generation){Clear();device=candidate;observedEpoch=generation;}
        IDirect3DVertexShader9* source=nullptr;
        if(FAILED(device->GetVertexShader(&source))||!source)return nullptr;
        if(count&&entries[last].source==source){source->Release();entries[last].used=++clock;return &entries[last];}
        for(unsigned i=0;i<count;++i)if(entries[i].source==source){source->Release();entries[i].used=++clock;last=i;return &entries[i];}
        Entry next;next.source=source;next.used=++clock;++shaderSeen;
        UINT bytes=0;D3DCAPS9 caps{};
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0&&
           SUCCEEDED(device->GetDeviceCaps(&caps))&&caps.MaxVertexShaderConst>=96) {
            std::vector<std::uint32_t> native(bytes/4),patched;
            if(SUCCEEDED(source->GetFunction(native.data(),&bytes))&&bytes==native.size()*4) {
                if(NativeEffectsKLShader::Patch(native,patched,&next.family)) {
                    ++shaderRecognized;
                    if((patched[0]&0xFFFFu)>WORD(caps.VertexShaderVersion)||
                       FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&next.patched)))++shaderFailed;
                } else next.family=NativeEffectsKLShader::ObservedFamily(native);
                ++familySeen[next.family];
                if(!next.family&&logger&&unknownLogged.fetch_add(1)<256){
                    std::uint64_t hash=0;unsigned words=0;
                    if(NativeEffectsKLShader::Fingerprint(native,hash,words)){
                        char line[160];std::snprintf(line,sizeof(line),"Unclassified native D3D9 VS version=%08x instructions=%u hash=%016llx; original retained.",
                            native[0],words,static_cast<unsigned long long>(hash));logger(line);
                    }
                }
            }
        }
        unsigned slot=count;
        if(count<entries.size())++count;
        else{slot=0;for(unsigned i=1;i<count;++i)if(entries[i].used<entries[slot].used)slot=i;entries[slot].Clear();}
        entries[slot]=next;last=slot;return &entries[slot];
    }
};
thread_local Cache cache;
struct ProbeKey {std::uint64_t frame=0;unsigned eye=0,generation=0;bool known=false;};
thread_local std::array<ProbeKey,10> probeKeys;
void Probe(IDirect3DDevice9* device,Entry& entry,const NativeCameraKL::EffectsView& view) {
    if(!entry.family||entry.family>=probeKeys.size())return;
    auto& key=probeKeys[entry.family];const auto generation=epoch.load(std::memory_order_relaxed);
    if(key.known&&key.frame==view.frame&&key.eye==view.eyeIndex&&key.generation==generation)return;
    key={view.frame,view.eyeIndex,generation,true};
    D3DVIEWPORT9 viewport{};IDirect3DSurface9* surface=nullptr;D3DSURFACE_DESC description{};DWORD colour=0;
    if(FAILED(device->GetViewport(&viewport))||FAILED(device->GetRenderTarget(0,&surface))||!surface)return;
    const auto result=surface->GetDesc(&description);surface->Release();
    if(FAILED(result)||FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&colour)))return;
    float actualModel[16]{},expectedModel[16]{},error=0;bool modelKnown=false,modelMatch=true,transposeMatch=true;
    if(entry.family==3&&view.worldKnown&&NativeEffectsKLMath::WorldViewShader(view.world,view.eye,expectedModel)&&
       SUCCEEDED(device->GetVertexShaderConstantF(65,actualModel,4))){
        modelKnown=true;
        for(unsigned i=0;i<16;++i){const float e=std::fabs(actualModel[i]-expectedModel[i]);error=std::max(error,e);
            modelMatch=modelMatch&&std::isfinite(actualModel[i])&&e<=.002f+std::fabs(expectedModel[i])*2e-5f;
            transposeMatch=transposeMatch&&std::isfinite(actualModel[i])&&std::fabs(actualModel[i]-expectedModel[(i%4)*4+i/4])<=.002f+std::fabs(expectedModel[(i%4)*4+i/4])*2e-5f;}
    }
    AcquireSRWLockExclusive(&probeLock);
    if(modelKnown){++probe.modelViewSamples;if(modelMatch)++probe.modelViewMatches;else if(transposeMatch)++probe.modelViewTransposeMatches;else ++probe.modelViewOther;
        probe.modelViewMaxError=std::max(probe.modelViewMaxError,error);
        std::memcpy(probe.lastActualModelView.data(),actualModel,sizeof(actualModel));std::memcpy(probe.lastExpectedModelView.data(),expectedModel,sizeof(expectedModel));}
    ++probe.viewportProbeSamples;probe.lastViewportWidth=viewport.Width;probe.lastViewportHeight=viewport.Height;
    probe.lastTargetWidth=description.Width;probe.lastTargetHeight=description.Height;
    probe.lastProbeFamily=entry.family;probe.lastTargetFormat=description.Format;probe.lastColourWriteMask=colour;
    if(std::uint64_t(viewport.Width)*viewport.Height>std::uint64_t(probe.maxViewportWidth)*probe.maxViewportHeight){probe.maxViewportWidth=viewport.Width;probe.maxViewportHeight=viewport.Height;}
    if(std::uint64_t(description.Width)*description.Height>std::uint64_t(probe.maxTargetWidth)*probe.maxTargetHeight){probe.maxTargetWidth=description.Width;probe.maxTargetHeight=description.Height;}
    ReleaseSRWLockExclusive(&probeLock);
}
struct Scope {
    NativeCinemaViewport::Scope viewport;
    IDirect3DDevice9* device=nullptr;
    IDirect3DVertexShader9* source=nullptr;
    float saved[8]{};
    bool active=false,failed=false;
    explicit Scope(IDirect3DDevice9* candidate) {
        const auto read=reader.load(std::memory_order_relaxed);if(!candidate||!read)return;
        if(cinematicViewport.load(std::memory_order_acquire)&&
           cinematicThread.load(std::memory_order_relaxed)==GetCurrentThreadId()) {
            ++cinematicViewportAttempts;
            float projection[16];AcquireSRWLockShared(&cinematicProjectionLock);
            std::memcpy(projection,cinematicProjection,sizeof(projection));ReleaseSRWLockShared(&cinematicProjectionLock);
            if(viewport.Begin(candidate,true,cinematicWidth.load(),cinematicHeight.load(),projection))
                ++cinematicViewportExpanded;
            cinematicViewportStage=viewport.stage;
            if(viewport.observed.Width&&viewport.observed.Height<cinematicHeight.load()){
                cinematicLastViewport[0]=viewport.observed.X;cinematicLastViewport[1]=viewport.observed.Y;
                cinematicLastViewport[2]=viewport.observed.Width;cinematicLastViewport[3]=viewport.observed.Height;}
        }
        auto* entry=cache.Get(candidate);if(!entry)return;++familyCalls[entry->family];if(!entry->patched)return;
        NativeCameraKL::EffectsView view;float columns[8]{};
        const bool main=read(view);
        NativeCameraKL::ReflectionTargetView reflectedView;
        const bool reflected=!main&&entry->family>=1&&entry->family<=4&&
            NativeCameraKL::ReadReflectionScatter(reflectedView,columns);
        const bool columnsReady=main&&(entry->family==5?NativeEffectsKLMath::RollColumns(view.eye,view.projection,columns):
            NativeEffectsKLMath::Columns(view.eye,view.centre,columns));
        if(!columnsReady&&!reflected){++viewRejected;++familyViewRejected[entry->family];return;}
        if(entry->family==8){
            NativeHudKL::MappedVertexContext source;
            if(!NativeHudKL::ReadParticleDrawContext(source)||source.particleMode!=0||!view.state||source.sourceState!=view.state){
                ++fogSourceRejected;return;
            }
        }
        if(entry->family<=4||entry->family==8) {
            float actualProjection[16]{};
            const auto start=entry->family==8?0u:entry->family<=2?24u:69u;
            // These exact compiled programs bind m44Proj at these registers.
            // Native Common matrices are transposed before upload. Require
            // the same accepted eye P, not a shadow/special-pass projection.
            const float* projection=reflected?reflectedView.projection:view.projection;
            if(FAILED(candidate->GetVertexShaderConstantF(start,actualProjection,4))||
               !NativeEffectsKLMath::ProjectionMatches(projection,actualProjection)) {
                ++projectionRejected;++familyProjectionRejected[entry->family];return;
            }
        } else {
            DWORD colour=0;
            if(FAILED(candidate->GetRenderState(D3DRS_COLORWRITEENABLE,&colour))||!(colour&7u)) {
                ++projectionRejected;++familyProjectionRejected[entry->family];return;
            }
        }
        if(main)Probe(candidate,*entry,view);
        device=candidate;source=entry->source;
        if(FAILED(device->GetVertexShaderConstantF(94,saved,2))){++shaderFailed;device=nullptr;return;}
        if(FAILED(device->SetVertexShaderConstantF(94,columns,2))){
            ++shaderFailed;if(FAILED(device->SetVertexShaderConstantF(94,saved,2))){++restoreFailed;failed=true;}return;
        }
        if(FAILED(device->SetVertexShader(entry->patched))){
            ++shaderFailed;
            const auto s=device->SetVertexShader(source),c=device->SetVertexShaderConstantF(94,saved,2);
            if(FAILED(s)||FAILED(c)){++restoreFailed;failed=true;}return;
        }
        active=true;++shaderDraws;++familyDraws[entry->family];
        if(reflected)++reflectedScatterDraws;
        if(entry->family==3)++radialFadeDraws;
        if(entry->family==8)++radialFogDraws;
    }
    HRESULT Finish(HRESULT result) {
        if(!viewport.Finish()){++cinematicViewportRestoreFailed;failed=true;}
        if(active){active=false;const auto s=device->SetVertexShader(source),c=device->SetVertexShaderConstantF(94,saved,2);
            if(FAILED(s)||FAILED(c)){++restoreFailed;failed=true;}else ++shaderRestored;}
        return failed&&SUCCEEDED(result)?E_FAIL:result;
    }
    ~Scope(){Finish(S_OK);}
};
thread_local std::optional<Scope> current;
thread_local bool entered=false;
}
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva){
    auto* scope=billboardScope;if(!scope||scope->state!=state||!scope->changed)return;
    ProfileTiming::Scope own(ProfileTiming::EffectsOwn);
    // Fail-open ordering is unconditional for this state. An unexpected caller
    // must NEVER get reference VIEW through the actual native world binder.
    scope->Restore();scope->observed=true;
    if(scope->visual&&scope->visual->valid&&source){
        auto next=*scope->visual;std::memcpy(next.rootBound,source,sizeof(next.rootBound));
        std::memcpy(next.world,source,sizeof(next.world));
        next.reason=callerRva==0x72F6A?0u:1u;
        AcquireSRWLockExclusive(&visualSourceLock);next.sequence=++visualSourceSequence;
        billboardVisualSources[billboardVisualCursor++%billboardVisualSources.size()]=next;
        ReleaseSRWLockExclusive(&visualSourceLock);
    }
    if(callerRva==0x72F6A&&source){
        ++billboardBound;
        if(scope->reflected)++reflectedBillboardBound;
    }else ++billboardUnexpected;
}
struct VisualMappedScope {
    void* data=nullptr;std::optional<VisualSourceDiagnostic> storage;
    VisualMappedScope(std::uint32_t writer,void* mapped,std::size_t bytes){
        if(!VisualTraceEnabled()||!mapped||bytes<96)return;
        NativeHudKL::MappedVertexContext context;
        if(!NativeHudKL::ReadMappedVertexContext(context)||!context.sourceEntry)return;
        storage.emplace();auto& next=*storage;
        if(!NativeHudKL::ReadSpriteVisualSource(context.sourceEntry,context.sourceState,next))return;
        data=mapped;next.writer=writer;next.allocatedQuads=unsigned(bytes/96);
        next.liveQuads=context.spriteType==7?(context.liveKnown?context.liveQuads:0):next.allocatedQuads;
        next.firstRecord=context.spriteType==7?0:context.spriteFirst;
        if(next.liveQuads)next.rawBeforeKnown=VisualRead(reinterpret_cast<std::uintptr_t>(mapped),next.rawQuadBefore,sizeof(next.rawQuadBefore));
        const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;
        if(read&&read(view)&&view.state==context.sourceState)VisualView(next,view);else next.reason=1;
        next.recordStride=next.type==5?40u:next.type==6?116u:next.type==7?60u:0;
        if(next.records&&next.recordStride){
            std::uint32_t count=0;
            if(VisualRead(next.geometry,&count,4)&&count<=0x100000&&next.firstRecord<=count){
                next.recordCount=std::min(4u,count-next.firstRecord);next.recordsKnown=true;
                for(unsigned i=0;i<next.recordCount;++i)if(!VisualRead(next.records+std::uintptr_t(next.firstRecord+i)*next.recordStride,
                    next.rawRecords[i],next.recordStride)){next.recordsKnown=false;break;}
            }
        }
    }
    ~VisualMappedScope(){
        if(!data||!storage)return;auto& next=*storage;
        if(next.rawBeforeKnown)next.rawAfterKnown=VisualRead(reinterpret_cast<std::uintptr_t>(data),next.rawQuadAfter,sizeof(next.rawQuadAfter));
        AcquireSRWLockExclusive(&visualSourceLock);next.sequence=++visualSourceSequence;ReleaseSRWLockExclusive(&visualSourceLock);
        mappedVisualSource=next;
    }
};
void FilterMapped(std::uint32_t writer,void* data,std::size_t bytes) {
    VisualMappedScope visual(writer,data,bytes);
    if(writer==0x99551||writer==0x996BF){
        if(writer==0x99551)++worldSprite5Writes;else ++worldSprite6Writes;
        NativeHudKL::MappedVertexContext context;NativeCameraKL::EffectsView view;
        const auto read=reader.load(std::memory_order_relaxed);const unsigned type=writer==0x99551?5u:6u;
        const std::size_t quads=bytes/(4*sizeof(NativeEffectsKLMath::Vertex));
        if(!data||!bytes||bytes%(4*sizeof(NativeEffectsKLMath::Vertex))||
           !NativeHudKL::ReadMappedVertexContext(context)||context.spriteType!=type||!context.sourceEntry||!context.spriteRecords||
           context.spriteFirst>context.spriteCount||quads>context.spriteCount-context.spriteFirst||
           !read||!read(view)||!view.worldKnown||!view.state||view.state!=context.sourceState){++worldSpriteRejected;return;}
        const auto records=context.spriteRecords+std::uintptr_t(context.spriteFirst)*(type==5?40u:0x74u);
        auto reference=view.reference;
        NativeTracerVisualKL::ReadReference(context.sourceEntry,context.sourceGeometry,context.spriteRecords,type,
            context.spriteFirst,static_cast<unsigned>(quads),reference);
        if(!NativeEffectsKLMath::MappedWorldSpriteQuads(data,bytes,reinterpret_cast<const void*>(records),type,
             view.world,view.eye,reference)){++worldSpriteRejected;return;}
        worldSpriteQuads+=quads;return;
    }
    if(writer==0x9997E){
        ++particleWrites;particleQuadsObserved+=bytes/(4*sizeof(NativeEffectsKLMath::Vertex));
        if(!data||!bytes||bytes%(4*sizeof(NativeEffectsKLMath::Vertex))||bytes>2048*4*sizeof(NativeEffectsKLMath::Vertex)){++particlePrefixRejected;return;}
        NativeHudKL::MappedVertexContext context;
        if(!NativeHudKL::ReadMappedVertexContext(context)||!context.liveKnown||context.liveQuads>bytes/(4*sizeof(NativeEffectsKLMath::Vertex))){++particlePrefixRejected;return;}
        if(context.particleMode!=0){++particleAlignedPreserved;return;}
        const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;
        if(!read||!read(view)||!view.state||view.state!=context.sourceState){++sourceStateRejected;++particleRejected;return;}
        if(!NativeEffectsKLMath::MappedFacingParticleQuads(data,context.liveQuads,view.eye,view.reference,view.centre)){++particleRejected;return;}
        particleQuads+=context.liveQuads;return;
    }
    if(writer!=0x99360&&writer!=0xA37D3)return;
    if(writer==0xA37D3)++manualWrites;else ++spriteWrites;
    const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;float columns[8]{};
    if(writer==0xA37D3){
        NativeHudKL::MappedVertexContext context;
        if(!NativeHudKL::ReadMappedVertexContext(context)||!read||!read(view)||!view.state||view.state!=context.sourceState){++sourceStateRejected;++manualRejected;return;}
        if(!NativeEffectsKLMath::MappedManualQuads(data,bytes,view.eye,view.reference)){++manualRejected;return;}
        manualQuads+=bytes/(4*sizeof(NativeEffectsKLMath::ManualVertex));return;
    }
    if(!read||!read(view)||!NativeEffectsKLMath::Columns(view.eye,view.centre,columns)||
       !NativeEffectsKLMath::MappedQuads(data,bytes,columns)){++spriteRejected;return;}
    spriteQuads+=bytes/(4*sizeof(NativeEffectsKLMath::Vertex));
}
void Configure(ReadView read){reader.store(read);}
void ConfigureVisualSourceDiagnostics(VisualTraceActive active){visualTraceActive.store(active,std::memory_order_relaxed);NativeHudKL::SetVisualSourceTrace(active);}
bool SnapshotVisualSource(std::uintptr_t entry,std::uintptr_t state,VisualSourceDiagnostic& output){
    if(!entry||!state||!VisualTraceEnabled())return false;
    VisualSourceDiagnostic sprite;
    if(NativeHudKL::ReadSpriteVisualSource(entry,state,sprite)){
        const auto& mapped=mappedVisualSource;
        if(mapped.valid&&mapped.entry==entry&&mapped.state==state&&mapped.slot==sprite.slot&&mapped.stamp==sprite.stamp&&
           mapped.drawScopeId==sprite.drawScopeId){auto next=mapped;next.producer=sprite.producer;output=next;return true;}
        const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;
        if(read&&read(view)&&view.state==state)VisualView(sprite,view);
        output=sprite;return true;
    }
    const auto read=reader.load(std::memory_order_relaxed);NativeCameraKL::EffectsView view;
    if(!read||!read(view)||view.state!=state||!view.frame)return false;
    std::uint32_t slot=2,vt=0;float world[12]{};
    if(!VisualRead(nativeImage+0x7678A4,&slot,4)||slot>1||!VisualRead(entry,&vt,4)||!VisualRead(state+0x8C,world,sizeof(world)))return false;
    bool found=false;VisualSourceDiagnostic next;
    AcquireSRWLockShared(&visualSourceLock);
    for(const auto& candidate:billboardVisualSources)if(candidate.valid&&candidate.entry==entry&&candidate.entryVtable==vt&&candidate.state==state&&candidate.slot==slot&&
        candidate.viewKnown&&candidate.frame==view.frame&&!std::memcmp(candidate.rootBound,world,sizeof(world))&&(!found||candidate.sequence>next.sequence)){
        next=candidate;found=true;}
    ReleaseSRWLockShared(&visualSourceLock);if(!found)return false;output=next;return true;
}
void SetCinematicViewport(bool enabled,unsigned width,unsigned height,const float* projection){
    cinematicViewport=false;
    if(enabled&&projection){AcquireSRWLockExclusive(&cinematicProjectionLock);std::memcpy(cinematicProjection,projection,sizeof(cinematicProjection));ReleaseSRWLockExclusive(&cinematicProjectionLock);
        cinematicWidth=width;cinematicHeight=height;cinematicThread=GetCurrentThreadId();
        cinematicViewport.store(true,std::memory_order_release);}
}
void InvalidateDevice(){epoch.fetch_add(1,std::memory_order_relaxed);}
bool Install(LogCallback log,bool worldObserverAvailable) {
    if(installed.load())return true;
    logger=log;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!NativeEffectsKLImage::Verify(image,[](auto at,void* out,auto bytes){SIZE_T got=0;return
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;})) {
        if(log)log("Native world-effect writer signatures rejected; original effects retained.");return false;
    }
    Configure(NativeCameraKL::ReadEffectsView);NativeHudKL::SetMappedVertexFilter(FilterMapped);installed=true;
    nativeImage=image;
    if(InstallParticleNormal(image)){if(log)log("Native lit mode0 particles use reference corner-lighting basis; authored normals, animation, colors and world placement retained.");}
    else if(log)log("Native particle normal callsite correction unavailable; original corner lighting retained.");
    if(InstallParticleDeferred(image)){if(log)log("Exact deferred Type7 particle queue uses stereo-position-facing corners before native batch copy; depth/material/UV/colors retained.");}
    else if(log)log("Native deferred Type7 callsite correction unavailable; original queued corners retained.");
    if(worldObserverAvailable&&NativeEffectsKLProducer::VerifyBillboard(image,[](auto at,void* out,auto bytes){SIZE_T got=0;return
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;})){
        void* target=reinterpret_cast<void*>(image+0x72E00);
        if(MH_CreateHook(target,reinterpret_cast<void*>(&BillboardBindHook),reinterpret_cast<void**>(&originalBillboardBind))==MH_OK){
            if(MH_EnableHook(target)==MH_OK)geometryInstalled=true;
            else {MH_RemoveHook(target);originalBillboardBind=nullptr;}
        }
    }
    if(log&&!geometryInstalled.load())log("Native geometry billboard correction unavailable; original mesh orientation retained.");
    if(log&&geometryInstalled.load())log("Native descriptor8 mesh billboards use reference basis; tracked VIEW restored before the original world binder.");
    if(log)log("Exact Scatter/SpriteBox and ManualScatter retain world/reference axes; mode0 live smoke faces stereo-centre position without head rotation. Aligned particles and foreign views retained.");
    return true;
}
void* BeginDraw(IDirect3DDevice9* device) {
    ProfileTiming::Scope profile(ProfileTiming::EffectsOwn);
    if(entered||!reader.load(std::memory_order_relaxed))return nullptr;
    entered=true;current.emplace(device);return &*current;
}
HRESULT FinishDraw(void* token,HRESULT result) {
    ProfileTiming::Scope profile(ProfileTiming::EffectsOwn);
    if(!current||token!=&*current)return result;
    result=current->Finish(result);current.reset();entered=false;return result;
}
Stats Snapshot() {
    AcquireSRWLockShared(&probeLock);Stats result=probe;ReleaseSRWLockShared(&probeLock);
    result.installed=installed.load();result.shaderSeen=shaderSeen.load();result.shaderRecognized=shaderRecognized.load();
    result.geometryInstalled=geometryInstalled.load();result.billboardCalls=billboardCalls.load();result.billboardEntered=billboardEntered.load();
    result.cinematicViewportExpanded=cinematicViewportExpanded;result.cinematicViewportRestoreFailed=cinematicViewportRestoreFailed;
    result.cinematicViewportAttempts=cinematicViewportAttempts;result.cinematicViewportStage=cinematicViewportStage;
    for(unsigned i=0;i<4;++i)result.cinematicLastViewport[i]=cinematicLastViewport[i];
    result.billboardBound=billboardBound.load();result.billboardRejected=billboardRejected.load();result.billboardUnexpected=billboardUnexpected.load();
    result.reflectedBillboardBound=reflectedBillboardBound.load();
    result.reflectedScatterDraws=reflectedScatterDraws.load();
    result.lastBillboardMode=lastBillboardMode.load();result.lastBillboardDescriptor=lastBillboardDescriptor.load();
    result.shaderDraws=shaderDraws.load();result.shaderRestored=shaderRestored.load();result.shaderFailed=shaderFailed.load();
    result.viewRejected=viewRejected.load();result.spriteWrites=spriteWrites.load();result.spriteQuads=spriteQuads.load();
    result.spriteRejected=spriteRejected.load();result.restoreFailed=restoreFailed.load();
    result.projectionRejected=projectionRejected.load();
    for(unsigned i=0;i<familySeen.size();++i){result.familySeen[i]=familySeen[i].load();result.familyCalls[i]=familyCalls[i].load();
        result.familyDraws[i]=familyDraws[i].load();result.familyViewRejected[i]=familyViewRejected[i].load();
        result.familyProjectionRejected[i]=familyProjectionRejected[i].load();}
    result.manualWrites=manualWrites.load();result.manualQuads=manualQuads.load();result.manualRejected=manualRejected.load();
    result.particleWrites=particleWrites.load();result.particleQuadsObserved=particleQuadsObserved.load();
    result.particleQuads=particleQuads.load();result.particleRejected=particleRejected.load();result.particleAlignedPreserved=particleAlignedPreserved.load();
    result.particlePrefixRejected=particlePrefixRejected.load();result.sourceStateRejected=sourceStateRejected.load();
    result.radialFadeDraws=radialFadeDraws.load();result.radialFogDraws=radialFogDraws.load();result.fogSourceRejected=fogSourceRejected.load();
    result.worldSprite5Writes=worldSprite5Writes;result.worldSprite6Writes=worldSprite6Writes;
    result.worldSpriteQuads=worldSpriteQuads;result.worldSpriteRejected=worldSpriteRejected;
    result.particleNormalInstalled=particleNormalInstalled;result.particleNormalCalls=particleNormalCalls;
    result.particleNormalApplied=particleNormalApplied;result.particleNormalAlignedPreserved=particleNormalAlignedPreserved;
    result.particleNormalRejected=particleNormalRejected;
    result.particleDeferredInstalled=particleDeferredInstalled;result.particleDeferredCalls=particleDeferredCalls;
    result.particleDeferredQuads=particleDeferredQuads;result.particleDeferredAlignedPreserved=particleDeferredAlignedPreserved;
    result.particleDeferredRejected=particleDeferredRejected;
    AcquireSRWLockShared(&particleNormalLock);result.lastParticleNormalApplied=lastParticleNormalApplied;
    result.lastParticleNormalRejected=lastParticleNormalRejected;ReleaseSRWLockShared(&particleNormalLock);return result;
}
}
