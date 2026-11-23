#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "MinHook.h"
#include "native_hud_kl.hpp"
#include "native_hud_math.hpp"
#include "native_reticle_kl_math.hpp"
#include "native_world_marker_math.hpp"
#include "native_marker_source_kl.hpp"
#include "native_marker_view_math.hpp"
#include "native_player_pose.hpp"
#include "native_hud_shadow.hpp"
#include "native_letterbox_state.hpp"
#include "native_camera_kl.hpp"
#include "native_effects_producer_kl.hpp"
#include "native_tracer_visual_kl.hpp"
#include "profile.hpp"
#include "profile_timing.hpp"
#include <atomic>
#include <cstring>
#include <vector>
#include <limits>
#include <optional>
#include <unordered_map>

namespace NativeHudKL {
namespace {
thread_local WristHudSource wristSource;
struct WristSprite {
    std::uintptr_t entry=0;
    std::uint32_t slot=2,stamp=0,geometry=0,material[5]{},type=0,tick=0;
    bool weaponAmmo=false;
    WristHudSource source;
};
thread_local const WristSprite* wristDrawing=nullptr;
struct TeamPresentation {
    NativeTeamHudKL::Roster roster;
    std::uint64_t pair=0;
    unsigned eye=2;
};
thread_local TeamPresentation teamPresented;
std::atomic<WristHudProvider> wristProvider{nullptr};
std::atomic<CinemaTextProvider> cinemaTextProvider{nullptr};
std::atomic<std::uint64_t> cinemaTextDraws{0};
SRWLOCK wristSpriteLock=SRWLOCK_INIT;
std::array<std::unordered_map<std::uintptr_t,WristSprite>,2> wristSprites;
DWORD lastWristPrune=0; // protected by wristSpriteLock
constexpr DWORD wristPacketLifetime=250;
std::atomic<DWORD> lastWristSprite{0};
std::atomic<std::uint64_t> wristDraws[static_cast<unsigned>(WristHudElement::Count)][2]{},teamDrops{0};
std::atomic<std::uint64_t> wristAmmoDraws[2]{},wristCaptionDraws[2]{},selectionMarkerDraws[2]{};
std::atomic<unsigned> lastTeamRejection{0};
static_assert(sizeof(void*)==4,"K&L native HUD is x86 only");
using Draw=bool(__thiscall*)(void*,void*);
using Indexed=HRESULT(__thiscall*)(void*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using VertexLock=void*(__thiscall*)(void*,UINT,UINT,UINT*);
using IndexLock=void*(__thiscall*)(void*,UINT,UINT*);
using BufferUnlock=void(__thiscall*)(void*);
using LetterboxDraw=void(__thiscall*)(void*,void*,void*);
using SpriteEmit=std::uintptr_t(__thiscall*)(void*,std::uint32_t,const void*,const void*,const void*,std::uint32_t,std::uint32_t,std::uint32_t);
using SpriteDraw=std::uintptr_t(__thiscall*)(void*,void*);
using MarkerPosition=void*(__thiscall*)(void*,void*);
Draw originalDraw=nullptr;
Indexed originalIndexed=nullptr;
VertexLock originalVertexLock=nullptr;
IndexLock originalIndex16Lock=nullptr,originalIndex32Lock=nullptr;
BufferUnlock originalBufferUnlock=nullptr;
LetterboxDraw originalLetterboxDraw=nullptr;
SpriteEmit originalSpriteEmit=nullptr;
SpriteDraw originalSpriteDraw=nullptr;
MarkerPosition originalMarkerPosition=nullptr;
std::atomic<ReticleProvider> reticleProvider{nullptr};
std::atomic<ReticleProvider> markerProvider{nullptr};
std::atomic<NativeScopeProvider> nativeScopeProvider{nullptr};
std::atomic<bool> reticleEnabled{false};
std::atomic<std::uint64_t> reticleCaptured{0},reticleRouted{0},reticleDrawn{0},reticleRejected{0},reticleRestoreFailures{0};
std::atomic<std::uint64_t> nativeScopeReticlePreserved{0};
struct ReticleEmitScope {std::uintptr_t player=0,manager=0;};
thread_local ReticleEmitScope reticleEmitScope;
thread_local std::array<ReticleEmitScope,8> reticleEmitStack{};
thread_local unsigned reticleEmitDepth=0;
struct MarkerSource {std::uintptr_t player=0,manager=0,record=0;NativeCameraMath::Vec3 target{};bool valid=false;std::uint64_t producer=0;std::uintptr_t camera=0;};
std::atomic<std::uint64_t> markerProducerSerial{0},nativeMarkerAmbiguous{0},nativeMarkerNegativeW{0};
thread_local MarkerSource nativeMarkerScope;
std::atomic<NativeMarkerObserver> nativeMarkerObserver{nullptr};
thread_local std::array<MarkerSource,8> nativeMarkerStack{};
thread_local unsigned nativeMarkerDepth=0;
thread_local bool nativeMarkerDrawing=false;
struct MarkerPacket {
    std::uintptr_t entry=0,player=0;
    std::uint32_t slot=2,geometry=0,stamp=0,tick=0,material[5]{};
    std::uintptr_t record=0;NativeCameraMath::Vec3 target{};
    std::uint32_t entity=0,entityGeometry=0;
    std::uint64_t producer=0;bool ambiguous=false,destination=false,fixed=false;
    std::uint32_t recordType=0,texture=0,caller=0;
    NativeWorldMarkerMath::Layout layout;
    NativeMarkerSourceKL::CanonicalQuad canonical;
};
thread_local MarkerPacket nativeMarkerDrawPacket;
thread_local std::uint64_t markerDrawSequence=0,markerDrawId=0,markerCanonicalizedDrawId=0;
std::atomic<std::uint64_t> markerCanonicalized{0},markerCanonicalRejected{0},markerCanonicalNonfinite{0};
std::atomic<float> markerCanonicalLastMagnitude{0},markerCanonicalMaxMagnitude{0},markerCanonicalSpanX{0},markerCanonicalSpanY{0};
std::atomic<bool> objectiveTraceEnabled{false};
SRWLOCK objectiveTraceLock=SRWLOCK_INIT;
NativeObjectiveTraceKL::Ring objectiveTrace;
void TraceObjective(const MarkerPacket& packet,const ReticleFrame* frame,unsigned phase,unsigned stage) {
    if(!objectiveTraceEnabled.load(std::memory_order_relaxed)||!NativeMarkerViewMath::Objective(packet.recordType))return;
    NativeObjectiveTraceKL::Event event;event.tick=GetTickCount();event.phase=phase;event.stage=stage;
    event.record=packet.record;event.entity=packet.entity;event.geometry=packet.entityGeometry;event.entry=packet.entry;
    event.producer=packet.producer;event.slot=packet.slot;event.stamp=packet.stamp;
    const auto target=frame?frame->target:packet.target;event.target[0]=target.x;event.target[1]=target.y;event.target[2]=target.z;
    if(frame){event.pair=frame->pairId;event.eye=frame->eye;
        if(NativeCameraMath::Finite(target)&&NativeCameraMath::ValidatePose(frame->actualEye,.05f)){
            const auto delta=NativeCameraMath::Subtract(target,frame->actualEye.position);
            const float x=NativeCameraMath::Dot(delta,frame->actualEye.basis.right),y=NativeCameraMath::Dot(delta,frame->actualEye.basis.up),z=NativeCameraMath::Dot(delta,frame->actualEye.basis.forward);
            event.clip[0]=x*frame->projection[0]+y*frame->projection[4]+z*frame->projection[8]+frame->projection[12];
            event.clip[1]=x*frame->projection[1]+y*frame->projection[5]+z*frame->projection[9]+frame->projection[13];
            event.clip[2]=x*frame->projection[3]+y*frame->projection[7]+z*frame->projection[11]+frame->projection[15];
            event.clipValid=std::isfinite(event.clip[0])&&std::isfinite(event.clip[1])&&std::isfinite(event.clip[2]);
        }
    }
    AcquireSRWLockExclusive(&objectiveTraceLock);objectiveTrace.Push(event);ReleaseSRWLockExclusive(&objectiveTraceLock);
}
SRWLOCK markerPacketLock=SRWLOCK_INIT;
std::array<MarkerPacket,32> markerPackets{};
std::atomic<std::uint64_t> nativeMarkerEmitted{0},nativeMarkerCaptured{0},nativeMarkerRouted{0},nativeMarkerDrawn{0},nativeMarkerRejected{0};
std::atomic<std::uint64_t> spriteMarkerRetired{0},spriteReticleRetired{0};
std::atomic<std::uint64_t> worldSprite5Sources{0},worldSprite6Sources{0},worldSpriteRelativePreserved{0},worldSpriteHudPreserved{0};
std::array<std::atomic<std::uint64_t>,9> spriteSourceTypes{};
SRWLOCK worldSpriteSourceLock=SRWLOCK_INIT;
Stats::WorldSpriteSourceSnapshot lastWorldSpriteEligible,lastWorldSpritePreserved;
std::uint64_t worldSpriteSourceSequence=0;
std::atomic<std::uint64_t> nativeMarkerReprojected{0};
std::atomic<std::uint64_t> nativeDestinationCaptured{0},nativeDestinationDrawn{0};
std::atomic<std::uint64_t> nativeObjectiveBobSuppressed{0},nativeObjectivePairsReused{0},nativeMarkerAspectCorrected{0};
thread_local NativeMarkerViewMath::PairCache objectivePairCache;
thread_local NativeMarkerViewMath::WorldScaleCache markerWorldScaleCache;
SRWLOCK markerEvidenceLock=SRWLOCK_INIT;MarkerSourceEvidence markerEvidence;
std::atomic<std::uint32_t> nativeMarkerRejectStage{0};
std::atomic<float> nativeMarkerMinZ{0},nativeMarkerMaxZ{0},nativeMarkerMinW{0},nativeMarkerMaxW{0};
std::atomic<std::uint32_t> nativeMarkerVertexCount{0};
std::atomic<std::uint64_t> nativeMarkerCalibrated{0},nativeMarkerCalibrationRejected{0};
std::atomic<float> nativeMarkerWidthNdc{0},nativeMarkerHeightNdc{0},nativeMarkerSourceDepth{0};
struct ReticleDrawScope {std::uintptr_t player=0;};
thread_local ReticleDrawScope reticleDrawScope;
struct ReticlePacket {
    std::uintptr_t entry=0,player=0;
    std::uint32_t slot=2,tick=0,stamp=0,geometry=0,header[3]{},material[5]{};
    std::array<unsigned char,8*116> payload{};
};
SRWLOCK reticlePacketLock=SRWLOCK_INIT;
std::array<ReticlePacket,24> reticlePackets{};
std::atomic<LetterboxProvider> letterboxProvider{nullptr};
std::atomic<MappedVertexFilter> mappedVertexFilter{nullptr};
thread_local MappedVertexContext particleDrawContext,mappedCallbackContext;
thread_local std::uint64_t worldSpriteDrawSequence=0;
std::atomic<NativeEffectsKL::VisualTraceActive> visualSourceActive{nullptr};
SRWLOCK spriteProducerLock=SRWLOCK_INIT;
std::array<NativeEffectsKL::SpriteProducerEvidence,2048> spriteProducers;
std::uint64_t spriteProducerSequence=0;
thread_local NativeEffectsKL::VisualSourceDiagnostic* spriteVisualScope=nullptr;
bool VisualSourceActive(){const auto active=visualSourceActive.load(std::memory_order_relaxed);return active&&active();}
thread_local NativeLetterboxKL::State letterboxState;
ProvideFrame provide=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> calls{0},scopes{0},uploads{0},rejected{0};
std::atomic<std::uint64_t> fullScreenPreserved{0};
std::atomic<std::uint64_t> indexedCalls{0},genericCandidates{0},genericUploads{0};
std::atomic<std::uint64_t> cameraRejected{0},viewportRejected{0},constantsRejected{0},bufferRejected{0};
std::atomic<std::uint32_t> lastCaller{0},lastStride{0};
std::atomic<std::uint32_t> lastVertexUsage{0},lastIndexUsage{0},lastReadStage{0};
std::atomic<std::uint64_t> constantReadRejected{0},backendRejected{0},provenanceRejected{0},depthRejected{0},geometryRejected{0};
std::atomic<std::uint64_t> nativeLocks{0},nativeUnlocks{0},shadowCaptured{0},shadowRejected{0},shadowReads{0},shadowMisses{0};
std::atomic<std::uint64_t> shadowSkippedWrites{0},shadowCopiedBytes{0},shadowTrackedInvalidations{0};
std::atomic<std::uint32_t> lastWriter{0},lastZWrite{0},lastZFunc{0},lastRawCaller{0};
std::atomic<std::uint64_t> scissorAdjusted{0},scissorRejected{0};
std::atomic<std::uint64_t> letterboxCalls{0},letterboxHidden{0},letterboxRejected{0};
std::atomic<std::uintptr_t> lastLetterbox{0};
std::atomic<std::uint32_t> lastLetterboxMini{0},lastLetterboxFull{0};
std::atomic<bool> lastLetterboxFirstPerson{false};
std::atomic<float> lastLetterboxAmount{0.f};
std::atomic<HRESULT> lastConstantsResult{S_OK};
std::atomic<DWORD> threadId{0};
SRWLOCK diagnosticLock=SRWLOCK_INIT;
float lastProjection[16]{};
float lastConstants[20]{};
float lastRawConstants[20]{};
float lastQuadMinZ=0.f,lastQuadMaxZ=0.f;
SRWLOCK shadowLock=SRWLOCK_INIT;
NativeHudShadow::Cache shadow;
struct NativeBufferControl {
    std::uint32_t resource=0,mapped=0,capacity=0,end=0;
};
static_assert(sizeof(NativeBufferControl)==16,"pinned native dynamic-buffer control layout");
struct PendingWrite {
    NativeHudShadow::Allocation allocation{};
    std::uintptr_t mapped=0;
    std::uint64_t token=0;
    bool capture=false;
    bool effects=false;
    MappedVertexContext effectsContext{};
    std::uint64_t markerDraw=0;
    MarkerPacket marker;
    std::uintptr_t wristWeaponEntry=0;
};
std::array<PendingWrite,16> pending{};
std::atomic<bool> pendingWrites{false};
void RefreshPending(){bool any=false;for(const auto& write:pending)any=any||write.token!=0||write.effects;pendingWrites=any;}
void RejectShadow() {
    ++shadowRejected;
    AcquireSRWLockExclusive(&shadowLock);shadow.Clear();pending={};pendingWrites=false;ReleaseSRWLockExclusive(&shadowLock);
}
struct Scope {
    bool active=false,projectionQualified=false,insideDraw=false;
    unsigned width=0,height=0;
    NativeHudMath::Plane plane{};
};
thread_local Scope scope;
thread_local std::vector<NativeHudMath::Vertex> vertices;
template<class T> bool Read(std::uintptr_t at,T& value) {
    SIZE_T got=0;
    return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),
                                &value,sizeof(value),&got)&&got==sizeof(value);
}
bool Match(std::uintptr_t rva,const unsigned char* expected,std::size_t size) {
    unsigned char bytes[32]{};SIZE_T got=0;
    return size<=sizeof(bytes)&&ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(image+rva),bytes,size,&got)&&got==size&&
        !std::memcmp(bytes,expected,size);
}

