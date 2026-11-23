#pragma once
#include <d3d12.h>
#include <d3d9.h>
#include <windows.h>

// Official Windows D3D9On12 ABI, absent from the pinned MinGW SDK headers.
// https://microsoft.github.io/DirectX-Specs/d3d/TranslationLayerResourceInterop.html
// Used by the isolated renderer probe and opt-in OpenXR GPU transport.
struct Args9On12 {
  BOOL enable;
  IUnknown *device;
  IUnknown *queues[2];
  UINT numQueues, nodeMask;
};
struct Device9On12 : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID, void **) = 0;
  virtual HRESULT STDMETHODCALLTYPE UnwrapUnderlyingResource(
      IDirect3DResource9 *, ID3D12CommandQueue *, REFIID, void **) = 0;
  virtual HRESULT STDMETHODCALLTYPE ReturnUnderlyingResource(
      IDirect3DResource9 *, UINT, UINT64 *, ID3D12Fence **) = 0;
};
inline constexpr GUID iid9on12 = {
    0xe7fda234,
    0xb589,
    0x4049,
    {0x94, 0x0d, 0x88, 0x78, 0x97, 0x75, 0x31, 0xc8}};
