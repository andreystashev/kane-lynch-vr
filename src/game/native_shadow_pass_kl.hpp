#pragma once
#include "native_action_kl.hpp"
#include <cstring>

// Pinned shader-package pass identity, independent of colour/depth formats.
// The parser stores named ShadowMap/ShadowDrop in slots2/3; native Draw uses
// the package's selector+62 to select that exact +C table on this call stack.
namespace NativeShadowPassKL {
constexpr std::uintptr_t DrawRva=0x8F0A0;
struct Context {std::uintptr_t package=0,state=0,technique=0;unsigned pass=0;bool valid=false;};
template<class Reader> bool Snapshot(std::uintptr_t image,std::uintptr_t package,std::uintptr_t state,Reader read,Context& output) {
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t vtable=0,method=0,technique=0,afterVtable=0,afterTechnique=0;std::uint8_t pass=0,afterPass=0;
    if(!NativeActionKL::Address(state,0x440)||!get(package,vtable)||!get(vtable+0x20,method)||method!=image+DrawRva||
       !get(package+0x62,pass)||(pass!=2&&pass!=3)||!get(package+0xC+pass*4,technique)||
       !NativeActionKL::Address(technique,0x20)||!get(package,afterVtable)||afterVtable!=vtable||
       !get(package+0x62,afterPass)||afterPass!=pass||!get(package+0xC+pass*4,afterTechnique)||afterTechnique!=technique)return false;
    output={package,state,technique,pass,true};return true;
}
template<class Reader> bool Matches(std::uintptr_t image,const Context& active,std::uintptr_t state,Reader read) {
    Context current;
    return active.valid&&active.state==state&&Snapshot(image,active.package,state,read,current)&&
        current.package==active.package&&current.technique==active.technique&&current.pass==active.pass;
}
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[32]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));};
    const unsigned char draw[]={0x83,0xEC,0x0C,0x83,0x7C,0x24,0x18,0,0x0F,0xB6,0x41,0x62,0x53,0x8B,0x5C,0x81,0x0C};
    const unsigned char map[]={0xC7,0x44,0x24,0x14,2,0,0,0};
    const unsigned char drop[]={0xC7,0x44,0x24,0x14,3,0,0,0};
    const unsigned char table[]={0x8B,0x54,0x24,0x14,0x8B,0x8C,0x24,0xD0,0,0,0,0x89,0x44,0x91,0x0C};
    const unsigned char end[]={0x5B,0x83,0xC4,0x0C,0xC2,0x10,0};
    std::uint32_t mapName=0,dropName=0;
    return match(DrawRva,draw)&&match(0x96415,map)&&match(0x96434,drop)&&match(0x96A1C,table)&&match(0x8F15C,end)&&
        read(image+0x96404,&mapName,4)&&mapName==image+0x53F938&&
        read(image+0x96423,&dropName,4)&&dropName==image+0x53F92C;
}
}
