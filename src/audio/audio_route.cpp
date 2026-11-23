#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "audio_route.hpp"
#include "audio_endpoint_names.hpp"
#include "audio_endpoint_selection.hpp"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <dsound.h>
#include <winstring.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <utility>

namespace AudioRoute { namespace {
template<class T> void Release(T*& p){if(p)p->Release();p=nullptr;}
bool Equal(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
std::string Utf8(const std::wstring& text){
    if(text.empty())return {};
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(bytes,0);if(bytes)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),bytes,nullptr,nullptr);return result;
}
std::wstring Property(IMMDevice* device,const PROPERTYKEY& key){
    IPropertyStore* properties=nullptr;std::wstring result;
    if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){
        PROPVARIANT value{};if(SUCCEEDED(properties->GetValue(key,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)result=value.pwszVal;
        PropVariantClear(&value);properties->Release();
    }return result;
}
HRESULT Devices(IMMDeviceEnumerator* enumerator,std::vector<Endpoint>& result){
    result.clear();if(!enumerator)return E_NOINTERFACE;
    IMMDevice* primary=nullptr;std::wstring defaultId;
    if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&primary))){LPWSTR id=nullptr;if(SUCCEEDED(primary->GetId(&id))){defaultId=id;CoTaskMemFree(id);}primary->Release();}
    IMMDeviceCollection* collection=nullptr;HRESULT hr=enumerator->EnumAudioEndpoints(eRender,DEVICE_STATEMASK_ALL,&collection);if(FAILED(hr))return hr;
    UINT count=0;hr=collection->GetCount(&count);
    if(SUCCEEDED(hr)){for(UINT i=0;i<count;++i){
        IMMDevice* device=nullptr;if(FAILED(collection->Item(i,&device)))continue;
        Endpoint endpoint;LPWSTR id=nullptr;
        if(SUCCEEDED(device->GetId(&id))){endpoint.id=id;CoTaskMemFree(id);device->GetState(&endpoint.state);
            constexpr PROPERTYKEY endpointGuid{{0x1da5d803,0xd492,0x4edd,{0x8c,0x23,0xe0,0xc0,0xff,0xee,0x7f,0x0e}},4};
            endpoint.name=Property(device,PKEY_Device_FriendlyName);endpoint.guid=Property(device,endpointGuid);endpoint.isDefault=Equal(endpoint.id,defaultId);result.push_back(std::move(endpoint));}
        device->Release();
    }}
    collection->Release();return hr;
}

// Private Windows per-application policy ABI, as documented by the EarTrumpet
// project. Only these two known QueryInterface contracts are used; a missing
// contract disables routing. The 19 preceding methods must never be called.
// Source: File-New-Project/EarTrumpet, Interop/MMDeviceAPI/
// IAudioPolicyConfigFactoryVariantFor21H2.cs and ...ForDownlevel.cs.
// The accompanying EarTrumpet notice is distributed with the release.
struct PolicyInterface: IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*,IID**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*)=0;
#define RESERVED(n) virtual HRESULT STDMETHODCALLTYPE Reserved##n()=0;
    RESERVED(0) RESERVED(1) RESERVED(2) RESERVED(3) RESERVED(4) RESERVED(5)
    RESERVED(6) RESERVED(7) RESERVED(8) RESERVED(9) RESERVED(10) RESERVED(11)
    RESERVED(12) RESERVED(13) RESERVED(14) RESERVED(15) RESERVED(16) RESERVED(17) RESERVED(18)
