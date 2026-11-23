#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_camera_kl.hpp"
#include "native_vehicle_camera_math.hpp"
#include "native_camera_kl_math.hpp"
#include "native_cinematic_kl.hpp"
#include "native_action_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_hands_kl.hpp"
#include "native_body_kl.hpp"
#include "native_projection_tail_kl.hpp"
#include "native_carried_camera_math.hpp"
#include "native_reflection_packets_kl.hpp"
#include "native_reflection_camera_math.hpp"
#include "profile.hpp"
#include "profile_timing.hpp"
#include "crash_diagnostics.hpp"
#include "d3d9_capture.hpp"
#include "vr_bridge.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace NativeCameraKL {
namespace {
using namespace NativeCameraMath;
using Space=NativeCameraKLMath::Space;
using Copy=void*(__thiscall*)(Space*,const void*);
using GpuSetter=void(__thiscall*)(void*,const float*);
using ReflectorRender=void(__thiscall*)(void*,void*,void*,void*,void*,void*,void*);
using CameraWorld=void(__thiscall*)(const void*,Basis*,Vec3*);
Copy originalCopy=nullptr;
GpuSetter originalGpu=nullptr;
ReflectorRender originalReflector=nullptr;
ReflectorRender originalMainRender=nullptr;
CameraWorld originalCameraWorld=nullptr;
ProvideFrame provide=nullptr;
AppliedCallback applied=nullptr;
void* callbackUser=nullptr;
LogCallback logger=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<bool> cinematicVR{false};
std::atomic<std::uint64_t> copies{0},visibilityCopies{0},rejected{0},gpuCalls{0},gpuViews{0};
std::atomic<std::uint64_t> shaderCalls{0};
std::atomic<std::uint64_t> cinematicDrawReused{0},cinematicNativeReads{0};
std::atomic<std::uint64_t> cinematicCameraDrawReused{0},cinematicCameraNativeReads{0};
std::atomic<std::uint64_t> cinematicPanelDrawReused{0},cinematicPanelNativeReads{0};
std::atomic<std::uint64_t> nativeActionTiltViews{0};
std::atomic<std::uint64_t> nativeAnimatedUpViews{0};
std::atomic<std::uint64_t> queuedProjectionRestored{0},queuedProjectionRowsRestored{0};
std::atomic<std::uint64_t> actionHeadingGaps{0},actionHeadingRetained{0},actionHeadingRejected{0};
std::atomic<std::uint64_t> authoredHeadingViews{0},authoredHeadingTurns{0};
std::atomic<std::uint64_t> pickupHeadViews{0};
std::atomic<std::uint64_t> vehicleTransitionViews{0},captureCompromisedFrames{0},unclassifiedPrimaryStatePasses{0};
std::atomic<std::uint64_t> pipSkipped{0},pipCompositeFrames{0};
std::atomic<std::uint64_t> reflectionScopes{0},reflectionCertified{0},reflectionRejected{0},reflectionViews{0},reflectionRestores{0};
std::atomic<std::uint64_t> reflectionLensRejected{0};
std::atomic<std::uint64_t> reflectionEmptyScopes{0};
std::atomic<std::uint64_t> tokyoReflectionCameraPreparations{0};
std::atomic<std::uint64_t> retomotoReflectionCameraPreparations{0};
std::atomic<std::uint64_t> secondaryProducerScopes{0},secondaryCertified{0},secondaryApplied{0},secondaryPopped{0},secondaryRejected{0};
std::atomic<std::uint64_t> secondaryBodyBinds{0},secondaryWeaponBinds{0};
std::atomic<DiagnosticsActive> diagnosticsActive{nullptr};
std::atomic<ProducerBatchObserver> producerBatchObserver{nullptr};
std::atomic<ProducerBytesObserver> producerBytesObserver{nullptr};
std::atomic<CameraProducerFilter> cameraProducerFilter{nullptr};
std::atomic<std::uint64_t> producerDiagnosticSequence{0};
SRWLOCK secondaryLock=SRWLOCK_INIT;
std::array<NativeSecondaryMaskProjectionKL::Certificate,128> secondaryPackets;
std::uint64_t secondaryCursor=0,secondaryEvidenceSequence=0;
SecondaryProjectionEvidence lastSecondaryAccepted,lastSecondaryRejected;
SecondaryProducerEvidence lastSecondaryProducerRejected;std::uint64_t secondaryProducerSequence=0;
std::atomic<std::uintptr_t> lastPipCamera{0},lastPipGeometry{0};
bool pipVerified=false;
std::atomic<std::uint64_t> frameGeneration{1};
std::atomic<DWORD> visibilityThread{0},gpuThread{0},cleanupThread{0},shaderThread{0};
std::atomic<std::uintptr_t> lastSource{0};
std::atomic<float> lastFar{0};
constexpr std::uintptr_t kCopyRva=0x32CF0,kCopyCallerRva=0xBF6ED;
constexpr std::uintptr_t kGpuSetterRva=0xB5220,kGpuCallerRva=0xB5462;
constexpr std::uintptr_t kCameraVtable=0x545A3C,kCameraEntryVtable=0x540F40;
constexpr std::uintptr_t kReflectorRenderRva=0xBE610,kReflectorVtable=0x5410AC;
SRWLOCK reflectionLock=SRWLOCK_INIT;
struct RetainedReflection {NativeReflectionPacketsKL::Certificate certificate;DWORD tick=0;std::uintptr_t queueBegin=0;};
std::array<RetainedReflection,128> reflectionPackets;
std::uint64_t reflectionCursor=0;
ReflectionEvidence lastReflectionAccepted,lastReflectionRejected;
std::uint64_t reflectionEvidenceSequence=0;
ReflectionProducerEvidence lastReflectionProducerAccepted,lastReflectionProducerRejected;
std::uint64_t reflectionProducerSequence=0;
thread_local Vec3 gpuVisibilityNativePosition{};
SRWLOCK visibilityLock=SRWLOCK_INIT;
struct Visibility {bool valid=false;Frame frame{};CameraPose eye{};Vec3 nativePosition{};DWORD tick=0;} visibility;
std::atomic<std::uint64_t> tokyoReflectorKey{0};
std::array<std::atomic<std::uint64_t>,8> retomotoReflectorKeys{};
struct ReflectorCamera {std::uintptr_t source=0;CameraPose pose{};bool retomoto=false;};
thread_local ReflectorCamera reflectorCamera;
struct GpuPair {
    bool valid=false;
    std::uintptr_t source=0,player=0;
    std::uint64_t pairId=0;
    bool cinematic=false,actorCinematic=false,authoredHeading=false,nativePickup=false,anchored=false,stableYaw=false,nativeActionTilt=false,nativeMovementAxes=false,nativeActionUpValid=false,nativeScope=false,nativeCarried=false;
    std::uintptr_t carriedReference=0;
    unsigned carriedReferenceKind=0;
    long recenterAck=0;
    CameraPose centre{};
    NativeCameraKLMath::GpuCameraContext context{};
    bool nativeVehicleTransition=false;
    std::uintptr_t vehicleAction=0;
};
thread_local GpuPair pair;
thread_local std::uint64_t invalidatedPair=0;
thread_local bool frameValid=false;
thread_local bool renderContextOpen=false;
thread_local bool captureCompromised=false,mainCameraApplied=false;
thread_local std::uintptr_t primaryCameraState=0;
thread_local NativeVehicleCamera::Scope vehicleCameraScope;
thread_local std::uint64_t gpuGeneration=0;
thread_local Frame gpuFrame;
// The same native snapshot that authorized the committed camera packet.
// Draw guards must not perform the cinematic pointer walk per mesh. Outside
// this exact GPU scene (including producer/input threads), read native state.
thread_local NativeCinematicKL::State gpuCinematic;
thread_local bool gpuCinematicVR=false;
thread_local NativeCameraKLMath::GpuCameraContext gpuContext;
thread_local CameraPose gpuEye;
thread_local std::uintptr_t gpuState=0;
thread_local std::uintptr_t currentCameraState=0;
thread_local std::uintptr_t diagnosticCameraPacket=0,diagnosticCameraState=0;
thread_local std::uint64_t diagnosticCameraGeneration=0;
thread_local NativeCameraKLMath::FirstPersonHeading firstPersonHeading;
thread_local NativeCameraKLMath::MovementHeading movementHeading;
thread_local float gpuProjection[16]{};
thread_local float gpuNativeProjection[16]{};
struct ReflectionFailure {bool valid=false;std::uint64_t generation=0;std::uintptr_t queue=0,reflector=0;
    NativeReflectionPacketsKL::Packet packet;};
thread_local ReflectionFailure reflectionFailure;
thread_local ReflectionTargetView reflectionTarget;
thread_local CameraPose reflectionBillboardReference;
thread_local bool reflectionBillboardReferenceValid=false;
thread_local std::uint64_t reflectionTargetSerial=0;
struct SecondaryActive {bool valid=false;std::uintptr_t state=0;std::uint64_t generation=0,pair=0;
    NativeSecondaryMaskProjectionKL::Certificate certificate;};
thread_local SecondaryActive secondaryActive;
std::atomic<unsigned> lastEye{0};
std::atomic<std::uint64_t> lastPair{0};
std::atomic<float> lastX{0},lastY{0},lastZ{0};

void Log(const char* message){if(logger)logger(message);}
template<class T> bool Read(std::uintptr_t at,T& value) {
    SIZE_T bytes=0;
    return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),&value,sizeof(value),&bytes)&&bytes==sizeof(value);
}
bool Matching(std::uintptr_t rva,const unsigned char* bytes,std::size_t count) {
    unsigned char actual[64]{};SIZE_T got=0;
    return count<=sizeof(actual)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(image+rva),actual,count,&got)&&
        got==count&&!std::memcmp(actual,bytes,count);
}
void __fastcall MainRenderHook(void* self,void*,void* queue,void* arg2,void* arg3,void* arg4,void* arg5,void* arg6) {
    // Native queue reuse starts on this producer path, after the consumer has
    // released that buffer. Pointer identity alone cannot survive this reuse.
    const auto q=reinterpret_cast<std::uintptr_t>(queue);
    std::uint32_t cursor=0,begin=0;const bool knownCursor=Read(q+0xC,cursor)&&Read(q+0x14,begin);
    std::uint32_t entryVt=0,geometry=0,source=0,sourceVt=0,capacity=0;
    const auto sourceEntry=reinterpret_cast<std::uintptr_t>(self);
    const auto active=diagnosticsActive.load();const auto diagnosticObserver=producerBatchObserver.load();const auto rawObserver=producerBytesObserver.load();
    const bool diagnostic=active&&(diagnosticObserver||rawObserver)&&active();
    const bool secondaryOwner=knownCursor&&Read(q+0x10,capacity)&&cursor>=begin&&cursor<=capacity&&
        Read(sourceEntry,entryVt)&&entryVt==image+kCameraEntryVtable&&Read(sourceEntry+0xF4,geometry)&&
        Read(geometry+0x60,source)&&Read(source,sourceVt)&&sourceVt==image+kCameraVtable;
    AcquireSRWLockExclusive(&secondaryLock);
    for(auto& retained:secondaryPackets)if(retained.queue==q&&(!knownCursor||retained.queueBegin!=begin||retained.end>cursor))retained={};
    ReleaseSRWLockExclusive(&secondaryLock);
    AcquireSRWLockExclusive(&reflectionLock);
    for(auto& retained:reflectionPackets)if(retained.certificate.queue==q&&
        (!knownCursor||retained.queueBegin!=begin||retained.certificate.end>cursor))retained={};
    ReleaseSRWLockExclusive(&reflectionLock);
    // BF600 has one detour owner. PIP used to verify/patch the same entry again
    // after this module had already installed its hook, and always rejected it.
    // Retire reused-queue receipts even when the original producer is skipped.
    if(const auto filter=cameraProducerFilter.load(std::memory_order_acquire))
        if(filter(self,reinterpret_cast<std::uintptr_t>(queue),reinterpret_cast<std::uintptr_t>(arg2),
                  reinterpret_cast<std::uintptr_t>(arg3),reinterpret_cast<std::uintptr_t>(arg4),
                  reinterpret_cast<std::uintptr_t>(arg5),reinterpret_cast<std::uintptr_t>(arg6)))return;
    originalMainRender(self,queue,arg2,arg3,arg4,arg5,arg6);
    if(!secondaryOwner&&!diagnostic)return;
    if(secondaryOwner)++secondaryProducerScopes;
    std::uint32_t after=0;
    static thread_local NativeCameraDiagnosticsKL::ProducerBatchContext trace;
    const auto publishTrace=[&](const void* bytes=nullptr,std::size_t size=0){
        if(diagnosticObserver)diagnosticObserver(trace);if(rawObserver)rawObserver(trace.sequence,bytes,size);
    };
    if(diagnostic){
        trace={};trace.tick=GetTickCount();trace.thread=GetCurrentThreadId();trace.sequence=++producerDiagnosticSequence;
        trace.generation=frameGeneration.load();trace.sourceEntry=sourceEntry;trace.queue=q;trace.queueBegin=begin;trace.before=cursor;trace.capacity=capacity;
        trace.arguments[0]=reinterpret_cast<std::uintptr_t>(arg2);trace.arguments[1]=reinterpret_cast<std::uintptr_t>(arg3);
        trace.arguments[2]=reinterpret_cast<std::uintptr_t>(arg4);trace.arguments[3]=reinterpret_cast<std::uintptr_t>(arg5);trace.arguments[4]=reinterpret_cast<std::uintptr_t>(arg6);
        trace.sourceVerified=secondaryOwner;
        Read(sourceEntry,entryVt);Read(sourceEntry+0xF4,geometry);if(geometry)Read(geometry+0x60,source);if(source)Read(source,sourceVt);
        trace.entryVtable=entryVt;trace.geometry=geometry;trace.sourceCamera=source;trace.cameraVtable=sourceVt;
        if(source)trace.cameraFlagsRead=Read(source+0x44,trace.cameraFlags);
        if(!capacity)Read(q+0x10,capacity);trace.capacity=capacity;
    }
    if(!Read(q+0xC,after)||!knownCursor||after<cursor||after>capacity||after-cursor>0x1000000){
        if(secondaryOwner)++secondaryRejected;
        if(diagnostic){trace.after=after;trace.reason=7;publishTrace();}return;}
    if(diagnostic)trace.after=after;
    if(after==cursor){if(diagnostic){trace.complete=true;publishTrace();}return;}
    static thread_local std::vector<unsigned char> closed;
    closed.resize(after-cursor);SIZE_T done=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(cursor),closed.data(),closed.size(),&done)||done!=closed.size()){
        if(secondaryOwner)++secondaryRejected;
        if(diagnostic){trace.reason=7;publishTrace();}return;}
    const auto read=[&](std::uintptr_t at,void* out,std::size_t size){if(at<cursor||at>after||size>after-at)return false;
        std::memcpy(out,closed.data()+at-cursor,size);return true;};
    if(diagnostic){NativeCameraDiagnosticsKL::TraceBatch(read,trace);publishTrace(closed.data(),closed.size());}
    if(!secondaryOwner)return;
    NativeSecondaryMaskProjectionKL::Batch batch;
    if(!NativeSecondaryMaskProjectionKL::Capture(q,begin,cursor,after,sourceEntry,source,read,batch)){++secondaryRejected;return;}
    secondaryRejected+=batch.rejected;
    AcquireSRWLockExclusive(&secondaryLock);
    if(batch.rejected){const auto& rejected=batch.lastRejected;SecondaryProducerEvidence evidence;evidence.valid=true;
        evidence.reason=batch.lastReason;evidence.tick=GetTickCount();evidence.sequence=++secondaryProducerSequence;
        evidence.queue=q;evidence.sourceEntry=sourceEntry;evidence.sourceCamera=source;
        evidence.cameraPacket=rejected.cameraPacket;evidence.projectionPacket=rejected.projectionPacket;evidence.drawPacket=rejected.drawPacket;evidence.popPacket=rejected.popPacket;
        std::memcpy(evidence.cameraHeader,rejected.camera.data(),24);std::memcpy(evidence.projectionHeader,rejected.projection.data(),24);
        std::memcpy(evidence.drawHeader,rejected.draw.data(),24);std::memcpy(evidence.popHeader,rejected.pop.data(),24);
        std::memcpy(evidence.mainProjection,rejected.camera.data()+0x48,64);std::memcpy(evidence.alternateProjection,rejected.projection.data()+24,64);
        lastSecondaryProducerRejected=evidence;}
    for(unsigned i=0;i<batch.count;++i){secondaryPackets[secondaryCursor++%secondaryPackets.size()]=batch.scopes[i];++secondaryCertified;}
    ReleaseSRWLockExclusive(&secondaryLock);
}
void ReflectionProducerRecord(const NativeReflectionPacketsKL::Trace& trace){
    ReflectionProducerEvidence next;next.valid=true;next.tick=GetTickCount();next.trace=trace;
    AcquireSRWLockExclusive(&reflectionLock);next.sequence=++reflectionProducerSequence;
    if(trace.reason==NativeReflectionPacketsKL::Failure::None)lastReflectionProducerAccepted=next;
    else lastReflectionProducerRejected=next;
    ReleaseSRWLockExclusive(&reflectionLock);
}
ReflectorCamera QualifiedFloorCamera(std::uintptr_t reflector,std::uintptr_t entry){
    auto key=tokyoReflectorKey.load(std::memory_order_relaxed);
    const bool tokyo=std::uint32_t(key)==reflector;
    if(!tokyo)for(const auto& slot:retomotoReflectorKeys){
        const auto registered=slot.load(std::memory_order_relaxed);
        if(std::uint32_t(registered)==reflector){key=registered;break;}
    }
    if(!originalCameraWorld||!key||std::uint32_t(key)!=reflector)return {};
    std::uint32_t reflectorGeometry=0,geometry=0,source=0;
    if(!Read(reflector+0xF4,reflectorGeometry)||reflectorGeometry!=(key>>32)||
       !Read(entry+0xF4,geometry)||!Read(geometry+0x60,source))return {};
    AcquireSRWLockShared(&visibilityLock);const auto last=visibility;ReleaseSRWLockShared(&visibilityLock);
    if(!last.valid||!last.frame.anchorValid||last.frame.nativeCinematic||last.frame.nativeScope||
       last.frame.sourceCamera!=source||GetTickCount()-last.tick>150||!ValidatePose(last.eye))return {};
    auto centre=last.eye;
    const auto& offset=last.frame.eyeOffset;
    centre.position=Subtract(centre.position,Scale(MapTracking(centre.basis,{offset[0],offset[1],offset[2]}),
        last.frame.worldUnitsPerMetre));
    const auto& p=centre.position;
    if(!Finite(p))return {};
    const bool eligible=tokyo?
        p.x>=2850.f&&p.x<=7400.f&&p.y>=225.f&&p.y<=1400.f&&p.z>=-9900.f&&p.z<=-4700.f:
        p.x>=-3300.f&&p.x<=6950.f&&p.y>=19100.f&&p.y<=21400.f&&p.z>=-4100.f&&p.z<=4100.f;
    if(!eligible)return {};
    return {source,centre,!tokyo};
}
void __fastcall CameraWorldHook(const void* camera,void*,Basis* basis,Vec3* position){
    originalCameraWorld(camera,basis,position);
    if(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))==image+0xB9894&&
       reflectorCamera.source==reinterpret_cast<std::uintptr_t>(camera)&&basis&&position){
        *basis=reflectorCamera.pose.basis;
        *position=reflectorCamera.pose.position;
        if(reflectorCamera.retomoto)++retomotoReflectionCameraPreparations;else ++tokyoReflectionCameraPreparations;
    }
}
void __fastcall ReflectorHook(void* self,void*,void* queue,void* cameraEntry,void* arg3,void* view,void* arg5,void* arg6) {
    const auto q=reinterpret_cast<std::uintptr_t>(queue),entry=reinterpret_cast<std::uintptr_t>(cameraEntry);
    const auto reflector=reinterpret_cast<std::uintptr_t>(self),viewId=reinterpret_cast<std::uintptr_t>(view);
    std::uint32_t before=0,queueBegin=0,capacity=0,vt=0,entryVt=0;
    const bool owned=Read(reflector,vt)&&vt==image+kReflectorVtable&&
        Read(entry,entryVt)&&entryVt==image+kCameraEntryVtable&&
        Read(q+0xC,before)&&Read(q+0x14,queueBegin)&&Read(q+0x10,capacity)&&
        before>=queueBegin&&before<=capacity;
    const auto previous=reflectorCamera;
    reflectorCamera=owned?QualifiedFloorCamera(reflector,entry):ReflectorCamera{};
    originalReflector(self,queue,cameraEntry,arg3,view,arg5,arg6);
    reflectorCamera=previous;
    if(!owned)return;
    ++reflectionScopes;
    std::uint32_t after=0;
    if(!Read(q+0xC,after)||after<before||after>capacity){++reflectionRejected;return;}
    if(after==before){++reflectionEmptyScopes;return;}
    NativeReflectionPacketsKL::Certificate certificate;
    // The native producer has closed this appended range. Copy it once;
    // parsing each command must not add per-draw kernel transitions.
    static thread_local std::vector<unsigned char> closedQueue;
    const auto span=static_cast<std::size_t>(after-before);SIZE_T copied=0;
    bool captured=span<=0x1000000;
    if(captured){closedQueue.resize(span);captured=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(before),
        closedQueue.data(),span,&copied)&&copied==span;}
    const auto reader=[&](std::uintptr_t at,void* data,std::size_t size){
        if(at<before||at-before>span||size>span-(at-before))return false;
        std::memcpy(data,closedQueue.data()+at-before,size);return true;};
    NativeReflectionPacketsKL::Trace trace;
    const bool valid=captured&&NativeReflectionPacketsKL::Capture(q,before,after,entry,reflector,viewId,reader,certificate,&trace);
    if(!captured){trace.reason=NativeReflectionPacketsKL::Failure::QueueRead;trace.at=before;
        trace.observed.queue=q;trace.observed.begin=before;trace.observed.end=after;
        trace.observed.sourceEntry=entry;trace.observed.reflector=reflector;trace.observed.view=viewId;}
    ReflectionProducerRecord(trace);
    if(valid)++reflectionCertified;
    else {++reflectionRejected;certificate.queue=q;certificate.begin=before;certificate.end=after;}
    // Invalid certified producer ranges are also retained: an unknown native
    // reflector must not fall through to the broad local-camera VR cache.
    AcquireSRWLockExclusive(&reflectionLock);
    reflectionPackets[reflectionCursor++%reflectionPackets.size()]={certificate,GetTickCount(),queueBegin};
    ReleaseSRWLockExclusive(&reflectionLock);
}
// 0 = ordinary packet, 1 = certified, -1 = unknown/stale reflector packet.
int ReflectionPacket(std::uintptr_t packet,NativeReflectionPacketsKL::Certificate& certificate,unsigned& phase) {
    std::array<unsigned char,NativeReflectionPacketsKL::PacketBytes> bytes{};bool readBytes=false;
    const DWORD now=GetTickCount();bool unknown=false,identified=false;
    AcquireSRWLockShared(&reflectionLock);
    for(const auto& retained:reflectionPackets){const auto& candidate=retained.certificate;
        if(packet<candidate.begin||packet>=candidate.end)continue;
        if(!readBytes){SIZE_T got=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(packet),bytes.data(),bytes.size(),&got)||got!=bytes.size())break;
            readBytes=true;
        }
        std::uint32_t owner=0;std::memcpy(&owner,bytes.data()+8,4);
        if(owner!=candidate.queue)continue;
        unknown=true;
        if(candidate.valid&&!identified)for(unsigned n=0;n<4;++n)if(candidate.packets[n].address==packet){
            certificate=candidate;phase=n;identified=true;break;}
        if(now-retained.tick>2000)continue;
        const int matched=NativeReflectionPacketsKL::Match(candidate,packet,bytes.data(),bytes.size());
        if(matched>=0){certificate=candidate;phase=static_cast<unsigned>(matched);
            ReleaseSRWLockShared(&reflectionLock);return 1;}
    }
    ReleaseSRWLockShared(&reflectionLock);return unknown?-1:0;
}
void ReflectionEvidenceRecord(const NativeReflectionPacketsKL::Certificate& certificate,unsigned phase,unsigned reason,
    std::uintptr_t source,const CameraPose& eyeView,const float* resultProjection){
    ReflectionEvidence next;next.valid=true;next.reason=reason;next.phase=phase;next.eye=gpuFrame.eye;
    next.tick=GetTickCount();next.pair=gpuFrame.pairId;next.packet=certificate.packets[phase].address;
    next.queue=certificate.queue;next.sourceEntry=certificate.sourceEntry;next.reflector=certificate.reflector;
    next.sourceCamera=source;next.viewId=certificate.view;
    std::memcpy(next.originalView,certificate.packets[0].bytes.data()+0x18,sizeof(next.originalView));
    std::memcpy(next.reflectedView,certificate.packets[1].bytes.data()+0x18,sizeof(next.reflectedView));
    std::memcpy(next.eyeView,&eyeView,sizeof(next.eyeView));
    std::memcpy(next.originalProjection,certificate.packets[0].bytes.data()+0x48,sizeof(next.originalProjection));
    std::memcpy(next.obliqueProjection,certificate.packets[1].bytes.data()+0x48,sizeof(next.obliqueProjection));
    std::memcpy(next.finalNativeProjection,certificate.packets[3].bytes.data()+0x48,sizeof(next.finalNativeProjection));
    std::memcpy(next.vrProjection,gpuProjection,sizeof(next.vrProjection));
    if(resultProjection)std::memcpy(next.resultProjection,resultProjection,sizeof(next.resultProjection));
    AcquireSRWLockExclusive(&reflectionLock);next.sequence=++reflectionEvidenceSequence;
    if(reason)lastReflectionRejected=next;else lastReflectionAccepted=next;
    ReleaseSRWLockExclusive(&reflectionLock);
}
std::uintptr_t LocalCamera(std::uintptr_t* localPlayer=nullptr) {
    std::uint32_t data=0,player=0,camera=0;
    if(localPlayer)*localPlayer=0;
    if(!Read(image+0x68760C,data)||!Read(data+0xA2C,player)||!Read(player+0x9A0,camera))return 0;
    if(localPlayer)*localPlayer=player;return camera;
}
NativeCinematicKL::State CinematicState() {
    return NativeCinematicKL::Snapshot(image,[](std::uintptr_t at,void* output,std::size_t size) {
        SIZE_T bytes=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&bytes)&&bytes==size;
    });
}
bool CommittedCinematicContext() {
    return renderContextOpen&&frameValid&&gpuState&&gpuState==currentCameraState&&
        gpuGeneration==frameGeneration.load()&&GetCurrentThreadId()==gpuThread.load()&&
        pair.valid&&pair.source==gpuFrame.sourceCamera&&pair.player==gpuFrame.anchorPlayer&&
        pair.pairId==gpuFrame.pairId&&gpuCinematicVR==cinematicVR.load();
}
bool PauseMenu() {
    return NativeCinematicKL::PauseMenu(image,[](std::uintptr_t at,void* output,std::size_t count){
        SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,count,&got)&&got==count;
    });
}
const NativePipKL::State& PipState() {
    static thread_local std::uint64_t observedGeneration=UINT64_MAX;
    static thread_local NativePipKL::State snapshot;
    const auto generation=frameGeneration.load();
    if(observedGeneration!=generation) {
        observedGeneration=generation;
        snapshot=pipVerified?NativePipKL::Snapshot(image,[](std::uintptr_t at,void* output,std::size_t size) {
            SIZE_T bytes=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&bytes)&&bytes==size;
        }):NativePipKL::State{};
    }
    return snapshot;
}
bool ActionToken(std::uintptr_t player,std::uintptr_t& token) {
    std::uint32_t dispatcher=0,action=0;
    // Detection only, never authorization: a change causes the provider to
    // repeat its complete ownership checks before another eye can be used.
    if(!player||!Read(player+0x194,dispatcher)||!dispatcher||!Read(dispatcher+4,action))return false;
    token=action;return true;
}
bool MovementToken(std::uintptr_t player,std::uintptr_t& token) {
    std::uint32_t movement=0;
    if(!player||!Read(player+0x67C,movement))return false;
    token=movement;return true;
}
void RetireEye(bool preserveStableHeading=false) {
    reflectionTarget={};
    secondaryActive={};
    renderContextOpen=false;
    NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();frameValid=false;gpuState=0;
    if(pair.valid)invalidatedPair=pair.pairId;pair.valid=false;
    NativeCameraKLMath::RetireFirstPersonHeading(firstPersonHeading,preserveStableHeading);movementHeading.Reset();
    AcquireSRWLockExclusive(&visibilityLock);visibility.valid=false;ReleaseSRWLockExclusive(&visibilityLock);
}
void ProjectionTail(std::uintptr_t state) {
    if(!installed.load()||!frameValid||!gpuFrame.nativeCinematic||!state||state!=gpuState||state!=currentCameraState||
       gpuGeneration!=frameGeneration.load()||GetCurrentThreadId()!=gpuThread.load()||
       !pair.valid||pair.source!=gpuFrame.sourceCamera||pair.pairId!=gpuFrame.pairId)return;
    auto* bytes=reinterpret_cast<unsigned char*>(state);CameraPose actual,expected;float current[16]{};
    std::memcpy(&actual,bytes+0xBC,sizeof(actual));std::memcpy(current,bytes+0x3F8,sizeof(current));
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,expected)||
       !NativeCameraKLMath::RestoreQueuedProjection(true,actual,expected,gpuNativeProjection,gpuProjection,current))return;
    std::memcpy(bytes+0x3F8,current,sizeof(current));++queuedProjectionRestored;
}
void SecondaryEvidence(unsigned reason,std::uintptr_t state,std::uintptr_t packet,const NativeSecondaryMaskProjectionKL::Certificate& certificate,const float* native){
    SecondaryProjectionEvidence next;next.valid=true;next.reason=reason;next.eye=gpuFrame.eye;next.tick=GetTickCount();next.pair=gpuFrame.pairId;
    next.state=state;next.packet=packet;next.queue=certificate.queue;next.sourceEntry=certificate.sourceEntry;next.sourceCamera=certificate.sourceCamera;
    if(native)std::memcpy(next.nativeProjection,native,64);std::memcpy(next.vrProjection,gpuProjection,64);
    AcquireSRWLockExclusive(&secondaryLock);next.sequence=++secondaryEvidenceSequence;
    if(reason)lastSecondaryRejected=next;else lastSecondaryAccepted=next;ReleaseSRWLockExclusive(&secondaryLock);
}
void SecondaryProjectionPacket(std::uintptr_t state,std::uintptr_t packet,unsigned phase){
    const auto previous=secondaryActive;
    if(phase==1){
        secondaryActive={};if(!previous.valid)return;
        if(previous.state!=state||previous.generation!=frameGeneration.load()||previous.pair!=gpuFrame.pairId||packet!=previous.certificate.popPacket){++secondaryRejected;return;}
    }else secondaryActive={};
    NativeSecondaryMaskProjectionKL::Certificate certificate;
    if(phase==1)certificate=previous.certificate;
    else {AcquireSRWLockShared(&secondaryLock);
        for(const auto& known:secondaryPackets)if(known.valid&&known.projectionPacket==packet){certificate=known;break;}
        ReleaseSRWLockShared(&secondaryLock);}
    if(!certificate.valid)return;
    const auto read=[](std::uintptr_t at,void* out,std::size_t size){SIZE_T copied=0;return at>=0x10000u&&size&&at<=0xffffffffu-size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&copied)&&copied==size;};
    if(!installed.load()||!frameValid||gpuFrame.nativeCinematic||!gpuFrame.anchorValid||!state||state!=gpuState||state!=currentCameraState||
       gpuGeneration!=frameGeneration.load()||GetCurrentThreadId()!=gpuThread.load()||!pair.valid||
       pair.source!=gpuFrame.sourceCamera||pair.pairId!=gpuFrame.pairId||certificate.sourceCamera!=gpuFrame.sourceCamera){
        ++secondaryRejected;SecondaryEvidence(2,state,packet,certificate,nullptr);return;}
    if(!NativeSecondaryMaskProjectionKL::Match(certificate,packet,read,phase==1)){++secondaryRejected;SecondaryEvidence(3,state,packet,certificate,nullptr);return;}
    std::uint32_t vt=0,geometry=0,source=0;
    if(!Read(certificate.sourceEntry,vt)||vt!=image+kCameraEntryVtable||!Read(certificate.sourceEntry+0xF4,geometry)||
       !Read(geometry+0x60,source)||source!=gpuFrame.sourceCamera||LocalCamera()!=source){++secondaryRejected;SecondaryEvidence(5,state,packet,certificate,nullptr);return;}
    CameraPose actual,expected;float projection[16]{},native[16]{};
    auto* bytes=reinterpret_cast<unsigned char*>(state);std::memcpy(&actual,bytes+0xBC,sizeof(actual));std::memcpy(projection,bytes+0x3F8,64);
    std::memcpy(native,certificate.projection.data()+24,64);
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,expected)||!NativeCameraKLMath::SameView(actual,expected)||
       (phase==0?std::memcmp(projection,native,64):!NativeCameraKLMath::SameProjection(projection,gpuProjection))){
        ++secondaryRejected;SecondaryEvidence(phase==0?4:6,state,packet,certificate,projection);return;}
    // The native alternate-FOV viewmodel layer and ordinary colour geometry
    // must have one physical headset lens. Its producer-owned push already
    // saved this main VR P; the matching native pop restores it untouched.
    if(phase==0){std::memcpy(bytes+0x3F8,gpuProjection,64);secondaryActive={true,state,frameGeneration.load(),gpuFrame.pairId,certificate};
        ++secondaryApplied;SecondaryEvidence(0,state,packet,certificate,native);}
    else ++secondaryPopped;
}

