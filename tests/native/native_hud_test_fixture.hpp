#pragma once
#include "native_team_marker_kl.hpp"
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

// Private read-only PE + object-layout fixture; never starts the game or maps
// executable code. Production helpers read these exact bytes via their Reader.
namespace NativeHudFixture {
constexpr std::uint32_t Base=0x400000,Data=0x10000000,Player=0x11000000,List=0x12000000,Records=0x13000000,Camera=0x14000000;
struct Image {
    std::vector<unsigned char> mapped;
    bool Load(const char* path) {
        std::ifstream file(path,std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
        const auto u32=[&](std::size_t at){std::uint32_t value=0;if(at<=raw.size()&&4<=raw.size()-at)std::memcpy(&value,raw.data()+at,4);return value;};
        const auto u16=[&](std::size_t at){std::uint16_t value=0;if(at<=raw.size()&&2<=raw.size()-at)std::memcpy(&value,raw.data()+at,2);return value;};
        const auto pe=u32(0x3C),optional=pe+24,size=u32(optional+56),sections=optional+u16(pe+20);
        if(u16(0)!=0x5A4D||u32(pe)!=0x4550||u16(pe+4)!=0x14C||u32(optional+28)!=Base||!size||size>0x2000000)return false;
        mapped.assign(size,0);
        for(unsigned i=0;i<u16(pe+6);++i){const auto at=sections+i*40,rva=u32(at+12),bytes=u32(at+16),offset=u32(at+20);
            if(rva>mapped.size()||bytes>mapped.size()-rva||offset>raw.size()||bytes>raw.size()-offset)return false;
            std::memcpy(mapped.data()+rva,raw.data()+offset,bytes);}
        return true;
    }
    bool Read(std::uintptr_t at,void* output,std::size_t size)const {
        if(at<Base||at-Base>mapped.size()||size>mapped.size()-(at-Base))return false;
        std::memcpy(output,mapped.data()+at-Base,size);return true;
    }
    auto Reader()const{return [&](auto at,void* output,auto size){return Read(at,output,size);};}
};
struct Memory {
    const Image* image=nullptr;
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=p[i];}
    bool Read(std::uintptr_t at,void* output,std::size_t size)const {
        auto* target=static_cast<unsigned char*>(output);
        for(std::size_t i=0;i<size;++i){const auto found=bytes.find(at+i);
            if(found!=bytes.end())target[i]=found->second;else if(!image||!image->Read(at+i,target+i,1))return false;}
        return true;
    }
    auto Reader()const{return [&](auto at,void* output,auto size){return Read(at,output,size);};}
    void Markers() {
        Set(Base+0x68760C,Data);Set(Data+0xA2C,Player);Set(Player,Base+0x56582C);Set(Player+0x804,std::int8_t(0));
        Set(Player+0x9A0,Camera);Set(Camera,Base+0x545A3C);Set(Base+0x8883A4,std::uint16_t(0));
        Set(Data+0x1550,List);Set(List+8,std::uint32_t(3));Set(List+0x14,Records);
        const NativeCameraMath::Vec3 positions[]={{250,20,70},{250,0,0},{250,20,-70}};
        const std::uint32_t types[]={9,7,8};
        for(unsigned i=0;i<3;++i){Set(Records+i*0x64,std::array<unsigned char,0x64>{});Set(Records+i*0x64+4,positions[i]);
            Set(Records+i*0x64+0x1C,types[i]);Set(Records+i*0x64+0x44,std::uint32_t(0));}
    }
};
}