bool ReadSpan(std::uintptr_t at,void* output,std::size_t bytes) {
    SIZE_T got=0;
    return output&&at>=0x10000u&&bytes&&bytes<=0xffffffffu&&at<=0xffffffffu-bytes&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,bytes,&got)&&got==bytes;
}
bool ReticleGeometry(std::uintptr_t geometry,ReticlePacket& packet) {
    const auto stride=NativeReticleKLMath::CopiedStride(packet.material[3]);
    // CrossHairDisplay constructor25439E fixes +2C=1 with a zero-centred
    // 40-byte authored sprite at +38. The five-bar +64 meter is excluded.
    return stride&&ReadSpan(geometry,packet.header,sizeof(packet.header))&&packet.header[0]==1&&
        packet.header[2]&&ReadSpan(packet.header[2],packet.payload.data(),packet.header[0]*stride);
}
bool ReticleResource(std::uint32_t handle,ReticlePacket& packet) {
    std::uint32_t descriptor=0;
    if(!handle)return false;
    if(handle&0x80000000u)descriptor=handle&0x7fffffffu;
    else if(handle>0x100000u||!Read(image+0x71F308+std::uintptr_t(handle)*4,descriptor))return false;
    return ReadSpan(descriptor,packet.material,sizeof(packet.material))&&NativeReticleKLMath::CopiedStride(packet.material[3]);
}
bool CaptureReticle(std::uintptr_t entry,ReticlePacket packet) {
    std::uint32_t vt=0,material[5]{};
    if(!entry||!Read(entry,vt)||vt!=image+0x540EAC||!Read(image+0x7678A0,packet.slot)||packet.slot>1||
       !Read(entry+0x100+packet.slot*4,packet.geometry)||!packet.geometry||
       !ReadSpan(entry+0x108+packet.slot*20,material,sizeof(material))||std::memcmp(material,packet.material,sizeof(material))||
       !Read(entry+0x130+packet.slot*4,packet.stamp))return false;
    ReticlePacket actual;std::memcpy(actual.material,packet.material,sizeof(packet.material));
    if(!ReticleGeometry(packet.geometry,actual)||actual.header[0]!=packet.header[0]||actual.header[1]!=packet.header[1]||
       std::memcmp(actual.payload.data(),packet.payload.data(),packet.header[0]*NativeReticleKLMath::CopiedStride(packet.material[3])))return false;
    std::memcpy(packet.header,actual.header,sizeof(packet.header));packet.entry=entry;packet.tick=GetTickCount();
    AcquireSRWLockExclusive(&reticlePacketLock);auto* slot=&reticlePackets[0];
    for(auto& candidate:reticlePackets){
        if(candidate.entry==entry&&candidate.slot==packet.slot){slot=&candidate;break;}
        if(!candidate.entry||packet.tick-candidate.tick>packet.tick-slot->tick)slot=&candidate;
    }
    *slot=packet;ReleaseSRWLockExclusive(&reticlePacketLock);++reticleCaptured;return true;
}
bool MatchReticle(std::uintptr_t entry,std::uintptr_t& player) {
    std::uint32_t slot=2;if(!Read(image+0x7678A4,slot)||slot>1)return false;
    ReticlePacket packet;bool found=false;
    AcquireSRWLockShared(&reticlePacketLock);
    for(const auto& candidate:reticlePackets)if(candidate.entry==entry&&candidate.slot==slot){packet=candidate;found=true;break;}
    ReleaseSRWLockShared(&reticlePacketLock);
    const auto now=GetTickCount();
    if(!found||!packet.player||std::int32_t(now-packet.tick)<0||now-packet.tick>250)return false;
    std::uint32_t vt=0,geometry=0,stamp=0,material[5]{};
    ReticlePacket actual;std::memcpy(actual.material,packet.material,sizeof(packet.material));
    if(!Read(entry,vt)||vt!=image+0x540EAC||!Read(entry+0x100+slot*4,geometry)||geometry!=packet.geometry||
       !Read(entry+0x130+slot*4,stamp)||stamp!=packet.stamp||
       !ReadSpan(entry+0x108+slot*20,material,sizeof(material))||std::memcmp(material,packet.material,sizeof(material))||
       !ReticleGeometry(geometry,actual)||std::memcmp(actual.header,packet.header,sizeof(packet.header))||
       std::memcmp(actual.payload.data(),packet.payload.data(),packet.header[0]*NativeReticleKLMath::CopiedStride(packet.material[3])))return false;
    player=packet.player;return true;
}
NativeWorldMarkerMath::Layout MarkerLayout(const void* geometry,const void* position,std::uint32_t kind,NativeMarkerSourceKL::CanonicalQuad* canonical=nullptr) {
    return NativeMarkerSourceKL::Capture(image,nativeMarkerScope.camera,reinterpret_cast<std::uintptr_t>(geometry),reinterpret_cast<std::uintptr_t>(position),kind,
        [](std::uintptr_t at,void* output,std::size_t bytes){return ReadSpan(at,output,bytes);},canonical);
}
void ObserveMarkerSource(const MarkerPacket& packet){
    if(!NativeMarkerViewMath::Objective(packet.recordType))return;
    const auto now=GetTickCount();MarkerSourceEvidence next,previous;
    AcquireSRWLockShared(&markerEvidenceLock);previous=markerEvidence;ReleaseSRWLockShared(&markerEvidenceLock);
    next.record=packet.record;next.tick=now;next.type=packet.recordType;next.texture=packet.texture;next.caller=packet.caller;next.target=packet.target;
    next.width=packet.layout.width;next.height=packet.layout.height;next.offsetX=packet.layout.offsetX;next.offsetY=packet.layout.offsetY;next.sourceDepth=packet.layout.sourceDepth;
    next.sourceExtentValid=packet.layout.sourceExtentValid;next.sourceWidth=packet.layout.sourceWidth;next.sourceHeight=packet.layout.sourceHeight;
    next.sourceOffsetX=packet.layout.sourceOffsetX;next.sourceOffsetY=packet.layout.sourceOffsetY;
    std::uint32_t entity=0,geometry=0,vt=0,parent=0,owner=0;
    Read(packet.record,entity);next.entity=entity;Read(packet.record+0x4D,next.fixed);Read(packet.record+0x4F,next.bob);Read(packet.record+0x4C,next.timed);
    if(entity&&Read(entity,vt)&&Read(entity+4,geometry)){next.entityVtable=vt;next.geometry=geometry;Read(geometry+0x50,parent);next.parent=parent;}
    const bool same=previous.entity==next.entity&&previous.geometry==next.geometry&&previous.entityVtable==next.entityVtable;
    next.captures=previous.captures+1;next.maxPointStep=same?previous.maxPointStep:0;
    if(same&&previous.tick){const auto delta=NativeCameraMath::Subtract(next.target,previous.target);const auto distance=std::sqrt(NativeCameraMath::Dot(delta,delta));
        if(std::isfinite(distance))next.maxPointStep=std::max(next.maxPointStep,distance);}
    if(same&&previous.worldTick&&now-previous.worldTick<100){next.worldValid=previous.worldValid;next.worldTick=previous.worldTick;next.world=previous.world;}
    else {NativeCameraMath::CameraPose world;std::uint32_t afterGeometry=0,afterOwner=0;
        next.worldTick=now;next.worldValid=geometry&&Read(geometry+0x60,owner)&&owner==entity&&
            NativePlayerPose::World(geometry,[](auto at,void* out,auto size){return ReadSpan(at,out,size);},world)&&
            Read(entity+4,afterGeometry)&&afterGeometry==geometry&&Read(geometry+0x60,afterOwner)&&afterOwner==owner;
        if(next.worldValid)next.world=world.position;}
    AcquireSRWLockExclusive(&markerEvidenceLock);markerEvidence=next;ReleaseSRWLockExclusive(&markerEvidenceLock);
}
void CaptureMarker(std::uintptr_t entry,std::uintptr_t player,const void* geometry,const void* position,std::uint32_t texture,std::uint32_t caller) {
    MarkerPacket next;next.entry=entry;next.player=player;next.tick=GetTickCount();std::uint32_t vt=0;
    next.record=nativeMarkerScope.record;next.target=nativeMarkerScope.target;next.producer=nativeMarkerScope.producer;
    std::uint32_t recordType=0;std::uint8_t fixed=0;
    next.destination=Read(next.record+0x1C,recordType)&&Read(next.record+0x4D,fixed)&&fixed&&
        (recordType==7||recordType==21||recordType==27);
    next.recordType=recordType;next.texture=texture;next.caller=caller;
    next.fixed=fixed!=0;
    // Native marker record slots are reused for different mission goals.
    // Retain the represented entity and geometry with the captured packet,
    // so a stereo pair cannot inherit the previous occupant's world point.
    if(!fixed){
        Read(next.record,next.entity);
        if(next.entity)Read(next.entity+4,next.entityGeometry);
    }
    if(!nativeMarkerScope.valid||!entry||!player||!Read(entry,vt)||vt!=image+0x540EAC||!Read(image+0x7678A0,next.slot)||next.slot>1||
       !Read(entry+0x100+next.slot*4,next.geometry)||!next.geometry||!Read(entry+0x130+next.slot*4,next.stamp)||
       !ReadSpan(entry+0x108+next.slot*20,next.material,sizeof(next.material))){++nativeMarkerRejected;return;}
    next.layout=MarkerLayout(geometry,position,next.material[3],&next.canonical);if(next.layout.valid)++nativeMarkerCalibrated;else ++nativeMarkerCalibrationRejected;
    ObserveMarkerSource(next);
    TraceObjective(next,nullptr,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Emit),0);
    AcquireSRWLockExclusive(&markerPacketLock);auto* target=&markerPackets[0];
    for(auto& candidate:markerPackets){
        if(candidate.entry==entry&&candidate.slot==next.slot){
            if(candidate.stamp==next.stamp&&candidate.producer==next.producer&&
               (candidate.record!=next.record||std::memcmp(&candidate.target,&next.target,sizeof(next.target)))){
                next.ambiguous=true;if(!candidate.ambiguous)++nativeMarkerAmbiguous;
            } else next.ambiguous=candidate.ambiguous&&candidate.stamp==next.stamp&&candidate.producer==next.producer;
            target=&candidate;break;}
        if(!candidate.entry||next.tick-candidate.tick>next.tick-target->tick)target=&candidate;
    }
    *target=next;ReleaseSRWLockExclusive(&markerPacketLock);++nativeMarkerCaptured;
    if(next.destination)++nativeDestinationCaptured;
}
bool MatchMarker(std::uintptr_t entry,MarkerPacket& output) {
    std::uint32_t slot=2;if(!Read(image+0x7678A4,slot)||slot>1)return false;
    MarkerPacket packet;bool found=false;
    AcquireSRWLockShared(&markerPacketLock);
    for(const auto& candidate:markerPackets)if(candidate.entry==entry&&candidate.slot==slot){packet=candidate;found=true;break;}
    ReleaseSRWLockShared(&markerPacketLock);
    const auto now=GetTickCount();std::uint32_t vt=0,geometry=0,stamp=0,material[5]{};
    const bool valid=found&&packet.player&&packet.tick&&std::int32_t(now-packet.tick)>=0&&now-packet.tick<=250&&
        Read(entry,vt)&&vt==image+0x540EAC&&Read(entry+0x100+slot*4,geometry)&&geometry==packet.geometry&&
        Read(entry+0x130+slot*4,stamp)&&stamp==packet.stamp&&
        ReadSpan(entry+0x108+slot*20,material,sizeof(material))&&!std::memcmp(material,packet.material,sizeof(material));
    if(valid)output=packet;return valid;
}
void RetireSpritePackets(std::uintptr_t entry,bool marker=true,bool reticle=true){
    // A shared native sprite entry may be emitted again for an unrelated UI
    // image with the SAME geometry pointer/stamp/material. Its old WORLD or
    // reticle provenance ends at this producer boundary, not after250ms.
    if(!entry)return;std::uint32_t slot=2;if(!Read(image+0x7678A0,slot)||slot>1)return;
    if(marker&&nativeMarkerCaptured.load(std::memory_order_relaxed)){
        AcquireSRWLockExclusive(&markerPacketLock);for(auto& packet:markerPackets)if(packet.entry==entry&&packet.slot==slot){packet={};++spriteMarkerRetired;}ReleaseSRWLockExclusive(&markerPacketLock);}
    if(reticle&&reticleCaptured.load(std::memory_order_relaxed)){
        AcquireSRWLockExclusive(&reticlePacketLock);for(auto& packet:reticlePackets)if(packet.entry==entry&&packet.slot==slot){packet={};++spriteReticleRetired;}ReleaseSRWLockExclusive(&reticlePacketLock);}
}
bool WristEmitter(void* manager,std::uint32_t caller,unsigned type,const void* geometry) {
    if(wristSource.renderer!=manager||!wristSource.player)return false;
    const auto relative=reinterpret_cast<std::uintptr_t>(geometry)-wristSource.display;
    switch(wristSource.element){
    case WristHudElement::Team:
        return (caller==0x25D879&&type>=10&&type<=17)||(caller==0x161467&&type==15);
    case WristHudElement::Minimap:
        return (caller==0x266D35&&type==13&&relative==0x900)||
               (caller==0x25A427&&type>=6&&type<=9)||(wristSource.caption&&caller==0x161467&&type==12);
    case WristHudElement::Weapon:
        // The compact inventory scope has already selected slot zero.
        return (caller==0x25A427&&type>=6&&type<=9&&relative==0x480+(type-6)*12)||
               (caller==0x161467&&type==12);
    case WristHudElement::WeaponWheel:
        // Both native inventory rings emit their own quads. The scope is
        // active only around the qualified expanded inventory draw.
        return caller==0x25A868||caller==0x25A88F||caller==0x25A98B||
               caller==0x25AE11||caller==0x25AF0B||caller==0x161467;
    case WristHudElement::Action:case WristHudElement::ClimbAction:
        return (caller==0x25C24B&&type==11&&relative==0x24)||caller==0x161467;
    case WristHudElement::Instruction:case WristHudElement::Speech:
        return caller==0x161467;
    default:return false;
    }
}
void ObserveWristSprite(std::uintptr_t entry,bool capture,unsigned type,const void* position) {
    if(!entry)return;
    const auto last=lastWristSprite.load(std::memory_order_relaxed);
    if(!capture&&!last)return;
    const auto now=GetTickCount();
    if(!capture&&now-last>wristPacketLifetime)return;
    WristSprite next;
    if(!Read(image+0x7678A0,next.slot)||next.slot>1)return;
    std::uint32_t vt=0;
    if(capture&&Read(entry,vt)&&vt==image+0x540EAC&&
       Read(entry+0x100+next.slot*4,next.geometry)&&next.geometry&&
       Read(entry+0x130+next.slot*4,next.stamp)&&
       ReadSpan(entry+0x108+next.slot*20,next.material,sizeof(next.material))&&
       (next.material[3]==5||next.material[3]==6)){
        next.entry=entry;next.type=type;next.tick=now;next.source=wristSource;
        if(wristSource.element==WristHudElement::Weapon&&type==12){
            NativeCameraMath::Vec3 glyph;
            // The firearm and grenade rows share an emitter. The firearm
            // row is authored just above the centre of the circular widget.
            next.weaponAmmo=Read(reinterpret_cast<std::uintptr_t>(position),glyph)&&
                NativeCameraMath::Finite(glyph)&&glyph.y>wristSource.anchor.y;
        }
    }
    AcquireSRWLockExclusive(&wristSpriteLock);
    if(next.entry&&(!lastWristPrune||now-lastWristPrune>=wristPacketLifetime)){
        for(auto& slot:wristSprites){
            for(auto sprite=slot.begin();sprite!=slot.end();){
                if(now-sprite->second.tick>wristPacketLifetime)sprite=slot.erase(sprite);
                else ++sprite;
            }
        }
        lastWristPrune=now;
    }
    // The render worker can still consume the other slot. Keep every
    // live entry until native reuse or expiry, regardless of hash collisions.
    auto& sprites=wristSprites[next.slot];
    if(next.entry){
        try {sprites.insert_or_assign(entry,next);}
        catch(...) {sprites.erase(entry);next.entry=0;}
    }else{
        sprites.erase(entry);
    }
    ReleaseSRWLockExclusive(&wristSpriteLock);
    if(next.entry)lastWristSprite.store(now,std::memory_order_relaxed);
}
WristSprite MatchWristSprite(std::uintptr_t entry) {
    const auto now=GetTickCount(),last=lastWristSprite.load(std::memory_order_relaxed);
    std::uint32_t slot=2;
    if(!entry||!last||now-last>wristPacketLifetime||!Read(image+0x7678A4,slot)||slot>1)return {};
    WristSprite next;
    AcquireSRWLockShared(&wristSpriteLock);
    const auto& sprites=wristSprites[slot];
    const auto found=sprites.find(entry);
    if(found!=sprites.end())next=found->second;
    ReleaseSRWLockShared(&wristSpriteLock);
    std::uint32_t geometry=0,stamp=0,material[5]{};
    if(!next.entry||now-next.tick>wristPacketLifetime||
       !Read(entry+0x100+slot*4,geometry)||geometry!=next.geometry||
       !Read(entry+0x130+slot*4,stamp)||stamp!=next.stamp||
       !ReadSpan(entry+0x108+slot*20,material,sizeof(material))||std::memcmp(material,next.material,sizeof(material)))return {};
    return next;
}
void* __fastcall MarkerPositionHook(void* record,void*,void* output) {
    void* result=originalMarkerPosition(record,output);
    if(nativeMarkerScope.player) {
        nativeMarkerScope.valid=false;NativeCameraMath::Vec3 position;
        if(Read(reinterpret_cast<std::uintptr_t>(output),position)&&NativeCameraMath::Finite(position)) {
            std::uint32_t type=0,afterType=0;std::uint8_t fixed=0,bob=0;NativeCameraMath::Vec3 base{},after{};
            const auto at=reinterpret_cast<std::uintptr_t>(record);
            if(Read(at+0x1C,type)&&NativeMarkerViewMath::Objective(type)&&Read(at+0x4D,fixed)&&Read(at+0x4F,bob)&&!fixed&&bob&&
               Read(at+4,base)&&NativeCameraMath::Finite(base)&&Read(at+0x1C,afterType)&&afterType==type&&Read(at+4,after)&&
               !std::memcmp(&base,&after,sizeof(base))){position=base;++nativeObjectiveBobSuppressed;}
            nativeMarkerScope.record=reinterpret_cast<std::uintptr_t>(record);nativeMarkerScope.target=position;nativeMarkerScope.valid=true;
        }
    }
    return result;
}
void RecordSpriteProducer(std::uintptr_t entry,void* manager,std::uint32_t caller,std::uint32_t texture,const void* geometry,
                          const void* transform,const void* position,std::uint32_t type,std::uint32_t flags){
    // Lean registry survives producer/GPU buffering. No guessed object names
    // or arbitrary memory walks: these are the exact native emitter inputs.
    if(!entry||!visualSourceActive.load(std::memory_order_relaxed))return;
    const auto borrowed=[](std::uintptr_t at,void* out,std::size_t bytes){
        if(at<0x10000u||at>0xffffffffu-bytes)return false;
        std::memcpy(out,reinterpret_cast<const void*>(at),bytes);return true;};
    NativeEffectsKL::SpriteProducerEvidence next;std::uint32_t vt=0,stored=0;
    if(!borrowed(entry,&vt,4)||vt!=image+0x540EAC||!borrowed(image+0x7678A0,&next.slot,4)||next.slot>1||
       !borrowed(entry+0x100+next.slot*4,&stored,4)||!borrowed(entry+0x130+next.slot*4,&next.stamp,4)||
       !borrowed(entry+0x108+next.slot*20,next.header,sizeof(next.header)))return;
    next.valid=true;next.entry=entry;next.manager=reinterpret_cast<std::uintptr_t>(manager);next.callerRva=caller;
    next.tick=GetTickCount();next.textureHandle=texture;next.argGeometry=reinterpret_cast<std::uintptr_t>(geometry);
    next.storedGeometry=stored;next.emitType=type;next.flags=flags;
    next.transformKnown=transform&&borrowed(reinterpret_cast<std::uintptr_t>(transform),next.transform,sizeof(next.transform));
    next.positionKnown=position&&borrowed(reinterpret_cast<std::uintptr_t>(position),next.position,sizeof(next.position));
    const auto first=((entry>>4)^(entry>>12)^next.slot)&(spriteProducers.size()-1);
    AcquireSRWLockExclusive(&spriteProducerLock);auto* target=&spriteProducers[first];
    for(unsigned i=0;i<8;++i){auto& candidate=spriteProducers[(first+i)&(spriteProducers.size()-1)];
        if(!candidate.valid||(candidate.entry==entry&&candidate.slot==next.slot)){target=&candidate;break;}
        if(candidate.sequence<target->sequence)target=&candidate;}
    next.sequence=++spriteProducerSequence;*target=next;ReleaseSRWLockExclusive(&spriteProducerLock);
}
std::uintptr_t __fastcall SpriteEmitHook(void* manager,void*,std::uint32_t texture,const void* geometry,const void* transform,
                                       const void* position,std::uint32_t type,std::uint32_t flags,std::uint32_t extra) {
    const auto caller=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-image);
    const bool wrist=WristEmitter(manager,caller,type,geometry);
    if(wristSource.player&&wristSource.renderer==manager&&!wrist&&
       (wristSource.element==WristHudElement::Weapon||wristSource.element==WristHudElement::WeaponWheel))return 0;
    if(wristSource.player&&wristSource.renderer==manager&&wristSource.element==WristHudElement::Minimap&&
       caller==0x161467&&type==12&&!wristSource.caption)return 0;
    std::uint32_t vt=0;
    if(reticleEnabled.load(std::memory_order_relaxed)&&reticleEmitScope.player&&
       reticleEmitScope.manager==reinterpret_cast<std::uintptr_t>(manager)&&NativeReticleKLMath::Emitter(caller,type)&&
       Read(reinterpret_cast<std::uintptr_t>(manager),vt)&&vt==image+0x53D994) {
        ReticlePacket packet;packet.player=reticleEmitScope.player;
        if(!ReticleResource(texture,packet)||!ReticleGeometry(reinterpret_cast<std::uintptr_t>(geometry),packet)){
            ++reticleRejected;return 0;
        }
        const auto entry=NativeTracerVisualKL::ForwardSpriteEmission(reinterpret_cast<std::uintptr_t>(manager),caller,type,flags,extra,transform,position,
            [&]{return originalSpriteEmit(manager,texture,geometry,transform,position,type,flags,extra);});
        RecordSpriteProducer(entry,manager,caller,texture,geometry,transform,position,type,flags);
        ObserveWristSprite(entry,wrist,type,position);
        RetireSpritePackets(entry);
        if(!CaptureReticle(entry,packet))++reticleRejected;
        return entry;
    }
    const auto entry=NativeTracerVisualKL::ForwardSpriteEmission(reinterpret_cast<std::uintptr_t>(manager),caller,type,flags,extra,transform,position,
        [&]{return originalSpriteEmit(manager,texture,geometry,transform,position,type,flags,extra);});
    ObserveWristSprite(entry,wrist,type,position);
    RecordSpriteProducer(entry,manager,caller,texture,geometry,transform,position,type,flags);
    const bool marker=nativeMarkerScope.player&&nativeMarkerScope.manager==reinterpret_cast<std::uintptr_t>(manager)&&type==10&&caller>=0x253B90&&caller<=0x253FB9;
    // Keep same-producer marker duplicates for CaptureMarker's ambiguity
    // rejection; only a different producer revokes that marker ownership.
    RetireSpritePackets(entry,!marker);
    if(marker){++nativeMarkerEmitted;CaptureMarker(entry,nativeMarkerScope.player,geometry,position,texture,caller);}
    return entry;
}
std::uintptr_t __fastcall SpriteDrawHook(void* binder,void*,void* state) {
    const auto previousWrist=wristDrawing;
    wristDrawing=nullptr;
    std::optional<WristSprite> drawnWrist;
    struct RestoreWrist {const WristSprite* saved;~RestoreWrist(){wristDrawing=saved;}} restoreWrist{previousWrist};
    const auto previousVisual=spriteVisualScope;spriteVisualScope=nullptr;
    struct RestoreVisual {NativeEffectsKL::VisualSourceDiagnostic* saved;~RestoreVisual(){spriteVisualScope=saved;}} restoreVisual{previousVisual};
    std::optional<NativeEffectsKL::VisualSourceDiagnostic> visualStorage;
    const auto previousParticle=particleDrawContext;particleDrawContext={};
    struct RestoreParticle {MappedVertexContext saved;~RestoreParticle(){particleDrawContext=saved;}} restoreParticle{previousParticle};
    NativeEffectsKLProducer::SpriteSourceInfo effectSource;
    const bool effectKnown=mappedVertexFilter.load(std::memory_order_relaxed)&&
        NativeEffectsKLProducer::SpriteSource(image,reinterpret_cast<std::uintptr_t>(binder),reinterpret_cast<std::uintptr_t>(state),
            [](auto at,auto& out){return Read(at,out);},effectSource);
    ++spriteSourceTypes[effectKnown&&effectSource.type<8?effectSource.type:8];
    if(effectKnown&&effectSource.type==7){
        particleDrawContext.sourceState=reinterpret_cast<std::uintptr_t>(state);particleDrawContext.particleMode=effectSource.mode;
        particleDrawContext.sourceEntry=effectSource.entry;particleDrawContext.sourceGeometry=effectSource.geometry;
        particleDrawContext.spriteType=7;particleDrawContext.spriteDrawId=++worldSpriteDrawSequence;
    }
    const auto previous=reticleDrawScope;reticleDrawScope={};
    struct Restore {ReticleDrawScope previous;~Restore(){reticleDrawScope=previous;}} restore{previous};
    std::uint32_t vt=0,entry=0;std::uintptr_t player=0;
    const auto source=reinterpret_cast<std::uintptr_t>(binder);
    const bool nativeSprite=Read(source,vt)&&vt==image+0x53FB88&&Read(source+0xC,entry);
    const auto lastWrist=lastWristSprite.load(std::memory_order_relaxed);
    if(nativeSprite&&lastWrist&&GetTickCount()-lastWrist<=wristPacketLifetime){
        drawnWrist=MatchWristSprite(entry);
        if(drawnWrist->entry)wristDrawing=&*drawnWrist;
    }
    if(nativeSprite&&reticleEnabled.load(std::memory_order_relaxed)&&MatchReticle(entry,player)){
        reticleDrawScope.player=player;++reticleRouted;
    }
    const bool markerPrevious=nativeMarkerDrawing;const auto previousPacket=nativeMarkerDrawPacket;
    const auto previousDraw=markerDrawId,previousCanonical=markerCanonicalizedDrawId;
    markerDrawId=markerCanonicalizedDrawId=0;
    nativeMarkerDrawing=nativeSprite&&MatchMarker(entry,nativeMarkerDrawPacket);
    struct RestoreMarker {bool saved;MarkerPacket packet;std::uint64_t draw,canonical;~RestoreMarker(){nativeMarkerDrawing=saved;nativeMarkerDrawPacket=packet;markerDrawId=draw;markerCanonicalizedDrawId=canonical;}} restoreMarker{markerPrevious,previousPacket,previousDraw,previousCanonical};
    if(nativeMarkerDrawing){++nativeMarkerRouted;markerDrawId=++markerDrawSequence;}
    if(effectKnown&&(effectSource.type==5||effectSource.type==6)){
        if(effectSource.type==5)++worldSprite5Sources;else ++worldSprite6Sources;
        if(effectSource.cameraRelative)++worldSpriteRelativePreserved;
        else if(nativeMarkerDrawing||reticleDrawScope.player||wristDrawing||OriginalScopeOverlayActive())++worldSpriteHudPreserved;
        else {
            particleDrawContext.sourceState=reinterpret_cast<std::uintptr_t>(state);particleDrawContext.particleMode=0;
            particleDrawContext.sourceEntry=effectSource.entry;particleDrawContext.sourceGeometry=effectSource.geometry;particleDrawContext.spriteType=effectSource.type;
            particleDrawContext.spriteRecords=effectSource.records;particleDrawContext.spriteCount=effectSource.count;
            particleDrawContext.spriteDrawId=++worldSpriteDrawSequence;
        }
    }
    if(effectKnown){
        Stats::WorldSpriteSourceSnapshot evidence;evidence.valid=true;evidence.tick=GetTickCount();
        evidence.type=effectSource.type;evidence.flags=effectSource.flags;evidence.slot=effectSource.slot;
        evidence.entry=effectSource.entry;evidence.geometry=effectSource.geometry;evidence.records=effectSource.records;
        evidence.count=effectSource.count;evidence.sourceState=reinterpret_cast<std::uintptr_t>(state);
        evidence.drawScopeId=particleDrawContext.spriteDrawId;
        evidence.reason=(effectSource.type==5||effectSource.type==6)&&particleDrawContext.spriteDrawId?0u:effectSource.cameraRelative?1u:
            (effectSource.type==5||effectSource.type==6)?2u:3u;
        if((effectSource.type==5||effectSource.type==6)&&effectSource.records&&effectSource.count){
            NativeCameraMath::Vec3 anchor;
            if(Read(effectSource.records,anchor)&&NativeCameraMath::Finite(anchor)){
                evidence.anchorKnown=true;evidence.anchor[0]=anchor.x;evidence.anchor[1]=anchor.y;evidence.anchor[2]=anchor.z;
            }
        }
        AcquireSRWLockExclusive(&worldSpriteSourceLock);evidence.sequence=++worldSpriteSourceSequence;
        if(evidence.reason)lastWorldSpritePreserved=evidence;else lastWorldSpriteEligible=evidence;
        ReleaseSRWLockExclusive(&worldSpriteSourceLock);
    }
    if(effectKnown&&VisualSourceActive()){
        visualStorage.emplace();auto& visual=*visualStorage;spriteVisualScope=&visual;visual.valid=true;visual.kind=1;visual.entry=effectSource.entry;
        visual.state=reinterpret_cast<std::uintptr_t>(state);visual.type=effectSource.type;visual.flags=effectSource.flags;
        visual.slot=effectSource.slot;visual.mode=effectSource.mode;visual.geometry=effectSource.geometry;
        visual.entryVtable=image+0x540EAC;visual.records=effectSource.records;visual.allocatedQuads=visual.sourceCount=effectSource.count;
        visual.drawScopeId=particleDrawContext.spriteDrawId;visual.tick=GetTickCount();
        Read(visual.entry+0x130+visual.slot*4,visual.stamp);
        if(visual.type==7){std::uint32_t count=0,records=0;
            if(Read(visual.geometry,count)&&count<=0x100000&&Read(visual.geometry+8,records)){
                visual.allocatedQuads=visual.sourceCount=count;visual.records=records;}}
    }
    return originalSpriteDraw(binder,state);
}

