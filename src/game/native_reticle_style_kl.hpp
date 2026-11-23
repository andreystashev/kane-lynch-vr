#pragma once
#include "native_camera_mover_kl.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace NativeReticleStyleKL {
enum class Style : unsigned {Dot=0,Crosshair=1};
constexpr std::size_t DisplayBytes=0x138;
using Display=std::array<unsigned char,DisplayBytes>;
// Exact ZCrossHairDisplay's native mode->texture table. Aiming mode13 uses
// Default instead of the armed shape in ordinary hip-fire. We select only
// its presentation resource; no precision flag, camera FOV or shot changes.
inline unsigned Glyph(unsigned mode,Style style){
    if(style!=Style::Dot&&style!=Style::Crosshair)return 0;
    switch(mode){
        case 1:case 11:return style==Style::Dot?10:2; // Gun
        case 2:case 7:return style==Style::Dot?10:3; // Rifle
        case 3:return style==Style::Dot?10:5; // Shotgun
        case 5:case 6:return style==Style::Dot?10:4; // MountedMG
        case 13:return style==Style::Dot?10:3; // native Default / sniper hip-fire
        default:return 0; // unarmed, grenade, heal, command and transfer
    }
}
template<class Reader>bool VerifyImage(std::uintptr_t image,Reader read){
    const auto bytes=[&](std::uintptr_t rva,const unsigned char* expected,std::size_t count){
        unsigned char actual[32]{};return count<=sizeof(actual)&&read(image+rva,actual,count)&&!std::memcmp(actual,expected,count);};
    const unsigned char draw[]={0x83,0xEC,0x58,0x53,0x8B,0x5C,0x24,0x60,0x55,0x56,0x57,0x8B,0xF9,0x83,0x7F,0x60,0};
    const unsigned char selected[]={0x8B,0x47,0x60,0xDB,0x40,4,0x8B,0x4F,0x0C};
    const unsigned char primary[]={0x89,0x4E,0x34,0xC7,0x46,0x2C,1,0,0,0};
    const unsigned char meter[]={0x8D,0x7E,0x70,0xBB,5,0,0,0};
    const std::uint32_t mapping[17]={0,2,3,5,0,4,4,3,6,0,0,2,0,10,16,17,0};
    std::uint32_t actual[17]{};
    if(!NativeCameraMoverKL::VerifyImage(image,read)||!bytes(0x2545A0,draw,sizeof(draw))||
       !bytes(0x254610,selected,sizeof(selected))||!bytes(0x25439B,primary,sizeof(primary))||
       !bytes(0x2543B6,meter,sizeof(meter))||!read(image+0x647C48,actual,sizeof(actual))||std::memcmp(actual,mapping,sizeof(actual)))return false;
    // These are the original textures, including MG's three-arm graphic and
    // its subtract layer, not a hand-drawn approximation of their shape.
    struct Resource {unsigned index;std::uintptr_t name;const char* suffix;};
    const Resource resources[]={{2,0x57533C,"Gun"},{3,0x575314,"Rifle"},{4,0x5752E8,"MountedMG"},
        {5,0x5752C0,"Shotgun"},{10,0x5751F8,"Default"},{15,0x575104,"Subtract_MountedMG"}};
    for(const auto& resource:resources){
        const auto descriptor=image+0x647D20+(resource.index-2)*20;
        std::uint32_t record[5]{};char name[80]{};
        const char prefix[]="tex:Crosshairs/Crosshair_Add_01#";
        const auto length=sizeof(prefix)-1+std::strlen(resource.suffix)+1;
        for(unsigned i=0;i<5;++i)if(!read(descriptor+i*4,&record[i],4))return false;
        if(record[0]!=resource.index||record[1]!=image+resource.name||record[2]!=(resource.index==15?0x60200u:0x60002u)||record[3]!=1||record[4]!=5||
           !read(record[1],name,length)||std::memcmp(name,prefix,sizeof(prefix)-1)||std::strcmp(name+sizeof(prefix)-1,resource.suffix))return false;
    }
    return true;
}
// Produce a complete render-only copy. The native emitter immediately copies
// its kind5 payload into its own queue. Neither display fields nor borrowed
// geometry/atlas data in the real game object are written.
template<class Reader>bool Prepare(std::uintptr_t image,std::uintptr_t display,std::uintptr_t player,
                                  Style style,std::uintptr_t copyAddress,Reader read,Display& output){
    const auto get=[&](std::uintptr_t at,auto& value){return at>=0x10000u&&at<=0xffffffffu-sizeof(value)&&read(at,&value,sizeof(value));};
    if(!player||copyAddress<0x10000u||copyAddress>0xffffffffu-DisplayBytes)return false;
    std::uint32_t vt=0,owner=0,table=0,mover=0,mode=~0u;
    if(!get(display,vt)||vt!=image+0x573F2C||!get(display+0x10,owner)||owner!=player||
       !get(display+8,table)||table<0x10000u||table>0xffffffffu-18*12||!get(player+0x9AC,mover)||!get(mover,vt)||
       !NativeCameraMoverKL::KnownVtable(image,vt)||!get(mover+8,owner)||owner!=player||!get(mover+0x68,mode))return false;
    const unsigned glyph=Glyph(mode,style);if(!glyph)return false;
    Display next{};
    if(!read(display,next.data(),next.size()))return false;
    const auto field=[&](unsigned offset){std::uint32_t value=0;std::memcpy(&value,next.data()+offset,4);return value;};
    if(field(0x2C)!=1||field(0x34)!=display+0x38||field(0x64)!=5||field(0x6C)!=display+0x70)return false;
    const std::uint32_t resource=table+glyph*12;
    std::uint32_t values[3]{};
    if(!read(resource,values,sizeof(values))||!values[0]||values[0]==0xffffffffu||!values[1]||!values[2]||values[1]>2048||values[2]>2048)return false;
    std::uint32_t afterOwner=0,afterMover=0,afterMode=~0u,afterTable=0;
    if(!get(display+0x10,afterOwner)||afterOwner!=player||!get(display+8,afterTable)||afterTable!=table||
       !get(player+0x9AC,afterMover)||afterMover!=mover||!get(mover+0x68,afterMode)||afterMode!=mode)return false;
    const auto put=[&](unsigned offset,std::uint32_t value){std::memcpy(next.data()+offset,&value,4);};
    put(0x60,resource);put(0x34,static_cast<std::uint32_t>(copyAddress+0x38));put(0x6C,static_cast<std::uint32_t>(copyAddress+0x70));
    output=next;return true;
}
}