// Visibility runs on the producer thread. It never obtains tracking, chooses
// an eye, updates reference/yaw, publishes metadata or edits source geometry.
void* __fastcall CopyHook(Space* space,void*,const void* camera) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    void* result=originalCopy(space,camera);++copies;
    ProfileTiming::Scope profile(ProfileTiming::CameraVisibility);
    const auto source=reinterpret_cast<std::uintptr_t>(camera);
    if((caller==image+0xBE74D||caller==image+0xB973D)&&reflectorCamera.source&&source==reflectorCamera.source&&
       space&&space->sourceCamera==source&&!space->orthographic){
        space->basis=reflectorCamera.pose.basis;
        space->position=space->positionCache=reflectorCamera.pose.position;
        return result;
    }
    if(caller!=image+kCopyCallerRva||!space||!camera||
       !NativeCameraKLMath::VisibilityCamera(source,LocalCamera(),CinematicCamera(),CinematicPanelActive())||
       space->sourceCamera!=source||space->orthographic||space->farUnits<10000.f)return result;
    std::uint32_t vt=0;if(!Read(source,vt)||vt!=image+kCameraVtable)return result;
    visibilityThread.store(GetCurrentThreadId());
    AcquireSRWLockShared(&visibilityLock);const Visibility last=visibility;ReleaseSRWLockShared(&visibilityLock);
    Space candidate=*space;
    float fov[4]={3.f,3.f,3.f,3.f};float nearUnits=std::max(.1f,candidate.nearUnits);
    if(last.valid&&last.frame.sourceCamera==source&&GetTickCount()-last.tick<250) {
        CameraPose culling;
        if(!NativeCameraKLMath::VisibilityPose({candidate.basis,candidate.position},last.eye,
                last.frame.delta,last.frame.nativeCinematic,culling))return result;
        if(last.frame.anchorValid&&!last.frame.nativeCinematic&&!last.frame.actorCinematic&&!last.frame.nativeScope) {
            const auto& offset=last.frame.eyeOffset;
            culling=NativeCameraKLMath::GameplayVisibilityCentre(culling,{offset[0],offset[1],offset[2]},
                last.frame.worldUnitsPerMetre,Subtract(candidate.position,last.nativePosition));
            nearUnits=std::min(nearUnits,std::max(.1f,last.frame.nearUnits));
        }
        candidate.basis=culling.basis;candidate.position=culling.position;
        std::memcpy(fov,last.frame.cullingFov,sizeof(fov));
    }
    StereoRenderMath::Mat4 identity;StereoRenderMath::Identity(identity);
    if(NativeCameraKLMath::Apply(candidate,&identity[0][0],fov,fov,nearUnits)) {
        *space=candidate;++visibilityCopies;
    }
    return result;
}

