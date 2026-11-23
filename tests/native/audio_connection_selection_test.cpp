#include "audio_endpoint_selection.hpp"
#include "audio_bootstrap.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool okay,const char* reason){++checks;if(!okay){std::printf("FAIL: %s\n",reason);std::exit(1);}}
using AudioRoute::Endpoint;
void Expect(const std::vector<Endpoint>& devices,const std::wstring& output,const std::wstring& hint,
            const std::string& backend,int index,const char* reason){
    const auto early=AudioEndpointSelection::Select(devices,output,hint,backend);
    Check(early.index==index&&early.reason==reason,"pure early selection matches the requested connection and exact endpoint policy");
    const auto policy=AudioRoute::Select(devices,output,hint,backend);
    Check(policy.index==early.index&&policy.reason==early.reason,"early bootstrap and per-process policy share one selection decision");
}
}
int main(){
    using AudioEndpointSelection::Backend;
    Check(Backend(L"Auto")=="Auto"&&Backend(L"aUtO")=="Auto","automatic connection remains automatic independently of installed Meta software");
    Check(Backend(L"Meta")=="Meta"&&Backend(L"meta-link")=="Meta","explicit Meta and legacy alias use Meta selection");
    Check(Backend(L"VDXR")=="VDXR"&&Backend(L"vdxr")=="VDXR","explicit VDXR retains its own audio family");
    Check(Backend(L"SteamVR")=="SteamVR"&&Backend(L"OpenXR")=="OpenXR","OpenXR transport choices are not converted to Meta");
    Check(Backend(L"").empty()&&Backend(L"unsupported").empty(),"invalid connection is distinguishable from automatic");
    Check(sizeof(AudioBootstrap::Request)==1072&&AudioBootstrap::Version==1,"early request ABI remains unchanged");
    std::vector<Endpoint> devices{
        {L"desktop",L"Speakers (Realtek Audio)",L"desktop-guid",DEVICE_STATE_ACTIVE,true},
        {L"meta",L"Headphones (Oculus Virtual Audio Device)",L"meta-guid",DEVICE_STATE_ACTIVE,false},
        {L"vd",L"Virtual Desktop Audio",L"vd-guid",DEVICE_STATE_ACTIVE,false},
        {L"steam",L"Speakers (Steam Streaming Speakers)",L"steam-guid",DEVICE_STATE_ACTIVE,false},
        {L"steam-mic",L"Steam Streaming Microphone",L"steam-mic-guid",DEVICE_STATE_ACTIVE,false},
        {L"vd-mic",L"Virtual Desktop Microphone",L"vd-mic-guid",DEVICE_STATE_ACTIVE,false},
        {L"meta-mic",L"Microphone (Oculus Virtual Audio Device)",L"meta-mic-guid",DEVICE_STATE_ACTIVE,false}};
    Expect(devices,L"Auto",L"","Meta",1,"headset_name");
    Expect(devices,L"Headset",L"","VDXR",2,"headset_name");
    for(const char* backend:{"Auto","SteamVR","OpenXR"})Expect(devices,L"Auto",L"",backend,-1,"ambiguous");
    const auto ambiguous=AudioEndpointSelection::Select(devices,L"Auto",L"","Auto");
    const auto connected=[&](const std::wstring& hint,const std::string& backend="Auto"){
        return AudioEndpointSelection::ConnectedMetaFallback(devices,L"Auto",backend,ambiguous,hint);};
    Check(connected(L"").index==-1,"installed Meta without a connected HMD hint cannot resolve ambiguity");
    Check(connected(L"meta-guid").index==1,"present Meta HMD with its exact playback GUID may resolve otherwise ambiguous Auto");
    Check(connected(L"desktop-guid").index==-1&&connected(L"vd-guid").index==-1&&connected(L"meta-mic-guid").index==-1,
          "connected Meta fallback cannot pick unrelated desktop, VD or microphone endpoint");
    Check(connected(L"meta-guid","VDXR").index==-1&&connected(L"meta-guid","SteamVR").index==-1&&connected(L"meta-guid","OpenXR").index==-1,
          "explicit other connections cannot acquire a Meta fallback");
    devices[0].isDefault=false;devices[2].isDefault=true;
    for(const char* backend:{"Auto","SteamVR","OpenXR"})Expect(devices,L"Auto",L"",backend,2,"headset_default");
    Expect(devices,L"Auto",L"","VDXR",2,"headset_name");
    const auto vdDefault=AudioEndpointSelection::Select(devices,L"Auto",L"","Auto");
    Check(AudioEndpointSelection::ConnectedMetaFallback(devices,L"Auto","Auto",vdDefault,L"meta-guid").index==2,
          "recognized connected VD playback default is retained even when Meta HMD is also present");
    devices[2].isDefault=false;devices[3].isDefault=true;
    for(const char* backend:{"Auto","SteamVR","OpenXR"})Expect(devices,L"Headset",L"",backend,3,"headset_default");
    Expect(devices,L"Auto",L"","Meta",1,"headset_name");
    Expect(devices,L"Auto",L"","VDXR",2,"headset_name");
    devices[3].isDefault=false;devices[1].isDefault=true;
    for(const char* backend:{"Auto","SteamVR","OpenXR"})Expect(devices,L"Auto",L"",backend,1,"headset_default");
    devices[2].isDefault=true;
    Expect(devices,L"Auto",L"","Auto",-1,"ambiguous");
    Expect(devices,L"vd",L"meta","Meta",2,"endpoint");
    Expect(devices,L"desktop",L"vd","VDXR",0,"endpoint");
    Expect(devices,L"steam-mic",L"vd","SteamVR",4,"endpoint");
    Expect(devices,L"VD-GUID",L"meta","OpenXR",2,"endpoint");
    Expect(devices,L"Default",L"meta","Meta",-1,"default");
    Expect(devices,L"Auto",L"meta-guid","VDXR",1,"endpoint");
    Expect(devices,L"Auto",L"missing-guid","Meta",-1,"endpoint_missing");
    Expect(devices,L"missing",L"vd","Auto",-1,"endpoint_missing");
    Expect(devices,L"\\\\?\\SWD#MMDEVAPI#vd#{e6327cad-dcec-4949-ae8a-991e976a79d2}",L"","Auto",2,"endpoint");
    devices[2].state=DEVICE_STATE_DISABLED;
    Expect(devices,L"Auto",L"","VDXR",-1,"disabled_or_disconnected");
    Expect(devices,L"vd",L"meta","Meta",-1,"disabled_or_disconnected");
    Expect(devices,L"Auto",L"vd","OpenXR",-1,"disabled_or_disconnected");
    devices[1].state=devices[3].state=DEVICE_STATE_UNPLUGGED;
    Expect(devices,L"Auto",L"","Auto",-1,"disabled_or_disconnected");
    devices.erase(devices.begin()+1,devices.begin()+4);
    for(const char* backend:{"Auto","Meta","VDXR","SteamVR","OpenXR"})Expect(devices,L"Auto",L"",backend,-1,"headset_missing");
    Check(AudioEndpointNames::Family(L"Virtual Desktop Microphone")==0&&
          AudioEndpointNames::Family(L"Microphone (Oculus Virtual Audio Device)")==0&&
          AudioEndpointNames::Family(L"\u041C\u0418\u041A\u0420\u041E\u0424\u041E\u041D (Virtual Desktop)")==0,"virtual microphone render endpoints are never automatic playback fallbacks");
    std::puts("Connection-aware audio selection: all synthetic checks PASS; no endpoints, game or headset changed.");
    std::printf("Checks: %u\n",checks);return 0;
}