void RegisterWrite(void* control,void* mapped,UINT count,UINT stride,UINT* first,
                   NativeHudShadow::Kind kind,std::uintptr_t caller) {
    ProfileTiming::Scope profile(ProfileTiming::HudShadowRegister);
    ++nativeLocks;
    const auto at=reinterpret_cast<std::uintptr_t>(control);
    const std::uint64_t bytes=std::uint64_t(count)*stride;
    NativeBufferControl snapshot;std::uint32_t firstIndex=0;
    if(!control||!mapped||!first||!stride||!count||bytes>NativeHudShadow::Cache::MaximumResourceBytes){RejectShadow();return;}
    // The original, signature-pinned Lock has just used this control and
    // written *first. Both remain borrowed native objects until its Unlock.
    std::memcpy(&snapshot,control,sizeof(snapshot));std::memcpy(&firstIndex,first,sizeof(firstIndex));
    if(!snapshot.resource||snapshot.mapped!=reinterpret_cast<std::uintptr_t>(mapped)) {
        RejectShadow();return;
    }
    const auto resourceAt=snapshot.resource,mappedAt=snapshot.mapped,capacity=snapshot.capacity,end=snapshot.end;
    const std::uint64_t offset=std::uint64_t(firstIndex)*stride;
    if(offset+bytes!=end||end>capacity){RejectShadow();return;}
    const auto provenance=static_cast<UINT>(caller-image);
    MappedVertexContext worldSpriteContext;
    const auto& sourceContext=particleDrawContext;
    const bool worldSpriteWriter=kind==NativeHudShadow::Kind::Vertex&&stride==24&&count%4==0&&count<=2048*4&&
        ((provenance==0x99551&&sourceContext.spriteType==5)||(provenance==0x996BF&&sourceContext.spriteType==6))&&
        sourceContext.sourceState&&sourceContext.sourceEntry&&sourceContext.spriteRecords&&
        sourceContext.spriteFirst<=sourceContext.spriteCount&&count/4<=sourceContext.spriteCount-sourceContext.spriteFirst;
    if(worldSpriteWriter){worldSpriteContext=sourceContext;particleDrawContext.spriteFirst+=count/4;}
    if(!NativeHudShadow::CaptureWriter(kind,provenance,stride)){
        ++shadowSkippedWrites;
        AcquireSRWLockExclusive(&shadowLock);
        for(auto& candidate:pending)if(candidate.allocation.identity.control==at)candidate={};
        if(shadow.ObserveUncaptured(at,resourceAt,capacity,static_cast<UINT>(offset),static_cast<UINT>(bytes),firstIndex==0))++shadowTrackedInvalidations;
        const bool effectsWriter=(provenance==0x99360&&stride==24)||(provenance==0x9997E&&stride==24)||
            (provenance==0xA37D3&&stride==28);
        if(kind==NativeHudShadow::Kind::Vertex&&effectsWriter&&count%4==0&&count<=2048*4&&mappedVertexFilter.load()) {
            for(auto& candidate:pending)if(!candidate.token&&!candidate.effects) {
                candidate.allocation={{resourceAt,0,at,capacity,kind},static_cast<UINT>(offset),static_cast<UINT>(bytes),firstIndex==0,provenance,GetTickCount()};
                candidate.mapped=mappedAt;candidate.effects=true;
                if(provenance==0x9997E)candidate.effectsContext=particleDrawContext;
                break;
            }
        }
        RefreshPending();ReleaseSRWLockExclusive(&shadowLock);return;
    }
    auto* resource=reinterpret_cast<IDirect3DResource9*>(resourceAt);
    IDirect3DDevice9* device=nullptr;
    if(FAILED(resource->GetDevice(&device))||!device){RejectShadow();return;}
    const auto deviceAt=reinterpret_cast<std::uintptr_t>(device);device->Release();
    bool descriptor=false;
    if(kind==NativeHudShadow::Kind::Vertex) {
        D3DVERTEXBUFFER_DESC desc{};
        descriptor=SUCCEEDED(reinterpret_cast<IDirect3DVertexBuffer9*>(resource)->GetDesc(&desc))&&
            desc.Type==D3DRTYPE_VERTEXBUFFER&&desc.Size==capacity;
    } else {
        D3DINDEXBUFFER_DESC desc{};
        descriptor=SUCCEEDED(reinterpret_cast<IDirect3DIndexBuffer9*>(resource)->GetDesc(&desc))&&
            desc.Type==D3DRTYPE_INDEXBUFFER&&desc.Size==capacity&&
            desc.Format==(kind==NativeHudShadow::Kind::Index16?D3DFMT_INDEX16:D3DFMT_INDEX32);
    }
    if(!descriptor){RejectShadow();return;}
    PendingWrite write;
    write.allocation={{resourceAt,deviceAt,at,capacity,kind},static_cast<UINT>(offset),static_cast<UINT>(bytes),
                      firstIndex==0,provenance,GetTickCount()};
    write.mapped=mappedAt;
    if(wristDrawing&&wristDrawing->source.element==WristHudElement::Weapon&&
       kind==NativeHudShadow::Kind::Vertex&&stride==sizeof(NativeHudMath::Vertex)&&count%4==0&&count<=64&&
       provenance==(wristDrawing->type==12?0x99551u:0x996BFu)){
        write.wristWeaponEntry=wristDrawing->entry;
    }
    if(nativeMarkerDrawing&&markerDrawId&&nativeMarkerDrawPacket.canonical.valid&&kind==NativeHudShadow::Kind::Vertex&&count==4&&stride==sizeof(NativeHudMath::Vertex)&&
       ((provenance==0x99551&&nativeMarkerDrawPacket.canonical.kind==5)||(provenance==0x996BF&&nativeMarkerDrawPacket.canonical.kind==6))){
        write.markerDraw=markerDrawId;write.marker=nativeMarkerDrawPacket;
    }
    if(worldSpriteWriter){write.effects=true;write.effectsContext=worldSpriteContext;}
    // Other vertex layouts must still invalidate the bytes they overwrite.
    write.capture=kind!=NativeHudShadow::Kind::Vertex||stride==sizeof(NativeHudMath::Vertex);
    AcquireSRWLockExclusive(&shadowLock);
    write.token=shadow.Begin(write.allocation);
    PendingWrite* slot=nullptr;
    for(auto& candidate:pending)if(candidate.allocation.identity.control==at){slot=&candidate;break;}
    if(!slot)for(auto& candidate:pending)if(!candidate.token&&!candidate.effects){slot=&candidate;break;}
    // The bounded CPU lease is independent of the optional HUD mirror's
    // resource-size/cache budget. A large native VB must not disable an
    // otherwise proven world-sprite write.
    if(slot&&(write.token||write.effects))*slot=write;else ++shadowRejected;
    RefreshPending();
    ReleaseSRWLockExclusive(&shadowLock);
}
void* __fastcall VertexLockHook(void* control,void*,UINT count,UINT stride,UINT* first) {
    void* mapped=nullptr;
    {ProfileTiming::Scope native(ProfileTiming::NativeVertexLock);mapped=originalVertexLock(control,count,stride,first);}
    RegisterWrite(control,mapped,count,stride,first,NativeHudShadow::Kind::Vertex,
                  reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));return mapped;
}
void* __fastcall Index16LockHook(void* control,void*,UINT count,UINT* first) {
    void* mapped=nullptr;
    {ProfileTiming::Scope native(ProfileTiming::NativeIndexLock);mapped=originalIndex16Lock(control,count,first);}
    RegisterWrite(control,mapped,count,2,first,NativeHudShadow::Kind::Index16,
                  reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));return mapped;
}
void* __fastcall Index32LockHook(void* control,void*,UINT count,UINT* first) {
    void* mapped=nullptr;
    {ProfileTiming::Scope native(ProfileTiming::NativeIndexLock);mapped=originalIndex32Lock(control,count,first);}
    RegisterWrite(control,mapped,count,4,first,NativeHudShadow::Kind::Index32,
                  reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));return mapped;
}
void NativeBufferUnlock(void* control) {
    ProfileTiming::Scope native(ProfileTiming::NativeBufferUnlock);originalBufferUnlock(control);
}
bool CanonicalizeMarkerWrite(const PendingWrite& write) {
    if(!write.markerDraw)return true;
    if(markerDrawId==write.markerDraw)markerCanonicalizedDrawId=0;
    const auto reject=[](){++markerCanonicalRejected;return false;};
    const auto& source=write.marker;const auto& current=nativeMarkerDrawPacket;
    MarkerPacket live;
    if(!nativeMarkerDrawing||markerDrawId!=write.markerDraw||!source.canonical.valid||
       source.entry!=current.entry||source.slot!=current.slot||source.stamp!=current.stamp||source.producer!=current.producer||
       source.geometry!=current.geometry||source.record!=current.record||source.player!=current.player||
       !MatchMarker(source.entry,live)||live.producer!=source.producer||live.slot!=source.slot||live.stamp!=source.stamp||
       live.geometry!=source.geometry||live.record!=source.record||live.player!=source.player||
       write.allocation.bytes!=4*sizeof(NativeHudMath::Vertex)||write.allocation.identity.kind!=NativeHudShadow::Kind::Vertex||
       !write.mapped||((source.canonical.kind!=5||write.allocation.provenance!=0x99551)&&
                      (source.canonical.kind!=6||write.allocation.provenance!=0x996BF)))return reject();
    auto* vertices=reinterpret_cast<NativeHudMath::Vertex*>(write.mapped);
    float magnitude=0,minX=0,maxX=0,minY=0,maxY=0;bool finiteXY=true;
    for(unsigned i=0;i<4;++i){const auto& p=vertices[i];
        // Sprite.fx also consumes input Z for fog. Keep that native value;
        // only XY take part in the cancellation-prone billboard expansion.
        if(!std::isfinite(p.z)){++markerCanonicalNonfinite;return reject();}
        finiteXY=finiteXY&&std::isfinite(p.x)&&std::isfinite(p.y);
        if(std::isfinite(p.x))magnitude=std::max(magnitude,std::fabs(p.x));
        if(std::isfinite(p.y))magnitude=std::max(magnitude,std::fabs(p.y));
        if(!i){minX=maxX=p.x;minY=maxY=p.y;}
        else{minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
    }
    if(!finiteXY)++markerCanonicalNonfinite;
    markerCanonicalLastMagnitude=magnitude;
    auto previous=markerCanonicalMaxMagnitude.load(std::memory_order_relaxed);
    while(previous<magnitude&&!markerCanonicalMaxMagnitude.compare_exchange_weak(previous,magnitude,std::memory_order_relaxed)){}
    markerCanonicalSpanX=finiteXY?maxX-minX:0;markerCanonicalSpanY=finiteXY?maxY-minY:0;
    for(unsigned i=0;i<4;++i){vertices[i].x=source.canonical.corners[i].x;vertices[i].y=source.canonical.corners[i].y;}
    markerCanonicalizedDrawId=markerDrawId;++markerCanonicalized;return true;
}
void KeepWristWeaponVisible(const PendingWrite& write){
    if(!write.wristWeaponEntry||!wristDrawing||wristDrawing->entry!=write.wristWeaponEntry||
       wristDrawing->source.element!=WristHudElement::Weapon)return;
    // The compact inventory fades its own colors on a timer. Use a readable
    // alpha on this wrist draw only, retaining the native atlas, UVs and tint.
    auto* vertices=reinterpret_cast<NativeHudMath::Vertex*>(write.mapped);
    for(unsigned i=0;i<write.allocation.bytes/sizeof(*vertices);++i){
        auto& colour=vertices[i].colour;
        const auto alpha=colour>>24;
        if(alpha>=127)continue;
        if(!alpha){colour=0x7f000000u|(colour?colour:0x007f7f7fu);continue;}
        const auto channel=[&](unsigned shift){
            const auto value=(colour>>shift)&0xffu;
            return std::min(255u,value*127u/alpha);
        };
        colour=0x7f000000u|(channel(16)<<16)|(channel(8)<<8)|channel(0);
    }
}
void __fastcall BufferUnlockHook(void* control,void*) {
    const auto caller=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-image);
    const auto nativeFrame=reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    ProfileTiming::Scope profile(ProfileTiming::HudShadowUnlock);
    ++nativeUnlocks;PendingWrite write;
    if(!pendingWrites.load()){profile.Stop();NativeBufferUnlock(control);return;}
    const auto at=reinterpret_cast<std::uintptr_t>(control);
    AcquireSRWLockExclusive(&shadowLock);
    for(auto& candidate:pending)if((candidate.token||candidate.effects)&&candidate.allocation.identity.control==at) {
        write=candidate;candidate={};break;
    }
    RefreshPending();
    ReleaseSRWLockExclusive(&shadowLock);
    if(write.effects||(write.token&&write.capture)) {
        NativeBufferControl snapshot;
        std::memcpy(&snapshot,control,sizeof(snapshot));
        const bool captured=snapshot.resource==write.allocation.identity.resource&&
            snapshot.mapped==write.mapped&&snapshot.end==write.allocation.offset+write.allocation.bytes&&
            snapshot.capacity==write.allocation.identity.capacity&&snapshot.end<=snapshot.capacity;
        bool committed=false;
        if(captured&&write.effects) {
            const auto filter=mappedVertexFilter.load();
            if(filter){
                auto context=write.effectsContext;
                if((context.spriteType==5||context.spriteType==6)&&
                   (!context.spriteDrawId||context.spriteDrawId!=particleDrawContext.spriteDrawId||
                    context.sourceEntry!=particleDrawContext.sourceEntry||context.spriteRecords!=particleDrawContext.spriteRecords))
                    context.spriteRecords=0; // Never dereference a source record after its native draw scope.
                if(write.allocation.provenance==0x9997E){
                    context.liveKnown=NativeEffectsKLProducer::ParticlePrefix(caller,nativeFrame,
                        write.allocation.bytes/(4*24),[](auto at,auto& out){return Read(at,out);},context.liveQuads);
                }else if(write.allocation.provenance==0xA37D3){
                    NativeEffectsKLProducer::ManualState(caller,nativeFrame,[](auto at,auto& out){return Read(at,out);},context.sourceState);
                }
                const auto previous=mappedCallbackContext;mappedCallbackContext=context;
                filter(write.allocation.provenance,reinterpret_cast<void*>(write.mapped),write.allocation.bytes);
                mappedCallbackContext=previous;
            }
        }
        if(captured&&write.token&&write.capture) {
            CanonicalizeMarkerWrite(write);
            KeepWristWeaponVisible(write);
            AcquireSRWLockExclusive(&shadowLock);
            // The native Lock still owns the source until original Unlock.
            // Copy directly into the bounded mirror after identity/token
            // validation, without a second scratch allocation and memcpy.
            committed=shadow.Capture(write.allocation,write.token,[&](unsigned char* destination,std::size_t bytes){
                SIZE_T got=0;
                return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(write.mapped),
                                         destination,bytes,&got)&&got==bytes;
            });
            ReleaseSRWLockExclusive(&shadowLock);
        }
        if(write.token&&write.capture) {
            if(committed){++shadowCaptured;shadowCopiedBytes+=write.allocation.bytes;}else ++shadowRejected;
        }
    }
    profile.Stop();NativeBufferUnlock(control);
}
bool ReadShadow(std::uintptr_t resource,IDirect3DDevice9* device,NativeHudShadow::Kind kind,
                UINT capacity,UINT offset,UINT count,std::vector<unsigned char>& output,UINT* writer=nullptr) {
    UINT tick=0,provenance=0;
    AcquireSRWLockShared(&shadowLock);
    const bool found=shadow.Read(resource,reinterpret_cast<std::uintptr_t>(device),kind,capacity,offset,count,
                                 output,&provenance,&tick,GetTickCount(),250);
    ReleaseSRWLockShared(&shadowLock);
    if(!found){++shadowMisses;return false;}
    if(writer)*writer=provenance;
    ++shadowReads;return true;
}

