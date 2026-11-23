#pragma once
#include "d3d9_capture.hpp"
#include <cstddef>

// Diagnostic capture only. No native state, shader, transform, or render target
// is changed. Color copies go to private surfaces; failures remain explicit.
namespace NativeVisualTraceKL {
struct Scene {
    std::uint64_t frame=0,epoch=0;
    unsigned eye=0,tick=0;
    bool tracked=false,applied=false,cinematic=false,scope=false,gameplay=false,support=false;
};
struct Stats {
    bool enabled=false,active=false,budgetReached=false;
    std::uint64_t frames=0,written=0,draws=0,droppedDraws=0,images=0,copyFailures=0,
        readbackFailures=0,writeFailures=0,queueDrops=0,bytes=0;
    std::uint64_t recordingSuspended=0,memorySuspended=0,previewSkipped=0;
    wchar_t directory[MAX_PATH]{};
};
using ContextCallback=void(*)();
void Configure(const wchar_t* packageFolder,bool enabled,ContextCallback context);
bool Active();
bool Enabled();
void TraceDraw(IDirect3DDevice9*,const D3D9Capture::DrawCall&,bool after,HRESULT result);
// Evidence is copied immediately. Tags:10 native owner,11 camera,12 effects,
// 13 closed producer queue. Schema JSON is supplied with the build.
void Evidence(unsigned tag,const void*,std::size_t,std::uint64_t sequence=0);
void Detail(std::uint64_t key);
void BeforeCapture(IDirect3DDevice9*,IDirect3DSurface9*,const Scene&);
void FrameBoundary(const Scene& completed);
void Reset();
Stats Snapshot();
}
