#pragma once
#include <cstdint>
#include <windows.h>
struct IDirect3DDevice9;
namespace NativeSkinLightingKL {
struct Stats {std::uint64_t candidates=0,matched=0,draws=0,restored=0,failed=0,cacheHits=0,cacheMisses=0;};
// Compose after effects BeginDraw; restore in reverse order before effects.
// Only exact native weighted shaders on an owned coupled normal skin draw.
void* BeginDraw(IDirect3DDevice9* device);
HRESULT FinishDraw(void* token,HRESULT result);
void InvalidateDevice();
Stats Snapshot();
}