void __fastcall LetterboxDrawHook(void* display,void*,void* renderer,void* parameter) {
    ++letterboxCalls;
    const auto at=reinterpret_cast<std::uintptr_t>(display);
    const auto provider=letterboxProvider.load();
    const bool firstPerson=provider&&provider();
    std::uint32_t vtable=0,count=0,records=0,owner=0;float amount=0.f;
    // This class owns precisely the native top/bottom cinema bands. Its
    // constructor stores two records inline; no colour/width classification
    // is applied to unrelated UI images, subtitles or black HUD panels.
    const bool exact=Read(at,vtable)&&vtable==image+0x575AF4&&
        Read(at+8,owner)&&owner>=0x10000&&Read(at+0x1C,count)&&count==2&&
        Read(at+0x24,records)&&records==at+0x28&&Read(at+0x9C,amount);
    NativeCinematicKL::State cinematic;
    if(firstPerson&&exact)cinematic=NativeCinematicKL::Snapshot(image,[](std::uintptr_t address,void* output,std::size_t size) {
        SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&got)&&got==size;
    });
    lastLetterbox=at;lastLetterboxFirstPerson=firstPerson;lastLetterboxAmount=amount;
    lastLetterboxMini=cinematic.mini;lastLetterboxFull=cinematic.full;
    const bool stereoFull=firstPerson&&cinematic.full&&NativeCameraKL::CinematicCamera()&&
        !NativeCameraKL::CinematicPanelActive();
    if(letterboxState.Step(at,owner,exact,firstPerson,cinematic,amount,GetTickCount(),stereoFull)) {
        ++letterboxHidden;return;
    }
    ++letterboxRejected;originalLetterboxDraw(display,renderer,parameter);
}

