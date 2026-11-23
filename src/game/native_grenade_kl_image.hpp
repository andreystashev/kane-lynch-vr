#pragma once
#include <cstdint>
#include <cstring>
#include "native_camera_mover_kl.hpp"
#include "native_weapon_type_kl.hpp"

namespace NativeGrenadeKLImage {
constexpr std::uintptr_t ThrowUpdate=0x1FE810,ThrowVtable=0x56665C,ThrowTargetReturn=0x1FEE16;
constexpr std::uintptr_t PlayerVtable=0x56582C,FFWeaponVtable=0x577B74;
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    if(!NativeCameraMoverKL::VerifyImage(image,read)||!NativeWeaponTypeKL::Verify(image,read))return false;
    const auto bytes=[&](std::uintptr_t rva,const unsigned char* expected,std::size_t size) {
        unsigned char actual[64]{};
        return size<=sizeof(actual)&&read(image+rva,actual,size)&&!std::memcmp(actual,expected,size);
    };
    std::uint32_t value=0;
    if(!read(image+ThrowVtable+0x10,&value,4)||value!=image+ThrowUpdate||
       !read(image+FFWeaponVtable+0x2D4,&value,4)||value!=image+0x11DF40||
       !read(image+FFWeaponVtable+0x344,&value,4)||value!=image+0x279D00)return false;
    const unsigned char update[]={0x81,0xEC,0xB8,0,0,0,0x56,0x8B,0xF1,0xD9,0x46,0x14};
    const unsigned char origin[]={0x8D,0x44,0x24,0x28,0x50,0x8B,0xCF,0xE8,0x56,0xA9,0x07,0};
    const unsigned char target[]={0x8B,0x07,0x8B,0x90,0xD4,0x02,0,0,0x8D,0x4C,0x24,0x34,0x51,0x8B,0xCF,0xFF,0xD2};
    const unsigned char fire[]={0x8B,0xCF,0xE8,0x73,0xEF,0xF1,0xFF};
    const unsigned char aimGetter[]={0x8B,0x81,0xAC,0x09,0,0,0xC3};
    const unsigned char nativeAim[]={0x8B,0x4E,0x08,0xE8,0x81,0x8C,0xFE,0xFF,0x8D,0x50,0x34,
        0x8D,0x4C,0x24,0x34,0xE8,0x95,0xFC,0xE2,0xFF,0xDD,0xD8};
    if(!bytes(ThrowUpdate,update,sizeof(update))||!bytes(0x1FEC3E,origin,sizeof(origin))||
       !bytes(0x1FEE05,target,sizeof(target))||!bytes(ThrowTargetReturn,fire,sizeof(fire))||
       !bytes(0x1E7910,aimGetter,sizeof(aimGetter))||!bytes(0x1FEC87,nativeAim,sizeof(nativeAim)))return false;
    const struct {std::uintptr_t node,name;std::uint32_t value;const char* text;} types[]={
        {0x65E3C0,0x584D9C,11,"WT_GRENADE"},{0x65E3CC,0x584D90,12,"WT_MOLOTOV"}};
    for(const auto& type:types) {
        std::uint32_t name=0;char text[16]{};
        if(!read(image+type.node+4,&value,4)||value!=type.value||
           !read(image+type.node+8,&name,4)||name!=image+type.name||
           !read(name,text,std::strlen(type.text)+1)||std::strcmp(text,type.text))return false;
    }
    return true;
}
} // namespace NativeGrenadeKLImage
