#pragma once
#include "native_scope_kl.hpp"

// The authored Dragunov overlay is camera geometry, not CrossHairDisplay's
// additive/subtractive ordinary sprite. No texture, tint or scope is invented.
namespace NativeScopeOverlayKL {
constexpr std::uintptr_t ActivateRva=0x22BFD0,ClearRva=0x22C0C0;
struct State {
    std::uintptr_t player=0,aim=0,camera=0,logic=0,overlay=0,shape=0,geometry=0;
    std::uint32_t mode=0,stage=0;
    bool valid=false,active=false;
};
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t at,const unsigned char* wanted,std::size_t count){
        unsigned char actual[32]{};return count<=sizeof(actual)&&read(image+at,actual,count)&&!std::memcmp(actual,wanted,count);
    };
    const unsigned char activate[]={0x56,0x8B,0xF1,0x80,0xBE,0x74,6,0,0,0,0x74,0x14};
    const unsigned char clear[]={0x56,0x8B,0xF1,0x80,0xBE,0x74,6,0,0,0,0x74,0x7C};
    const unsigned char getter[]={0x8B,0x86,0xB0,9,0,0};
    const unsigned char logic[]={0x8B,0x4E,8,0xE8,0xB4,0x6E,0xFB,0xFF,0x85,0xC0,0x89,0x46,0xC};
    const unsigned char variant[]={0x88,0x9E,0xB4,1,0,0,0x8B,0x86,0xC4,1,0,0,0x89,0x86,0xB0,1,0,0};
    const unsigned char selected[]={0x8B,0x8E,0xB0,1,0,0,0x89,0xBE,0x20,2,0,0};
    std::uint32_t branch=0,vt=0;
    return bytes(ActivateRva,activate,sizeof(activate))&&bytes(ClearRva,clear,sizeof(clear))&&
        bytes(0x1E78FE,getter,sizeof(getter))&&bytes(0x230A34,logic,sizeof(logic))&&
        bytes(0x236156,variant,sizeof(variant))&&bytes(0x236168,selected,sizeof(selected))&&
        read(image+0x230310,&branch,4)&&branch==image+0x2301BF&&
        read(image+0x22B018,&vt,4)&&vt==image+0x56A96C&&
        read(image+0x22E772,&vt,4)&&vt==image+0x56AB44&&
        read(image+0x235C40,&vt,4)&&vt==image+0x56AF04;
}
template<class Reader> State Capture(std::uintptr_t image,const NativeScopeKL::State& optic,Reader read) {
    State out;out.player=optic.player;out.aim=optic.aim;out.camera=optic.sourceCamera;
    const auto get=[&](std::uintptr_t at,auto& value){return NativeWeaponOwnership::Read(at,value,read);};
    std::uint32_t data=0,player=0,mover=0,logic=0,vt=0,owner=0,camera=0,overlay=0,shape=0,geometry=0,name=0,selected=0;
    std::uint8_t active=0;out.stage=1;
    if(!optic.valid||!get(image+0x68760C,data)||!get(data+0xA2C,player)||player!=optic.player||
       !get(player,vt)||vt!=image+0x56582C||!get(player+0x9AC,mover)||mover!=optic.aim||
       !get(mover,vt)||!NativeCameraMoverKL::KnownVtable(image,vt)||!get(mover+8,owner)||owner!=player)return out;
    out.stage=2;
    if(!get(mover+0xC,logic)||!get(player+0x9B0,owner)||owner!=logic||!get(logic,vt)||vt!=image+0x56AB44||
       !get(logic+0x490,owner)||owner!=player||!get(logic+0x24,camera)||camera!=optic.sourceCamera||
       !get(player+0x9A0,owner)||owner!=camera)return out;
    out.logic=logic;out.stage=3;
    if(!get(logic+0x678,overlay)||!get(overlay,vt)||vt!=image+0x56AF04||
       !get(logic+0x674,active)||active>1||!get(overlay+0x220,out.mode))return out;
    out.overlay=overlay;out.active=active!=0;out.stage=4;
    if(!get(overlay+0x1C4,shape)||!get(shape+4,geometry)||!get(geometry+0x60,owner)||owner!=shape||
       !get(geometry+0x6C,name))return out;
    char actual[sizeof("Scope_Dragunov")]{};
    if(!read(name,actual,sizeof(actual))||std::memcmp(actual,"Scope_Dragunov",sizeof(actual)))return out;
    out.shape=shape;out.geometry=geometry;out.stage=5;
    if((out.mode==1&&(!get(overlay+0x1B0,selected)||selected!=shape||!active))||
       (out.mode==0&&active))return out;
    std::uint32_t after=0;
    if(!get(player+0x9AC,after)||after!=mover||!get(mover+0xC,after)||after!=logic||
       !get(player+0x9B0,after)||after!=logic||!get(player+0x9A0,after)||after!=camera||
       !get(logic+0x678,after)||after!=overlay||!get(overlay+0x1C4,after)||after!=shape)return out;
    out.valid=true;out.stage=6;return out;
}
inline bool Same(const State& a,const State& b) {
    return a.valid&&b.valid&&a.player==b.player&&a.aim==b.aim&&a.camera==b.camera&&a.logic==b.logic&&
        a.overlay==b.overlay&&a.shape==b.shape&&a.geometry==b.geometry;
}
} // namespace NativeScopeOverlayKL
