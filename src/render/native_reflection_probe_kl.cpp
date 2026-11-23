#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_reflection_probe_kl.hpp"
#include <atomic>
#include <array>
#include <cmath>

namespace NativeReflectionProbeKL {
namespace {
std::atomic<bool> enabled{false};
std::atomic<std::uint32_t> sampleTick{0};
std::atomic<std::uint32_t> samplerEpoch{1};
std::atomic<ReadContext> contextReader{nullptr};
SRWLOCK lock=SRWLOCK_INIT;
Stats stats;
std::uint32_t Hash(const void* data,std::size_t size){
    const auto* p=static_cast<const unsigned char*>(data);std::uint32_t value=2166136261u;
    for(std::size_t i=0;i<size;++i){value^=p[i];value*=16777619u;}return value;
}
bool Finite(const float* values,std::size_t count){
    for(std::size_t i=0;i<count;++i)if(!std::isfinite(values[i]))return false;
    return true;
}
bool ReadConstants(IDirect3DDevice9* device,unsigned first,float* values,unsigned count){
    if(FAILED(device->GetVertexShaderConstantF(first,values,count))||!Finite(values,count*4)){
        std::memset(values,0,count*4*sizeof(float));return false;
    }
    return true;
}
}
void Configure(bool value){if(enabled.exchange(value,std::memory_order_acq_rel)!=value)++samplerEpoch;}
void ConfigureContext(ReadContext read){if(contextReader.exchange(read,std::memory_order_acq_rel)!=read)++samplerEpoch;}
bool Enabled(){return enabled.load(std::memory_order_acquire);}
void Observe(IDirect3DDevice9* device,std::uint64_t frameId,ScenePredicate scene){
    if(!device||!Enabled())return;
    thread_local StratifiedSampler sampler;thread_local std::uint32_t localEpoch=0;
    const auto epoch=samplerEpoch.load(std::memory_order_acquire);
    if(epoch!=localEpoch){sampler.Reset();localEpoch=epoch;}
    if(!sampler.Next(frameId))return;
    const auto now=GetTickCount();auto previous=sampleTick.load(std::memory_order_relaxed);
    do {if(previous&&now-previous<40)return;}
    while(!sampleTick.compare_exchange_weak(previous,now?now:1,std::memory_order_relaxed));
    // The observer is called for every native draw. Previously main evaluated
    // its cinematic/scene pointer walks before this cheap rejection, costing
    // tens of thousands of extra native reads per second when the probe was on.
    if(scene&&!scene())return;
    IDirect3DPixelShader9* shader=nullptr;UINT bytes=0;
    bool available=false,ctab=false;Sample next;next.tick=now;next.frameId=frameId;
    thread_local std::array<std::uint32_t,16384> code{};
    if(SUCCEEDED(device->GetPixelShader(&shader))&&shader){
        if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=sizeof(code)&&!(bytes&3)){
            UINT capacity=bytes;
            if(SUCCEEDED(shader->GetFunction(code.data(),&capacity))&&capacity==bytes){
                available=true;next.shaderHash=Hash(code.data(),bytes);ctab=ParseSamplers(code.data(),bytes,next.samplers);
            }
        }
        shader->Release();
    }
    next.ctabKnown=ctab;next.pixelAvailable=available;
    next.valid=available&&ctab&&(next.samplers.reflection||next.samplers.refraction||next.samplers.environment);
    // PS=NULL and stripped ps_1_1 are legitimate shadow techniques. Their VS
    // remains useful evidence, and this single getter is still after both gates.
    IDirect3DVertexShader9* vertex=nullptr;UINT vertexBytes=0;
    if(SUCCEEDED(device->GetVertexShader(&vertex))&&vertex){
        if(SUCCEEDED(vertex->GetFunction(nullptr,&vertexBytes))&&vertexBytes>=8&&vertexBytes<=sizeof(code)&&!(vertexBytes&3)){
            UINT capacity=vertexBytes;
            if(SUCCEEDED(vertex->GetFunction(code.data(),&capacity))&&capacity==vertexBytes){
                next.vertexAvailable=true;next.vertexHash=Hash(code.data(),vertexBytes);
                next.vertexCtab=ParseVertexConstants(code.data(),vertexBytes,next.vertex);
                if(next.vertex.eye.count==1)next.eyeRead=SUCCEEDED(device->GetVertexShaderConstantF(next.vertex.eye.start,next.eye,1));
                if(next.vertex.cube.count>=3&&next.vertex.cube.count<=4)next.cubeRead=SUCCEEDED(device->GetVertexShaderConstantF(next.vertex.cube.start,next.cube,next.vertex.cube.count));
                if(next.samplers.reflection){
                    next.planarMatricesAttempted=true;
                    next.planarMatricesRead=ReadConstants(device,0,next.planarMatrices,8);
                }
                for(unsigned slot=0;slot<ShadowRangeCount;++slot){
                    const auto range=next.vertex.shadow[slot];if(!range.count)continue;
                    next.shadowAttemptMask|=static_cast<std::uint16_t>(1u<<slot);
                    if(ReadConstants(device,range.start,next.shadowValues[slot],range.count))
                        next.shadowReadMask|=static_cast<std::uint16_t>(1u<<slot);
                }
                if(next.shadowAttemptMask&&next.vertex.modelViewProjection.count==4){
                    next.shadowModelViewProjectionAttempted=true;
                    next.shadowModelViewProjectionRead=ReadConstants(device,next.vertex.modelViewProjection.start,next.shadowModelViewProjection,4);
                }
            }
        }
        vertex->Release();
    }
    const bool observed=available||next.vertexAvailable;
    if(observed){
        if(const auto read=contextReader.load(std::memory_order_acquire)){
            DrawContext context;
            if(!read(context))next.contextResult=ContextResult::Unavailable;
            else if(!context.state||!context.generation||context.eyeIndex>1||
                    !Finite(context.world,12)||!Finite(context.eye,12)||!Finite(context.projection,16))
                next.contextResult=ContextResult::Invalid;
            else {next.context=context;next.contextResult=ContextResult::Available;}
        }
        // Stripped CTAB is common in shipping shaders. Preserve a bounded
        // fallback (samplers 0..3 + shader/VS/target) rather than mistaking
        // missing names for proof that reflections are absent.
        const unsigned mask=next.valid?(next.samplers.reflection|next.samplers.refraction|next.samplers.environment|next.samplers.mask|next.samplers.falloff):0x0fu;
        for(unsigned i=0;i<16;++i){if(!(mask&(1u<<i)))continue;IDirect3DBaseTexture9* texture=nullptr;
            if(SUCCEEDED(device->GetTexture(i,&texture))&&texture){auto& output=next.textures[i];
                output.identity=reinterpret_cast<std::uintptr_t>(texture);output.kind=static_cast<std::uint32_t>(texture->GetType());
                if(output.kind==D3DRTYPE_TEXTURE){D3DSURFACE_DESC desc{};
                    if(SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc))){output.width=desc.Width;output.height=desc.Height;output.format=desc.Format;}}
                else if(output.kind==D3DRTYPE_CUBETEXTURE){D3DSURFACE_DESC desc{};
                    if(SUCCEEDED(static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0,&desc))){output.width=desc.Width;output.height=desc.Height;output.format=desc.Format;}}
                texture->Release();
            }
        }
        IDirect3DSurface9* target=nullptr;
        if(SUCCEEDED(device->GetRenderTarget(0,&target))&&target){D3DSURFACE_DESC desc{};next.target=reinterpret_cast<std::uintptr_t>(target);
            if(SUCCEEDED(target->GetDesc(&desc))){next.targetWidth=desc.Width;next.targetHeight=desc.Height;next.targetFormat=desc.Format;}target->Release();}
        D3DVIEWPORT9 view{};if(SUCCEEDED(device->GetViewport(&view))){next.viewportX=view.X;next.viewportY=view.Y;next.viewportWidth=view.Width;next.viewportHeight=view.Height;}
        DWORD value=0;if(SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE,&value)))next.zEnabled=value;
        if(SUCCEEDED(device->GetRenderState(D3DRS_ZWRITEENABLE,&value)))next.zWrite=value;
        if(SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&value)))next.alphaBlend=value;
    }
    AcquireSRWLockExclusive(&lock);
    // Reset/disable may race the read-only COM getters. Never publish an old
    // device observation into the newly reset history.
    if(epoch!=samplerEpoch.load(std::memory_order_acquire)) {ReleaseSRWLockExclusive(&lock);return;}
    ++stats.samples;
    if(available)++stats.shaderReads;else ++stats.shaderUnavailable;
    if(ctab)++stats.ctab;if(observed){
        ++stats.recorded;next.sequence=++stats.latestSequence;
        if(next.valid){++stats.named;stats.lastNamed=next;}
        stats.history[(stats.recorded-1)%HistoryLimit]=next;
        stats.historyCount=static_cast<unsigned>(stats.recorded<HistoryLimit?stats.recorded:HistoryLimit);
        stats.historyOverwritten=stats.recorded-stats.historyCount;
    }
    ReleaseSRWLockExclusive(&lock);
}
Stats Snapshot(){AcquireSRWLockShared(&lock);auto next=stats;
    // Return chronological order even after wrap. Statistics and all fields
    // were collected synchronously at the same draw and under the same lock.
    for(unsigned i=0;i<next.historyCount;++i)
        next.history[i]=stats.history[(stats.recorded-stats.historyCount+i)%HistoryLimit];
    ReleaseSRWLockShared(&lock);next.enabled=Enabled();return next;}
void Reset(){sampleTick.store(0);++samplerEpoch;AcquireSRWLockExclusive(&lock);
    const auto sequence=stats.latestSequence;stats={};stats.latestSequence=sequence;
    ReleaseSRWLockExclusive(&lock);}
}
