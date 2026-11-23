#pragma once
#include <cstdint>
#include "native_camera_math.hpp"
namespace NativeRopeKL {
using LogCallback=void(*)(const char*);
bool Install(bool enabled,LogCallback log);
// Only an applied first-person eye may authorize local rope suppression.
void SetFrame(std::uintptr_t player,std::uint64_t frame,bool firstPerson);
void ClearFrame();
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva);
bool SuppressDraw();
struct Stats {
    bool installed=false;
    std::uint64_t frames=0,owned=0,candidates=0,suppressed=0,rejected=0,cacheHits=0;
    std::uint64_t multiPassSuppressed=0,bindRetired=0,scopeBudgetRetired=0,rigidBoundaries=0,rigidCandidates=0;
};
Stats Snapshot();
}
