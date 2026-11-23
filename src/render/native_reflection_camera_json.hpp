#pragma once
#include "native_camera_kl.hpp"
#include <cmath>
#include <cstdio>
namespace NativeReflectionCameraJSON {
inline void WriteRows(FILE* out,const char* key,const float* values,unsigned count){
    std::fprintf(out,",\"%s\":[",key);
    for(unsigned n=0;n<count;++n){if(n)std::fputc(',',out);
        if(std::isfinite(values[n]))std::fprintf(out,"%.9g",static_cast<double>(values[n]));else std::fputs("null",out);}
    std::fputc(']',out);
}
inline void WriteEvidence(FILE* out,const char* name,const NativeCameraKL::ReflectionEvidence& value) {
    std::fprintf(out,"\"%s\":",name);
    if(!value.valid){std::fputs("null",out);return;}
    std::fprintf(out,"{\"reason\":%u,\"phase\":%u,\"eye\":%u,\"tick\":%u,\"sequence\":%llu,\"pair\":%llu,\"packet\":%llu,\"queue\":%llu,\"sourceEntry\":%llu,\"reflector\":%llu,\"sourceCamera\":%llu,\"viewId\":%llu",
        value.reason,value.phase,value.eye,value.tick,static_cast<unsigned long long>(value.sequence),static_cast<unsigned long long>(value.pair),
        static_cast<unsigned long long>(value.packet),static_cast<unsigned long long>(value.queue),static_cast<unsigned long long>(value.sourceEntry),
        static_cast<unsigned long long>(value.reflector),static_cast<unsigned long long>(value.sourceCamera),static_cast<unsigned long long>(value.viewId));
    const auto rows=[&](const char* key,const float* values,unsigned count){WriteRows(out,key,values,count);};
    rows("originalView",value.originalView,12);rows("reflectedView",value.reflectedView,12);rows("eyeView",value.eyeView,12);
    rows("originalProjection",value.originalProjection,16);rows("obliqueProjection",value.obliqueProjection,16);
    rows("finalNativeProjection",value.finalNativeProjection,16);rows("vrProjection",value.vrProjection,16);rows("resultProjection",value.resultProjection,16);
    std::fputc('}',out);
}
inline void WriteProducerEvidence(FILE* out,const char* name,const NativeCameraKL::ReflectionProducerEvidence& value){
    std::fprintf(out,"\"%s\":",name);if(!value.valid){std::fputs("null",out);return;}
    const auto& t=value.trace;const auto& observed=t.observed;
    std::fprintf(out,"{\"tick\":%u,\"sequence\":%llu,\"reason\":%u,\"commands\":%u,\"cameras\":%u,\"at\":%llu,\"queue\":%llu,\"begin\":%llu,\"end\":%llu,\"sourceEntry\":%llu,\"reflector\":%llu,\"viewId\":%llu,\"header\":[",
        value.tick,static_cast<unsigned long long>(value.sequence),static_cast<unsigned>(t.reason),t.commands,t.cameras,
        static_cast<unsigned long long>(t.at),static_cast<unsigned long long>(observed.queue),static_cast<unsigned long long>(observed.begin),
        static_cast<unsigned long long>(observed.end),static_cast<unsigned long long>(observed.sourceEntry),static_cast<unsigned long long>(observed.reflector),static_cast<unsigned long long>(observed.view));
    for(unsigned n=0;n<6;++n)std::fprintf(out,"%s%u",n?",":"",t.header[n]);
    std::fputs("],\"packets\":[",out);
    for(unsigned n=0;n<observed.packets.size();++n){
        if(n)std::fputc(',',out);const auto& p=observed.packets[n];if(!p.address){std::fputs("null",out);continue;}
        std::fprintf(out,"{\"address\":%llu,\"header\":[",static_cast<unsigned long long>(p.address));
        for(unsigned j=0;j<6;++j)std::fprintf(out,"%s%u",j?",":"",p.Word(j*4));
        std::fputc(']',out);float view[12]{},projection[16]{};
        std::memcpy(view,p.bytes.data()+24,sizeof(view));std::memcpy(projection,p.bytes.data()+72,sizeof(projection));
        WriteRows(out,"view",view,12);WriteRows(out,"projection",projection,16);
        std::fputs(",\"rawHex\":\"",out);constexpr char hex[]="0123456789abcdef";
        for(unsigned char byte:p.bytes){std::fputc(hex[byte>>4],out);std::fputc(hex[byte&15],out);}
        std::fputs("\"}",out);
    }
    std::fputs("]}",out);
}
}