bool __fastcall DrawHook(void* manager,void*,void* state) {
    ++calls;
    const auto saved=scope;scope={};
    struct Restore {Scope previous;~Restore(){scope=previous;}} restore{saved};
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    std::uint32_t vt=0;
    float projection[16]{};
    Frame frame;
    if(!saved.active&&caller==image+0xB5CD7&&manager&&state&&provide&&
       Read(reinterpret_cast<std::uintptr_t>(manager),vt)&&vt==image+0x540D3C&&
       Read(reinterpret_cast<std::uintptr_t>(state)+0x3F8,projection)) {
        AcquireSRWLockExclusive(&diagnosticLock);
        std::memcpy(lastProjection,projection,sizeof(projection));
        ReleaseSRWLockExclusive(&diagnosticLock);
        const bool screen=NativeHudMath::ScreenProjection(projection);
        // The renderer owns the complete scope, not the CPU layout thread.
        // A projection alone never authorizes a draw. The actual UI depth
        // state and source plane are validated at the indexed boundary.
        if(provide(frame)&&(screen||NativeHudMath::InfiniteScreenCandidate(projection))) {
            std::uint32_t stride=0,count=0,data=0;SIZE_T got=0;
            const auto address=reinterpret_cast<std::uintptr_t>(manager);
            if(Read(address+8,stride)&&stride==24&&Read(address+0x10,count)&&count&&count<=8192&&
               Read(address+0x28,data)&&data&&frame.width&&frame.height&&
               NativeHudMath::Build(frame.fov,frame.eyeOffset,frame.eye,frame.aspect,
                                    frame.distanceMetres,frame.coverage,scope.plane)) {
                vertices.resize(count*4);
                const auto bytes=vertices.size()*sizeof(vertices[0]);
                if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(data),
                                     vertices.data(),bytes,&got)&&got==bytes) {
                    scope.active=true;scope.projectionQualified=screen;
                    scope.width=frame.width;scope.height=frame.height;
                    ++scopes;threadId=GetCurrentThreadId();
                } else ++rejected;
            } else ++rejected;
        }
    }
    return originalDraw(manager,state);
}

bool MainViewport(IDirect3DDevice9* device,unsigned width,unsigned height) {
    D3DVIEWPORT9 viewport{};
    if(FAILED(device->GetViewport(&viewport))||viewport.X||viewport.Y||
       viewport.Width!=width||viewport.Height!=height)return false;
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->GetRenderTarget(0,&target))||!target)return false;
    D3DSURFACE_DESC description{};const HRESULT result=target->GetDesc(&description);target->Release();
    return SUCCEEDED(result)&&description.Width==width&&description.Height==height;
}

// Source bytes come from the exact native dynamic writer before Unlock.
// The native buffers are WRITEONLY; never issue a speculative READONLY Lock.
bool ReadGenericQuads(IDirect3DDevice9* device,D3DPRIMITIVETYPE primitive,INT baseVertex,
                      UINT minVertex,UINT numVertices,UINT startIndex,UINT numPrimitives,
                      std::vector<NativeHudMath::Vertex>& quads,UINT& writer) {
    lastReadStage=1;
    if(primitive!=D3DPT_TRIANGLELIST||!numPrimitives||(numPrimitives%2)||
       numPrimitives>16384||!numVertices||numVertices>65536)return false;
    IDirect3DVertexBuffer9* vertexBuffer=nullptr;IDirect3DIndexBuffer9* indexBuffer=nullptr;
    struct Release {
        IDirect3DVertexBuffer9*& vertex;IDirect3DIndexBuffer9*& index;
        ~Release(){if(vertex)vertex->Release();if(index)index->Release();}
    } release{vertexBuffer,indexBuffer};
    UINT streamOffset=0,stride=0;lastReadStage=4;
    if(FAILED(device->GetStreamSource(0,&vertexBuffer,&streamOffset,&stride))||!vertexBuffer)return false;
    lastStride=stride;
    if(stride!=sizeof(NativeHudMath::Vertex)||FAILED(device->GetIndices(&indexBuffer))||!indexBuffer)return false;
    D3DINDEXBUFFER_DESC indexDescription{};D3DVERTEXBUFFER_DESC vertexDescription{};
    if(FAILED(indexBuffer->GetDesc(&indexDescription))||FAILED(vertexBuffer->GetDesc(&vertexDescription)))return false;
    lastVertexUsage=vertexDescription.Usage;lastIndexUsage=indexDescription.Usage;
    lastReadStage=2;
    IDirect3DVertexDeclaration9* declaration=nullptr;
    if(FAILED(device->GetVertexDeclaration(&declaration))||!declaration)return false;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT elementCount=MAXD3DDECLLENGTH+1;
    const HRESULT declarationResult=declaration->GetDeclaration(elements,&elementCount);declaration->Release();
    if(FAILED(declarationResult)||elementCount>MAXD3DDECLLENGTH+1)return false;
    bool position=false,colour=false,uv=false;
    for(unsigned i=0;i<elementCount&&elements[i].Stream!=0xFF;++i) {
        const auto& element=elements[i];
        if(element.Stream||element.Method!=D3DDECLMETHOD_DEFAULT||element.UsageIndex)return false;
        if(element.Offset==0&&element.Type==D3DDECLTYPE_FLOAT3&&element.Usage==D3DDECLUSAGE_POSITION)position=true;
        else if(element.Offset==12&&element.Type==D3DDECLTYPE_D3DCOLOR&&element.Usage==D3DDECLUSAGE_COLOR)colour=true;
        else if(element.Offset==16&&element.Type==D3DDECLTYPE_FLOAT2&&element.Usage==D3DDECLUSAGE_TEXCOORD)uv=true;
        else return false;
    }
    lastReadStage=3;if(!position||!colour||!uv)return false;
    lastReadStage=5;
    const UINT indexBytes=indexDescription.Format==D3DFMT_INDEX16?2:indexDescription.Format==D3DFMT_INDEX32?4:0;
    const std::uint64_t indexStart=std::uint64_t(startIndex)*indexBytes,indexCount=std::uint64_t(numPrimitives)*3;
    if(!indexBytes||indexStart+indexCount*indexBytes>indexDescription.Size)return false;
    thread_local std::vector<std::uint32_t> indices;
    thread_local std::vector<unsigned char> indexData,vertexData;
    indices.resize(static_cast<std::size_t>(indexCount));lastReadStage=6;
    if(!ReadShadow(reinterpret_cast<std::uintptr_t>(indexBuffer),device,
            indexBytes==2?NativeHudShadow::Kind::Index16:NativeHudShadow::Kind::Index32,indexDescription.Size,
            static_cast<UINT>(indexStart),static_cast<UINT>(indexCount*indexBytes),indexData))return false;
    UINT lowest=std::numeric_limits<UINT>::max(),highest=0;bool valid=true;
    for(unsigned i=0;i<indices.size();++i) {
        std::uint32_t index=0;
        if(indexBytes==2){std::uint16_t small=0;std::memcpy(&small,indexData.data()+i*2,2);index=small;}
        else std::memcpy(&index,indexData.data()+i*4,4);
        if(index<minVertex||std::uint64_t(index)>=std::uint64_t(minVertex)+numVertices)valid=false;
        indices[i]=index;lowest=std::min(lowest,index);highest=std::max(highest,index);
    }
    if(!valid)return false;
    const std::int64_t first=std::int64_t(baseVertex)+lowest,last=std::int64_t(baseVertex)+highest;
    if(first<0||last<first)return false;
    const std::uint64_t vertexStart=std::uint64_t(streamOffset)+std::uint64_t(first)*stride;
    const std::uint64_t vertexBytes=(std::uint64_t(highest)-lowest+1)*stride;
    if(vertexStart+vertexBytes>vertexDescription.Size||vertexBytes>4*1024*1024)return false;
    lastReadStage=7;
    if(!ReadShadow(reinterpret_cast<std::uintptr_t>(vertexBuffer),device,NativeHudShadow::Kind::Vertex,
            vertexDescription.Size,static_cast<UINT>(vertexStart),static_cast<UINT>(vertexBytes),vertexData,&writer))return false;
    quads.clear();quads.reserve(indices.size()/6*4);lastReadStage=8;
    for(unsigned at=0;at<indices.size();at+=6) {
        std::uint32_t unique[4]{};unsigned count=0;
        for(unsigned i=0;i<6;++i) {
            const auto index=indices[at+i];bool found=false;
            for(unsigned j=0;j<count;++j)found=found||unique[j]==index;
            if(!found){if(count==4)return false;unique[count++]=index;}
        }
        if(count!=4)return false;
        for(unsigned i=0;i<4;++i) {
            NativeHudMath::Vertex vertex;
            std::memcpy(&vertex,vertexData.data()+static_cast<std::size_t>(unique[i]-lowest)*stride,sizeof(vertex));
            quads.push_back(vertex);
        }
    }
    lastReadStage=0;return !quads.empty();
}

bool InterfaceGeometry(IDirect3DDevice9* device,const std::vector<NativeHudMath::Vertex>& quads) {
    DWORD zWrite=~0u,zFunc=~0u;
    const HRESULT writeResult=device->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite);
    const HRESULT functionResult=device->GetRenderState(D3DRS_ZFUNC,&zFunc);
    lastZWrite=zWrite;lastZFunc=zFunc;
    float minZ=std::numeric_limits<float>::infinity(),maxZ=-minZ;
    for(const auto& vertex:quads){minZ=std::min(minZ,vertex.z);maxZ=std::max(maxZ,vertex.z);}
    AcquireSRWLockExclusive(&diagnosticLock);lastQuadMinZ=minZ;lastQuadMaxZ=maxZ;ReleaseSRWLockExclusive(&diagnosticLock);
    // Native UI material bits 0x40000/0x20000 set WriteZ=0 and
    // ZFunc=ALWAYS; ZENABLE can remain enabled and is not the UI gate.
    if(FAILED(writeResult)||FAILED(functionResult)||zWrite||zFunc!=D3DCMP_ALWAYS){++depthRejected;return false;}
    if(!NativeHudMath::InterfacePlane(quads.data(),quads.size())){++geometryRejected;return false;}
    return true;
}

