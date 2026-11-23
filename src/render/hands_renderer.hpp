#pragma once
#include <cstdint>
#include "vr_ipc.hpp"
#include "native_hand_hud_kl.hpp"
#include "native_aim_render_math.hpp"
struct IDirect3DDevice9;
struct IDirect3DSurface9;
namespace HandsRenderer {
struct Stats {
    bool enabled=false,connected=false;
    std::uint32_t validHands=0,ageMs=0;
    std::uint64_t frames=0,hands=0,failures=0;
    bool hudReady=false;
    std::uint64_t hudFrames=0,hudCards=0,hudRejected=0,hudFailures=0,hudTextureBuilds=0;
    std::uint64_t commandMarkerFrames=0,commandMarkers=0,commandMarkerRejected=0;
};
void Initialize(const wchar_t* folder);
// Render thread only, immediately before readback. Uses physical metres and
// the actual rendered asymmetric eye FOV. Native depth is intentionally not
// used until the game's postprocess depth surface is reliably identified.
bool Draw(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,
          const VRIPC::EyeMetadata& metadata,const float eyeOffset[3]);
// Independent of optional fallback gloves; current world-space visual wrists.
bool DrawHud(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeHandHudKL::Frame& frame,
             bool* contextDrawn=nullptr);
// Fixed native destination shields use only the current eye/player snapshot.
// They need no tracked wrist and never draw recipient icons or wrist cards.
bool DrawWorldMarkers(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeHandHudKL::Frame& frame);
bool DrawAim(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeAimRenderKL::Frame& frame);
unsigned DrawReloadProps(IDirect3DDevice9* device,const NativeAimRenderKL::Frame* frames,unsigned count);
// Render thread before Reset/device release. Releases DEFAULT-pool card textures.
void InvalidateDevice();
Stats Snapshot();
} // namespace HandsRenderer
