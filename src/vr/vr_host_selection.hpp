#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

// The bridge resolves its own package INI: Steam/attach need not inherit the
// launcher's environment. Only the new compositor receives this environment.
namespace VRHostSelection {
enum class Backend { Meta,OpenXR,Invalid };
inline Backend Parse(const wchar_t* value){
    if(!value||!*value||!_wcsicmp(value,L"Meta"))return Backend::Meta;
    return !_wcsicmp(value,L"OpenXR")?Backend::OpenXR:Backend::Invalid;
}
inline const wchar_t* Executable(Backend backend){
    return backend==Backend::Meta?L"KaneLynchVRHost.exe":backend==Backend::OpenXR?L"KaneLynchOpenXRHost.exe":nullptr;
}
inline bool AutoMetaFallback(bool automatic,bool meta,bool attempted,LONG error){
    // Missing installed runtime and real LibOVR NoHMD are qualified. A
    // graphics failure or explicit profile never silently changes runtime.
    return automatic&&meta&&!attempted&&(error==-1007||error==-91001);
}
inline bool Variable(const std::wstring& entry,const wchar_t* name){
    const auto count=std::wcslen(name);return entry.size()>count&&entry[count]==L'='&&!_wcsnicmp(entry.c_str(),name,count);
}
inline std::vector<wchar_t> ChildEnvironment(const wchar_t* inherited,Backend backend,const std::wstring& runtimeJson){
    std::vector<std::wstring> entries;
    if(inherited)for(auto* at=inherited;*at;at+=std::wcslen(at)+1){
        std::wstring entry(at);
        if(!Variable(entry,L"XR_RUNTIME_JSON")&&!Variable(entry,L"KL1_VR_BACKEND"))entries.push_back(std::move(entry));
    }
    entries.emplace_back(backend==Backend::OpenXR?L"KL1_VR_BACKEND=OpenXR":L"KL1_VR_BACKEND=Meta");
    if(backend==Backend::OpenXR&&!runtimeJson.empty())entries.emplace_back(L"XR_RUNTIME_JSON="+runtimeJson);
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> block;
    for(const auto& entry:entries){block.insert(block.end(),entry.begin(),entry.end());block.push_back(0);}
    block.push_back(0);return block;
}
inline bool ChildEnvironment(Backend backend,const std::wstring& runtimeJson,std::vector<wchar_t>& block){
    if(backend==Backend::Invalid)return false;
    auto* inherited=GetEnvironmentStringsW();if(!inherited)return false;
    block=ChildEnvironment(inherited,backend,runtimeJson);FreeEnvironmentStringsW(inherited);return true;
}
}
