#pragma once
#include "native_camera_mover_kl.hpp"
#include "native_weapon_ownership.hpp"
#include "native_camera_math.hpp"
#include <algorithm>
#include <array>

namespace NativeTeamAssistKL {
constexpr std::size_t MoverBytes=0x2B0;
constexpr std::uintptr_t Picker=0x232620,Point=0x230750,Origin=0x231E50;
constexpr float CommandCone=36.f*0.01745329252f,PrecisionCommandCone=18.f*0.01745329252f;
struct Snapshot {std::uintptr_t mover=0,player=0;alignas(16) std::array<unsigned char,MoverBytes> bytes{},original{};float angle=0,rangeSquared=0;};
template<class T>void Store(Snapshot& value,std::size_t offset,const T& field){std::memcpy(value.bytes.data()+offset,&field,sizeof(field));}
template<class Reader> bool Prepare(std::uintptr_t image,std::uintptr_t player,NativeCameraMath::Vec3 origin,NativeCameraMath::Vec3 direction,Reader read,Snapshot& output){
    using namespace NativeWeaponOwnership;Snapshot next;std::uint32_t mover=0,vt=0,owner=0,controller=0,count=0,data=0;std::uint8_t precision=0;
    if(!NativeCameraMath::Finite(origin)||!NativeCameraMath::Finite(direction)||!Read(player+0x9AC,mover,read)||!Read(mover,vt,read)||
       !NativeCameraMoverKL::KnownVtable(image,vt)||!Read(mover+8,owner,read)||owner!=player||!Read(mover+0xC,controller,read)||controller<0x10000||
       !read(mover,next.bytes.data(),next.bytes.size())||!Read(mover+0x9C,precision,read)||precision>1||
       !Read(image+(precision?0x56AC84:0x56AC7C),next.angle,read)||!Read(image+0x646288,next.rangeSquared,read)||
       !std::isfinite(next.angle)||next.angle<=0||next.angle>1||!std::isfinite(next.rangeSquared)||next.rangeSquared<=0||next.rangeSquared>625000000.f||
       !Read(image+0x68760C,data,read)||!Read(data+4,count,read)||count>2048||!Read(data+0x2D88,count,read)||count>2048)return false;
    // The wider cone applies only to the private picker for a squad order.
    next.angle=std::max(next.angle,precision?PrecisionCommandCone:CommandCone);
    next.original=next.bytes;next.mover=mover;next.player=player;Store(next,0x34,direction);Store(next,0x22C,origin);Store(next,0x270,origin);Store(next,0x288,next.angle);Store(next,0x28C,next.rangeSquared);
    output=next;return true;
}
template<class Reader>bool Current(const Snapshot& snapshot,Reader read){
    std::uint32_t mover=0;std::array<unsigned char,MoverBytes> actual{};
    return NativeWeaponOwnership::Read(snapshot.player+0x9AC,mover,read)&&mover==snapshot.mover&&read(mover,actual.data(),actual.size())&&actual==snapshot.original;
}
template<class Reader>bool Verify(std::uintptr_t image,Reader read){
    const auto match=[&](std::uintptr_t rva,const auto& expected){std::array<unsigned char,sizeof(expected)> actual{};
        return read(image+rva,actual.data(),actual.size())&&!std::memcmp(actual.data(),expected,actual.size());};
    const unsigned char picker[]={0x83,0xEC,0x34,0x83,0x7C,0x24,0x38,1};
    const unsigned char point[]={0x53,0x56,0x8B,0x74,0x24,0x10,0x8B,0x1E};
    const unsigned char origin[]={0x83,0xEC,0x0C,0x56,0x8B,0xF1,0x83,0x7E,0x68,5};
    const unsigned char originCaller[]={0xE8,0x70,0xFF,0xFF,0xFF};
    float normal=0,precision=0,range=0;
    return match(Picker,picker)&&match(Point,point)&&match(Origin,origin)&&match(0x231EDB,originCaller)&&
        NativeWeaponOwnership::Read(image+0x56AC7C,normal,read)&&std::fabs(normal-.418879032f)<1e-7f&&
        NativeWeaponOwnership::Read(image+0x56AC84,precision,read)&&std::fabs(precision-.0872664601f)<1e-7f&&
        NativeWeaponOwnership::Read(image+0x646288,range,read)&&range==625000000.f;
}
}