// The decoder has copied kind1's queued inverse view into state+BC and P into
// state+3F8 immediately before calling this setter. All following entity WV
// binds multiply by this exact state (RVA 0x71770), on the GPU consumer thread.
void __fastcall GpuHook(void* state,void*,const float* vector) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(caller==image+kGpuCallerRva){renderContextOpen=false;if(state&&vector)D3D9Capture::ObserveNativeCamera();}
    originalGpu(state,vector);
    if(caller!=image+kGpuCallerRva||!state||!vector||!provide)return;
    secondaryActive={};
    reflectionTarget={};
    currentCameraState=reinterpret_cast<std::uintptr_t>(state);
    diagnosticCameraPacket=reinterpret_cast<std::uintptr_t>(vector)-0xA0;
    diagnosticCameraState=currentCameraState;diagnosticCameraGeneration=frameGeneration.load();
    ++gpuCalls;gpuThread.store(GetCurrentThreadId());
    ProfileTiming::Scope profile(ProfileTiming::CameraParameter);
    static thread_local std::uint64_t breadcrumbFrame=UINT64_MAX;
    const auto crumbGeneration=frameGeneration.load();
    if(breadcrumbFrame!=crumbGeneration){breadcrumbFrame=crumbGeneration;CrashDiagnostics::Breadcrumb("native_gpu_camera",crumbGeneration);}
    const auto cinematic=CinematicState();
    const auto& pip=PipState();
    const bool composite=NativeCinematicKL::CompositePipPanel(cinematic,pip);
    std::uintptr_t panelPlayer=0;LocalCamera(&panelPlayer);
    if(PauseMenu()||NativeCinematicKL::FullScenePanel(cinematic,cinematicVR.load())||composite||
       NativeCinematicKL::ActorScenePanel(NativeActionKL::Current(),panelPlayer,cinematicVR.load())) {
        // A scripted camera can reuse the local player's ZCAMERA. Leave its
        // queued view/projection, actor pose and visibility entirely native.
        RetireEye();
        if(!composite)return;
        static thread_local std::uint64_t countedComposite=UINT64_MAX;
        if(countedComposite!=crumbGeneration){countedComposite=crumbGeneration;++pipCompositeFrames;}
    }
    const auto packet=reinterpret_cast<std::uintptr_t>(vector)-0xA0;
    NativeReflectionPacketsKL::Certificate reflection;unsigned reflectionPhase=0;
    const int reflectionKind=ReflectionPacket(packet,reflection,reflectionPhase);
    if(reflectionKind<0) {
        // Unknown nested/changed scopes stay entirely native; never feed
        // them to the broad cached local-camera VR eye.
        gpuState=0;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();
        if(reflection.valid&&reflectionPhase==1)
            reflectionFailure={true,frameGeneration.load(),reflection.queue,reflection.reflector,reflection.packets[1]};
        if(reflectionKind<0)++reflectionRejected;
        return;
    }
    std::uint32_t kind=0,words=0,entry=0,entryVt=0,geometry=0,source=0,sourceVt=0;
    std::uint32_t owner=0,begin=0,end=0,viewId=0;
    std::uintptr_t localPlayer=0;
    const auto localCamera=LocalCamera(&localPlayer);
    const bool sourceEntryKnown=reflectionKind>0?
        (entry=static_cast<std::uint32_t>(reflection.sourceEntry),true):Read(packet+4,entry);
    if(!Read(packet,kind)||kind!=1||!Read(packet+0x14,words)||words!=0x26||
       !Read(packet+8,owner)||!Read(owner+0x14,begin)||!Read(owner+0xC,end)||
       packet<begin||end<packet||end-packet<0xB0||
       !Read(packet+0xC,viewId)||!viewId||
       !sourceEntryKnown||!Read(entry,entryVt)||entryVt!=image+kCameraEntryVtable||
       !Read(entry+0xF4,geometry)||!Read(geometry+0x60,source)||!Read(source,sourceVt)||
       sourceVt!=image+kCameraVtable) {
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
    }
    if(NativeCinematicKL::PipCameraPass(pip,source,geometry)) {
        // This camera belongs to the verified native auxiliary PIP owner.
        // Keep its projection and scene native; a later main-camera packet
        // can still restore this generation's cached VR eye normally.
        ++pipSkipped;lastPipCamera=source;lastPipGeometry=geometry;
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
    }
    if(composite) {
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
    }
    if(source!=localCamera&&source!=cinematic.camera) {
        // State identity alone cannot distinguish a fullscreen replacement
        // from an offscreen auxiliary target. Count this boundary without
        // turning every unknown reflection into a dropped gameplay image.
        if(gpuGeneration==frameGeneration.load()&&mainCameraApplied&&primaryCameraState==reinterpret_cast<std::uintptr_t>(state))
            ++unclassifiedPrimaryStatePasses;
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
    }
    NativeCameraKLMath::GpuCameraContext context{source,localPlayer,0,cinematic.full!=0,false};
    if(!context.fullCinematic)context.actionKnown=ActionToken(localPlayer,context.action);
    if(!context.fullCinematic)context.movementKnown=MovementToken(localPlayer,context.movement);
    const auto generation=frameGeneration.load();
    if(gpuGeneration!=generation){gpuGeneration=generation;frameValid=false;captureCompromised=false;mainCameraApplied=false;}
    struct MainPass {
        bool owned=false,completed=false;
        ~MainPass(){if(owned&&!completed){if(!captureCompromised)++captureCompromisedFrames;captureCompromised=true;}}
    } mainPass{reflectionKind==0,false};
    if(frameValid&&!NativeCameraKLMath::SameCameraContext(gpuContext,context)) {
        frameValid=false;
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();
    }
    if(!frameValid) {
        Frame requested{};requested.sourceCamera=source;
        if(!provide(requested,callbackUser)||!requested.valid||requested.anchorCamera!=source) {
            // An unavailable anatomical anchor closes this eye, not the
            // established yaw reference. Never retain through UI/cinema,
            // tracking loss, recenter or a different player/camera/state.
            const auto action=NativeActionKL::Current();const auto head=VRBridge::HeadPose();
            const NativeCameraKLMath::HeadingRetentionContext retain{localPlayer,source,reinterpret_cast<std::uintptr_t>(state),
                head.recenterAck,GetTickCount(),head.tick,NativeCinematicKL::PhysicalActorCamera(action,localPlayer),
                PauseMenu()||CinematicPanelActive()||CinematicState().PreserveNative(),VRBridge::Ready(),head.valid,head.mounted};
            const bool preserveHeading=requested.anchorUnavailable&&NativeCameraKLMath::RetainPhysicalHeading(firstPersonHeading,retain);
            if(requested.anchorUnavailable){++actionHeadingGaps;if(preserveHeading)++actionHeadingRetained;else ++actionHeadingRejected;}
            RetireEye(preserveHeading);return;
        }
        requested.sourceCamera=source;
        const auto vehicleState=NativeActionKL::Current();
        const bool vehicleScope=vehicleCameraScope.Select(vehicleState);
        if(requested.nativeVehicleTransition&&(!vehicleScope||!requested.vehicleAction||
           requested.nativeCinematic||requested.nativeActionTilt||requested.nativeCarried||!requested.anchorValid||!requested.animatedEye||
           requested.anchorPlayer!=localPlayer||vehicleState.player!=localPlayer||vehicleState.action!=requested.vehicleAction||
           !context.actionKnown||context.action!=requested.vehicleAction||!context.movementKnown||context.movement!=vehicleState.movement||
           !ValidatePose(requested.vehicleBase,.05f))){++rejected;RetireEye();return;}
        if(requested.nativeCinematic||!requested.anchorValid||!requested.stableFirstPersonYaw)firstPersonHeading={};
        std::uint32_t authoredVtable=0,authoredMovementVtable=0;
        requested.authoredHeading=requested.authoredHeading&&!requested.nativeCinematic&&!requested.nativeActionTilt&&
            !requested.nativeCarried&&requested.anchorValid&&requested.stableFirstPersonYaw&&requested.anchorPlayer==localPlayer&&
            cinematic.valid&&!cinematic.full&&cinematic.mini&&context.actionKnown&&context.action&&
            context.action==requested.authoredHeadingAction&&Read(context.action,authoredVtable)&&
            (authoredVtable==image+0x5667F4||authoredVtable==image+0x566834)&&
            context.movementKnown&&context.movement&&Read(context.movement,authoredMovementVtable)&&
            (authoredMovementVtable==image+0x566A44||authoredMovementVtable==image+0x566AEC);
        std::uint32_t pickupVtable=0,pickupMovement=0;
        requested.nativePickup=requested.nativePickup&&!requested.nativeCinematic&&!requested.nativeActionTilt&&!requested.nativeCarried&&
            requested.anchorValid&&requested.animatedEye&&requested.stableFirstPersonYaw&&requested.nativeActionUpValid&&
            cinematic.valid&&!cinematic.full&&
            requested.anchorPlayer==localPlayer&&context.actionKnown&&context.action==requested.pickupAction&&context.action&&
            Read(context.action,pickupVtable)&&pickupVtable==image+0x566628&&
            context.movementKnown&&context.movement&&Read(context.movement,pickupMovement)&&
            (pickupMovement==image+0x566A44||pickupMovement==image+0x566AEC);
        CameraPose inverseView,queuedWorld,rawWorld;
        std::uint32_t gpuIndex=0;
        // The producer's CameraSpace has conservative visibility from the
        // preceding GPU eye. Its queued view is therefore not an untracked
        // centre and must never be used as the next frame's camera base.
        // Entry world caches remain native and are double-buffered with the
        // engine's GPU index, independently of CPU CameraSpace visibility.
        const bool readView=reflectionKind>0?
            (std::memcpy(&inverseView,reflection.packets[0].bytes.data()+0x18,sizeof(inverseView)),true):Read(packet+0x18,inverseView);
        if(!readView||!NativeCameraKLMath::WorldFromView(inverseView,queuedWorld)||
           !Read(image+0x7678A4,gpuIndex)||gpuIndex>1||
           !Read(entry+0x10+gpuIndex*0x30,rawWorld)||!ValidatePose(rawWorld)) {++rejected;return;}
        const bool reuse=pair.valid&&pair.source==source&&pair.player==requested.anchorPlayer&&
            pair.cinematic==requested.nativeCinematic&&pair.pairId==requested.pairId&&
            pair.actorCinematic==requested.actorCinematic&&
            pair.authoredHeading==requested.authoredHeading&&
            pair.nativePickup==requested.nativePickup&&
            pair.anchored==requested.anchorValid&&pair.stableYaw==requested.stableFirstPersonYaw&&
            pair.nativeActionTilt==requested.nativeActionTilt&&
            pair.nativeMovementAxes==requested.nativeMovementAxes&&
            pair.nativeActionUpValid==requested.nativeActionUpValid&&
            pair.nativeScope==requested.nativeScope&&
            pair.nativeCarried==requested.nativeCarried&&pair.carriedReference==requested.carriedReference&&pair.carriedReferenceKind==requested.carriedReferenceKind&&
            pair.nativeVehicleTransition==requested.nativeVehicleTransition&&pair.vehicleAction==requested.vehicleAction&&
            pair.recenterAck==requested.recenterAck&&NativeCameraKLMath::SameCameraContext(pair.context,context);
        requested.pairDiscontinuity=NativeCameraKLMath::RequiresFreshPair(requested.eye,
            pair.valid?pair.pairId:invalidatedPair,requested.pairId,reuse);
        CameraPose base;
        if(!NativeCarriedCamera::EyeBase(requested.nativeCarried,reuse,requested.carriedBase,pair.centre,rawWorld,base)){++rejected;return;}
        if(requested.nativeVehicleTransition) {
            // The head turns and translates through the native door clip.
            // Neither a frozen gameplay yaw nor the seated actor-root latch
            // describes that current anatomical view.
            if(!NativeCameraKLMath::FirstPersonBase(firstPersonHeading,requested.anchorPlayer,source,
                requested.stableFirstPersonYaw,rawWorld,requested.vehicleBase.position,base,
                requested.recenterAck,true,&requested.vehicleBase.basis)){++rejected;return;}
        } else if(requested.nativeCarried) {
            // One tracking/projection pair still has TWO current native seat
            // poses. A moving carrier cannot reuse the first eye's centre.
            if(!requested.anchorValid||!requested.carriedReference||!ValidatePose(requested.carriedBase,.05f)){++rejected;return;}
            base=requested.carriedBase;
        } else if((!reuse||requested.animatedEye)&&requested.anchorValid) {
            const Vec3 actionUp{requested.nativeActionUp[0],requested.nativeActionUp[1],requested.nativeActionUp[2]};
            const Vec3 actionForward{requested.nativeActionForward[0],requested.nativeActionForward[1],requested.nativeActionForward[2]};
            const Basis actionBasis{actionForward,actionUp,Cross(actionUp,actionForward)};
            const NativeCameraKLMath::AuthoredHeadingInput authored{requested.authoredHeadingAction,requested.pairId,requested.authoredActorForward};
            const NativeCameraKLMath::PickupHeadInput pickup{{requested.pickupAction,requested.pairId,requested.pickupActorForward},actionBasis};
            const auto previousHeading=firstPersonHeading;
            if(!NativeCameraKLMath::FirstPersonBase(firstPersonHeading,requested.anchorPlayer,source,
                requested.stableFirstPersonYaw,rawWorld,
                {requested.anchorPosition[0],requested.anchorPosition[1],requested.anchorPosition[2]},base,
                requested.recenterAck,requested.nativeActionTilt,requested.nativeActionUpValid?&actionBasis:nullptr,
                requested.authoredHeading?&authored:nullptr,requested.nativePickup?&pickup:nullptr)){++rejected;return;}
            if(requested.authoredHeading){
                ++authoredHeadingViews;
                if(previousHeading.valid&&previousHeading.recenterAck==requested.recenterAck&&
                   previousHeading.authored.valid&&firstPersonHeading.authored.valid&&
                   previousHeading.authored.action==firstPersonHeading.authored.action&&
                   Dot(Subtract(previousHeading.basis.forward,firstPersonHeading.basis.forward),
                       Subtract(previousHeading.basis.forward,firstPersonHeading.basis.forward))>1e-10f)++authoredHeadingTurns;
            }
        }
        std::uint32_t exitMovementVtable=0;
        const bool uprightExit=requested.anchorPlayer==localPlayer&&context.movementKnown&&context.movement&&
            Read(context.movement,exitMovementVtable)&&(exitMovementVtable==image+0x566A44||exitMovementVtable==image+0x566AEC||exitMovementVtable==image+0x566EAC);
        if(uprightExit&&!requested.nativeActionTilt&&!requested.nativeCarried&&!requested.nativeVehicleTransition&&requested.anchorValid&&
           requested.stableFirstPersonYaw&&firstPersonHeading.traversalHeadingPending&&requested.actorForwardValid&&
           !NativeCameraKLMath::ResumeActorHeading(firstPersonHeading,requested.actorForward,requested.delta,base)){++rejected;return;}
        CameraPose eye;
        if(!NativeCameraKLMath::ApplyPose(base,requested.delta,eye)) {++rejected;return;}
        Basis inputBasis;Vec3 movementForward;
        if(requested.nativeCinematic)movementHeading.Reset();
        requested.movementYawValid=!requested.nativeCinematic&&!requested.nativeCarried&&
            movementHeading.Select(requested.anchorPlayer,source,requested.recenterAck,
                requested.anchorValid,requested.stableFirstPersonYaw,eye,movementForward)&&
            NativeCoverKL::InputBasis(requested.anchorPlayer,inputBasis)&&
            NativeCameraKLMath::MovementYaw(inputBasis,movementForward,requested.movementYawRadians);
        float rawP[16]{};
        if(reflectionKind>0)std::memcpy(rawP,reflection.packets[0].bytes.data()+0x48,sizeof(rawP));
        else if(!Read(packet+0x48,rawP)) {++rejected;return;}
        std::memcpy(gpuNativeProjection,rawP,sizeof(rawP));
        if(requested.nativeCinematic&&!NativeCameraKLMath::CinematicNearPlane(rawP,requested.nearUnits,requested.nearUnits)) {++rejected;return;}
        const bool infinite=std::fabs(rawP[10]-1.f)<1e-6f;
        float farUnits=40000.f;
        if(!infinite&&rawP[10]>1.f)farUnits=-rawP[14]/(rawP[10]-1.f);
        if(!std::isfinite(farUnits)||farUnits<=requested.nearUnits)farUnits=40000.f;
        if(!NativeCameraKLMath::SelectCameraProjection(requested.nativeScope,rawP,requested.fov,
            requested.nearUnits,farUnits,infinite,gpuProjection)){++rejected;return;}
        if(requested.nativeScope) {
            if(rawP[0]>.01f&&rawP[5]>.01f) {
                const float horizontal=(1.f+std::fabs(rawP[8]))/rawP[0]*1.1f;
                const float vertical=(1.f+std::fabs(rawP[9]))/rawP[5]*1.1f;
                if(horizontal>.01f&&horizontal<10.f&&vertical>.01f&&vertical<10.f)
                    {requested.cullingFov[0]=requested.cullingFov[1]=horizontal;requested.cullingFov[2]=requested.cullingFov[3]=vertical;}
            }
        }
        pair={true,source,requested.anchorPlayer,requested.pairId,requested.nativeCinematic,requested.actorCinematic,requested.authoredHeading,requested.nativePickup,
              requested.anchorValid,requested.stableFirstPersonYaw,requested.nativeActionTilt,requested.nativeMovementAxes,requested.nativeActionUpValid,requested.nativeScope,requested.nativeCarried,requested.carriedReference,requested.carriedReferenceKind,requested.recenterAck,base,context};
        pair.nativeVehicleTransition=requested.nativeVehicleTransition;pair.vehicleAction=requested.vehicleAction;
        gpuFrame=requested;gpuEye=eye;gpuVisibilityNativePosition=rawWorld.position;gpuContext=context;frameValid=true;lastFar=farUnits;
    }
    if(gpuFrame.sourceCamera!=source) {++rejected;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;}
    CameraPose view;
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,view)) {++rejected;return;}
    if(reflectionKind>0&&(reflectionPhase==1||reflectionPhase==3)){
        if(reflectionPhase==3&&reflectionFailure.valid&&reflectionFailure.generation==frameGeneration.load()&&
           reflectionFailure.queue==reflection.queue&&reflectionFailure.reflector==reflection.reflector&&
           reflectionFailure.packet.address==reflection.packets[1].address&&
           !std::memcmp(reflectionFailure.packet.bytes.data(),reflection.packets[1].bytes.data(),NativeReflectionPacketsKL::PacketBytes)){
            // Keep native sampling when its matching RT was left native.
            // A successful main restore between the two phases does not
            // authorize mixing a rejected RT with tracked reflection UVs.
            gpuState=0;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
        }
        CameraPose originalView,reflectedView,reflectedEye;float nativeBaseP[16]{},nativeClippedP[16]{},reflectedP[16]{};
        std::memcpy(&originalView,reflection.packets[0].bytes.data()+0x18,sizeof(originalView));
        std::memcpy(&reflectedView,reflection.packets[1].bytes.data()+0x18,sizeof(reflectedView));
        std::memcpy(nativeBaseP,reflection.packets[0].bytes.data()+0x48,sizeof(nativeBaseP));
        std::memcpy(nativeClippedP,reflection.packets[1].bytes.data()+0x48,sizeof(nativeClippedP));
        bool reflectedReady=false,lensRejected=false;
        if(reflectionPhase==1){
            for(unsigned n=0;n<16;++n)if(n%4!=2&&std::fabs(nativeBaseP[n]-nativeClippedP[n])>1.e-5f){++reflectionLensRejected;lensRejected=true;break;}
            reflectedReady=NativeReflectionCameraMath::Build(originalView,reflectedView,nativeBaseP,nativeClippedP,
                view,gpuProjection,reflectedEye,reflectedP);
        }
        else {
            // Native final reflector-entry state supplies the screen-texture
            // projection used by Mirror/GlassSurface m44WorldViewProjScale.
            // It must share the tracked mirror pose with the rendered RT,
            // while retaining an ordinary lens instead of oblique clipping.
            reflectedReady=NativeReflectionCameraMath::BuildView(originalView,reflectedView,view,reflectedEye);
            if(reflectedReady)std::memcpy(reflectedP,gpuProjection,sizeof(reflectedP));
        }
        if(!reflectedReady){
            if(reflectionPhase==1)reflectionFailure={true,frameGeneration.load(),reflection.queue,reflection.reflector,reflection.packets[1]};
            ReflectionEvidenceRecord(reflection,reflectionPhase,lensRejected?1:2,source,view,nullptr);
            ++reflectionRejected;gpuState=0;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
        }
        if(reflectionPhase==1)reflectionFailure={};
        ReflectionEvidenceRecord(reflection,reflectionPhase,0,source,view,reflectedP);
        auto* reflectedBytes=static_cast<unsigned char*>(state);
        std::memcpy(reflectedBytes+0xBC,&reflectedEye,sizeof(reflectedEye));
        std::memcpy(reflectedBytes+0x3F8,reflectedP,sizeof(reflectedP));
        const float reflectedPlane[4]={-reflectedEye.basis.right.z,-reflectedEye.basis.up.z,
            -reflectedEye.basis.forward.z,-reflectedEye.position.z};
        std::memcpy(reflectedBytes+0x3AC,reflectedPlane,sizeof(reflectedPlane));
        if(reflectionPhase==1){
            reflectionTarget.state=reinterpret_cast<std::uintptr_t>(state);
            reflectionTarget.reflector=reflection.reflector;
            reflectionTarget.serial=++reflectionTargetSerial;
            reflectionTarget.frame=frameGeneration.load();reflectionTarget.pair=gpuFrame.pairId;
            reflectionTarget.eyeIndex=gpuFrame.eye;reflectionTarget.view=reflectedEye;
            reflectionTarget.horizontal=NativeReflectionCameraMath::HorizontalPlane(originalView,reflectedView,reflectionTarget.floorHeight);
            std::memcpy(reflectionTarget.projection,reflectedP,sizeof(reflectedP));
            CameraPose reference;
            reflectionBillboardReferenceValid=NativeCameraKLMath::ViewFromWorld(pair.centre,reference)&&
                NativeReflectionCameraMath::BuildView(originalView,reflectedView,reference,reflectionBillboardReference);
        }
        // Reflection must not publish a host eye, rotate the player's arms,
        // overwrite the main cached pose, or grant first-person draw guards.
        gpuState=0;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();++reflectionViews;return;
    }
    if(reflectionKind>0&&reflectionPhase==2)++reflectionRestores;
    float plane[4];NativeCameraKLMath::EyePlane(gpuEye,plane);
    auto* bytes=static_cast<unsigned char*>(state);
    std::memcpy(bytes+0xBC,&view,sizeof(view));
    std::memcpy(bytes+0x3F8,gpuProjection,sizeof(gpuProjection));
    std::memcpy(bytes+0x3AC,plane,sizeof(plane));
    if(reflectionKind>0){
        // All four packets belong to the reflector. The two original-view
        // phases adapt stencil/refraction/restore state, but do not become a
        // main presented eye or grant tracked-player palette authorization.
        gpuState=0;NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();return;
    }
    gpuState=reinterpret_cast<std::uintptr_t>(state);
    gpuCinematic=cinematic;
    gpuCinematicVR=cinematicVR.load();
    renderContextOpen=true;
    if(firstPersonHeading.valid&&gpuFrame.anchorValid&&gpuFrame.stableFirstPersonYaw&&!gpuFrame.nativeCinematic&&!gpuFrame.nativeActionTilt&&!gpuFrame.nativeCarried&&!gpuFrame.nativePickup&&!gpuFrame.nativeVehicleTransition) {
        Vec3 unused;firstPersonHeading.worldGaze.Select(gpuEye,unused);
    }
    if(firstPersonHeading.valid&&gpuFrame.anchorValid&&!gpuFrame.nativeCinematic&&!gpuFrame.nativeCarried)
        firstPersonHeading.sourceState=reinterpret_cast<std::uintptr_t>(state);
    if(gpuFrame.nativeCinematic&&!gpuFrame.actorCinematic) {
        NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();
    } else if(gpuFrame.actorCinematic) {
        NativeBodyKL::ClearFrame();
        NativeHandsKL::SetFrame(gpuEye,gpuFrame.rawHeadOrientation,gpuFrame.rawHeadPosition,
            gpuFrame.eyeOffset,gpuFrame.worldUnitsPerMetre,gpuFrame.pairId,false,
            static_cast<std::uint32_t>(gpuFrame.recenterAck),true);
    } else {
        NativeBodyKL::SetFrame(gpuEye,gpuFrame.pairId,gpuFrame.worldUnitsPerMetre,true,
                               static_cast<std::uint32_t>(gpuFrame.recenterAck));
        NativeHandsKL::SetFrame(gpuEye,gpuFrame.rawHeadOrientation,gpuFrame.rawHeadPosition,
                                gpuFrame.eyeOffset,gpuFrame.worldUnitsPerMetre,gpuFrame.pairId,gpuFrame.anchorValid,
                                static_cast<std::uint32_t>(gpuFrame.recenterAck));
    }
    AcquireSRWLockExclusive(&visibilityLock);
    visibility={true,gpuFrame,gpuEye,gpuVisibilityNativePosition,GetTickCount()};
    ReleaseSRWLockExclusive(&visibilityLock);
    lastSource=source;lastEye=gpuFrame.eye;lastPair=gpuFrame.pairId;
    lastX=gpuEye.position.x;lastY=gpuEye.position.y;lastZ=gpuEye.position.z;
    const auto count=gpuViews.fetch_add(1);
    if(gpuFrame.nativeActionTilt)++nativeActionTiltViews;
    if(gpuFrame.nativePickup)++pickupHeadViews;
    if(gpuFrame.nativeVehicleTransition)++vehicleTransitionViews;
    if(gpuFrame.nativeActionTilt&&gpuFrame.nativeActionUpValid)++nativeAnimatedUpViews;
    if(count<8) {
        char message[240];std::snprintf(message,sizeof(message),"GPU camera packet=%08lx source=%08lx eye=%u pair=%llu worldEye=(%.2f,%.2f,%.2f).",
            static_cast<unsigned long>(packet),static_cast<unsigned long>(source),gpuFrame.eye,
            static_cast<unsigned long long>(gpuFrame.pairId),gpuEye.position.x,gpuEye.position.y,gpuEye.position.z);Log(message);
    }
    mainPass.completed=true;mainCameraApplied=true;primaryCameraState=reinterpret_cast<std::uintptr_t>(state);
    if(applied)applied(gpuFrame,gpuEye,gpuProjection,callbackUser);
}
}

