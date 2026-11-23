#pragma once
#include <cwchar>
#include <initializer_list>
#if __has_include("kl1_built_on12.hpp")
#include "kl1_built_on12.hpp"
#endif
namespace RendererIdentity {
enum class Kind { Original, DXVK, D3D9On12, Unknown };
inline const wchar_t* Name(Kind kind){
    switch(kind){case Kind::Original:return L"Original";case Kind::DXVK:return L"DXVK";case Kind::D3D9On12:return L"D3D9On12";default:return L"Unknown";}
}
inline Kind Parse(const wchar_t* text){
    for(auto kind:{Kind::Original,Kind::DXVK,Kind::D3D9On12})if(!_wcsicmp(text,Name(kind)))return kind;
    return Kind::Unknown;
}
// vendor/dxvk-x86/provenance.json: pinned DXVK 3.1.1 x86 D3D9 DLL.
inline constexpr const char* DXVKDllSha256="265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa";
inline constexpr const char* On12LegacyDllSha256="c5334b75bb6d3af9d955aa50b7bb38ee93b62265e4fad0c35e7772bd05a149c3";
inline constexpr const char* On12ResearchDllSha256="365f0f3d108cca5b948131b1492212348d29f4bd64a50f7b404abadadcd50451";
inline constexpr const char* On12StockDllSha256="ea924b3f127ebaac344c79d1726b81056e3c7790b90ff2e2dc507f2cf630a996";
inline constexpr const char* On12ReleaseDllSha256="6cf2c4e7c2f6b2ad7d7fb485fd18abb64d0e03b7d3266a76184284cadc963509";
#ifdef KL1_BUILT_ON12_SHA256
inline constexpr const char* On12DllSha256=KL1_BUILT_ON12_SHA256;
#else
inline constexpr const char* On12DllSha256=On12ReleaseDllSha256;
#endif
}
