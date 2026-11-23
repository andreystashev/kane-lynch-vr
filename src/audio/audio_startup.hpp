#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#define OVR_CAPI_NO_UTILS
#include "OVR_CAPI.h"
#include "audio_bootstrap.hpp"
#include "audio_profile_math.hpp"
#include "audio_endpoint_selection.hpp"
#include "kl1_launch_connection.hpp"
#include "profile.hpp"

namespace AudioStartup {
struct Endpoint {std::wstring name,id;GUID guid{};};
inline std::wstring Property(IMMDevice* device,const PROPERTYKEY& key) {
    IPropertyStore* store=nullptr;std::wstring value;
    if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store))) {
        PROPVARIANT item{};if(SUCCEEDED(store->GetValue(key,&item))&&item.vt==VT_LPWSTR&&item.pwszVal)value=item.pwszVal;
        PropVariantClear(&item);store->Release();
    }return value;
}
inline std::wstring MetaGuid(bool requireConnectedHmd=false) {
    const auto discovered=MetaRuntime::TryLoad(MetaRuntime::Directories(),false,MetaRuntime::Exists,
        [](const std::wstring& path){return LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);},
        []{return GetLastError();});
    HMODULE runtime=discovered.module;
    if(!runtime)return {};
    const auto initialize=reinterpret_cast<decltype(&ovr_Initialize)>(GetProcAddress(runtime,"ovr_Initialize"));
    const auto shutdown=reinterpret_cast<decltype(&ovr_Shutdown)>(GetProcAddress(runtime,"ovr_Shutdown"));
    const auto describe=reinterpret_cast<decltype(&ovr_GetHmdDesc)>(GetProcAddress(runtime,"ovr_GetHmdDesc"));
    using Guid=ovrResult(OVR_CDECL*)(wchar_t*);
    const auto audio=reinterpret_cast<Guid>(GetProcAddress(runtime,"ovr_GetAudioDeviceOutGuidStr"));
    std::wstring selected;
    if(initialize&&shutdown&&audio) {
        ovrInitParams params{};params.Flags=ovrInit_RequestVersion;params.RequestedMinorVersion=OVR_MINOR_VERSION;params.ConnectionTimeoutMS=1000;
        if(OVR_SUCCESS(initialize(&params))) {
            const bool connected=!requireConnectedHmd||(describe&&describe(nullptr).Type!=ovrHmd_None);
            wchar_t guid[128]{};if(connected&&OVR_SUCCESS(audio(guid))&&!guid[127])selected=guid;shutdown();
        }
    }
    FreeLibrary(runtime);return selected;
}
inline bool Select(const std::wstring& output,Endpoint& selected,const std::string& backend="Auto") {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;
    IMMDeviceEnumerator* enumerator=nullptr;IMMDeviceCollection* collection=nullptr;std::vector<Endpoint> endpoints;
    const auto finish=[&](){if(collection)collection->Release();if(enumerator)enumerator->Release();if(SUCCEEDED(init))CoUninitialize();};
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator)))||
       FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection))) {finish();return false;}
    IMMDevice* primary=nullptr;std::wstring defaultId;
    if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&primary))){
        LPWSTR id=nullptr;if(SUCCEEDED(primary->GetId(&id))){defaultId=id;CoTaskMemFree(id);}primary->Release();
    }
    UINT count=0;collection->GetCount(&count);std::vector<AudioRoute::Endpoint> choices;
    constexpr PROPERTYKEY endpointGuid{{0x1da5d803,0xd492,0x4edd,{0x8c,0x23,0xe0,0xc0,0xff,0xee,0x7f,0x0e}},4};
    for(UINT i=0;i<count;++i) {
        IMMDevice* device=nullptr;if(FAILED(collection->Item(i,&device)))continue;
        Endpoint value;LPWSTR id=nullptr;if(SUCCEEDED(device->GetId(&id))){value.id=id;CoTaskMemFree(id);}
        value.name=Property(device,PKEY_Device_FriendlyName);const auto text=Property(device,endpointGuid);
        if(!text.empty()&&SUCCEEDED(CLSIDFromString(text.c_str(),&value.guid))&&!IsEqualGUID(value.guid,GUID_NULL)){
            choices.push_back({value.id,value.name,text,DEVICE_STATE_ACTIVE,_wcsicmp(value.id.c_str(),defaultId.c_str())==0});
            endpoints.push_back(value);
        }
        device->Release();
    }
    finish();
    const bool automatic=_wcsicmp(output.c_str(),L"Auto")==0||_wcsicmp(output.c_str(),L"Headset")==0;
    if(automatic&&backend.empty())return false;
    // Explicit Meta keeps its runtime hint. Generic modes first respect an
    // existing recognized playback default, never a mere installed driver.
    const auto hint=automatic&&backend=="Meta"?MetaGuid():std::wstring{};
    auto decision=AudioEndpointSelection::Select(choices,output,hint,backend);
    // Old virtual drivers can remain active while desktop speakers are the
    // default. Only actual connected Meta hardware may resolve that otherwise
    // ambiguous Auto case. Do not query Meta for VDXR/SteamVR/OpenXR or after
    // a recognized VD/Steam/default endpoint has already been selected.
    if(automatic&&backend=="Auto"&&decision.reason=="ambiguous"){
        VrModConnection::Choice openxr;
        std::wstring error;
        if(!Kl1LaunchConnection::ResolveActiveOpenXR(Kl1LaunchConnection::ActiveRuntime(),openxr,error))
            decision=AudioEndpointSelection::ConnectedMetaFallback(choices,output,backend,decision,MetaGuid(true));
    }
    if(decision.index<0||unsigned(decision.index)>=endpoints.size())return false;
    selected=endpoints[unsigned(decision.index)];return true;
}
inline bool Bytes(const std::wstring& path,std::string& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};bool okay=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=1024*1024;
    if(okay){bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD got=0;okay=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size();}
    CloseHandle(file);return okay;
}
inline bool Write(const std::wstring& path,const std::string& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD copied=0;const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&copied,nullptr)&&copied==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);if(!okay)DeleteFileW(path.c_str());return okay;
}
inline bool DisableEaxProfile() {
    wchar_t folder[MAX_PATH]{};const DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,MAX_PATH);if(!count||count>=MAX_PATH)return false;
    const std::wstring path=std::wstring(folder)+L"\\kaneandlynch\\main.ini";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND;
    std::string source,updated;if(!Bytes(path,source))return false;
    const auto result=AudioProfileMath::DisableEax(source,updated);
    if(result==AudioProfileMath::Result::Invalid)return false;if(result==AudioProfileMath::Result::Unchanged)return true;
    char digest[65]{};if(!HashFile(path.c_str(),digest))return false;
    wchar_t hash[65]{};MultiByteToWideChar(CP_UTF8,0,digest,-1,hash,65);
    const std::wstring backup=path+L".before-vr-audio-"+std::wstring(hash,12)+L".bak";
    std::string checked;
    if(!Bytes(backup,checked)&&!Write(backup,source))return false;
    if(!Bytes(backup,checked)||checked!=source)return false;
    const auto temporary=path+L".vr-audio-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    if(!Write(temporary,updated))return false;
    if(!Bytes(path,checked)||checked!=source||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temporary.c_str());return false;}
    if(!Bytes(path,checked)||checked!=updated)return false;
    std::wprintf(L"VR audio: disabled legacy EAX only; original profile backed up at %ls\n",backup.c_str());return true;
}
inline bool PrepareBootstrap(const std::wstring& package,const wchar_t* game,AudioBootstrap::Request& request) {
    const auto module=package+L"KaneLynchVRAudio.dll";
    if(module.size()>=MAX_PATH||wcslen(game)>=MAX_PATH)return false;
    request.entryRva=AudioBootstrap::EntryRva(module.c_str());
    if(!request.entryRva)return false;
    std::wcscpy(request.gamePath,game);std::wcscpy(request.modulePath,module.c_str());
    return true;
}
inline bool Prepare(const std::wstring& package,const wchar_t* game,AudioBootstrap::Request& request) {
    const auto settings=package+L"vr.ini";
    if(!GetPrivateProfileIntW(L"Audio",L"Enabled",1,settings.c_str()))return false;
    wchar_t output[1024]{};GetPrivateProfileStringW(L"Audio",L"Output",L"Auto",output,1024,settings.c_str());
    if(_wcsicmp(output,L"Default")==0)return false;
    wchar_t connection[64]{};GetPrivateProfileStringW(L"Connection",L"Mode",L"Auto",connection,64,settings.c_str());
    const auto backend=AudioEndpointSelection::Backend(connection);
    Endpoint selected;
    if(!Select(output,selected,backend)) {std::puts("VR audio: headset output unavailable or ambiguous; keeping the game's audio selection.");return false;}
    if(!PrepareBootstrap(package,game,request)) {std::puts("VR audio: early audio module is unavailable; keeping the game's audio selection.");return false;}
    if(GetPrivateProfileIntW(L"Audio",L"DisableEAX",1,settings.c_str())&&!DisableEaxProfile()) {std::puts("VR audio: profile could not be safely prepared; keeping the game's audio selection.");return false;}
    request.endpoint=selected.guid;request.fallback=GetPrivateProfileIntW(L"Audio",L"FallbackToDefault",1,settings.c_str())!=0;
    std::wprintf(L"VR audio: preparing explicit headset output before game startup: %ls\n",selected.name.c_str());return true;
}
}
