#pragma once
#include "launcher_paths.hpp"
#include "meta_runtime.hpp"

// One read-only preflight for settings, CLI and injected bridge. The bridge
// resolves again from its own package: an already running Steam client and an
// attached game do not inherit the launcher's environment.
namespace Kl1LaunchConnection {
struct Plan {VrModConnection::Choice connection;std::wstring host;VrModConnection::Mode requested=VrModConnection::Mode::Auto;std::wstring customRuntime;};
inline bool X64Image(const std::wstring& path,bool dll){
    using namespace VrModConnection;
    Detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)return false;
    IMAGE_DOS_HEADER dos{};DWORD read=0;
    if(!ReadFile(file.value,&dos,sizeof dos,&read,nullptr)||read!=sizeof dos||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<64||dos.e_lfanew>4*1024*1024)return false;
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;IMAGE_NT_HEADERS64 nt{};
    if(!SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN)||!ReadFile(file.value,&nt,sizeof nt,&read,nullptr)||read!=sizeof nt)return false;
    return nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&bool(nt.FileHeader.Characteristics&IMAGE_FILE_DLL)==dll;
}
inline std::wstring ActiveRuntime(){
    return VrModConnection::Detail::Registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\OpenXR\\1",L"ActiveRuntime",KEY_WOW64_64KEY);
}
inline bool ResolveActiveOpenXR(const std::wstring& activeRuntime,VrModConnection::Choice& choice,std::wstring& error){
    if(activeRuntime.empty())return false;
    const auto separator=activeRuntime.find_last_of(L"\\/");
    const auto* filename=activeRuntime.c_str()+(separator==std::wstring::npos?0:separator+1);
    return _wcsicmp(filename,L"oculus_openxr_64.json")!=0&&
        VrModConnection::Resolve(VrModConnection::Mode::OpenXR,activeRuntime,false,choice,error);
}
inline bool Prepare(const std::wstring& folder,VrModConnection::Mode requested,const std::wstring& custom,bool metaInstalled,
                    const std::wstring& activeRuntime,Plan& plan,std::wstring& error){
    using namespace VrModConnection;
    plan={};plan.requested=requested;plan.customRuntime=custom;
    // An installed Meta DLL does not establish which headset is in use.
    const bool useActive=requested==Mode::Auto&&metaInstalled&&
        ResolveActiveOpenXR(activeRuntime,plan.connection,error);
    if(!useActive){
        const bool openxrRequested=IsOpenXR(requested)||(requested==Mode::Auto&&!metaInstalled);
        const bool hasCustom=openxrRequested&&!VrModIni::Trim(custom).empty();
        const auto manifest=hasCustom?Kl1LauncherPaths::ResolveFile(folder,custom):std::wstring{};
        if(hasCustom&&manifest.empty())return Detail::Fail(error,L"The OpenXR runtime path is invalid. Choose its JSON manifest or leave it empty for automatic detection.");
        if(!Resolve(requested,manifest,metaInstalled,plan.connection,error))return false;
    }
    if(plan.connection.mode==Mode::Meta&&!metaInstalled)return Detail::Fail(error,L"The installed x64 Meta PC runtime was not found. Install Quest Link, or choose SteamVR / Virtual Desktop in Connection & game.");
    plan.host=Kl1LauncherPaths::Join(folder,IsOpenXR(plan.connection.mode)?L"KaneLynchOpenXRHost.exe":L"KaneLynchVRHost.exe");
    if(!X64Image(plan.host,false))return Detail::Fail(error,L"The selected x64 VR host is missing or invalid. Reinstall the complete Kane & Lynch VR package.");
    if(IsOpenXR(plan.connection.mode)&&!X64Image(Kl1LauncherPaths::Join(folder,L"openxr_loader.dll"),true))
        return Detail::Fail(error,L"The x64 OpenXR loader is missing or invalid. Reinstall the complete Kane & Lynch VR package.");
    return true;
}
inline bool Prepare(const std::wstring& folder,VrModConnection::Mode requested,const std::wstring& custom,bool metaInstalled,Plan& plan,std::wstring& error){
    using namespace VrModConnection;
    const auto activeRuntime=requested==Mode::Auto&&metaInstalled?ActiveRuntime():std::wstring{};
    return Prepare(folder,requested,custom,metaInstalled,activeRuntime,plan,error);
}
inline bool Prepare(const std::wstring& folder,Plan& plan,std::wstring& error){
    VrModIni::File settings;if(!VrModIni::Load(Kl1LauncherPaths::Join(folder,L"vr.ini"),settings,error,true))return false;
    const auto mode=VrModConnection::Parse(VrModIni::Read(settings,L"Connection",L"Mode",L"Auto"));
    return Prepare(folder,mode,VrModIni::Read(settings,L"Connection",L"RuntimeJson",L""),MetaRuntime::Installed(true),plan,error);
}
}
