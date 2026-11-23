#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace NativeShaderConstantMap {
struct Range {unsigned start=0,count=0;};
struct Map {Range mvp{},eye{},lights{},lights11{};bool mvpColumns=false;};
// Read-only D3D9 CTAB parser. Offsets are relative to the CTAB structure
// after its four-byte tag; every read and string is bounded by that comment.
inline bool Parse(const void* data,std::size_t bytes,Map& output) {
    if(!data||bytes<8||bytes%4)return false;const auto* words=static_cast<const std::uint32_t*>(data);
    for(std::size_t index=1;index<bytes/4;) {
        const auto token=words[index];if((token&0xffff)!=0xfffe){++index;continue;}
        const auto count=(token>>16)&0x7fff;if(count>bytes/4-index-1)return false;
        if(count>=8&&words[index+1]==0x42415443) {
            const auto* c=reinterpret_cast<const unsigned char*>(words+index+2);const std::size_t size=(count-1)*4;
            const auto u32=[&](std::size_t at,std::uint32_t& value){if(at+4>size)return false;std::memcpy(&value,c+at,4);return true;};
            std::uint32_t constants=0,at=0;if(!u32(12,constants)||!u32(16,at)||constants>256||at>size||constants>(size-at)/20)return false;
            Map next;
            for(unsigned i=0;i<constants;++i) {
                const auto offset=at+i*20;std::uint32_t name=0;if(!u32(offset,name)||name>=size)return false;
                const auto* end=static_cast<const unsigned char*>(std::memchr(c+name,0,size-name));if(!end)return false;
                std::uint16_t set=0,start=0,n=0;std::memcpy(&set,c+offset+4,2);std::memcpy(&start,c+offset+6,2);std::memcpy(&n,c+offset+8,2);
                if(set!=2||!n||start+n>256)continue;
                const auto equal=[&](const char* text){return std::strlen(text)==std::size_t(end-(c+name))&&!std::memcmp(c+name,text,std::strlen(text));};
                const Range range{start,n};
                if(equal("m44ModelViewProj")){next.mvp=range;std::uint32_t type=0;std::uint16_t shape[5]{};
                    if(u32(offset+12,type)&&type<=size&&sizeof(shape)<=size-type){std::memcpy(shape,c+type,sizeof(shape));next.mvpColumns=shape[0]==3&&shape[1]==3&&shape[2]==4&&shape[3]==4&&shape[4]==1;}}
                else if(equal("v4EyePosObject"))next.eye=range;
                else if(equal("v4BonesLights"))next.lights=range;
                else if(equal("v4BonesLights11"))next.lights11=range;
            }
            output=next;return true;
        }
        index+=count+1;
    }
    return false;
}
}