#undef RESERVED
    virtual HRESULT STDMETHODCALLTYPE Set(UINT,EDataFlow,ERole,HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE Get(UINT,EDataFlow,ERole,HSTRING*)=0;
};
constexpr GUID NewPolicy{0xab3d4648,0xe242,0x459f,{0xb0,0x2f,0x54,0x1c,0x70,0x30,0x63,0x24}};
constexpr GUID OldPolicy{0x2a59116d,0x6c4f,0x45e0,{0xa7,0x4f,0x70,0x7e,0x3f,0xef,0x92,0x58}};
const wchar_t* Prefix=L"\\\\?\\SWD#MMDEVAPI#";
const wchar_t* Suffix=L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
std::wstring Packed(const std::wstring& endpoint){return endpoint.empty()?std::wstring():std::wstring(Prefix)+endpoint+Suffix;}
struct Policy {
    HMODULE library=nullptr;PolicyInterface* object=nullptr;bool initialized=false;
    HRESULT (WINAPI* createString)(PCNZWCH,UINT32,HSTRING*)=nullptr;
    HRESULT (WINAPI* deleteString)(HSTRING)=nullptr;
    PCWSTR (WINAPI* rawString)(HSTRING,UINT32*)=nullptr;
    void (WINAPI* uninitialize)()=nullptr;
    HRESULT Open(){
        if(object)return S_OK;
        library=LoadLibraryExW(L"combase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)return HRESULT_FROM_WIN32(GetLastError());
        const auto init=reinterpret_cast<HRESULT(WINAPI*)(int)>(GetProcAddress(library,"RoInitialize"));
        const auto factory=reinterpret_cast<HRESULT(WINAPI*)(HSTRING,REFIID,void**)>(GetProcAddress(library,"RoGetActivationFactory"));
        createString=reinterpret_cast<decltype(createString)>(GetProcAddress(library,"WindowsCreateString"));
        deleteString=reinterpret_cast<decltype(deleteString)>(GetProcAddress(library,"WindowsDeleteString"));
        rawString=reinterpret_cast<decltype(rawString)>(GetProcAddress(library,"WindowsGetStringRawBuffer"));
        uninitialize=reinterpret_cast<decltype(uninitialize)>(GetProcAddress(library,"RoUninitialize"));
        if(!init||!factory||!createString||!deleteString||!rawString||!uninitialize)return E_NOINTERFACE;
        HRESULT hr=init(1);initialized=SUCCEEDED(hr);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return hr;
        HSTRING name=nullptr;const wchar_t* runtime=L"Windows.Media.Internal.AudioPolicyConfig";
        hr=createString(runtime,UINT32(wcslen(runtime)),&name);if(FAILED(hr))return hr;
        hr=factory(name,NewPolicy,reinterpret_cast<void**>(&object));if(FAILED(hr))hr=factory(name,OldPolicy,reinterpret_cast<void**>(&object));
        deleteString(name);return hr;
    }
    HRESULT Get(DWORD pid,ERole role,std::wstring& value){
        value.clear();if(!object)return E_NOINTERFACE;
        HSTRING string=nullptr;const auto hr=object->Get(pid,eRender,role,&string);
        if(SUCCEEDED(hr)&&string){UINT32 length=0;const auto text=rawString(string,&length);if(text)value.assign(text,length);}
        if(string)deleteString(string);return hr;
    }
    HRESULT Set(DWORD pid,ERole role,const std::wstring& packed){
        if(!object)return E_NOINTERFACE;HSTRING string=nullptr;
        auto hr=packed.empty()?S_OK:createString(packed.c_str(),UINT32(packed.size()),&string);
        if(SUCCEEDED(hr))hr=object->Set(pid,eRender,role,string);if(string)deleteString(string);return hr;
    }
    void Close(){Release(object);if(initialized&&uninitialize)uninitialize();initialized=false;if(library)FreeLibrary(library);library=nullptr;}
};
struct State {
    Policy policy;IMMDeviceEnumerator* enumerator=nullptr;
    DWORD pid=0,lastTick=0;std::wstring output=L"Auto",lastHint,lastTarget;bool lastVrActive=false;
    std::string lastBackend;LogCallback logger=nullptr;bool active=false,fallback=true;
    std::array<Detail::Role,2> roles{};Status status;bool userOverride=false;
    std::wstring journal,identity;bool foreignJournal=false,recoveryPending=false;
    bool SaveJournal(){
        if(journal.empty())return true; // Only the explicitly opt-in own-PID smoke.
        if(foreignJournal){SetLastError(ERROR_ACCESS_DENIED);return false;}
        if(recoveryPending){SetLastError(ERROR_RETRY);return false;}
        if(!roles[0].changed&&!roles[1].changed)return DeleteFileW(journal.c_str())||GetLastError()==ERROR_FILE_NOT_FOUND;
        std::wstring text=L"\xFEFF[AudioRoute]\r\nOwner="+identity+L"\r\n";
        for(unsigned i=0;i<2;++i)if(roles[i].changed){
            const auto key=i?L"Multimedia":L"Console";
            if(roles[i].previous.find_first_of(L"\r\n")!=std::wstring::npos||roles[i].applied.find_first_of(L"\r\n")!=std::wstring::npos||roles[i].alternate.find_first_of(L"\r\n")!=std::wstring::npos){SetLastError(ERROR_INVALID_DATA);return false;}
            text+=std::wstring(key)+L"Previous="+roles[i].previous+L"\r\n"+key+L"Applied="+roles[i].applied+L"\r\n"+key+L"Alternate="+roles[i].alternate+L"\r\n"+key+L"AlternatePresent="+(roles[i].alternateValid?L"1":L"0")+L"\r\n";
        }
        const auto temporary=journal+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
        DWORD bytes=0;const DWORD expected=DWORD(text.size()*sizeof(wchar_t));
        bool ok=WriteFile(file,text.data(),expected,&bytes,nullptr)&&bytes==expected&&FlushFileBuffers(file);DWORD error=GetLastError();CloseHandle(file);
        if(ok){ok=MoveFileExW(temporary.c_str(),journal.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;error=GetLastError();}
        if(!ok){DeleteFileW(temporary.c_str());SetLastError(error);}return ok;
    }
    void LoadJournal(){
        if(journal.empty())return;
        WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(journal.c_str(),GetFileExInfoStandard,&info)){
            const auto error=GetLastError();recoveryPending=error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;return;}
        if((info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||info.nFileSizeHigh||info.nFileSizeLow>32768){foreignJournal=true;return;}
        wchar_t text[2048]{};GetPrivateProfileStringW(L"AudioRoute",L"Owner",L"",text,2048,journal.c_str());
        if(identity!=text){foreignJournal=true;return;}
        recoveryPending=true;if(!policy.object)return;
        std::array<Detail::Role,2> recovered{};
        for(unsigned i=0;i<2;++i){
            const auto key=i?L"Multimedia":L"Console";
            GetPrivateProfileStringW(L"AudioRoute",(std::wstring(key)+L"Applied").c_str(),L"",text,2048,journal.c_str());
            Detail::Role saved;saved.applied=text;saved.changed=!saved.applied.empty();
            GetPrivateProfileStringW(L"AudioRoute",(std::wstring(key)+L"Previous").c_str(),L"",text,2048,journal.c_str());saved.previous=text;
            GetPrivateProfileStringW(L"AudioRoute",(std::wstring(key)+L"Alternate").c_str(),L"",text,2048,journal.c_str());saved.alternate=text;
            saved.alternateValid=GetPrivateProfileIntW(L"AudioRoute",(std::wstring(key)+L"AlternatePresent").c_str(),saved.alternate.empty()?0:1,journal.c_str())!=0;
            // A temporary policy read failure must preserve the entire old
            // recovery journal and block new writes until all roles are known.
            if(FAILED(Detail::RecoverRole(saved,recovered[i],[&](std::wstring& value){return policy.Get(pid,i?eMultimedia:eConsole,value);})))return;
        }
        roles=std::move(recovered);recoveryPending=false;if(!SaveJournal())recoveryPending=true;
    }
    void Message(const char* code,const std::string& text,HRESULT error=S_OK){
        const bool different=status.state!=code||status.message!=text||status.error!=error;
        status.state=code;status.message=text;status.error=error;
        if(different&&logger){char message[1200];std::snprintf(message,sizeof(message),"Audio: %s (HRESULT 0x%08lx)",text.c_str(),static_cast<unsigned long>(error));logger(message);}
    }
    bool Restore(){
        if(recoveryPending){SetLastError(ERROR_RETRY);return false;}
        // Register this caller with the audio service even if the native
        // backend released its final device before ExitProcess. Otherwise
        // the private per-PID getter can return E_INVALIDARG at shutdown.
        IMMDevice* prime=nullptr;
        if(enumerator&&SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&prime)))prime->Release();
        bool ok=true;
        for(unsigned i=0;i<2;++i){
            const auto role=i?eMultimedia:eConsole;
            if(FAILED(Detail::RestoreRole(roles[i],[&](std::wstring& value){return policy.Get(pid,role,value);},[&](const std::wstring& value){return policy.Set(pid,role,value);})))ok=false;
            userOverride=userOverride||roles[i].userChanged;
        }
        if(ok){lastTarget.clear();status.applied=false;status.verified=false;ok=SaveJournal();}return ok;
    }
    HRESULT Route(const std::wstring& target){
        const auto packed=Packed(target);
        for(unsigned i=0;i<2;++i){
            const auto role=i?eMultimedia:eConsole;
            // Persist recovery ownership before the private API can change an
            // application preference. No cleanup is attempted under loader lock.
            const auto hr=Detail::ApplyRole(roles[i],packed,[&](std::wstring& value){return policy.Get(pid,role,value);},[&](const std::wstring& value){return policy.Set(pid,role,value);},[&]{return SaveJournal();});
            if(hr==S_FALSE){userOverride=true;return S_FALSE;}if(FAILED(hr))return hr;
        }
        lastTarget=target;status.applied=true;return S_OK;
    }
    void Sessions(const std::vector<Endpoint>& endpoints,const std::wstring& target){
        status.sessions=status.verifiedSessions=0;
        for(const auto& endpoint:endpoints)if(endpoint.state&DEVICE_STATE_ACTIVE){
            IMMDevice* device=nullptr;if(FAILED(enumerator->GetDevice(endpoint.id.c_str(),&device)))continue;
            IAudioSessionManager2* manager=nullptr;
            if(SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&manager)))){
                IAudioSessionEnumerator* sessions=nullptr;
                if(SUCCEEDED(manager->GetSessionEnumerator(&sessions))){
                    int count=0;sessions->GetCount(&count);
                    for(int i=0;i<count;++i){
                        IAudioSessionControl* base=nullptr;IAudioSessionControl2* control=nullptr;
                        if(SUCCEEDED(sessions->GetSession(i,&base))){
                            if(SUCCEEDED(base->QueryInterface(__uuidof(IAudioSessionControl2),reinterpret_cast<void**>(&control)))){
                                DWORD process=0;AudioSessionState sessionState=AudioSessionStateExpired;
                                // A shared session reports its first creator,
                                // which cannot prove ownership of every stream.
                                if(control->GetProcessId(&process)==S_OK&&process==pid&&SUCCEEDED(control->GetState(&sessionState))&&sessionState==AudioSessionStateActive){
                                    ++status.sessions;if(Equal(endpoint.id,target))++status.verifiedSessions;
                                }control->Release();
                            }base->Release();
                        }
                    }sessions->Release();
                }manager->Release();
            }device->Release();
        }
        status.verified=status.sessions>0&&status.sessions==status.verifiedSessions;
    }
};
State state;
} // namespace

