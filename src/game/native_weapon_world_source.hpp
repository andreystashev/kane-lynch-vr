#pragma once
#include "native_weapon_ownership.hpp"
#include "native_camera_math.hpp"

namespace NativeWeaponWorldSource {
using namespace NativeCameraMath;
enum class Kind {None,Primary,Instance,ViewAligned};
struct Source {std::uintptr_t entry=0;Kind kind=Kind::None;};
// scopedEntry must come from the current native48F0A0 material dispatch.
// The return caller additionally identifies the exact native binder branch.
// Nothing here authorizes ownership; MeshFromEntry still performs that proof.
template<class Reader> bool Resolve(std::uintptr_t image,std::uintptr_t state,std::uintptr_t scopedEntry,
                                    const CameraPose* source,std::uintptr_t caller,bool sourceOnStack,
                                    Reader read,Source& output) {
    using namespace NativeWeaponOwnership;
    if(!source||!scopedEntry)return false;
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    std::uint32_t vt=0,gpu=2,method=0;CameraPose raw,inlineRoot;
    if(!Read(scopedEntry,vt,read)||!Read(vt+0x18,method,read)||!Read(image+0x7678A4,gpu,read)||gpu>1||
       !Read(address,raw,read)||!ValidatePose(raw,.05f)||!Read(scopedEntry+0x10+gpu*48,inlineRoot,read)||!ValidatePose(inlineRoot,.05f))return false;
    Source next;next.entry=scopedEntry;
    if(caller==image+0x72001&&vt==image+0x541034&&method==image+0x71F80&&address==scopedEntry+0x10+gpu*48)next.kind=Kind::Primary;
    else if(caller==image+0x7205F&&vt==image+0x541034&&method==image+0x71F80) {
        std::uint32_t list=0,rows=0,count=0,index=0;
        if(!Read(scopedEntry+0x90+gpu*4,list,read)||!list||!Read(list,count,read)||!count||count>1024||
           !Read(list+4,rows,read)||!Read(state+0x3C4,index,read)||index>=count||
           !Address(rows,std::size_t(count)*0x38)||address!=rows+std::uintptr_t(index)*0x38)return false;
        next.kind=Kind::Instance;
    } else if(caller==image+0x72F6A&&(vt==image+0x540EE0||vt==image+0x540FF8)&&method==image+0x72E00&&sourceOnStack) {
        std::uint32_t record=0,provider=0,resource=0,handle=0;std::uintptr_t descriptor=0;std::uint16_t type=0;std::uint8_t flags=0;
        if(!Read(state+0x14,record,read)||!Read(record+0xC,provider,read)||provider!=scopedEntry||!Read(record+4,resource,read)||
           !Read(resource+0x14,handle,read)||!Resource(image,handle,read,descriptor)||!Read(descriptor+2,type,read)||type!=8||
           !Read(descriptor+0xD,flags,read)||((flags&7)!=1&&(flags&7)!=2&&(flags&7)!=4)||
           std::memcmp(&raw.position,&inlineRoot.position,sizeof(Vec3)))return false;
        next.kind=Kind::ViewAligned;
    } else return false;
    output=next;return true;
}
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[32]{};return
        sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));};
    const unsigned char instance[]={0x8B,0x41,0x04,0x8D,0x3C,0xD0,0x57,0x8B,0xCE,0xE8,0x11,0xF7,0xFF,0xFF};
    const unsigned char viewAligned[]={0x53,0x8B,0xCE,0xE8,0x06,0xE8,0xFF,0xFF};
    const unsigned char dispatch[]={0x89,0x77,0x14,0x8B,0x4E,0x0C,0x8B,0x11,0x8B,0x42,0x14};
    return match(0x72051,instance)&&match(0x72F62,viewAligned)&&match(0x8F0DD,dispatch);
}
}