bool Install(ProvideFrame provider,AppliedCallback onApplied,void* user,LogCallback log) {
    if(installed.load())return true;
    logger=log;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!provider||!onApplied||!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    pipVerified=NativePipKL::VerifyImage(image,[](std::uintptr_t at,void* output,std::size_t size) {
        SIZE_T bytes=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&bytes)&&bytes==size;
    });
    if(!pipVerified)Log("Native PIP ownership signatures unavailable; auxiliary camera guard disabled.");
    if(!NativeActionKL::InitializeVerifiedImage(image)) {Log("Native action-state signatures differ; camera hooks retained native.");return false;}
    if(!NativeCoverKL::InitializeVerifiedImage(image)) {Log("Native cover/input-yaw signatures differ; camera hooks retained native.");return false;}
    const unsigned char copySig[]={0x51,0x53,0x55,0x56,0x8B,0xF1,0x57,0x8B,0x7C,0x24,0x18,0x8D,0x5E,0x24,0x53,0x56};
    const unsigned char gpuSig[]={0x83,0xEC,0x08,0x8B,0x44,0x24,0x0C,0xD9,0x40,0x04};
    const unsigned char gpuCall[]={0xE8,0xBE,0xFD,0xFF,0xFF};
    const unsigned char fullBegin[]={0x83,0x80,0x00,0x03,0x00,0x00,0x01};
    const unsigned char fullEnd[]={0x83,0x80,0x00,0x03,0x00,0x00,0xFF};
    const unsigned char miniBegin[]={0x83,0x05,0xB0,0xE8,0xC8,0x00,0x01};
    const unsigned char miniEnd[]={0x83,0x2D,0xB0,0xE8,0xC8,0x00,0x01};
    const unsigned char fullCameraSet[]={0x89,0x2D,0x0C,0x75,0xC7,0x00};
    const unsigned char fullCameraClear[]={0xC7,0x05,0x0C,0x75,0xC7,0x00,0x00,0x00,0x00,0x00};
    const unsigned char fullCameraArgument[]={0x8B,0x45,0x24,0x50,0xFF,0xD2};
    const unsigned char projectionCopy[]={0x8B,0x7C,0x24,0x10,0x8D,0x75,0x18,0x81,0xC7,0xF8,0x03,0,0,0xB9,0x10,0,0,0,0xF3,0xA5,0x8B,0x7C,0x24,0x10,0xDD,0xD8};
    const unsigned char projectionDefault[]={0x83,0xF8,0x3C,0x0F,0x87,0x3D,0x01,0,0};
    const unsigned char projectionPop[]={0xB9,0x10,0,0,0,0xF3,0xA5,0x8B,0x7C,0x24,0x10};
    const unsigned char reflectorSig[]={0x6A,0xFF,0x68,0xFB,0x0F,0x8F,0,0x64,0xA1,0,0,0,0,0x50,0xB8,0x9C,0xE6,0x0A,0};
    const unsigned char mainRenderSig[]={0x6A,0xFF,0x68,0x3B,0x10,0x8F,0,0x64,0xA1,0,0,0,0,0x50,0xB8,0x74,0xD1,0x03,0};
    const unsigned char reflectorReturn[]={0xC2,0x18,0};
    if(!Matching(kCopyRva,copySig,sizeof(copySig))||!Matching(kGpuSetterRva,gpuSig,sizeof(gpuSig))||
       !Matching(kGpuCallerRva-5,gpuCall,sizeof(gpuCall))||
       !Matching(0x158586,fullBegin,sizeof(fullBegin))||!Matching(0x158879,fullEnd,sizeof(fullEnd))||
       !Matching(0x2F719E,miniBegin,sizeof(miniBegin))||!Matching(0x2F7260,miniEnd,sizeof(miniEnd))||
       !Matching(0x15858D,fullCameraSet,sizeof(fullCameraSet))||!Matching(0x15888A,fullCameraClear,sizeof(fullCameraClear))||
       !Matching(0x158541,fullCameraArgument,sizeof(fullCameraArgument))||!Matching(0xB54C3,projectionCopy,sizeof(projectionCopy))||
       !Matching(0xB5395,projectionDefault,sizeof(projectionDefault))||
       !Matching(0xB5569,projectionPop,sizeof(projectionPop))||
       !Matching(kReflectorRenderRva,reflectorSig,sizeof(reflectorSig))||
       !Matching(0xBF600,mainRenderSig,sizeof(mainRenderSig))||
       !Matching(0xBF34F,reflectorReturn,sizeof(reflectorReturn))) {
        Log("Native GPU camera/cinematic signatures differ; no camera hooks installed.");return false;
    }
    provide=provider;applied=onApplied;callbackUser=user;
    NativeProjectionTailKL::observer=ProjectionTail;
    NativeProjectionTailKL::packetObserver=SecondaryProjectionPacket;
    // The default decoder packet jumps directly to 4B54DB (FSTP).
    // A five-byte jump at 4B54D7 corrupts that independent entry.
    // Replace only MOV ECX,16; the adapter reproduces its REP and resumes
    // at the untouched MOV/FSTP tail, with no trampoline through the REP.
    KL1ProjectionTailOriginal=reinterpret_cast<void*>(image+0xB54D7);
    KL1ProjectionPopOriginal=reinterpret_cast<void*>(image+0xB5570);
    void* targets[]={reinterpret_cast<void*>(image+kCopyRva),reinterpret_cast<void*>(image+kGpuSetterRva),reinterpret_cast<void*>(image+0xB54D0),
        reinterpret_cast<void*>(image+kReflectorRenderRva),reinterpret_cast<void*>(image+0xBF600),reinterpret_cast<void*>(image+0xB5569)};
    void* hooks[]={reinterpret_cast<void*>(&CopyHook),reinterpret_cast<void*>(&GpuHook),reinterpret_cast<void*>(&KL1ProjectionTailHook),
        reinterpret_cast<void*>(&ReflectorHook),reinterpret_cast<void*>(&MainRenderHook),reinterpret_cast<void*>(&KL1ProjectionPopHook)};
    void** originals[]={reinterpret_cast<void**>(&originalCopy),reinterpret_cast<void**>(&originalGpu),nullptr,
        reinterpret_cast<void**>(&originalReflector),reinterpret_cast<void**>(&originalMainRender),nullptr};
    constexpr unsigned targetCount=sizeof(targets)/sizeof(targets[0]);
    unsigned created=0;
    for(;created<targetCount;++created)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK)break;
    bool ok=created==targetCount;
    if(ok)for(unsigned n=0;n<targetCount;++n)if(MH_QueueEnableHook(targets[n])!=MH_OK){ok=false;break;}
    if(ok)ok=MH_ApplyQueued()==MH_OK;
    if(!ok) {for(unsigned n=0;n<created;++n){MH_DisableHook(targets[n]);MH_RemoveHook(targets[n]);}return false;}
    const unsigned char worldGetter[]={0x8B,0x49,4,0xE9,0x58,0x38,0xF3,0xFF};
    const unsigned char worldGetterCall[]={0xE8,0x2C,0xEB,3,0};
    const unsigned char reflectorCopy[]={0xE8,0xA3,0x45,0xF7,0xFF};
    const unsigned char reflectorPrepareCopy[]={0xE8,0xB3,0x95,0xF7,0xFF};
    if(Matching(0xF83C0,worldGetter,sizeof(worldGetter))&&Matching(0xB988F,worldGetterCall,sizeof(worldGetterCall))&&
       Matching(0xBE748,reflectorCopy,sizeof(reflectorCopy))&&Matching(0xB9738,reflectorPrepareCopy,sizeof(reflectorPrepareCopy))){
        auto* worldTarget=reinterpret_cast<void*>(image+0xF83C0);
        if(MH_CreateHook(worldTarget,reinterpret_cast<void*>(&CameraWorldHook),reinterpret_cast<void**>(&originalCameraWorld))==MH_OK&&
           (MH_QueueEnableHook(worldTarget)!=MH_OK||MH_ApplyQueued()!=MH_OK)){
            MH_RemoveHook(worldTarget);
            originalCameraWorld=nullptr;
        }
    }
    installed=true;Log("Native GPU camera: exact kind1 consumer view/P/eye-plane enabled; CPU geometry/cache untouched.");return true;
}
bool Installed(){return installed.load();}
bool RegisterCameraProducerFilter(std::uintptr_t verifiedImage,CameraProducerFilter filter){
    if(!filter||!installed.load()||!verifiedImage||verifiedImage!=image||!originalMainRender)return false;
    unsigned char branch[5]{};SIZE_T got=0;const auto target=image+0xBF600;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(target),branch,sizeof(branch),&got)||
       got!=sizeof(branch)||branch[0]!=0xE9)return false;
    std::int32_t relative=0;std::memcpy(&relative,branch+1,4);
    const auto destination=static_cast<std::uint32_t>(target+5+relative);
    if(destination!=reinterpret_cast<std::uintptr_t>(&MainRenderHook))return false;
    CameraProducerFilter expected=nullptr;
    return cameraProducerFilter.compare_exchange_strong(expected,filter,std::memory_order_acq_rel)||expected==filter;
}
void SetCinematicVR(bool enabled){cinematicVR.store(enabled);}
bool ReadEffectsView(EffectsView& output) {
    if(!installed.load()||!frameValid||!gpuState||gpuState!=currentCameraState||
       gpuGeneration!=frameGeneration.load()||GetCurrentThreadId()!=gpuThread.load()||
       !pair.valid||pair.source!=gpuFrame.sourceCamera||pair.pairId!=gpuFrame.pairId||!ValidatePose(pair.centre))return false;
    CameraPose actual,expected;float projection[16]{};
    const auto* bytes=reinterpret_cast<const unsigned char*>(gpuState);
    std::memcpy(&actual,bytes+0xBC,sizeof(actual));std::memcpy(projection,bytes+0x3F8,sizeof(projection));
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,expected))return false;
    if(NativeCameraKLMath::RestoreQueuedProjection(gpuFrame.nativeCinematic,actual,expected,
        gpuNativeProjection,gpuProjection,projection)) {
        std::memcpy(reinterpret_cast<unsigned char*>(gpuState)+0x3F8,projection,sizeof(projection));++queuedProjectionRestored;
    }
    if(!NativeCameraKLMath::SameView(actual,expected)||
       !NativeCameraKLMath::SameProjection(projection,gpuProjection)||
       !std::isfinite(gpuFrame.worldUnitsPerMetre)||gpuFrame.worldUnitsPerMetre<=0)return false;
    EffectsView next;next.state=gpuState;next.eye=gpuEye;next.reference=pair.centre;
    std::memcpy(next.world,bytes+0x8C,sizeof(next.world));next.worldKnown=true;
    std::memcpy(&next.modelViewScale,bytes+0x3D4,sizeof(next.modelViewScale));
    std::memcpy(&next.modelViewDepthBias,bytes+0x3D8,sizeof(next.modelViewDepthBias));
    for(float value:next.world)if(!std::isfinite(value))next.worldKnown=false;
    next.centre=Subtract(gpuEye.position,Scale(MapTracking(gpuEye.basis,
        {gpuFrame.eyeOffset[0],gpuFrame.eyeOffset[1],gpuFrame.eyeOffset[2]}),gpuFrame.worldUnitsPerMetre));
    if(!Finite(next.centre))return false;
    std::memcpy(next.projection,gpuProjection,sizeof(next.projection));next.frame=gpuGeneration;
    next.eyeIndex=gpuFrame.eye;output=next;return true;
}
std::uint64_t ActiveReflectionTargetSerial() {
    if(!reflectionTarget.state||!installed.load()||!frameValid||
       reflectionTarget.state!=currentCameraState||reflectionTarget.frame!=frameGeneration.load()||
       reflectionTarget.frame!=gpuGeneration||reflectionTarget.pair!=gpuFrame.pairId||
       reflectionTarget.eyeIndex!=gpuFrame.eye||GetCurrentThreadId()!=gpuThread.load())return 0;
    return reflectionTarget.serial;
}
bool ReadReflectionTargetView(ReflectionTargetView& output) {
    if(!ActiveReflectionTargetSerial())return false;
    const auto* bytes=reinterpret_cast<const unsigned char*>(reflectionTarget.state);
    if(std::memcmp(bytes+0xBC,&reflectionTarget.view,sizeof(reflectionTarget.view))||
       std::memcmp(bytes+0x3F8,reflectionTarget.projection,sizeof(reflectionTarget.projection)))return false;
    output=reflectionTarget;return true;
}
void QualifyFloorReflector(std::uintptr_t reflector,bool retomoto){
    std::uint32_t geometry=0,type=0;
    if(!reflector||!Read(reflector+0xF4,geometry)||!geometry)return;
    const auto key=(std::uint64_t(geometry)<<32)|reflector;
    if(retomoto){
        auto* available=&retomotoReflectorKeys[(reflector>>4)%retomotoReflectorKeys.size()];
        for(auto& slot:retomotoReflectorKeys){
            const auto old=slot.load(std::memory_order_relaxed);
            if(old==key)return;
            if(!old||std::uint32_t(old)==reflector)available=&slot;
        }
        if(Read(reflector,type)&&type==image+kReflectorVtable)available->store(key,std::memory_order_relaxed);
    }else if(key!=tokyoReflectorKey.load(std::memory_order_relaxed)&&Read(reflector,type)&&type==image+kReflectorVtable)
        tokyoReflectorKey.store(key,std::memory_order_relaxed);
}
bool ReadReflectionBillboardReference(std::uintptr_t state,CameraPose& view) {
    if(!state||state!=reflectionTarget.state||!reflectionBillboardReferenceValid)return false;
    ReflectionTargetView exact;
    if(!ReadReflectionTargetView(exact))return false;
    view=reflectionBillboardReference;return true;
}
bool ReadReflectionScatter(ReflectionTargetView& camera,float (&columns)[8]) {
    if(!ReadReflectionTargetView(camera)||!std::isfinite(gpuFrame.worldUnitsPerMetre)||
       gpuFrame.worldUnitsPerMetre<=0)return false;
    // The packed reflected VIEW is orthogonal with negative parity. Its
    // second row is world +Y in view space. Reflecting both the eye offset and
    // its camera axes leaves its local components unchanged.
    const auto up=camera.view.basis.up;
    const float scale=gpuFrame.worldUnitsPerMetre;
    const Vec3 offset{gpuFrame.eyeOffset[0]*scale,gpuFrame.eyeOffset[1]*scale,-gpuFrame.eyeOffset[2]*scale};
    if(!Finite(up)||!Finite(offset))return false;
    const float result[]{up.x,up.y,up.z,1e-6f,offset.x,offset.y,offset.z,0};
    std::memcpy(columns,result,sizeof(columns));return true;
}
bool CinematicActive() {
    ProfileTiming::Scope profile(ProfileTiming::CinematicRead);
    if(!installed.load())return false;
    if(CommittedCinematicContext()){
        ++cinematicDrawReused;return gpuCinematic.PreserveNative();
    }
    ++cinematicNativeReads;
    return CinematicState().PreserveNative();
}
std::uintptr_t CinematicCamera() {
    ProfileTiming::Scope profile(ProfileTiming::CinematicRead);
    if(!installed.load())return 0;
    if(CommittedCinematicContext()){++cinematicCameraDrawReused;return gpuCinematic.camera;}
    ++cinematicCameraNativeReads;
    return CinematicState().camera;
}
bool CinematicPanelActive() {
    ProfileTiming::Scope profile(ProfileTiming::CinematicRead);
    if(!installed.load())return false;
    // GpuHook can commit a main scene only after rejecting PauseMenu, full
    // panel, actor panel and composite-PIP. Retain that classification only
    // within this exact packet; producer/input/capture cleanup read fresh.
    if(CommittedCinematicContext()){++cinematicPanelDrawReused;return false;}
    ++cinematicPanelNativeReads;
    if(PauseMenu())return true;
    const auto state=CinematicState();
    std::uintptr_t player=0;LocalCamera(&player);
    return NativeCinematicKL::FullScenePanel(state,cinematicVR.load())||
        NativeCinematicKL::ActorScenePanel(NativeActionKL::Current(),player,cinematicVR.load())||
        (state.full&&state.camera&&NativeCinematicKL::CompositePipPanel(state,PipState()));
}
bool DrawStateActive(std::uintptr_t state,bool* firstPerson,bool allowActorCinematic) {
    ProfileTiming::Scope profile(ProfileTiming::RenderGuard);
    if(firstPerson)*firstPerson=false;
    if(!installed.load()||!state||state!=gpuState||!frameValid||
       gpuGeneration!=frameGeneration.load()||GetCurrentThreadId()!=gpuThread.load()||
       (gpuFrame.nativeCinematic&&!(allowActorCinematic&&gpuFrame.actorCinematic)))return false;
    CameraPose actualView,expectedView;float actualProjection[16]{};
    // This is the exact live state already consumed by the original native
    // binder/setter on this GPU thread, not a speculative heap address. The
    // camera hook itself writes these same fields directly. Avoid two RPM
    // kernel transitions for every world object while still rechecking the
    // actual matrices on EVERY call (subpasses can reuse this state pointer).
    const auto* bytes=reinterpret_cast<const unsigned char*>(state);
    std::memcpy(&actualView,bytes+0xBC,sizeof(actualView));
    std::memcpy(actualProjection,bytes+0x3F8,sizeof(actualProjection));
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,expectedView)||
       !NativeCameraKLMath::SameView(actualView,expectedView)||
       !NativeCameraKLMath::SameProjection(actualProjection,gpuProjection))return false;
    if(firstPerson)*firstPerson=gpuFrame.anchorValid;
    return true;
}
void ConfigureDiagnostics(DiagnosticsActive active,ProducerBatchObserver observer,ProducerBytesObserver bytesObserver){
    producerBytesObserver.store(bytesObserver);producerBatchObserver.store(observer);diagnosticsActive.store(active);
}
bool SnapshotDrawCamera(std::uintptr_t state,DrawCameraContext& output){
    DrawCameraContext next;next.tick=GetTickCount();next.thread=GetCurrentThreadId();next.gpuThread=gpuThread.load();
    next.state=state;next.gpuState=gpuState;next.currentState=currentCameraState;next.generation=frameGeneration.load();
    next.gpuGeneration=gpuGeneration;next.pair=gpuFrame.pairId;next.eye=gpuFrame.eye;
    next.sourceCamera=gpuFrame.sourceCamera;next.anchorCamera=gpuFrame.anchorCamera;next.anchorPlayer=gpuFrame.anchorPlayer;
    next.installed=installed.load();next.frameValid=frameValid;next.anchorValid=gpuFrame.anchorValid;
    next.nativeCinematic=gpuFrame.nativeCinematic;next.actorCinematic=gpuFrame.actorCinematic;
    next.pairValid=pair.valid;next.pairSource=pair.source;next.cachedPair=pair.pairId;next.secondaryActive=secondaryActive.valid;
    if(!next.installed)next.failures|=CameraNotInstalled;
    if(!state)next.failures|=CameraMissingState;
    if(state!=gpuState)next.failures|=CameraForeignState;
    if(!frameValid)next.failures|=CameraNoFrame;
    if(gpuGeneration!=next.generation)next.failures|=CameraOldGeneration;
    if(next.thread!=next.gpuThread)next.failures|=CameraForeignThread;
    if(gpuFrame.nativeCinematic)next.failures|=CameraCinematic;
    std::array<unsigned char,0x440> bytes{};SIZE_T got=0;
    next.stateRead=state&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(state),bytes.data(),bytes.size(),&got)&&got==bytes.size();
    CameraPose actualView{},expectedView{};
    if(next.stateRead){
        std::memcpy(next.actualWorld,bytes.data()+0x8C,sizeof(next.actualWorld));
        std::memcpy(&actualView,bytes.data()+0xBC,sizeof(actualView));
        std::memcpy(next.actualView,&actualView,sizeof(next.actualView));
        std::memcpy(next.actualProjection,bytes.data()+0x3F8,sizeof(next.actualProjection));
    }else next.failures|=CameraStateUnreadable;
    next.expectedViewValid=NativeCameraKLMath::ViewFromWorld(gpuEye,expectedView);
    if(next.expectedViewValid)std::memcpy(next.expectedView,&expectedView,sizeof(next.expectedView));else next.failures|=CameraExpectedViewInvalid;
    std::memcpy(next.vrProjection,gpuProjection,sizeof(next.vrProjection));
    std::memcpy(next.nativeProjection,gpuNativeProjection,sizeof(next.nativeProjection));
    next.viewMatches=next.stateRead&&next.expectedViewValid&&NativeCameraKLMath::SameView(actualView,expectedView);
    next.projectionMatches=next.stateRead&&NativeCameraKLMath::SameProjection(next.actualProjection,gpuProjection);
    if(!next.viewMatches)next.failures|=CameraViewMismatch;
    if(!next.projectionMatches)next.failures|=CameraProjectionMismatch;
    next.packet=diagnosticCameraPacket;next.packetState=diagnosticCameraState;next.packetGeneration=diagnosticCameraGeneration;
    std::array<unsigned char,0xB0> packet{};got=0;
    next.packetHeaderRead=next.packet&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(next.packet),packet.data(),packet.size(),&got)&&got==packet.size();
    if(next.packetHeaderRead){
        std::memcpy(next.packetHeader,packet.data(),sizeof(next.packetHeader));
        std::memcpy(next.packetView,packet.data()+0x18,sizeof(next.packetView));
        std::memcpy(next.packetProjection,packet.data()+0x48,sizeof(next.packetProjection));
        next.packetEntry=next.packetHeader[1];std::uint32_t vt=0,geometry=0,source=0,sourceVt=0;
        next.packetSourceRead=next.packetHeader[0]==1&&next.packetHeader[5]==0x26&&
            Read(next.packetEntry,vt)&&Read(next.packetEntry+0xF4,geometry)&&Read(geometry+0x60,source)&&Read(source,sourceVt);
        next.packetEntryVtable=vt;next.packetGeometry=geometry;next.packetCamera=source;next.packetCameraVtable=sourceVt;
        if(source)next.packetCameraFlagsRead=Read(source+0x44,next.packetCameraFlags);
    }
    output=next;return next.stateRead;
}
bool IsSecondaryProjectionDraw(std::uintptr_t state){return secondaryActive.valid&&secondaryActive.state==state&&
    secondaryActive.generation==frameGeneration.load()&&secondaryActive.pair==gpuFrame.pairId&&DrawStateActive(state);}
