#pragma once
#include "native_camera_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// Exact read-only command markers consumed by native ZActionMarkerDisplay.
// Their lifetime and target position are supplied by the game, not by the
// last accepted VR request. This also observes native AI/order changes.
namespace NativeTeamMarkerKL {
using NativeCameraMath::Vec3;
enum class Icon : unsigned {Circle,Shield,Lightning};
constexpr unsigned MaximumMarkers=16,MaximumNativeMarkers=256,Stride=0x64;
constexpr unsigned SelectionType=2,SelectionTexture=0x12;
struct Marker {Icon icon=Icon::Circle;Vec3 position{};bool fixed=false;};
struct Snapshot {
    bool valid=false;std::uintptr_t player=0;std::uint32_t tick=0;
    unsigned count=0;std::array<Marker,MaximumMarkers> markers{};
};
inline Icon StatusIcon(std::uint32_t status){return status==7?Icon::Shield:status==8?Icon::Lightning:Icon::Circle;}
inline bool TypeIcon(std::uint32_t type,Icon& icon) {
    if(type==7||type==21||type==27){icon=Icon::Shield;return true;}
    if(type==8||type==20){icon=Icon::Lightning;return true;}
    if(type==9){icon=Icon::Circle;return true;}
    return false;
}
inline bool Fresh(const Snapshot& value,std::uintptr_t player,std::uint32_t now) {
    if(!value.valid||!player||value.player!=player||!value.tick||std::int32_t(now-value.tick)<0||
       now-value.tick>150||value.count>MaximumMarkers)return false;
    for(unsigned i=0;i<value.count;++i)if(!NativeCameraMath::Finite(value.markers[i].position)||
        unsigned(value.markers[i].icon)>unsigned(Icon::Lightning))return false;
    return true;
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[64]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char manager[]={0x8B,0x81,0x50,0x15,0,0,0xC3};
    const unsigned char count[]={0x8B,0x41,8,0xC3};
    const unsigned char record[]={0x6B,0xC0,0x64,0x03,0x41,0x14,0xC2,4,0};
    const unsigned char camera[]={0x8B,0x81,0xA0,9,0,0,0xC3};
    const unsigned char playerIndex[]={0x8B,0x41,4,0x8B,0x0D,0x0C,0x76,0xA8,0,0x50};
    const unsigned char filterArgument[]={0x8B,0x4E,4,0x51,0x8B,0xCF};
    const unsigned char filterCompare[]={0x8B,0x41,0x44,0x83,0xF8,0xFF,0x74,0x0B,0x39,0x44,0x24,4};
    const unsigned char table[]={0x8B,0x57,0x1C,0x8B,0x04,0x95,0x08,0x7B,0xA4,0};
    const unsigned char fixed[]={0xD9,0x41,8,0xDC,0x05,0x60,0x12,0x94,0};
    const unsigned char nativeMove[]={0x6A,0,0x6A,7};
    const unsigned char nativeAttack[]={0x6A,0,0x6A,8};
    const unsigned char nativeRecall[]={0x6A,0,0x6A,9};
    // Move613530 creates a fixed type21 marker owned by the local player.
    // Its entity is a lifetime owner, not the object at the destination.
    const unsigned char pointType[]={0x6A,0x15,0x52,0x51,0x50};
    const unsigned char pointFixed[]={0x88,0x51,0x4D,0xD9,0x05,0x94,0x3E,0x97,0};
    if(!match(0x270490,manager)||!match(0x12F500,count)||!match(0x25418E,record)||
       !match(0x1E8950,camera)||!match(0x2548C0,playerIndex)||!match(0x253C3E,filterArgument)||
       !match(0x2531C0,filterCompare)||!match(0x253C56,table)||!match(0x25348D,fixed)||
       !match(0x213303,nativeMove)||!match(0x213F1F,nativeAttack)||!match(0x213C35,nativeRecall)||
       !match(0x213549,pointType)||!match(0x25308E,pointFixed))return false;
    const struct {std::uint32_t type,texture,nameRva;const char* name;} types[]={
        {7,0x19,0x574F28,"tex:UserInterface/Icons/UI_KL_Communications_01#Defend"},
        {8,0x17,0x574F98,"tex:UserInterface/Icons/UI_KL_Communications_01#Attack"},
        {9,0x1A,0x574EF0,"tex:UserInterface/Icons/UI_KL_Communications_01#Follow"},
        {20,0x22,0x574F98,"tex:UserInterface/Icons/UI_KL_Communications_01#Attack"},
        {21,0x23,0x574F28,"tex:UserInterface/Icons/UI_KL_Communications_01#Defend"},
        {27,0x23,0x574F28,"tex:UserInterface/Icons/UI_KL_Communications_01#Defend"}};
    for(const auto& entry:types) {
        std::uint32_t texture=0;char text[64]{};
        if(!read(image+0x647B08+entry.type*4,&texture,4)||texture!=entry.texture||
           !read(image+entry.nameRva,text,std::strlen(entry.name)+1)||std::strcmp(text,entry.name))return false;
    }
    double offset=0;return read(image+0x541260,&offset,sizeof(offset))&&offset==30.;
}
template<class Reader> Snapshot Capture(std::uintptr_t image,std::uintptr_t expected,std::uint32_t now,Reader read) {
    Snapshot output;std::uint32_t data=0,player=0,vt=0,camera=0,list=0,records=0,count=0;
    std::int8_t playerIndex=-1;
    std::uint8_t paused[2]{};
    const auto get=[&](std::uintptr_t at,auto& value){return at>=0x10000u&&at<=0xffffffffu-sizeof(value)&&read(at,&value,sizeof(value));};
    if(!image||!expected||!now||!get(image+0x68760C,data)||!get(data+0xA2C,player)||player!=expected||
       !get(player,vt)||vt!=image+0x56582C||!get(player+0x804,playerIndex)||playerIndex<0||playerIndex>1||
       !get(player+0x9A0,camera)||!get(camera,vt)||vt!=image+0x545A3C||
       !read(image+0x8883A4,paused,sizeof(paused))||paused[0]||paused[1]||
       !get(data+0x1550,list)||!get(list+8,count)||count>MaximumNativeMarkers||!get(list+0x14,records))return output;
    std::array<std::array<unsigned char,Stride>,MaximumNativeMarkers> raw{};
    if(count&&(!records||records>0xffffffffu-std::size_t(count)*Stride||!read(records,raw.data(),std::size_t(count)*Stride)))return output;
    output.valid=true;output.player=player;output.tick=now;
    for(unsigned i=0;i<count;++i) {
        const auto field=[&](unsigned at,auto& value){std::memcpy(&value,raw[i].data()+at,sizeof(value));};
        std::uint32_t type=0,filter=0,entity=0;Vec3 position;float bob=0;
        Icon icon;field(0x1C,type);if(!TypeIcon(type,icon))continue;
        // Native653C3E passes Display+4 (GetPlayer index6548C0), not
        // player+9A0's camera pointer. Most single-player records use index0.
        field(0x44,filter);if(filter!=0xffffffffu&&filter!=static_cast<unsigned>(playerIndex))continue;
        field(0,entity);field(4,position);field(0x40,bob);
        // Fixed destinations are authored WORLD points. Their owner is the
        // local player (652FE0), whose mesh is intentionally hidden in VR.
        // Applying attached-target visibility to that owner hid every shield.
        if(entity&&!raw[i][0x4D]) {
            std::uint32_t geometry=0,owner=0,flags=0;
            if(!get(entity+4,geometry)||!get(geometry+0x60,owner)||owner!=entity||!get(geometry+0x3C,flags))return {};
            if(flags&0xC00u)continue;
        }
        // Native Position653480 adds30 units for fixed markers and the
        // native animation offset only when its +4F flag is set.
        if(raw[i][0x4D])position.y+=30.f;
        else if(raw[i][0x4F])position.y+=bob;
        if(!NativeCameraMath::Finite(position))continue;
        std::array<unsigned char,Stride> checked{};
        if(!read(records+std::size_t(i)*Stride,checked.data(),Stride)||checked!=raw[i])return {};
        if(output.count<MaximumMarkers)output.markers[output.count++]={icon,position,raw[i][0x4D]!=0};
    }
    std::uint32_t afterData=0,afterPlayer=0,afterCamera=0,afterList=0,afterCount=0,afterRecords=0;
    std::int8_t afterIndex=-1;
    if(!get(image+0x68760C,afterData)||afterData!=data||!get(data+0xA2C,afterPlayer)||afterPlayer!=player||
       !get(player+0x804,afterIndex)||afterIndex!=playerIndex||!get(player+0x9A0,afterCamera)||afterCamera!=camera||
       !get(data+0x1550,afterList)||afterList!=list||
       !get(list+8,afterCount)||afterCount!=count||!get(list+0x14,afterRecords)||afterRecords!=records)return {};
    return output;
}
} // namespace NativeTeamMarkerKL
