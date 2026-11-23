#pragma once
#include "native_action_kl.hpp"
#include <array>
#include <cstring>
namespace NativeRopeKLImage {
constexpr std::uint32_t RopeVtable=0x57A79C;
struct Owned {std::uintptr_t object=0,geometry=0;};
struct Pool {std::uintptr_t player=0;unsigned count=0;std::array<Owned,8> ropes{};};
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[32]{};return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));};
    const unsigned char owner[]={0x89,0x86,0xF4,7,0,0,0x8B,0x82,0x10,1,0,0};
    const unsigned char pool[]={0x8B,0x0D,0x0C,0x76,0xA8,0,0x8B,0x81,0xCC,0x0A,0,0};
    const unsigned char allocate[]={0xE8,0x4E,0xA5,8,0,0x89,0x86,0x28,0x14,0,0};
    const unsigned char world[]={0x53,0x8B,0xD9,0x56,0x8B,0x74,0x24,0x0C,0x57,0x8D,0x93,0x8C,0,0,0};
    const unsigned char rigid[]={0x8B,0x44,0x24,0x08,0x81,0xEC,0xBC,0,0,0,0x53,0x55,0x8B,0xE9,0x8B,0x48,4,0x8B,0x41,0x14};
    std::uint32_t value=0;
    return bytes(0x2916DA,owner)&&bytes(0x291580,pool)&&bytes(0x206FFD,allocate)&&
        bytes(0x71770,world)&&bytes(0x739E0,rigid)&&
        read(image+0x541070+0x18,&value,4)&&value==image+0x739E0&&
        read(image+RopeVtable+0x14C,&value,4)&&value==image+0x291400;
}
template<class Reader> Pool Capture(std::uintptr_t image,std::uintptr_t player,Reader read) {
    Pool out;std::uint32_t data=0,local=0,vt=0,count=0;std::array<std::uint32_t,8> objects{};
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    if(!player||!get(image+0x68760C,data)||!get(data+0xA2C,local)||local!=player||!get(player,vt)||vt!=image+0x56582C||
       !get(data+0xACC,count)||count>8||!read(data+0xAD0,objects.data(),count*4))return out;
    out.player=player;
    for(unsigned i=0;i<count;++i) {
        const auto object=std::uintptr_t(objects[i]);std::uint32_t owner=0,geometry=0,back=0;
        if(!get(object,vt)||vt!=image+RopeVtable||!get(object+0x7F4,owner)||owner!=player||
           !get(object+4,geometry)||!get(geometry+0x60,back)||back!=object)continue;
        bool duplicate=false;for(unsigned j=0;j<out.count;++j)if(out.ropes[j].object==object)duplicate=true;
        if(!duplicate)out.ropes[out.count++]={object,geometry};
    }
    std::uint32_t after=0;if(!get(data+0xACC,after)||after!=count||!get(data+0xA2C,after)||after!=player)return {};
    return out;
}
inline bool Same(const Pool& a,const Pool& b) {
    if(a.player!=b.player||a.count!=b.count)return false;
    for(unsigned i=0;i<a.count;++i)if(a.ropes[i].object!=b.ropes[i].object||a.ropes[i].geometry!=b.ropes[i].geometry)return false;
    return true;
}
template<class Reader> bool OwnEntry(std::uintptr_t image,std::uintptr_t entry,const Pool& pool,Reader read) {
    std::uint32_t vt=0,geometry=0;
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    if(!pool.player||!pool.count||!get(entry,vt))return false;
    const auto rva=vt-image;
    if(rva!=0x540EE0&&rva!=0x540FF8&&rva!=0x541034&&rva!=0x541070)return false;
    if(!get(entry+0xF4,geometry))return false;
    for(unsigned depth=0;geometry&&depth<16;++depth) {
        for(unsigned i=0;i<pool.count;++i)if(geometry==pool.ropes[i].geometry) {
            std::uint32_t back=0,owner=0,currentGeometry=0;
            return get(geometry+0x60,back)&&back==pool.ropes[i].object&&get(back,vt)&&vt==image+RopeVtable&&
                get(back+0x7F4,owner)&&owner==pool.player&&get(back+4,currentGeometry)&&currentGeometry==geometry;
        }
        std::uint32_t parent=0;if(!get(geometry+0x50,parent)||parent==geometry)return false;geometry=parent;
    }
    return false;
}
}