void ObserveSecondaryMaskBind(std::uintptr_t state,std::uintptr_t entry,unsigned kind){
    if(!entry||kind>1||!IsSecondaryProjectionDraw(state))return;
    if(kind==0)++secondaryBodyBinds;else ++secondaryWeaponBinds;
}
void FinishScene(){reflectionTarget={};renderContextOpen=false;secondaryActive={};cleanupThread=GetCurrentThreadId();NativeHandsKL::ClearFrame();NativeBodyKL::ClearFrame();}
void FrameBoundary(){FinishScene();NativeHandsKL::RetireRenderPairImage();++frameGeneration;frameValid=false;}
bool ReadRenderContext(std::uint64_t& epoch,unsigned& eye){
    bool firstPerson=false;
    if(!renderContextOpen||captureCompromised||!gpuState||currentCameraState!=gpuState||
       !gpuFrame.anchorValid||gpuFrame.nativeCinematic||gpuFrame.eye>1||!pair.valid||
       pair.source!=gpuFrame.sourceCamera||pair.player!=gpuFrame.anchorPlayer||pair.pairId!=gpuFrame.pairId||
       !DrawStateActive(gpuState,&firstPerson)||!firstPerson)return false;
    epoch=gpuGeneration;eye=gpuFrame.eye;return true;
}
bool CaptureReady(std::uint64_t pairId,unsigned eye){return installed.load()&&frameValid&&mainCameraApplied&&!captureCompromised&&
    gpuGeneration==frameGeneration.load()&&gpuFrame.pairId==pairId&&gpuFrame.eye==eye&&
    NativeHandsKL::RenderPairCaptureReady();}
