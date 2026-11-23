#pragma once
#include "audio_route.hpp"
#include "audio_endpoint_names.hpp"
#include <mmdeviceapi.h>
#include <cwctype>

// Shared pure decision for early DirectSound selection and per-process policy.
// No enumeration, runtime loading, COM initialization or endpoint mutation.
namespace AudioEndpointSelection {
inline std::string Backend(const std::wstring& mode){
    if(!_wcsicmp(mode.c_str(),L"Meta")||!_wcsicmp(mode.c_str(),L"meta-link"))return "Meta";
    if(!_wcsicmp(mode.c_str(),L"VDXR"))return "VDXR";
    if(!_wcsicmp(mode.c_str(),L"SteamVR"))return "SteamVR";
    if(!_wcsicmp(mode.c_str(),L"OpenXR"))return "OpenXR";
    if(!_wcsicmp(mode.c_str(),L"Auto"))return "Auto";
    return {};
}
inline std::wstring Canonical(std::wstring value){
    const std::wstring prefix=L"\\\\?\\SWD#MMDEVAPI#",suffix=L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
    const auto equal=[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;};
    if(value.size()>=prefix.size()&&equal(value.substr(0,prefix.size()),prefix))value.erase(0,prefix.size());
    if(value.size()>=suffix.size()&&equal(value.substr(value.size()-suffix.size()),suffix))value.resize(value.size()-suffix.size());
    return value;
}
inline AudioRoute::Decision Select(const std::vector<AudioRoute::Endpoint>& devices,const std::wstring& output,
                                  const std::wstring& runtimeEndpoint,const std::string& backend){
    const auto equal=[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;};
    if(equal(output,L"Default"))return {-1,"default"};
    const bool automatic=equal(output,L"Auto")||equal(output,L"Headset");
    const auto requested=Canonical(automatic?runtimeEndpoint:output);
    if(!requested.empty()){
        for(unsigned i=0;i<devices.size();++i)if(equal(requested,devices[i].id)||equal(requested,devices[i].guid))
            return devices[i].state&DEVICE_STATE_ACTIVE?AudioRoute::Decision{int(i),"endpoint"}:AudioRoute::Decision{-1,"disabled_or_disconnected"};
        return {-1,"endpoint_missing"};
    }
    int selected=-1,defaultHeadset=-1;unsigned defaults=0;bool inactive=false,ambiguous=false;
    for(unsigned i=0;i<devices.size();++i){
        const int family=AudioEndpointNames::Family(devices[i].name);
        if(!family||(backend=="Meta"&&family!=1)||(backend=="VDXR"&&family!=2))continue;
        if(!(devices[i].state&DEVICE_STATE_ACTIVE)){inactive=true;continue;}
        if(devices[i].isDefault){defaultHeadset=int(i);++defaults;}
        if(selected>=0)ambiguous=true;else selected=int(i);
    }
    // SteamVR may transport through Meta, VD or another headset. Auto must
    // not prefer Meta merely because its PC software is installed. Only a
    // recognized playback default may disambiguate several active headsets.
    if(ambiguous)return defaults==1?AudioRoute::Decision{defaultHeadset,"headset_default"}:AudioRoute::Decision{-1,"ambiguous"};
    if(selected>=0)return {selected,"headset_name"};
    return {-1,inactive?"disabled_or_disconnected":"headset_missing"};
}
inline AudioRoute::Decision ConnectedMetaFallback(const std::vector<AudioRoute::Endpoint>& devices,
        const std::wstring& output,const std::string& backend,const AudioRoute::Decision& initial,
        const std::wstring& connectedMetaEndpoint){
    // This hint may be supplied only after GetHmdDesc(NULL) proves a present
    // Meta HMD. A recognized existing playback default always wins first.
    if(backend!="Auto"||initial.reason!="ambiguous"||connectedMetaEndpoint.empty())return initial;
    const auto candidate=AudioEndpointSelection::Select(devices,output,connectedMetaEndpoint,backend);
    return candidate.index>=0&&AudioEndpointNames::Family(devices[unsigned(candidate.index)].name)==1?candidate:initial;
}
}