// CPU-only part of the actual marker consumer, after native vertex/source
// ownership has been established. Kept separate from D3D state management so
// goal orientation and stereo invariants can be checked without a GPU.
bool TranslateNativeMarker(const MarkerPacket& packet,ReticleFrame& frame,const float* original,
                           const std::vector<NativeHudMath::Vertex>& quads,float* corrected,unsigned& stage) {
    NativeWorldMarkerMath::Observation observation;
    stage=46;if(!packet.layout.valid)return false;
    auto layout=packet.layout;
    const bool objective=NativeMarkerViewMath::Objective(packet.recordType);
    if(objective){
        bool reused=false;NativeMarkerViewMath::Key key{frame.player,packet.record,packet.recordType,packet.texture,packet.caller,packet.entity,packet.entityGeometry};
        key.fixed=packet.fixed;key.fixedPoint=packet.target;
        stage=47;if(!objectivePairCache.Select(key,frame.pairId,GetTickCount(),frame.target,layout,reused))return false;
        if(reused)++nativeObjectivePairsReused;
    }
    const auto width=layout.width;layout=NativeMarkerViewMath::InEyeAspect(layout,frame.width,frame.height,frame.referenceProjection);
    stage=48;if(!layout.valid)return false;if(std::fabs(width-layout.width)>1e-6f)++nativeMarkerAspectCorrected;
    if(layout.sourceExtentValid){
        const auto delta=NativeCameraMath::Subtract(frame.target,frame.centre.position);const float distance=std::sqrt(NativeCameraMath::Dot(delta,delta));
        bool reused=false;NativeMarkerViewMath::Key key{frame.player,packet.record,packet.recordType,packet.texture,packet.caller,packet.entity,packet.entityGeometry};
        key.fixed=packet.fixed;key.fixedPoint=packet.target;
        stage=49;if(!markerWorldScaleCache.Select(key,GetTickCount(),distance,layout,reused))return false;
    }
    // Every positively owned WORLD ActionMarker, including its native white
    // parts, keeps the perspective source's authored physical extent. This
    // path is separate from reticle aiming and ordinary screen UI producers.
    stage=44;if(!NativeWorldMarkerMath::Translate(original,quads.data(),quads.size(),frame.actualEye,frame.projection,frame.target,corrected,observation,&layout,&frame.centre,frame.referenceProjection,
        objective||layout.sourceExtentValid))return false;
    nativeMarkerWidthNdc=layout.width;nativeMarkerHeightNdc=layout.height;nativeMarkerSourceDepth=layout.sourceDepth;
    nativeMarkerMinZ=observation.minZ;nativeMarkerMaxZ=observation.maxZ;nativeMarkerMinW=observation.minW;nativeMarkerMaxW=observation.maxW;
    if(observation.normalizedNegativeW)++nativeMarkerNegativeW;
    return true;
}

bool MarkerWriteReady() {
    if(!nativeMarkerDrawPacket.canonical.valid)return true;
    MarkerPacket live;
    return markerDrawId&&markerCanonicalizedDrawId==markerDrawId&&MatchMarker(nativeMarkerDrawPacket.entry,live)&&
        live.slot==nativeMarkerDrawPacket.slot&&live.stamp==nativeMarkerDrawPacket.stamp&&live.producer==nativeMarkerDrawPacket.producer&&
        live.geometry==nativeMarkerDrawPacket.geometry&&live.record==nativeMarkerDrawPacket.record&&live.player==nativeMarkerDrawPacket.player;
}

HRESULT DrawReticle(void* backend,D3DPRIMITIVETYPE primitive,INT baseVertex,
                    UINT minVertex,UINT numVertices,UINT startIndex,UINT numPrimitives,const ReticleFrame* markerFrame=nullptr,bool* markerFallback=nullptr) {
    ProfileTiming::Scope timing(ProfileTiming::ReticleProjection,!markerFrame);
    const auto optic=nativeScopeProvider.load(std::memory_order_relaxed);
    if(!markerFrame&&optic&&optic()) {
        const auto result=originalIndexed(backend,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives);
        if(SUCCEEDED(result))++nativeScopeReticlePreserved;
        return result;
    }
    unsigned stage=1;ReticleFrame frame;
    const auto reject=[&](){if(markerFrame){++nativeMarkerRejected;nativeMarkerRejectStage=stage;if(markerFallback)*markerFallback=true;}
        else ++reticleRejected;if(markerFrame)TraceObjective(nativeMarkerDrawPacket,&frame,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Draw),stage);return D3D_OK;};
    const auto provider=reticleProvider.load(std::memory_order_relaxed);
    if(markerFrame)frame=*markerFrame;
    if((!markerFrame&&(!reticleEnabled.load(std::memory_order_relaxed)||!provider||!provider(frame)))||
       frame.player!=(markerFrame?nativeMarkerDrawPacket.player:reticleDrawScope.player)||!frame.pairId||!frame.tick||!frame.width||!frame.height||
       std::int32_t(GetTickCount()-frame.tick)<0||GetTickCount()-frame.tick>150)return reject();
    stage=50;if(markerFrame&&!MarkerWriteReady())return reject();
    stage=2;
    std::uint32_t deviceAt=0;
    if(!backend||!Read(reinterpret_cast<std::uintptr_t>(backend)+4,deviceAt)||!deviceAt)return reject();
    auto* device=reinterpret_cast<IDirect3DDevice9*>(deviceAt);
    stage=3;
    float original[16]{},corrected[16]{};
    if(FAILED(device->GetVertexShaderConstantF(0,original,4))||!MainViewport(device,frame.width,frame.height))return reject();
    stage=4;
    thread_local std::vector<NativeHudMath::Vertex> quads;UINT writer=0;
    if(markerFrame){
        stage=41;if(!ReadGenericQuads(device,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives,quads,writer))return reject();
        nativeMarkerVertexCount=static_cast<std::uint32_t>(quads.size());
        stage=42;if(!NativeHudShadow::InterfaceVertexWriter(writer))return reject();
        DWORD write=~0u,function=~0u;stage=43;
        if(FAILED(device->GetRenderState(D3DRS_ZWRITEENABLE,&write))||FAILED(device->GetRenderState(D3DRS_ZFUNC,&function))||write||function!=D3DCMP_ALWAYS)return reject();
        if(!TranslateNativeMarker(nativeMarkerDrawPacket,frame,original,quads,corrected,stage))return reject();
    } else if(!ReadGenericQuads(device,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives,quads,writer)||
              !NativeHudShadow::InterfaceVertexWriter(writer)||!InterfaceGeometry(device,quads)||
              !NativeReticleKLMath::Translate(original,quads.data(),quads.size(),frame.actualEye,frame.projection,frame.target,corrected))return reject();
    stage=5;
    DWORD scissor=FALSE;if(FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor)))return reject();
    struct Restore {IDirect3DDevice9* device;const float* rows;DWORD scissor;
        ~Restore(){
            if(FAILED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor)))++reticleRestoreFailures;
            if(FAILED(device->SetVertexShaderConstantF(0,rows,4)))++reticleRestoreFailures;
        }} restore{device,original,scissor};
    // Keep the native sprite and spread geometry in the eye's angular aspect.
    // Its original centre scissor must not clip the relocated draw.
    if(FAILED(device->SetVertexShaderConstantF(0,corrected,4))||
       (scissor&&FAILED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE))))return reject();
    const auto result=originalIndexed(backend,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives);
    if(SUCCEEDED(result)){if(markerFrame){++nativeMarkerDrawn;++nativeMarkerReprojected;
        if(nativeMarkerDrawPacket.destination)++nativeDestinationDrawn;
        if(frame.eye<2&&nativeMarkerDrawPacket.recordType==NativeTeamMarkerKL::SelectionType)++selectionMarkerDraws[frame.eye];
    }else ++reticleDrawn;}
    if(markerFrame)TraceObjective(nativeMarkerDrawPacket,&frame,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Draw),SUCCEEDED(result)?0:90);
    return result;
}

HRESULT __fastcall IndexedHook(void* backend,void*,D3DPRIMITIVETYPE primitive,INT baseVertex,
                                UINT minVertex,UINT numVertices,UINT startIndex,UINT numPrimitives) {
    ProfileTiming::Scope profile(ProfileTiming::HudDraw);
    const auto pass=[&](){profile.Stop();const auto result=originalIndexed(backend,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives);
        if(nativeMarkerDrawing&&SUCCEEDED(result))++nativeMarkerDrawn;return result;};
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    ++indexedCalls;
    const auto path=NativeHudMath::IndexedSource(static_cast<std::uint32_t>(caller-image),scope.active);
    if(wristDrawing&&!ScreenText(wristDrawing->source.element)) {
        ProfileTiming::Scope timing(ProfileTiming::WristProjection);
        const auto& source=wristDrawing->source;
        const auto drop=[&](unsigned reason){
            if(source.element==WristHudElement::Team){++teamDrops;lastTeamRejection=reason;}
            return source.element==WristHudElement::Action?pass():D3D_OK;
        };
        if(scope.insideDraw||path!=NativeHudMath::IndexedPath::QuadArray)return drop(8);
        WristHudEye eye;
        const auto provider=wristProvider.load(std::memory_order_relaxed);
        if(!provider||!provider(source.element,source.player,eye)||eye.eye>1||!eye.pair)return drop(1);
        std::uint32_t deviceAt=0;
        if(!backend||!Read(reinterpret_cast<std::uintptr_t>(backend)+4,deviceAt)||!deviceAt)return drop(2);
        auto* device=reinterpret_cast<IDirect3DDevice9*>(deviceAt);
        if(!MainViewport(device,eye.width,eye.height))return drop(3);
        float originalRows[16]{};
        DWORD zWrite=1,zFunc=0;
        if(FAILED(device->GetVertexShaderConstantF(0,originalRows,4))||!NativeHudMath::NativeMonitorProjection(originalRows)||
           FAILED(device->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite))||zWrite||
           FAILED(device->GetRenderState(D3DRS_ZFUNC,&zFunc))||zFunc!=D3DCMP_ALWAYS)return drop(4);
        NativeHudMath::Plane placement;
        if(!NativeHudMath::PlaceAt(originalRows,source.anchor,eye.x,eye.y,eye.scale,placement,eye.aspectCorrection))return drop(5);
        float rows[16];
        std::memcpy(rows,originalRows,sizeof(rows));
        if(!NativeHudMath::PatchRows(rows,placement))return drop(5);
        DWORD scissor=0;
        RECT previous{},moved{};
        if(FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor)))return drop(6);
        if(scissor){
            NativeHudMath::Rect transformed;
            if(FAILED(device->GetScissorRect(&previous))||!NativeHudMath::PatchScissor(
               {previous.left,previous.top,previous.right,previous.bottom},eye.width,eye.height,placement,transformed))return drop(6);
            moved={transformed.left,transformed.top,transformed.right,transformed.bottom};
        }
        if(FAILED(device->SetVertexShaderConstantF(0,rows,4)))return drop(7);
        const bool scissorReady=!scissor||SUCCEEDED(device->SetScissorRect(&moved));
        const auto result=scissorReady?pass():drop(6);
        const bool scissorRestored=!scissor||SUCCEEDED(device->SetScissorRect(&previous));
        const bool rowsRestored=SUCCEEDED(device->SetVertexShaderConstantF(0,originalRows,4));
        if(scissorReady&&SUCCEEDED(result)&&scissorRestored&&rowsRestored){
            ++wristDraws[static_cast<unsigned>(source.element)][eye.eye];
            if(wristDrawing->weaponAmmo)++wristAmmoDraws[eye.eye];
            if(source.element==WristHudElement::Minimap&&wristDrawing->type==12)++wristCaptionDraws[eye.eye];
            if(source.element==WristHudElement::Team&&wristDrawing->type==10)teamPresented={source.roster,eye.pair,eye.eye};
        }
        return result;
    }
    // Once this exact native sprite's XY have been rebased, an unexpected
    // caller cannot expose them through the original native c0 projection.
    if(nativeMarkerDrawing&&markerDrawId&&markerCanonicalizedDrawId==markerDrawId&&
       (scope.insideDraw||path!=NativeHudMath::IndexedPath::QuadArray)){
        ++nativeMarkerRejected;nativeMarkerRejectStage=51;TraceObjective(nativeMarkerDrawPacket,nullptr,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Draw),51);return D3D_OK;
    }
    if(scope.insideDraw||path==NativeHudMath::IndexedPath::None)return pass();
    const bool managerScope=path==NativeHudMath::IndexedPath::SpriteManager;
    if(!managerScope&&nativeMarkerDrawing) {
        if(nativeMarkerDrawPacket.ambiguous){++nativeMarkerRejected;nativeMarkerRejectStage=45;TraceObjective(nativeMarkerDrawPacket,nullptr,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Draw),45);return D3D_OK;}
        ReticleFrame markerFrame;const auto provider=markerProvider.load(std::memory_order_relaxed);
        if(!provider||!provider(markerFrame)){++nativeMarkerRejected;TraceObjective(nativeMarkerDrawPacket,nullptr,static_cast<unsigned>(NativeObjectiveTraceKL::Phase::Draw),91);return D3D_OK;}
        markerFrame.target=nativeMarkerDrawPacket.target;
        scope.insideDraw=true;struct Exit {~Exit(){scope.insideDraw=false;}} exit;
        bool fallback=false;
        profile.Stop();const auto result=DrawReticle(backend,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives,&markerFrame,&fallback);
        // This is a positively identified world marker. Native screen-space
        // fallback on a rejected/behind-eye target caused huge displaced
        // icons during traversal. Restore state, then hide just this packet.
        return result;
    }
    if(!managerScope&&reticleDrawScope.player){
        scope.insideDraw=true;struct Exit {~Exit(){scope.insideDraw=false;}} exit;
        profile.Stop();return DrawReticle(backend,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives);
    }
    Frame frame;
    const bool screenText=wristDrawing&&ScreenText(wristDrawing->source.element);
    CinemaTextFrame cinema;
    bool cinemaText=false;
    if(!managerScope&&(!provide||!provide(frame))){
        const auto provider=screenText?cinemaTextProvider.load(std::memory_order_relaxed):nullptr;
        if(!provider||!provider(cinema)){++cameraRejected;return pass();}
        cinemaText=true;
        frame.width=cinema.width;frame.height=cinema.height;
    }
    scope.insideDraw=true;
    struct Exit {~Exit(){scope.insideDraw=false;}} exit;
    std::uint32_t deviceAt=0;
    if(!backend||!Read(reinterpret_cast<std::uintptr_t>(backend)+4,deviceAt)||!deviceAt){++backendRejected;return pass();}
    auto* device=reinterpret_cast<IDirect3DDevice9*>(deviceAt);
    float original[20]{};
    // Native shader parameters and c4 depth are complete now, regardless of
    // upload order or whether the engine reused its cached shader matrices.
    const HRESULT constantResult=device->GetVertexShaderConstantF(0,original,5);lastConstantsResult=constantResult;
    lastRawCaller=static_cast<UINT>(caller-image);
    if(FAILED(constantResult)){++constantReadRejected;return pass();}
    AcquireSRWLockExclusive(&diagnosticLock);std::memcpy(lastRawConstants,original,sizeof(original));ReleaseSRWLockExclusive(&diagnosticLock);
    if(NativeHudMath::NativeMonitorProjection(original)) {
        lastCaller=static_cast<std::uint32_t>(caller-image);
        AcquireSRWLockExclusive(&diagnosticLock);std::memcpy(lastConstants,original,sizeof(original));ReleaseSRWLockExclusive(&diagnosticLock);
    }
    NativeHudMath::Plane plane=scope.plane;
    thread_local std::vector<NativeHudMath::Vertex> genericQuads;
    const std::vector<NativeHudMath::Vertex>* activeVertices=&vertices;
    if(managerScope) {
        if(!scope.projectionQualified&&!NativeHudMath::NativeMonitorProjection(original)){++constantsRejected;return pass();}
        if(!MainViewport(device,scope.width,scope.height)){++viewportRejected;return pass();}
    } else {
        if(!NativeHudMath::NativeMonitorProjection(original)){++constantsRejected;return pass();}
        ++genericCandidates;threadId=GetCurrentThreadId();
        if(!MainViewport(device,frame.width,frame.height)){++viewportRejected;return pass();}
        if(cinemaText)plane={};
        else if(!NativeHudMath::Build(frame.fov,frame.eyeOffset,frame.eye,frame.aspect,
                                      frame.distanceMetres,frame.coverage,plane))return pass();
        UINT writer=0;
        if(!ReadGenericQuads(device,primitive,baseVertex,minVertex,numVertices,startIndex,numPrimitives,genericQuads,writer)) {
            ++bufferRejected;return pass();
        }
        lastWriter=writer;
        if(!NativeHudShadow::InterfaceVertexWriter(writer)){++provenanceRejected;return pass();}
        activeVertices=&genericQuads;
    }
    // The legacy SpriteManager scope also needs the actual UI state/plane;
    // removing its old c4 fog-range heuristic must not admit world sprites.
    if(!InterfaceGeometry(device,*activeVertices))return pass();
    if(screenText){
        const bool speech=wristDrawing->source.element==WristHudElement::Speech;
        NativeHudMath::EnlargeText(plane,cinemaText?cinema.scale:NativeHudMath::ReadableTextScale,
                                   speech?-.72f:.14f);
    }
    // Video/loading/fade/damage quads can use this same manager. Preserve the
    // entire mixed scope whenever any quad covers the original full screen.
    for(std::size_t i=0;i+4<=activeVertices->size();i+=4)
        if(NativeHudMath::FullScreenQuad(activeVertices->data()+i,original)) {
            ++fullScreenPreserved;return pass();
        }
    float corrected[16];std::memcpy(corrected,original,sizeof(corrected));
    if(!NativeHudMath::PatchRows(corrected,plane)){++rejected;return pass();}
    DWORD scissorEnabled=FALSE;RECT oldScissor{},nextScissor{};
    if(FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled))){++scissorRejected;return pass();}
    if(scissorEnabled) {
        NativeHudMath::Rect transformed;
        if(FAILED(device->GetScissorRect(&oldScissor))||
           !NativeHudMath::PatchScissor({oldScissor.left,oldScissor.top,oldScissor.right,oldScissor.bottom},
                managerScope?scope.width:frame.width,managerScope?scope.height:frame.height,plane,transformed)) {
            ++scissorRejected;return pass();
        }
        nextScissor={transformed.left,transformed.top,transformed.right,transformed.bottom};
    }
    if(FAILED(device->SetVertexShaderConstantF(0,corrected,4))){++rejected;return pass();}
    struct Restore {
        IDirect3DDevice9* device;const float* rows;const RECT* scissor;bool active=true;
        void Now() {
            if(!active)return;
            ProfileTiming::Scope profile(ProfileTiming::HudRestore);
            if(scissor&&FAILED(device->SetScissorRect(scissor))){++scissorRejected;scope.active=false;}
            if(FAILED(device->SetVertexShaderConstantF(0,rows,4))){++rejected;scope.active=false;}
            active=false;
        }
        ~Restore(){Now();}
    } restore{device,original,scissorEnabled?&oldScissor:nullptr};
    if(scissorEnabled) {
        if(FAILED(device->SetScissorRect(&nextScissor))){++scissorRejected;restore.Now();return pass();}
        ++scissorAdjusted;
    }
    ++uploads;
    if(!managerScope)++genericUploads;
    const auto result=pass();
    if(!managerScope&&screenText&&SUCCEEDED(result)){
        if(cinemaText)++cinemaTextDraws;
        else if(frame.eye<2)++wristDraws[static_cast<unsigned>(wristDrawing->source.element)][frame.eye];
    }
    return result;
}
}

