#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only proof for the pinned Dead Men Bink playback path. Call VerifyImage
// once after the existing EXE SHA256 check; Snapshot then checks the actual
// current object/handle twice. It does not classify arbitrary video textures
// or ordinary MiniCutscene counters as a movie.
namespace NativeMovieKL {
inline constexpr std::uintptr_t CurrentRva=0x885664,VtableRva=0x562DA0;
inline constexpr std::uintptr_t StartRva=0x1CF340,DrawRva=0x1CF5A0,CloseRva=0x1CF760;
inline constexpr std::size_t ActiveOffset=0x1B0,HandleOffset=0x1C8;
struct State {
    bool valid=false,active=false;
    std::uintptr_t object=0,handle=0;
    std::uint32_t width=0,height=0,frames=0,frame=0;
};
inline bool Address(std::uintptr_t address,std::size_t size){
    return address>=0x10000u&&size<=0xffffffffu&&address<=0xffffffffu-size;
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read){
    if(!Address(image,CurrentRva+4))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[16]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char start[]={0x83,0xEC,0x20,0x55,0x8B,0xE9};
    unsigned char currentStore[]={0x89,0x2D,0,0,0,0};
    const auto current=static_cast<std::uint32_t>(image+CurrentRva);std::memcpy(currentStore+2,&current,4);
    const unsigned char activeSet[]={0xC6,0x85,0xB0,1,0,0,1};
    const unsigned char dimensionsHeight[]={0x8B,0x57,4,0xDB,0x47,4};
    const unsigned char dimensionsWidth[]={0x8B,7,0xDB,7};
    const unsigned char activeTest[]={0x80,0xBF,0xB0,1,0,0,0};
    const unsigned char activeClear[]={0xC6,0x86,0xB0,1,0,0,0};
    std::uint32_t startMethod=0,drawMethod=0,closeMethod=0;
    return match(StartRva,start)&&match(0x1CF34D,currentStore)&&match(0x1CF3BA,activeSet)&&
        match(0x1CF3C1,dimensionsHeight)&&match(0x1CF3DD,dimensionsWidth)&&
        match(0x1CF5A6,activeTest)&&match(0x1CF7A8,activeClear)&&
        read(image+VtableRva,&closeMethod,4)&&closeMethod==image+CloseRva&&
        read(image+VtableRva+0x18,&startMethod,4)&&startMethod==image+StartRva&&
        read(image+VtableRva+0x24,&drawMethod,4)&&drawMethod==image+DrawRva;
}
template<class Reader> State Snapshot(std::uintptr_t image,Reader read){
    State output;
    if(!Address(image,CurrentRva+4))return output;
    const auto get=[&](std::uintptr_t address,auto& value){return Address(address,sizeof(value))&&read(address,&value,sizeof(value));};
    std::uint32_t object=0,vtable=0,handle=0,objectAfter=0,vtableAfter=0,handleAfter=0;
    std::uint8_t active=0,activeAfter=0;
    if(!get(image+CurrentRva,object))return output;
    if(!object){output.valid=true;return output;}
    if(!Address(object,HandleOffset+4)||!get(object,vtable)||vtable!=image+VtableRva||
       !get(object+ActiveOffset,active)||active>1)return output;
    std::uint32_t header[4]{};
    if(active){
        if(!get(object+HandleOffset,handle)||!Address(handle,sizeof(header))||!read(handle,header,sizeof(header))||
           !header[0]||!header[1]||header[0]>16384||header[1]>16384||
           !header[2]||header[2]>10000000||!header[3]||header[3]>header[2]+1)return output;
    }
    if(!get(image+CurrentRva,objectAfter)||objectAfter!=object||!get(object,vtableAfter)||vtableAfter!=vtable||
       !get(object+ActiveOffset,activeAfter)||activeAfter!=active)return output;
    if(active){
        std::uint32_t dimensions[2]{};
        if(!get(object+HandleOffset,handleAfter)||handleAfter!=handle||!read(handle,dimensions,sizeof(dimensions))||
           dimensions[0]!=header[0]||dimensions[1]!=header[1])return output;
    }
    output.valid=true;output.active=active!=0;output.object=object;output.handle=handle;
    output.width=header[0];output.height=header[1];output.frames=header[2];output.frame=header[3];return output;
}
}
