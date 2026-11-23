#pragma once
#include "native_camera_math.hpp"
#include <cstdint>
namespace NativeGrenadeVisualKL {
bool InitializeVerifiedImage(std::uintptr_t image);
void SetFrame(std::uintptr_t player,std::uint64_t pair,bool enabled);
void ClearFrame();
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source);
bool SuppressDraw();
struct Stats {bool verified=false;std::uint64_t frames=0,candidates=0,owned=0,suppressed=0,rejected=0;};
Stats Snapshot();
}
