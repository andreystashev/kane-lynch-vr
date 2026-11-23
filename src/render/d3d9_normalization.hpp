#pragma once
#include <d3d9.h>

// Preserve the game's dimensions and multisampling. MSAA is resolved into a
// same-format render target before an optional BGRA conversion; neither copy
// asks the driver to scale the image or changes game presentation settings.
namespace D3D9Normalization {
inline bool NeedsNativeResolve(const D3DSURFACE_DESC& source){
    return source.MultiSampleType!=D3DMULTISAMPLE_NONE&&source.Format!=D3DFMT_A8R8G8B8;
}
enum class Step {NativeResolve,BGRACopy};
inline HRESULT Copy(IDirect3DDevice9* device,IDirect3DSurface9* source,const D3DSURFACE_DESC& description,
                    IDirect3DSurface9* nativeResolve,IDirect3DSurface9* bgra,Step& step){
    if(NeedsNativeResolve(description)){
        step=Step::NativeResolve;
        if(!nativeResolve)return D3DERR_INVALIDCALL;
        const auto result=device->StretchRect(source,nullptr,nativeResolve,nullptr,D3DTEXF_NONE);
        if(FAILED(result))return result;
        source=nativeResolve;
    }
    step=Step::BGRACopy;return device->StretchRect(source,nullptr,bgra,nullptr,D3DTEXF_NONE);
}
}