Decision Select(const std::vector<Endpoint>& devices,const std::wstring& output,const std::wstring& hint,const std::string& backend){
    return AudioEndpointSelection::Select(devices,output,hint,backend);
}

void Initialize(const std::wstring& folder,DWORD pid,LogCallback logger,const std::wstring& recoveryFolder){
    Shutdown();state.pid=pid;state.logger=logger;state.active=true;
    const auto ini=folder+(folder.empty()||folder.back()==L'\\'||folder.back()==L'/'?L"":L"\\")+L"vr.ini";
    wchar_t output[1024]{};GetPrivateProfileStringW(L"Audio",L"Output",L"Auto",output,1024,ini.c_str());state.output=output;
    state.fallback=GetPrivateProfileIntW(L"Audio",L"FallbackToDefault",1,ini.c_str())!=0;
    if(!folder.empty()){
        state.journal=folder+(folder.back()==L'\\'||folder.back()==L'/'?L"":L"\\")+L".audio-route.state";
        wchar_t executable[32768]{};const DWORD count=GetModuleFileNameW(nullptr,executable,32768);
        unsigned long long hash=14695981039346656037ull;for(DWORD i=0;i<count&&count<32768;++i){hash^=static_cast<unsigned>(std::towlower(executable[i]));hash*=1099511628211ull;}
        state.identity=std::to_wstring(hash);
        if(!recoveryFolder.empty())state.journal=recoveryFolder+
            (recoveryFolder.back()==L'\\'||recoveryFolder.back()==L'/'?L"":L"\\")+
            L".audio-route-"+state.identity+L".state";
    }
    const auto hr=state.policy.Open();state.status.policyAvailable=SUCCEEDED(hr);
    const auto enumerated=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&state.enumerator));
    if(SUCCEEDED(enumerated)) {
        IMMDevice* prime=nullptr;
        if(SUCCEEDED(state.enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&prime)))prime->Release();
    }
    state.LoadJournal();
    if(FAILED(enumerated))state.Message("unavailable","Windows audio devices cannot be enumerated; existing audio is unchanged.",enumerated);
    else if(FAILED(hr))state.Message("policy_unavailable","This Windows per-application audio policy interface is unavailable; select output in Windows Volume mixer.",hr);
    else state.Message("ready","Per-application audio routing ready; the global Windows output will not be changed.");
}
void Tick(const std::wstring& hint,const std::string& backend,bool vrActive){
    if(!state.active||!state.enumerator)return;
    const DWORD now=GetTickCount();
    if(state.lastTick&&DWORD(now-state.lastTick)<2000&&hint==state.lastHint&&backend==state.lastBackend&&vrActive==state.lastVrActive)return;
    state.lastTick=now;state.lastHint=hint;state.lastBackend=backend;state.lastVrActive=vrActive;
    if(state.recoveryPending){state.LoadJournal();if(state.recoveryPending){state.Message("recovery_pending","Previous game audio preferences could not be read safely; recovery data was retained and automatic routing is waiting. Check Windows Volume mixer.");return;}}
    std::vector<Endpoint> endpoints;const auto hr=Devices(state.enumerator,endpoints);
    if(FAILED(hr)){state.Message("unavailable","Audio device enumeration failed; existing output is unchanged.",hr);return;}
    state.status.devices=unsigned(endpoints.size());state.status.active=0;
    for(const auto& endpoint:endpoints)if(endpoint.state&DEVICE_STATE_ACTIVE)++state.status.active;
    if(!vrActive||Equal(state.output,L"Default")){
        if(!state.Restore()){state.Message("restore_failed","Previous game audio preferences could not be fully restored. Recovery state was retained; check Windows Volume mixer.");return;}
        state.status.deviceName.clear();state.status.sessions=state.status.verifiedSessions=0;state.Message(vrActive?"default":"waiting_vr",vrActive?"Windows/application audio selection is in use.":"Waiting for a working VR backend before selecting headset audio.");return;
    }
    const auto decision=Select(endpoints,state.output,hint,backend);
    if(decision.index<0){
        if(!state.Restore()){state.Message("restore_failed","Headset audio is unavailable and the previous game preference could not be fully restored. Recovery state was retained; check Windows Volume mixer.");return;}
        state.status.deviceName.clear();state.status.sessions=state.status.verifiedSessions=0;
        const char* text=decision.reason=="ambiguous"?"Several headset audio outputs are active; set [Audio] Output to an exact endpoint ID. Windows output is unchanged."
            :decision.reason=="disabled_or_disconnected"?"Headset audio is disabled or disconnected. Enable it in Windows Playback devices and check Hear Computer Audio in VR in Meta Link."
            :"Headset audio output was not found. Check the headset connection/audio driver in Windows; a VR backend change cannot create a missing audio device.";
        state.Message(decision.reason.c_str(),std::string(text)+(state.fallback?" Existing Windows audio remains available.":" Headset audio was requested; it cannot be routed until the device is available."));return;
    }
    const auto& selected=endpoints[unsigned(decision.index)];state.status.deviceName=Utf8(selected.name);
    if(!state.status.policyAvailable){state.Message("policy_unavailable","Headset audio was found, but this Windows audio policy interface is unsupported. Select the headset for Kane & Lynch in Windows Volume mixer.");return;}
    if(state.userOverride){state.Message("user_override","The game audio output was changed manually. Automatic routing is paused for this launch to preserve that choice.");return;}
    const auto applied=state.Route(selected.id);
    if(applied==S_FALSE){
        if(!state.Restore())state.Message("restore_failed","A manual audio change was detected, but another owned preference could not be restored. Check Windows Volume mixer.");
        else state.Message("user_override","The game audio output was changed manually. Automatic routing is paused for this launch to preserve that choice.");return;
    }
    if(FAILED(applied)){
        const bool restored=state.Restore();state.Message(restored?"route_failed":"restore_failed",restored?"Windows rejected the game audio output preference; previous application settings were restored. Select output in Windows Volume mixer.":"Windows rejected game audio routing and previous preferences could not be fully restored. Recovery state was retained; check Windows Volume mixer.",applied);return;
    }
    state.Sessions(endpoints,selected.id);
    if(state.status.verified)state.Message("routed","Verified process audio sessions are on the selected headset output: "+state.status.deviceName+".");
    else if(state.status.sessions)state.Message("restart_required","Headset audio preference was applied, but an active game audio session remains on another output. Restart the game if the sound does not move.");
    else state.Message("pending_audio","Headset audio preference applied: "+state.status.deviceName+". Waiting for an active game audio session to verify the route.");
}
bool Shutdown(){
    bool restored=true;
    if(state.active){restored=state.Restore();Release(state.enumerator);state.policy.Close();}
    state=State{};return restored;
}
Status Snapshot(){return state.status;}