bool ReadCaptureContext(std::uint64_t& epoch,unsigned& eye,std::uint64_t& pairId){
    if(!installed.load()||GetCurrentThreadId()!=gpuThread.load()||!gpuFrame.anchorValid||
       gpuFrame.nativeCinematic||gpuFrame.eye>1||!pair.valid||pair.source!=gpuFrame.sourceCamera||
       pair.player!=gpuFrame.anchorPlayer||pair.pairId!=gpuFrame.pairId||
       !CaptureReady(gpuFrame.pairId,gpuFrame.eye))return false;
    epoch=gpuGeneration;eye=gpuFrame.eye;pairId=gpuFrame.pairId;return true;
}
void ObserveShaderConstants(){
    if(!installed.load())return;
    static thread_local bool seen=false;if(!seen){shaderThread=GetCurrentThreadId();seen=true;}
    static thread_local unsigned count=0;if(++count==1024){shaderCalls+=count;count=0;}
}
bool MayRepairProjectionConstants() {
    return installed.load()&&frameValid&&gpuFrame.nativeCinematic&&
       gpuState&&gpuState==currentCameraState&&gpuGeneration==frameGeneration.load()&&
       GetCurrentThreadId()==gpuThread.load()&&pair.valid&&pair.source==gpuFrame.sourceCamera&&pair.pairId==gpuFrame.pairId;
}
bool RepairProjectionConstants(unsigned start,float* rows,unsigned count) {
    if(count<4||!MayRepairProjectionConstants())return false;
    CameraPose actual,expected;float current[16]{};
    auto* bytes=reinterpret_cast<unsigned char*>(gpuState);
    std::memcpy(&actual,bytes+0xBC,sizeof(actual));std::memcpy(current,bytes+0x3F8,sizeof(current));
    if(!NativeCameraKLMath::ViewFromWorld(gpuEye,expected)||!NativeCameraKLMath::SameView(actual,expected)||
       (!NativeCameraKLMath::SameProjection(current,gpuProjection)&&std::memcmp(current,gpuNativeProjection,sizeof(current))))return false;
    if(!NativeCameraKLMath::RestoreProjectionRows(start,rows,count,gpuNativeProjection,gpuProjection))return false;
    if(NativeCameraKLMath::RestoreQueuedProjection(true,actual,expected,gpuNativeProjection,gpuProjection,current)) {
        std::memcpy(bytes+0x3F8,current,sizeof(current));++queuedProjectionRestored;
    }
    ++queuedProjectionRowsRestored;return true;
}
Stats Snapshot(){
    Stats result{};result.installed=installed;result.copies=copies;result.ownedCopies=visibilityCopies;
    result.cinematicDrawReused=cinematicDrawReused;result.cinematicNativeReads=cinematicNativeReads;
    result.cinematicCameraDrawReused=cinematicCameraDrawReused;result.cinematicCameraNativeReads=cinematicCameraNativeReads;
    result.cinematicPanelDrawReused=cinematicPanelDrawReused;result.cinematicPanelNativeReads=cinematicPanelNativeReads;
    result.pipGuardVerified=pipVerified;result.gpuPipPassesSkipped=pipSkipped;result.gpuPipCompositeFrames=pipCompositeFrames;
    result.lastPipCamera=lastPipCamera;result.lastPipGeometry=lastPipGeometry;
    result.rejected=rejected;result.projections=gpuViews;result.renderPoses=gpuViews;
    result.shaderCallsOutsideRender=shaderCalls;result.cameraRenderThreadId=visibilityThread;
    result.shaderThreadId=shaderThread;result.cleanupThreadId=cleanupThread;
    result.lastSource=lastSource;result.lastFarUnits=lastFar;
    result.gpuCameraCalls=gpuCalls;result.gpuCameraViews=gpuViews;result.gpuCameraRejected=rejected;
    result.nativeActionTiltViews=nativeActionTiltViews;
    result.nativeAnimatedUpViews=nativeAnimatedUpViews;
    result.queuedProjectionRestored=queuedProjectionRestored;result.queuedProjectionRowsRestored=queuedProjectionRowsRestored;
    result.actionHeadingGaps=actionHeadingGaps;result.actionHeadingRetained=actionHeadingRetained;result.actionHeadingRejected=actionHeadingRejected;
    result.authoredHeadingViews=authoredHeadingViews;result.authoredHeadingTurns=authoredHeadingTurns;
    result.pickupHeadViews=pickupHeadViews;
    result.vehicleTransitionViews=vehicleTransitionViews;result.captureCompromisedFrames=captureCompromisedFrames;
    result.unclassifiedPrimaryStatePasses=unclassifiedPrimaryStatePasses;
    result.reflectionScopes=reflectionScopes;result.reflectionCertified=reflectionCertified;
    result.reflectionRejected=reflectionRejected;result.reflectionViews=reflectionViews;result.reflectionRestores=reflectionRestores;
    result.reflectionLensRejected=reflectionLensRejected;
    result.secondaryProducerScopes=secondaryProducerScopes;result.secondaryCertified=secondaryCertified;
    result.secondaryApplied=secondaryApplied;result.secondaryPopped=secondaryPopped;result.secondaryRejected=secondaryRejected;
    result.secondaryBodyBinds=secondaryBodyBinds;result.secondaryWeaponBinds=secondaryWeaponBinds;
    AcquireSRWLockShared(&secondaryLock);result.lastSecondaryAccepted=lastSecondaryAccepted;result.lastSecondaryRejected=lastSecondaryRejected;ReleaseSRWLockShared(&secondaryLock);
    AcquireSRWLockShared(&secondaryLock);result.lastSecondaryProducerRejected=lastSecondaryProducerRejected;ReleaseSRWLockShared(&secondaryLock);
    AcquireSRWLockShared(&reflectionLock);result.lastReflectionAccepted=lastReflectionAccepted;
    result.lastReflectionRejected=lastReflectionRejected;ReleaseSRWLockShared(&reflectionLock);
    result.reflectionEmptyScopes=reflectionEmptyScopes;
    result.tokyoReflectionCameraPreparations=tokyoReflectionCameraPreparations;
    result.retomotoReflectionCameraPreparations=retomotoReflectionCameraPreparations;
    AcquireSRWLockShared(&reflectionLock);result.lastReflectionProducerAccepted=lastReflectionProducerAccepted;
    result.lastReflectionProducerRejected=lastReflectionProducerRejected;ReleaseSRWLockShared(&reflectionLock);
    result.gpuCameraThreadId=gpuThread;
    result.lastGpuEye=lastEye;result.lastGpuPair=lastPair;result.lastGpuPosition[0]=lastX;
    result.lastGpuPosition[1]=lastY;result.lastGpuPosition[2]=lastZ;return result;
}
}