void SetLetterboxProvider(LetterboxProvider provider){letterboxProvider.store(provider);}
void SetMappedVertexFilter(MappedVertexFilter filter){mappedVertexFilter.store(filter);}
bool ReadMappedVertexContext(MappedVertexContext& output){output=mappedCallbackContext;return output.sourceState!=0;}
bool ReadParticleDrawContext(MappedVertexContext& output){output=particleDrawContext;return output.sourceState!=0;}
void SetVisualSourceTrace(NativeEffectsKL::VisualTraceActive active){visualSourceActive.store(active,std::memory_order_relaxed);}
bool ReadSpriteVisualSource(std::uintptr_t entry,std::uintptr_t state,NativeEffectsKL::VisualSourceDiagnostic& output){
    if(!VisualSourceActive()||!spriteVisualScope||!spriteVisualScope->valid||spriteVisualScope->entry!=entry||spriteVisualScope->state!=state)return false;
    auto next=*spriteVisualScope;std::uint32_t header[5]{},geometry=0,stamp=0,slot=2;
    if(!Read(image+0x7678A4,slot)||slot!=next.slot||!Read(entry+0x100+slot*4,geometry)||geometry!=next.geometry||
       !Read(entry+0x130+slot*4,stamp)||stamp!=next.stamp||!ReadSpan(entry+0x108+slot*20,header,sizeof(header)))return false;
    std::memcpy(next.materialHeader,header,sizeof(header));
    const auto first=((entry>>4)^(entry>>12)^slot)&(spriteProducers.size()-1);
    AcquireSRWLockShared(&spriteProducerLock);
    for(unsigned i=0;i<8;++i){const auto& p=spriteProducers[(first+i)&(spriteProducers.size()-1)];
        if(p.valid&&p.entry==entry&&p.slot==slot&&p.stamp==stamp&&p.storedGeometry==geometry&&!std::memcmp(p.header,header,sizeof(header))){next.producer=p;break;}}
    ReleaseSRWLockShared(&spriteProducerLock);
    ReadSpan(state+0x8C,next.world,sizeof(next.world));ReadSpan(state+0xBC,next.nativeView,sizeof(next.nativeView));
    ReadSpan(state+0x3F8,next.projection,sizeof(next.projection));
    next.rootKnown=ReadSpan(entry+0x10+slot*0x30,next.rootOriginal,sizeof(next.rootOriginal));
    std::memcpy(next.rootBound,next.world,sizeof(next.rootBound));
    output=next;return true;
}
void SetReticleProvider(ReticleProvider provider){reticleProvider.store(provider,std::memory_order_relaxed);}
void SetWristHudProvider(WristHudProvider provider){wristProvider.store(provider,std::memory_order_relaxed);}
void SetCinemaTextProvider(CinemaTextProvider provider){cinemaTextProvider.store(provider,std::memory_order_relaxed);}
std::uint64_t CinemaTextDraws(){return cinemaTextDraws.load(std::memory_order_relaxed);}
bool WristHudReady(){return installed&&wristProvider.load(std::memory_order_relaxed);}
WristHudSource ReplaceWristHudSource(const WristHudSource& source){
    const auto previous=wristSource;wristSource=source;return previous;
}
NativeTeamHudKL::Roster PresentedTeamRoster(std::uintptr_t player,std::uint64_t pair,unsigned eye){
    const auto presentation=teamPresented;teamPresented={};
    if(presentation.pair!=pair||presentation.eye!=eye||
       !NativeTeamHudKL::Fresh(presentation.roster,player,GetTickCount()))return {};
    return presentation.roster;
}
std::uint64_t WristHudDraws(WristHudElement element,unsigned eye){
    return wristDraws[static_cast<unsigned>(element)][eye].load();
}
std::uint64_t WristAmmoDraws(unsigned eye){return wristAmmoDraws[eye].load();}
std::uint64_t WristCaptionDraws(unsigned eye){return wristCaptionDraws[eye].load();}
std::uint64_t SelectionMarkerDraws(unsigned eye){return selectionMarkerDraws[eye].load();}
TeamPanelStats SnapshotTeamPanel(){
    return {WristHudDraws(WristHudElement::Team,0),WristHudDraws(WristHudElement::Team,1),teamDrops.load(),lastTeamRejection.load()};
}
void SetNativeMarkerProvider(ReticleProvider provider){markerProvider.store(provider,std::memory_order_relaxed);}
void SetNativeScopeProvider(NativeScopeProvider provider){nativeScopeProvider.store(provider,std::memory_order_relaxed);}
bool OriginalScopeOverlayActive(){const auto provider=nativeScopeProvider.load(std::memory_order_relaxed);return provider&&provider();}
void SetReticleEnabled(bool enabled){reticleEnabled.store(enabled,std::memory_order_relaxed);}
bool ReticleEnabled(){return reticleEnabled.load(std::memory_order_relaxed);}
void SetObjectiveTraceEnabled(bool enabled){objectiveTraceEnabled.store(enabled,std::memory_order_relaxed);}
bool ObjectiveTraceEnabled(){return objectiveTraceEnabled.load(std::memory_order_relaxed);}
NativeObjectiveTraceKL::Snapshot SnapshotObjectiveTrace(){
    AcquireSRWLockShared(&objectiveTraceLock);auto out=objectiveTrace.Copy();ReleaseSRWLockShared(&objectiveTraceLock);return out;
}
void BeginReticle(std::uintptr_t player,void* renderer){
    if(reticleEmitDepth<reticleEmitStack.size())reticleEmitStack[reticleEmitDepth]=reticleEmitScope;
    ++reticleEmitDepth;
    reticleEmitScope=reticleEmitDepth<=reticleEmitStack.size()&&ReticleEnabled()?
        ReticleEmitScope{player,reinterpret_cast<std::uintptr_t>(renderer)}:ReticleEmitScope{};
}
void EndReticle(){
    if(!reticleEmitDepth){reticleEmitScope={};return;}
    --reticleEmitDepth;
    reticleEmitScope=reticleEmitDepth<reticleEmitStack.size()?reticleEmitStack[reticleEmitDepth]:ReticleEmitScope{};
}
void BeginNativeMarkers(std::uintptr_t player,void* renderer,void* nativeCamera){
    if(nativeMarkerDepth<nativeMarkerStack.size())nativeMarkerStack[nativeMarkerDepth]=nativeMarkerScope;
    ++nativeMarkerDepth;
    nativeMarkerScope=nativeMarkerDepth<=nativeMarkerStack.size()?MarkerSource{player,reinterpret_cast<std::uintptr_t>(renderer),0,{},false,++markerProducerSerial,reinterpret_cast<std::uintptr_t>(nativeCamera)}:MarkerSource{};
    if(const auto observer=nativeMarkerObserver.load())observer(NativeMarkerEvent::Begin,nativeMarkerScope.player);
}
void EndNativeMarkers(){
    if(!nativeMarkerDepth){nativeMarkerScope={};return;}
    if(const auto observer=nativeMarkerObserver.load())observer(NativeMarkerEvent::End,nativeMarkerScope.player);
    --nativeMarkerDepth;nativeMarkerScope=nativeMarkerDepth<nativeMarkerStack.size()?nativeMarkerStack[nativeMarkerDepth]:MarkerSource{};
}
void SetNativeMarkerObserver(NativeMarkerObserver observer){nativeMarkerObserver=observer;}
void InvalidateDevice(){
    if(const auto observer=nativeMarkerObserver.load())observer(NativeMarkerEvent::Reset,0);
    AcquireSRWLockExclusive(&shadowLock);shadow.Clear();pending={};pendingWrites=false;ReleaseSRWLockExclusive(&shadowLock);
    scope={};vertices.clear();
    AcquireSRWLockExclusive(&reticlePacketLock);reticlePackets={};ReleaseSRWLockExclusive(&reticlePacketLock);
    reticleEmitScope={};reticleEmitDepth=0;reticleDrawScope={};
    AcquireSRWLockExclusive(&markerPacketLock);markerPackets={};ReleaseSRWLockExclusive(&markerPacketLock);
    objectivePairCache.Reset();markerWorldScaleCache.Reset();
    AcquireSRWLockExclusive(&markerEvidenceLock);markerEvidence={};ReleaseSRWLockExclusive(&markerEvidenceLock);
    nativeMarkerScope={};nativeMarkerDepth=0;nativeMarkerDrawing=false;nativeMarkerDrawPacket={};markerDrawId=markerCanonicalizedDrawId=0;
    AcquireSRWLockExclusive(&wristSpriteLock);
    for(auto& slot:wristSprites)slot.clear();
    lastWristPrune=0;lastWristSprite=0;
    ReleaseSRWLockExclusive(&wristSpriteLock);
    wristSource={};wristDrawing=nullptr;teamPresented={};
}