int LiveSmoke(LogCallback logger){
    // This test is explicitly opt-in: it changes only its own two playback-role
    // preferences and restores them even on a failed verification.
    Initialize(L"",GetCurrentProcessId(),logger);
    std::vector<Endpoint> endpoints;const auto enumerated=Devices(state.enumerator,endpoints);
    if(FAILED(enumerated)||!state.status.policyAvailable){Shutdown();return 2;}
    int target=-1,original=-1;for(unsigned i=0;i<endpoints.size();++i)if(endpoints[i].state&DEVICE_STATE_ACTIVE){if(endpoints[i].isDefault)original=int(i);else if(target<0)target=int(i);}
    if(original<0||target<0){if(logger)logger("Audio smoke skipped: two active playback endpoints are required for a real routing proof.");Shutdown();return 3;}
    const auto defaultBefore=endpoints[unsigned(original)].id;
    IDirectSound* sound=nullptr;IDirectSoundBuffer* buffer=nullptr;
    int existingCode=0,newCode=0,result=0;bool policyAccepted=false,existingPassed=false,newPassed=false;
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=22050;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=44100;
    DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);desc.dwFlags=DSBCAPS_GLOBALFOCUS;desc.dwBufferBytes=44100;desc.lpwfxFormat=&format;
    auto stop=[&]{if(buffer){buffer->Stop();buffer->Release();buffer=nullptr;}Release(sound);};
    auto play=[&](bool fresh)->int{
        stop();if(FAILED(DirectSoundCreate(nullptr,&sound,nullptr))||FAILED(sound->SetCooperativeLevel(GetDesktopWindow(),DSSCL_NORMAL)))return fresh?10:4;
        if(FAILED(sound->CreateSoundBuffer(&desc,&buffer,nullptr)))return fresh?10:5;
        void* a=nullptr,*b=nullptr;DWORD na=0,nb=0;
        if(FAILED(buffer->Lock(0,desc.dwBufferBytes,&a,&na,&b,&nb,0)))return fresh?10:6;
        std::memset(a,0,na);if(nb)std::memset(b,0,nb);
        if(FAILED(buffer->Unlock(a,na,b,nb))||FAILED(buffer->Play(0,0,DSBPLAY_LOOPING)))return fresh?10:7;
        DWORD flags=0;if(FAILED(buffer->GetStatus(&flags))||!(flags&DSBSTATUS_PLAYING)||!(flags&DSBSTATUS_LOOPING))return fresh?10:7;
        return 0;
    };
    auto dump=[&](const char* stage){
        state.Sessions(endpoints,endpoints[unsigned(target)].id);
        if(!logger)return;
        unsigned all=0,ownInactive=0,ownExpired=0,sharedSessions=0,pidFailures=0;
        for(const auto& endpoint:endpoints)if(endpoint.state&DEVICE_STATE_ACTIVE){
            IMMDevice* device=nullptr;IAudioSessionManager2* manager=nullptr;IAudioSessionEnumerator* sessions=nullptr;
            if(SUCCEEDED(state.enumerator->GetDevice(endpoint.id.c_str(),&device))&&SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&manager)))&&SUCCEEDED(manager->GetSessionEnumerator(&sessions))){
                int count=0;sessions->GetCount(&count);all+=unsigned(count);
                for(int i=0;i<count;++i){IAudioSessionControl* base=nullptr;IAudioSessionControl2* control=nullptr;
                    if(SUCCEEDED(sessions->GetSession(i,&base))&&SUCCEEDED(base->QueryInterface(__uuidof(IAudioSessionControl2),reinterpret_cast<void**>(&control)))){
                        DWORD pid=0;AudioSessionState activity=AudioSessionStateExpired;const auto hr=control->GetProcessId(&pid);
                        if(FAILED(hr))++pidFailures;else{if(hr!=S_OK)++sharedSessions;
                            if(pid==GetCurrentProcessId()&&SUCCEEDED(control->GetState(&activity))){if(activity==AudioSessionStateInactive)++ownInactive;else if(activity==AudioSessionStateExpired)++ownExpired;}}
                    }Release(control);Release(base);
                }
            }Release(sessions);Release(manager);Release(device);
        }
        char text[700];std::snprintf(text,sizeof(text),"Audio smoke %s: ownActive=%u targetActive=%u allSessions=%u ownInactive=%u ownExpired=%u sharedSessions=%u pidReadFailures=%u target=%.200s",
            stage,state.status.sessions,state.status.verifiedSessions,all,ownInactive,ownExpired,sharedSessions,pidFailures,Utf8(endpoints[unsigned(target)].name).c_str());logger(text);
    };
    existingCode=play(false);
    if(!existingCode){
        // A user's prior per-app choice may already point at the non-default
        // endpoint. Pick the other endpoint so this proves a real migration.
        for(unsigned i=0;i<10;++i){state.Sessions(endpoints,endpoints[unsigned(target)].id);if(state.status.sessions)break;Sleep(100);}
        if(state.status.verified)target=original;
    }
    dump("before-policy");
    const auto requested=state.Route(endpoints[unsigned(target)].id);policyAccepted=requested==S_OK;
    if(!policyAccepted)result=8;
    if(policyAccepted&&!existingCode){
        for(unsigned i=0;i<30;++i){state.Sessions(endpoints,endpoints[unsigned(target)].id);if(state.status.verified)break;Sleep(100);}
        existingPassed=state.status.verified;if(!existingPassed)existingCode=9;
    }
    dump("existing-after-policy");
    if(logger)logger(existingPassed?"Existing DirectSound migration PASS.":existingCode==9?"Existing DirectSound migration not verified within three seconds; checking a fresh DirectSound stream separately.":"Existing DirectSound stream or routing setup failed; fresh-stream check will still run.");
    stop();
    // DirectSound can retain an engine/session after the last buffer stops.
    // Report this rather than letting a stale old active session conceal a
    // newly created session on the correct target endpoint.
    for(unsigned i=0;i<20;++i){state.Sessions(endpoints,endpoints[unsigned(target)].id);if(!state.status.sessions)break;Sleep(100);}
    dump("old-buffer-released");
    newCode=play(true);
    if(!newCode){
        for(unsigned i=0;i<30;++i){state.Sessions(endpoints,endpoints[unsigned(target)].id);if(state.status.verifiedSessions)break;Sleep(100);}
        newPassed=state.status.verifiedSessions>0;if(!newPassed)newCode=11;
    }
    dump("new-stream-after-policy");
    if(logger)logger(newPassed?"Fresh DirectSound target session PASS (silent buffer is playing and looping).":newCode==10?"Fresh DirectSound buffer could not be created, filled, started or verified as playing/looping.":"Fresh DirectSound stream has no verified active target session; successful policy readback alone does not prove audio routing.");
    stop();
    if(!result)result=newCode?newCode:existingCode;
    const bool restored=state.Restore();
    // GetDefaultAudioEndpoint can reflect this caller's per-application override.
    // Compare only after restoring it; a second executable's read-only probe is
    // needed to independently establish the system-wide default during the test.
    std::vector<Endpoint> after;const auto checked=Devices(state.enumerator,after);
    bool sameCallerDefault=false;for(const auto& endpoint:after)if(endpoint.isDefault&&Equal(endpoint.id,defaultBefore))sameCallerDefault=true;
    if(!restored)result=13;else if(FAILED(checked)||!sameCallerDefault)result=12;
    if(logger)logger(result==13?"Audio live smoke could not restore a process preference; inspect this test process in Windows Volume mixer. No global default API was called.":result==12?"Audio live smoke could not confirm the caller's original default after restoring preferences. No global default API was called.":result==9&&newPassed?"Audio live smoke PARTIAL: new DirectSound target session verified, existing session did not migrate. Original process preferences and caller default restored; a new DS device was verified, a full process restart was not tested.":result?"Audio live smoke FAILED: one or more routing checks did not pass. Previous process preferences and caller default restored; no global default API was called.":"Audio live smoke PASS: existing and new DirectSound sessions routed, original process preferences and caller default restored. Use an independent executable probe to verify the system default.");
    Shutdown();return result;
}
} // namespace AudioRoute
