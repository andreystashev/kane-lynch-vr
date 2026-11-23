// Compile against the SDK's C vtables so a mistaken slot cannot silently
// redirect a game call to a function with a different ABI.
#define CINTERFACE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstddef>
#include <cstdio>
#include "d3d9_slots.hpp"
#include "native_buffer_flush_kl.hpp"

static_assert(offsetof(IDirect3DDevice9Vtbl,QueryInterface)/sizeof(void*)==NativeBufferFlushKL::DeviceQueryInterfaceSlot);
static_assert(offsetof(IDirect3DDevice9Vtbl,GetDeviceCaps)/sizeof(void*)==NativeBufferFlushKL::DeviceCapsSlot);
static_assert(offsetof(IDirect3DDevice9Vtbl,GetCreationParameters)/sizeof(void*)==NativeBufferFlushKL::DeviceCreationParametersSlot);

static_assert(offsetof(IDirect3DDevice9Vtbl,Reset)/sizeof(void*)==D3D9Slots::Reset);
static_assert(offsetof(IDirect3DDevice9Vtbl,Present)/sizeof(void*)==D3D9Slots::Present);
static_assert(offsetof(IDirect3DDevice9Vtbl,EndScene)/sizeof(void*)==D3D9Slots::EndScene);
static_assert(offsetof(IDirect3DDevice9Vtbl,SetTransform)/sizeof(void*)==D3D9Slots::SetTransform);
static_assert(offsetof(IDirect3DDevice9Vtbl,DrawPrimitive)/sizeof(void*)==D3D9Slots::DrawPrimitive);
static_assert(offsetof(IDirect3DDevice9Vtbl,DrawIndexedPrimitive)/sizeof(void*)==D3D9Slots::DrawIndexedPrimitive);
static_assert(offsetof(IDirect3DDevice9Vtbl,SetVertexShaderConstantF)/sizeof(void*)==D3D9Slots::SetVertexShaderConstantF);
static_assert(offsetof(IDirect3DSwapChain9Vtbl,Present)/sizeof(void*)==D3D9Slots::SwapChainPresent);

int main() {
    std::puts("D3D9 capture vtable slots match the SDK ABI.");
}
