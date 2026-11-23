#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cwchar>

// Routing belongs to the game process, not the system-wide default endpoint.
// Call Initialize/Tick/Shutdown on one worker thread (never under DllMain).
namespace AudioRoute {
using LogCallback=void(*)(const char*);
struct Endpoint {
    std::wstring id,name,guid;
    DWORD state=0;
    bool isDefault=false;
};
struct Decision {
    int index=-1;
    std::string reason;
};
Decision Select(const std::vector<Endpoint>& devices,const std::wstring& output,
                const std::wstring& runtimeEndpoint,const std::string& backend);
struct Status {
    std::string state="idle",message="Audio routing has not started.",deviceName;
    unsigned devices=0,active=0,sessions=0,verifiedSessions=0;
    bool policyAvailable=false,applied=false,verified=false;
    HRESULT error=S_OK;
};
void Initialize(const std::wstring& folder,DWORD gamePid,LogCallback log=nullptr,
                const std::wstring& recoveryFolder=L"");
void Tick(const std::wstring& runtimeEndpoint,const std::string& backend,bool vrActive);
bool Shutdown();
Status Snapshot();

namespace Detail {
// The transaction is independent of Windows COM so failure and ownership paths
// can be checked without changing a real user's audio preferences.
struct Role {
    std::wstring previous,applied,alternate;
    bool changed=false,userChanged=false,alternateValid=false;
};
inline bool Equal(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
template<class Get,class Set>
HRESULT RestoreRole(Role& role,Get get,Set set){
    if(!role.changed)return S_OK;
    std::wstring current;auto hr=get(current);if(FAILED(hr))return hr;
    if(Equal(current,role.applied)||(role.alternateValid&&Equal(current,role.alternate))){
        if(!Equal(current,role.previous)){hr=set(role.previous);if(FAILED(hr))return hr;}
        hr=get(current);if(FAILED(hr)||!Equal(current,role.previous))return FAILED(hr)?hr:E_FAIL;
    }
    else role.userChanged=true; // Relinquish ownership; never overwrite this edit.
    role.changed=false;role.previous.clear();role.applied.clear();role.alternate.clear();role.alternateValid=false;return S_OK;
}
template<class Get>
HRESULT RecoverRole(const Role& saved,Role& output,Get get){
    if(!saved.changed){output={};return S_OK;}
    std::wstring current;const auto hr=get(current);if(FAILED(hr))return hr;
    output=Equal(current,saved.applied)||(saved.alternateValid&&Equal(current,saved.alternate))?saved:Role{};return S_OK;
}
template<class Get,class Set,class Save>
HRESULT ApplyRole(Role& role,const std::wstring& target,Get get,Set set,Save save){
    if(role.userChanged)return S_FALSE;
    std::wstring current;auto hr=get(current);if(FAILED(hr))return hr;
    if(role.changed&&!Equal(current,role.applied)&&(!role.alternateValid||!Equal(current,role.alternate))){
        role.userChanged=true;role.changed=false;role.previous.clear();role.applied.clear();role.alternate.clear();role.alternateValid=false;save();return S_FALSE;
    }
    if(Equal(current,target))return S_OK;
    const Role before=role;if(!role.changed)role.previous=current;
    // Journal both possible owned values before Set: it may fail, or the
    // process may exit before its result/readback can be observed.
    role.applied=target;role.alternate=current;role.alternateValid=true;role.changed=true;
    if(!save()){const auto error=GetLastError();role=before;return HRESULT_FROM_WIN32(error?error:ERROR_WRITE_FAULT);}
    hr=set(target);if(FAILED(hr))return hr;
    hr=get(current);if(FAILED(hr)||!Equal(current,target))return FAILED(hr)?hr:E_FAIL;
    // Once readback confirms the new value, the old endpoint stops being ours:
    // selecting it manually later must be respected as a user change.
    role.alternate.clear();role.alternateValid=false;
    if(!save()){const auto error=GetLastError();return HRESULT_FROM_WIN32(error?error:ERROR_WRITE_FAULT);}return S_OK;
}
}

// The smoke check only changes its OWN process preference, restores it on exit,
// and uses silent buffers. No global/default/capture-device settings are changed.
int LiveSmoke(LogCallback log=nullptr);
}
