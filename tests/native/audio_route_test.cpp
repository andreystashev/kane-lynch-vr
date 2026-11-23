#include "audio_route.hpp"
#include "audio_endpoint_names.hpp"
#include <mmdeviceapi.h>
#include <cassert>
#include <cstdio>
#include <cstring>

void Log(const char* text){std::puts(text);}
int DefaultEndpoint(){
    // Read-only mode for an independent executable identity. It must not call
    // AudioRoute::Initialize, private policy APIs, or alter endpoint settings.
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)&&com!=RPC_E_CHANGED_MODE)return 20;
    IMMDeviceEnumerator* enumerator=nullptr;
    const auto created=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator));
    int result=0;
    if(FAILED(created))result=21;
    else for(unsigned role=0;role<3;++role){
        IMMDevice* device=nullptr;LPWSTR id=nullptr;
        if(FAILED(enumerator->GetDefaultAudioEndpoint(eRender,static_cast<ERole>(role),&device))||FAILED(device->GetId(&id)))result=22;
        else std::wprintf(L"role%u=%ls\n",role,id);
        if(id)CoTaskMemFree(id);if(device)device->Release();
    }
    if(enumerator)enumerator->Release();if(SUCCEEDED(com))CoUninitialize();return result;
}
void Ownership(){
    using namespace AudioRoute::Detail;
    Role role;std::wstring current=L"user-original";unsigned sets=0,saves=0;bool getFails=false,setFails=false,saveFails=false,ignoreSet=false;
    auto get=[&](std::wstring& value)->HRESULT{if(getFails)return E_FAIL;value=current;return S_OK;};
    auto set=[&](const std::wstring& value)->HRESULT{++sets;if(setFails)return E_ACCESSDENIED;if(!ignoreSet)current=value;return S_OK;};
    auto save=[&]{++saves;if(saveFails){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;};
    assert(ApplyRole(role,L"headset",get,set,save)==S_OK);
    assert(role.changed&&role.previous==L"user-original"&&current==L"headset"&&sets==1&&saves==2);
    assert(ApplyRole(role,L"headset",get,set,save)==S_OK&&sets==1&&saves==2);
    getFails=true;assert(FAILED(RestoreRole(role,get,set))&&role.changed&&current==L"headset");getFails=false;
    setFails=true;assert(FAILED(RestoreRole(role,get,set))&&role.changed&&current==L"headset");setFails=false;
    assert(RestoreRole(role,get,set)==S_OK&&!role.changed&&current==L"user-original");
    sets=saves=0;saveFails=true;
    assert(FAILED(ApplyRole(role,L"headset",get,set,save))&&!role.changed&&sets==0&&current==L"user-original");saveFails=false;
    ignoreSet=true;assert(FAILED(ApplyRole(role,L"headset",get,set,save))&&role.changed&&current==L"user-original");ignoreSet=false;
    assert(RestoreRole(role,get,set)==S_OK&&current==L"user-original");
    role={};assert(ApplyRole(role,L"headset",get,set,save)==S_OK);
    current=L"user-changed-during-game";const auto before=sets;
    assert(ApplyRole(role,L"headset",get,set,save)==S_FALSE&&role.userChanged&&!role.changed&&sets==before);
    assert(RestoreRole(role,get,set)==S_OK&&current==L"user-changed-during-game"&&sets==before);
    assert(ApplyRole(role,L"headset",get,set,save)==S_FALSE&&sets==before);
    role={};current=L"";assert(ApplyRole(role,L"headset",get,set,save)==S_OK&&role.previous.empty());
    assert(RestoreRole(role,get,set)==S_OK&&current.empty()); // Restore implicit default, not a guessed endpoint.
    role={};current=L"headset";assert(ApplyRole(role,L"HEADSET",get,set,save)==S_OK&&!role.changed);
    assert(RestoreRole(role,get,set)==S_OK&&current==L"headset"); // Never claim a user's pre-existing preference.
    // A -> B fails: A is still owned, so restore the original user preference.
    role={};current=L"original";assert(ApplyRole(role,L"A",get,set,save)==S_OK);
    setFails=true;assert(FAILED(ApplyRole(role,L"B",get,set,save))&&current==L"A"&&role.alternate==L"A");setFails=false;
    assert(RestoreRole(role,get,set)==S_OK&&current==L"original"&&!role.userChanged);
    // Crash between journaling B and the failed native call retains both A/B.
    Role saved;saved.previous=L"original";saved.applied=L"B";saved.alternate=L"A";saved.changed=saved.alternateValid=true;
    Role recovered;recovered.previous=L"sentinel";getFails=true;
    assert(FAILED(RecoverRole(saved,recovered,get))&&recovered.previous==L"sentinel"&&saved.applied==L"B"&&saved.alternate==L"A");getFails=false;
    current=L"A";assert(RecoverRole(saved,recovered,get)==S_OK&&recovered.changed);
    assert(RestoreRole(recovered,get,set)==S_OK&&current==L"original");
    current=L"B";assert(RecoverRole(saved,recovered,get)==S_OK&&recovered.changed);
    assert(RestoreRole(recovered,get,set)==S_OK&&current==L"original");
    current=L"manual";assert(RecoverRole(saved,recovered,get)==S_OK&&!recovered.changed&&current==L"manual");
    // Successful A -> B must stop claiming A, otherwise a later manual A
    // selection would be mistaken for a failed Set and overwritten on restore.
    role={};current=L"original";assert(ApplyRole(role,L"A",get,set,save)==S_OK);
    assert(ApplyRole(role,L"B",get,set,save)==S_OK&&!role.alternateValid);
    current=L"A";assert(RestoreRole(role,get,set)==S_OK&&current==L"A"&&role.userChanged);
    role={};current=L"";setFails=true;assert(FAILED(ApplyRole(role,L"A",get,set,save)));setFails=false;
    assert(RestoreRole(role,get,set)==S_OK&&current.empty()&&!role.userChanged);
}
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--live-smoke")==0)return AudioRoute::LiveSmoke(Log);
    if(argc==2&&std::strcmp(argv[1],"--default-endpoint")==0)return DefaultEndpoint();
    Ownership();using AudioRoute::Endpoint;using AudioRoute::Select;
    std::vector<Endpoint> outputs{{L"speakers",L"Speakers",L"speaker-guid",DEVICE_STATE_ACTIVE,true},
        {L"meta",L"Headphones (Oculus Virtual Audio Device)",L"meta-guid",DEVICE_STATE_ACTIVE,false}};
    assert(Select(outputs,L"Auto",L"","Meta").index==1);
    assert(Select(outputs,L"Default",L"meta","Meta").index==-1);
    assert(Select(outputs,L"missing",L"meta","Meta").reason=="endpoint_missing");
    assert(Select(outputs,L"Auto",L"missing","Meta").reason=="endpoint_missing");
    assert(Select(outputs,L"meta-guid",L"","Meta").index==1);
    assert(Select(outputs,L"AUTO",L"META","OpenXR").index==1);
    outputs[1].state=DEVICE_STATE_DISABLED;
    assert(Select(outputs,L"Auto",L"meta","OpenXR").reason=="disabled_or_disconnected");
    assert(Select(outputs,L"Auto",L"","Meta").reason=="disabled_or_disconnected");
    outputs.push_back({L"vd",L"Virtual Desktop Audio",L"",DEVICE_STATE_ACTIVE,false});
    assert(Select(outputs,L"Auto",L"","Meta").index==-1);
    assert(Select(outputs,L"Auto",L"","OpenXR").index==2);
    outputs[1].state=DEVICE_STATE_ACTIVE;
    assert(Select(outputs,L"Headset",L"","OpenXR").reason=="ambiguous");
    assert(Select(outputs,L"Headset",L"vd","OpenXR").index==2);
    assert(Select(outputs,L"Headset",L"","Meta").index==1);
    assert(Select({},L"Auto",L"","Meta").reason=="headset_missing");
    assert(AudioEndpointNames::Family(L"Headphones (Oculus Virtual Audio Device)")==1);
    assert(AudioEndpointNames::Family(L"Virtual Desktop Audio")==2);
    assert(AudioEndpointNames::Family(L"Speakers (Steam Streaming Speakers)")==3);
    assert(AudioEndpointNames::Family(L"Steam Streaming Microphone")==0);
    assert(AudioEndpointNames::Family(L"\u041C\u0418\u041A\u0420\u041E\u0424\u041E\u041D (STEAM STREAMING)")==0);
    assert(AudioEndpointNames::Family(L"Speakers (Realtek Audio)")==0);
    // Steam Link can coexist with installed Meta and Virtual Desktop drivers.
    // Prefer a recognized active headset default only when it disambiguates
    // playback; a desktop default and streaming microphone are not evidence.
    std::vector<Endpoint> connected{
        {L"desktop",L"Speakers (Realtek Audio)",L"",DEVICE_STATE_ACTIVE,true},
        {L"meta",L"Headphones (Oculus Virtual Audio Device)",L"",DEVICE_STATE_ACTIVE,false},
        {L"vd",L"Virtual Desktop Audio",L"",DEVICE_STATE_ACTIVE,false},
        {L"steam",L"Speakers (Steam Streaming Speakers)",L"",DEVICE_STATE_ACTIVE,false},
        {L"steam-mic",L"Speakers (Steam Streaming Microphone)",L"",DEVICE_STATE_ACTIVE,false}};
    assert(Select(connected,L"Auto",L"","OpenXR").reason=="ambiguous");
    connected[0].isDefault=false;connected[3].isDefault=true;
    const auto steamDefault=Select(connected,L"Auto",L"","OpenXR");
    assert(steamDefault.index==3&&steamDefault.reason=="headset_default");
    assert(Select(connected,L"Headset",L"meta","OpenXR").index==1); // A runtime hint outranks the default.
    assert(Select(connected,L"vd",L"steam","OpenXR").index==2); // Explicit selection outranks either.
    assert(Select(connected,L"steam-mic",L"","OpenXR").index==4); // Explicit virtual endpoints remain allowed.
    assert(Select(connected,L"Auto",L"","Meta").index==1); // Meta's family restriction remains in force.
    connected[3].isDefault=false;connected[2].isDefault=true;
    assert(Select(connected,L"Auto",L"","OpenXR").index==2);
    connected[2].isDefault=false;connected[1].isDefault=true;
    assert(Select(connected,L"Auto",L"","OpenXR").index==1);
    connected[3].isDefault=true;
    assert(Select(connected,L"Auto",L"","OpenXR").reason=="ambiguous"); // Conflicting defaults are not guessed.
    std::vector<Endpoint> microphones{
        {L"desktop",L"Speakers (Realtek Audio)",L"",DEVICE_STATE_ACTIVE,true},
        {L"steam-mic",L"Speakers (Steam Streaming Microphone)",L"",DEVICE_STATE_ACTIVE,false},
        {L"steam-mic-ru",L"\u041C\u0438\u043A\u0440\u043E\u0444\u043E\u043D (Steam Streaming)",L"",DEVICE_STATE_ACTIVE,false}};
    assert(Select(microphones,L"Auto",L"","OpenXR").reason=="headset_missing");
    std::puts("Audio endpoint policy tests passed; no audio settings changed.");return 0;
}
