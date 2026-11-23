#pragma once
#include <cwctype>
#include <string>

// Name classification is only a fallback for automatic playback selection.
// Explicit endpoint IDs and runtime-provided IDs are handled separately.
namespace AudioEndpointNames {
inline int Family(std::wstring name){
    for(auto& c:name){
        // The product tokens are English. Also fold Cyrillic microphone labels
        // without depending on the process-wide C locale.
        if(c>=wchar_t(0x0410)&&c<=wchar_t(0x042F))c=wchar_t(c+0x20);
        else c=std::towlower(c);
    }
    const auto has=[&](const wchar_t* token){return name.find(token)!=std::wstring::npos;};
    // Some virtual microphone drivers expose a render endpoint too. Device
    // flow alone must not make one an automatic headset playback candidate.
    // Exact user/runtime endpoint selection is handled before this fallback.
    if(has(L"microphone")||has(L"\u043C\u0438\u043A\u0440\u043E\u0444\u043E\u043D"))return 0;
    if(has(L"oculus")||has(L"meta quest"))return 1;
    if(has(L"virtual desktop")||has(L"virtualdesktop"))return 2;
    if(has(L"steam streaming")||has(L"steamvr")||has(L"valve index")||has(L"vive")||has(L"pico")||has(L"windows mixed reality"))return 3;
    return 0;
}
}
