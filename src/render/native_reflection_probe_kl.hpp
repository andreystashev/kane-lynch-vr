#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d9.h>

namespace NativeReflectionProbeKL {
// Names in game shader CTAB, when retained by the game's compiler. This is
// evidence about a sampler, not proof that the observed draw is the
// Retomoto floor or that a missing name means no reflection is in use.
struct Samplers {std::uint16_t reflection=0,refraction=0,environment=0,mask=0,falloff=0;};
struct ConstantRange {std::uint16_t start=0,count=0;};
// Exact CTAB names only. These are observations, not shader certificates.
inline constexpr unsigned ShadowRangeCount=9;
inline constexpr const char* ShadowRangeNames[ShadowRangeCount]={
    "m44StaticShadowProjectionMatrix","m44WorldShadowProj","m44WorldShadowClip",
    "m44WorldShadowProj1","m44WorldShadowClip1","m44WorldShadowProj2","m44WorldShadowClip2",
    "m44WorldShadowProj3","m44WorldShadowClip3"};
struct VertexMap {
    ConstantRange eye{},cube{},modelViewProjection{},worldViewProjectionScale{};
    ConstantRange shadow[ShadowRangeCount]{};
};
inline bool ParseSamplers(const void* code,std::size_t bytes,Samplers& out){
    if(!code||bytes<8||(bytes&3))return false;
    const auto* p=static_cast<const unsigned char*>(code);
    const auto word=[&](std::size_t i){std::uint32_t x=0;std::memcpy(&x,p+i*4,4);return x;};
    for(std::size_t i=1;i<bytes/4;){const auto token=word(i);
        if((token&0xffff)!=0xfffe){++i;continue;}
        const auto n=(token>>16)&0x7fff;if(n>bytes/4-i-1)return false;
        if(n>=8&&word(i+1)==0x42415443){
            const auto* c=p+(i+2)*4;const std::size_t size=(n-1)*4;
            const auto u32=[&](std::size_t at,std::uint32_t& v){if(at+4>size)return false;std::memcpy(&v,c+at,4);return true;};
            std::uint32_t count=0,at=0;if(!u32(12,count)||!u32(16,at)||count>256||at>size||count>(size-at)/20)return false;
            Samplers next;
            for(unsigned k=0;k<count;++k){const auto row=at+k*20;std::uint32_t name=0;
                if(!u32(row,name)||name>=size)return false;
                const auto* end=static_cast<const unsigned char*>(std::memchr(c+name,0,size-name));if(!end)return false;
                std::uint16_t set=0,start=0,number=0;std::memcpy(&set,c+row+4,2);std::memcpy(&start,c+row+6,2);std::memcpy(&number,c+row+8,2);
                if(set!=3||!number||start>=16||number>16-start)continue;
                const auto equals=[&](const char* s){const auto len=std::strlen(s);return std::size_t(end-(c+name))==len&&!std::memcmp(c+name,s,len);};
                const auto mask=static_cast<std::uint16_t>(((1u<<number)-1u)<<start);
                // Compiled HLSL often reports the sampler variable rather
                // than the texture object from the effect source. Accept both
                // exact documented names; never infer from substrings.
                if(equals("mapReflection2D")||equals("samplerReflection2D"))next.reflection|=mask;
                else if(equals("mapRefraction2D")||equals("samplerRefraction2D"))next.refraction|=mask;
                // Standard20/30 surface reflections sample a cube map, not
                // the separate planar mapReflection2D texture.
                else if(equals("mapEnvironment")||equals("samplerEnvironment"))next.environment|=mask;
                else if(equals("mapReflectionMask")||equals("samplerReflectionMask"))next.mask|=mask;
                else if(equals("mapReflectionFallOff")||equals("samplerReflectionFallOff"))next.falloff|=mask;
            }
            out=next;return true;
        }
        i+=n+1;
    }
    return false;
}
// Same-draw VS CTAB evidence. Exact eye/cube/MVP and shadow names have bounded
// ranges; absent/stripped CTAB must never imply invented registers.
inline bool ParseVertexConstants(const void* code,std::size_t bytes,VertexMap& out){
    if(!code||bytes<8||(bytes&3))return false;
    const auto* p=static_cast<const unsigned char*>(code);
    const auto word=[&](std::size_t i){std::uint32_t x=0;std::memcpy(&x,p+i*4,4);return x;};
    for(std::size_t i=1;i<bytes/4;){const auto token=word(i);
        if((token&0xffff)!=0xfffe){++i;continue;}
        const auto n=(token>>16)&0x7fff;if(n>bytes/4-i-1)return false;
        if(n>=8&&word(i+1)==0x42415443){
            const auto* c=p+(i+2)*4;const std::size_t size=(n-1)*4;
            const auto u32=[&](std::size_t at,std::uint32_t& v){if(at+4>size)return false;std::memcpy(&v,c+at,4);return true;};
            std::uint32_t count=0,at=0;if(!u32(12,count)||!u32(16,at)||count>256||at>size||count>(size-at)/20)return false;
            VertexMap next;
            for(unsigned k=0;k<count;++k){const auto row=at+k*20;std::uint32_t name=0;
                if(!u32(row,name)||name>=size)return false;
                const auto* end=static_cast<const unsigned char*>(std::memchr(c+name,0,size-name));if(!end)return false;
                std::uint16_t set=0,start=0,nregs=0;std::memcpy(&set,c+row+4,2);std::memcpy(&start,c+row+6,2);std::memcpy(&nregs,c+row+8,2);
                if(set!=2||!nregs||unsigned(start)+unsigned(nregs)>256)continue;
                const auto equals=[&](const char* s){const auto len=std::strlen(s);return std::size_t(end-(c+name))==len&&!std::memcmp(c+name,s,len);};
                if(equals("v4EyePosObject")&&nregs==1)next.eye={start,nregs};
                else if(equals("m34ObjToCubeSpace")&&(nregs==3||nregs==4))next.cube={start,nregs};
                else if(equals("m44ModelViewProj")&&nregs==4)next.modelViewProjection={start,nregs};
                else if(equals("m44WorldViewProjScale")&&nregs==4)next.worldViewProjectionScale={start,nregs};
                else if(nregs<=4)for(unsigned slot=0;slot<ShadowRangeCount;++slot)
                    if(equals(ShadowRangeNames[slot]))next.shadow[slot]={start,nregs};
            }
            out=next;return true;
        }
        i+=n+1;
    }
    return false;
}
struct Texture {std::uintptr_t identity=0;std::uint32_t kind=0,width=0,height=0,format=0;};
// Distribute candidates across the preceding observed frame's draw count.
// This avoids repeatedly selecting only the first draw after a timer expires.
// It is sampling evidence, not a guarantee of observing every object.
class StratifiedSampler {
public:
    void Reset(){*this={};}
    bool Next(std::uint64_t frameId){
        if(!valid_||frameId!=frame_){
            const auto previous=count_;
            frame_=frameId;valid_=true;count_=0;
            stride_=previous/4+(previous%4?1u:0u);if(!stride_)stride_=1;
            // Non-linear avalanche matters: a fixed capture interval may hit
            // every fourth frame; a linear hash would then visit only one of
            // four modulo classes when stride is a power of two.
            auto seed=static_cast<std::uint32_t>(frameId)^static_cast<std::uint32_t>(frameId>>32);
            seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;
            phase_=seed%stride_;
        }
        const auto draw=count_;if(count_!=0xffffffffu)++count_;
        return draw/stride_<4&&draw%stride_==phase_;
    }
private:
    std::uint64_t frame_=0;std::uint32_t count_=0,stride_=1,phase_=0;bool valid_=false;
};
struct Sample {
    // valid means a recognised reflection sampler; stripped/unknown CTAB
    // observations are still recorded with ctabKnown=false or valid=false.
    bool valid=false,ctabKnown=false,pixelAvailable=false,vertexAvailable=false;
    std::uint32_t tick=0,shaderHash=0;std::uint64_t frameId=0,sequence=0;
    Samplers samplers{};Texture textures[16]{};
    std::uintptr_t target=0;std::uint32_t targetWidth=0,targetHeight=0,targetFormat=0;
    std::uint32_t viewportX=0,viewportY=0,viewportWidth=0,viewportHeight=0;
    std::uint32_t zEnabled=0,zWrite=0,alphaBlend=0;
    std::uint32_t vertexHash=0;bool vertexCtab=false,eyeRead=false,cubeRead=false;
    VertexMap vertex{};float eye[4]{},cube[16]{};
    // Raw c0..7 only on a named planar-reflection draw. CTAB ranges above
    // distinguish the known Mirror convention from unrelated planar shaders.
    bool planarMatricesAttempted=false,planarMatricesRead=false;
    float planarMatrices[32]{};
    std::uint16_t shadowAttemptMask=0,shadowReadMask=0;
    float shadowValues[ShadowRangeCount][16]{};
    bool shadowModelViewProjectionAttempted=false,shadowModelViewProjectionRead=false;
    float shadowModelViewProjection[16]{};
    struct DrawContext {
        std::uintptr_t state=0;
        std::uint64_t generation=0;
        unsigned eyeIndex=0;
        float world[12]{};
        // Camera basis right/up/forward then position, XYZ for each vector.
        float eye[12]{};
        float projection[16]{};
    } context{};
    // No context is substituted from a later global status snapshot. This
    // callback observation grants no native render or write authorization.
    enum class ContextResult:unsigned {NotConfigured,Unavailable,Invalid,Available};
    ContextResult contextResult=ContextResult::NotConfigured;
};
using DrawContext=Sample::DrawContext;
using ContextResult=Sample::ContextResult;
using ReadContext=bool(*)(DrawContext&);
// At 25 attempts/sec, 128 samples survive several 1-second status intervals.
// sequence/historyOverwritten expose losses if status writing stalls longer.
constexpr unsigned HistoryLimit=128;
struct Stats {
    // latestSequence is process-monotonic even when device reset clears the
    // per-device counters and history; readers can distinguish new samples.
    bool enabled=false;std::uint64_t samples=0,shaderReads=0,ctab=0,named=0,shaderUnavailable=0,recorded=0,latestSequence=0;
    Sample lastNamed{};unsigned historyCount=0;std::uint64_t historyOverwritten=0;Sample history[HistoryLimit]{};
};
void Configure(bool enabled);
// Startup configuration only; invoked after the existing sampling/time gates.
// The caller must return false unless the same draw has a fresh owned context.
void ConfigureContext(ReadContext reader);
bool Enabled();
// Call on a game D3D draw only when Enabled() is true. It never changes the
// pipeline, tries at most four distributed draws per frame and 25 per second,
// and releases all COM getters. Diagnostic overhead can affect FPS.
// Evaluate potentially expensive scene ownership only AFTER the stratified
// candidate and global time limiter. A caller must not eagerly evaluate this
// predicate on every draw merely because diagnostics are enabled.
using ScenePredicate=bool(*)();
void Observe(IDirect3DDevice9* device,std::uint64_t frameId,ScenePredicate scene=nullptr);
Stats Snapshot();
void Reset();
}