bool Install(ProvideFrame provider,LogCallback log) {
    if(installed.load())return true;
    if(!provider)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const unsigned char entry[]={0xB8,0x30,0xC0,0x00,0x00,0xE8,0xA6,0x1F,0x42,0x00,
                                  0x57,0x8B,0xF9,0x8B,0x47,0x10};
    const unsigned char call[]={0x8B,0x06,0x8B,0x50,0x04,0x57,0x8B,0xCE,0xFF,0xD2};
    const unsigned char exit[]={0xC2,0x04,0x00};
    const unsigned char indexedEntry[]={0x8B,0x54,0x24,0x18,0x8B,0x41,0x04,0x8B,0x08,0x52,0x8B,0x54,0x24,0x18,0x52};
    const unsigned char indexedDispatch[]={0x8B,0x81,0x48,0x01,0x00,0x00,0xFF,0xD0,0xC2,0x18,0x00};
    const unsigned char indexedCall[]={0xE8,0xFD,0x4B,0xFF,0xFF};
    const unsigned char quadIndexCall[]={0xE8,0x04,0x5D,0xFF,0xFF};
    const unsigned char vertexLockEntry[]={0x53,0x55,0x56,0x8B,0xF1,0x83,0x7E,0x04,0x00,0x8D,0x5E,0x04,0x57,0x74,0x01,0xCC};
    const unsigned char indexLockEntry[]={0x53,0x56,0x8B,0xF1,0x8D,0x5E,0x04,0x33,0xC0,0x39,0x03,0x57,0x74,0x01,0xCC,0x8B};
    const unsigned char unlockEntry[]={0x56,0x8B,0xF1,0x83,0x7E,0x04,0x00,0x75,0x01,0xCC,0x8B,0x06,0x8B,0x08,0x8B,0x51};
    const unsigned char return12[]={0xC2,0x0C,0x00},return8[]={0xC2,0x08,0x00},return0[]={0xC3};
    const unsigned char letterboxEntry[]={0x83,0xEC,0x54,0x57,0x8B,0xF9,0x8D,0x4C,0x24,0x34};
    const unsigned char spriteEmitEntry[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x44,0x53,0x56,0x8B,0x75,8};
    const unsigned char spriteEmitReturn[]={0x8B,0xC7,0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC2,0x1C,0};
    const unsigned char spriteDrawEntry[]={0x81,0xEC,0x3C,1,0,0,0x8B,0x41,4,0x53,0x55,0x8B,0x68,4};
    const unsigned char spriteSlotWrite[]={0xA1,0xA0,0x78,0xB6,0,0x8B,0x0B,0x8B,0x75,0x0C};
    const unsigned char spriteStamp[]={0x8B,0x15,0xA0,0x78,0xB6,0,0x83,0xC0,0x48,0x8D,0x8C,0x91,0x30,1,0,0};
    const unsigned char reticleGeometryInit[]={0x89,0x4E,0x34,0xC7,0x46,0x2C,1,0,0,0};
    const unsigned char markerPosition[]={0x8B,0x44,0x24,4,0x83,0xEC,8,0x80,0x79,0x4D,0};
    const float imageCorners[]={-.5f,-.5f,0,.5f,-.5f,0,.5f,.5f,0,-.5f,.5f,0};
    const std::uint32_t spriteCopies[]={0x47080F,0x4708EB,0x4708E0,0x4708E0,0x470855,0x470877,0x470893};
    std::uint32_t slot=0;
    if(!Match(0x98C00,entry,sizeof(entry))||!Match(0xB5CCD,call,sizeof(call))||
       !Match(0x9905A,exit,sizeof(exit))||!Read(image+0x540D40,slot)||slot!=image+0x98C00||
       !Match(0x8DC00,indexedEntry,sizeof(indexedEntry))||!Match(0x8DC24,indexedDispatch,sizeof(indexedDispatch))||
       !Match(0x98FFE,indexedCall,sizeof(indexedCall))||!Match(0x98517,quadIndexCall,sizeof(quadIndexCall))||
       !Match(0x8E150,vertexLockEntry,sizeof(vertexLockEntry))||!Match(0x8E1D0,return12,sizeof(return12))||
       !Match(0x8E220,indexLockEntry,sizeof(indexLockEntry))||!Match(0x8E283,return8,sizeof(return8))||
       !Match(0x8E2E0,indexLockEntry,sizeof(indexLockEntry))||!Match(0x8E346,return8,sizeof(return8))||
       !Match(0x8E350,unlockEntry,sizeof(unlockEntry))||!Match(0x8E36C,return0,sizeof(return0))||
       !Match(0x25E850,letterboxEntry,sizeof(letterboxEntry))||!Match(0x25E98F,return8,sizeof(return8))||
       !Read(image+0x575AFC,slot)||slot!=image+0x25E850||
       !Match(0x70740,spriteEmitEntry,sizeof(spriteEmitEntry))||!Match(0x7092A,spriteEmitReturn,sizeof(spriteEmitReturn))||
       !Match(0x990A0,spriteDrawEntry,sizeof(spriteDrawEntry))||!Match(0x707C4,spriteSlotWrite,sizeof(spriteSlotWrite))||
       !Match(0x71D39,spriteStamp,sizeof(spriteStamp))||!Match(0x70938,reinterpret_cast<const unsigned char*>(spriteCopies),sizeof(spriteCopies))||
       !Match(0x25439B,reticleGeometryInit,sizeof(reticleGeometryInit))||!Match(0x253480,markerPosition,sizeof(markerPosition))||
       // Match deliberately has a32-byte scratch limit. Prove all48 bytes
       // as two complete24-byte spans rather than passing an oversized proof
       // that would reject every actual HUD installation.
       !Match(0x634728,reinterpret_cast<const unsigned char*>(imageCorners),24)||
       !Match(0x634740,reinterpret_cast<const unsigned char*>(imageCorners)+24,24)||
       !Match(0x9A1EF,exit,sizeof(exit))||!Read(image+0x53D9A8,slot)||slot!=image+0x70740||
       !Read(image+0x53FB8C,slot)||slot!=image+0x990A0) {
        if(log)log("Native HUD draw signatures differ; native UI retained.");return false;
    }
    provide=provider;
    void* targets[]={reinterpret_cast<void*>(image+0x98C00),reinterpret_cast<void*>(image+0x8DC00),
        reinterpret_cast<void*>(image+0x8E150),reinterpret_cast<void*>(image+0x8E220),
        reinterpret_cast<void*>(image+0x8E2E0),reinterpret_cast<void*>(image+0x8E350),
        reinterpret_cast<void*>(image+0x25E850),reinterpret_cast<void*>(image+0x70740),reinterpret_cast<void*>(image+0x990A0),reinterpret_cast<void*>(image+0x253480)};
    void* detours[]={reinterpret_cast<void*>(&DrawHook),reinterpret_cast<void*>(&IndexedHook),
        reinterpret_cast<void*>(&VertexLockHook),reinterpret_cast<void*>(&Index16LockHook),
        reinterpret_cast<void*>(&Index32LockHook),reinterpret_cast<void*>(&BufferUnlockHook),
        reinterpret_cast<void*>(&LetterboxDrawHook),reinterpret_cast<void*>(&SpriteEmitHook),reinterpret_cast<void*>(&SpriteDrawHook),reinterpret_cast<void*>(&MarkerPositionHook)};
    void** originals[]={reinterpret_cast<void**>(&originalDraw),reinterpret_cast<void**>(&originalIndexed),
        reinterpret_cast<void**>(&originalVertexLock),reinterpret_cast<void**>(&originalIndex16Lock),
        reinterpret_cast<void**>(&originalIndex32Lock),reinterpret_cast<void**>(&originalBufferUnlock),
        reinterpret_cast<void**>(&originalLetterboxDraw),reinterpret_cast<void**>(&originalSpriteEmit),reinterpret_cast<void**>(&originalSpriteDraw),reinterpret_cast<void**>(&originalMarkerPosition)};
    constexpr unsigned hookCount=sizeof(targets)/sizeof(targets[0]);
    unsigned created=0;bool okay=true;
    for(;created<hookCount;++created)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK){okay=false;break;}
    if(okay)for(unsigned i=0;i<hookCount;++i)if(MH_QueueEnableHook(targets[i])!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay) {
        for(unsigned i=0;i<created;++i){MH_QueueDisableHook(targets[i]);MH_DisableHook(targets[i]);}
        MH_ApplyQueued();
        for(unsigned i=0;i<created;++i)MH_RemoveHook(targets[i]);
        provide=nullptr;if(log)log("Native HUD draw hook transaction failed; native UI retained.");return false;
    }
    installed=true;
    if(log)log("Native HUD: native pre-Unlock CPU shadow; exact glyph/image writer, UI depth/plane gates; temporary eye-frustum/IPD c0 correction restored after each draw.");
    return true;
}
Stats Snapshot() {
    Stats result;result.installed=installed;result.calls=calls;result.scopes=scopes;
    result.uploads=uploads;result.rejected=rejected;result.threadId=threadId;
    result.fullScreenPreserved=fullScreenPreserved;
    result.indexedCalls=indexedCalls;result.genericCandidates=genericCandidates;result.genericUploads=genericUploads;
    result.cameraRejected=cameraRejected;result.viewportRejected=viewportRejected;result.constantsRejected=constantsRejected;result.bufferRejected=bufferRejected;
    result.lastCaller=lastCaller;result.lastStride=lastStride;
    result.lastVertexUsage=lastVertexUsage;result.lastIndexUsage=lastIndexUsage;result.lastReadStage=lastReadStage;
    result.constantReadRejected=constantReadRejected;result.backendRejected=backendRejected;
    result.provenanceRejected=provenanceRejected;result.depthRejected=depthRejected;result.geometryRejected=geometryRejected;
    result.nativeLocks=nativeLocks;result.nativeUnlocks=nativeUnlocks;result.shadowCaptured=shadowCaptured;
    result.shadowRejected=shadowRejected;result.shadowReads=shadowReads;result.shadowMisses=shadowMisses;
    result.shadowSkippedWrites=shadowSkippedWrites;result.shadowCopiedBytes=shadowCopiedBytes;result.shadowTrackedInvalidations=shadowTrackedInvalidations;
    result.lastWriter=lastWriter;result.lastZWrite=lastZWrite;result.lastZFunc=lastZFunc;
    result.lastRawCaller=lastRawCaller;result.lastConstantsResult=lastConstantsResult;
    result.scissorAdjusted=scissorAdjusted;result.scissorRejected=scissorRejected;
    result.letterboxCalls=letterboxCalls;result.letterboxHidden=letterboxHidden;result.letterboxRejected=letterboxRejected;
    result.lastLetterbox=lastLetterbox;result.lastLetterboxMini=lastLetterboxMini;result.lastLetterboxFull=lastLetterboxFull;
    result.lastLetterboxFirstPerson=lastLetterboxFirstPerson;
    result.reticleCaptured=reticleCaptured;result.reticleRouted=reticleRouted;result.reticleDrawn=reticleDrawn;
    result.reticleRejected=reticleRejected;result.reticleRestoreFailures=reticleRestoreFailures;
    result.nativeScopeReticlePreserved=nativeScopeReticlePreserved;
    result.nativeMarkerEmitted=nativeMarkerEmitted;result.nativeMarkerCaptured=nativeMarkerCaptured;
    result.nativeMarkerRouted=nativeMarkerRouted;result.nativeMarkerDrawn=nativeMarkerDrawn;result.nativeMarkerRejected=nativeMarkerRejected;
    result.nativeMarkerReprojected=nativeMarkerReprojected;
    result.nativeDestinationCaptured=nativeDestinationCaptured;result.nativeDestinationDrawn=nativeDestinationDrawn;
    result.nativeObjectiveBobSuppressed=nativeObjectiveBobSuppressed;result.nativeObjectivePairsReused=nativeObjectivePairsReused;result.nativeMarkerAspectCorrected=nativeMarkerAspectCorrected;
    result.nativeMarkerRejectStage=nativeMarkerRejectStage;
    result.nativeMarkerAmbiguous=nativeMarkerAmbiguous;result.nativeMarkerNegativeW=nativeMarkerNegativeW;
    result.nativeMarkerCalibrated=nativeMarkerCalibrated;result.nativeMarkerCalibrationRejected=nativeMarkerCalibrationRejected;
    result.nativeMarkerWidthNdc=nativeMarkerWidthNdc;result.nativeMarkerHeightNdc=nativeMarkerHeightNdc;result.nativeMarkerSourceDepth=nativeMarkerSourceDepth;
    result.markerCanonicalized=markerCanonicalized;result.markerCanonicalRejected=markerCanonicalRejected;result.markerCanonicalNonfinite=markerCanonicalNonfinite;
    result.markerCanonicalLastMagnitude=markerCanonicalLastMagnitude;result.markerCanonicalMaxMagnitude=markerCanonicalMaxMagnitude;
    result.markerCanonicalSpanX=markerCanonicalSpanX;result.markerCanonicalSpanY=markerCanonicalSpanY;
    result.spriteMarkerRetired=spriteMarkerRetired;result.spriteReticleRetired=spriteReticleRetired;
    result.worldSprite5Sources=worldSprite5Sources;result.worldSprite6Sources=worldSprite6Sources;
    result.worldSpriteRelativePreserved=worldSpriteRelativePreserved;result.worldSpriteHudPreserved=worldSpriteHudPreserved;
    for(unsigned n=0;n<spriteSourceTypes.size();++n)result.spriteSourceTypes[n]=spriteSourceTypes[n];
    AcquireSRWLockShared(&worldSpriteSourceLock);result.lastWorldSpriteEligible=lastWorldSpriteEligible;
    result.lastWorldSpritePreserved=lastWorldSpritePreserved;ReleaseSRWLockShared(&worldSpriteSourceLock);
    AcquireSRWLockShared(&markerEvidenceLock);result.markerSource=markerEvidence;ReleaseSRWLockShared(&markerEvidenceLock);
    result.nativeMarkerVertexCount=nativeMarkerVertexCount;
    result.nativeMarkerMinZ=nativeMarkerMinZ;result.nativeMarkerMaxZ=nativeMarkerMaxZ;result.nativeMarkerMinW=nativeMarkerMinW;result.nativeMarkerMaxW=nativeMarkerMaxW;
    const float amount=lastLetterboxAmount;result.lastLetterboxAmount=std::isfinite(amount)?amount:0.f;
    AcquireSRWLockShared(&diagnosticLock);std::memcpy(result.lastProjection,lastProjection,sizeof(lastProjection));
    std::memcpy(result.lastConstants,lastConstants,sizeof(lastConstants));
    std::memcpy(result.lastRawConstants,lastRawConstants,sizeof(lastRawConstants));
    result.lastQuadMinZ=std::isfinite(lastQuadMinZ)?lastQuadMinZ:0.f;
    result.lastQuadMaxZ=std::isfinite(lastQuadMaxZ)?lastQuadMaxZ:0.f;
    ReleaseSRWLockShared(&diagnosticLock);
    for(float& value:result.lastProjection)if(!std::isfinite(value))value=0.f;
    for(float& value:result.lastConstants)if(!std::isfinite(value))value=0.f;
    for(float& value:result.lastRawConstants)if(!std::isfinite(value))value=0.f;
    return result;
}
}
