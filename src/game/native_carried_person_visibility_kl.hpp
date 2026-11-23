#pragma once
#include <d3d9.h>
#include <cstdint>
namespace NativeCarriedPersonVisibilityKL {
using LogCallback=void(*)(const char*);
struct Stats {bool installed=false;std::uint64_t frames=0,carrying=0,candidates=0,clipped=0,restored=0,foreign=0,identityRejected=0,shaderRejected=0,unsupported=0,failed=0;};
bool Install(bool enabled,LogCallback log);
void SetFrame(std::uintptr_t player,float unitsPerMetre,bool firstPerson);
void ClearFrame();
void* BeginDraw(IDirect3DDevice9* device);
HRESULT FinishDraw(void* token,HRESULT result);
void InvalidateDevice();
Stats Snapshot();
}
