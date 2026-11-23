#pragma once
#include <cstddef>
#include <cstdint>
namespace NativeEffectsKLProducer {
template<class Read> bool BillboardDescriptor(std::uintptr_t image,std::uintptr_t instance,Read read,std::uintptr_t& descriptor,unsigned& mode) {
    if(image<0x10000u||image>0xFFFFFFFFu-0x71F30Cu||instance<0x10000u)return false;
    std::uint32_t object=0,handle=0,resolved=0;std::uint16_t type=0;unsigned char flags=0;
    if(!read(instance+4,object)||!object||!read(object+0x14,handle))return false;
    if(handle&0x80000000u)resolved=handle&0x7FFFFFFFu;
    else if(handle>0x100000u||!read(image+0x71F308+std::uintptr_t(handle)*4,resolved))return false;
    if(resolved<0x10000u||!read(resolved+2,type)||type!=8||!read(resolved+0xD,flags))return false;
    descriptor=resolved;mode=flags&7u;return true;
}
template<class Read> bool VerifyBillboard(std::uintptr_t image,Read read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[32]{};return sizeof(expected)<=sizeof(actual)&&
        read(image+rva,actual,sizeof(expected))&&!__builtin_memcmp(actual,expected,sizeof(expected));};
    const unsigned char entry[]={0xA1,0xA4,0x78,0xB6,0,0x83,0xEC,0x30,0x53,0x56,0x57,0x8B,0xF9};
    const unsigned char mode[]={0x66,0x83,0x78,2,8,0x8B,0x74,0x24,0x40,0x0F,0x85,0x20,1,0,0,
        0x0F,0xB6,0x40,0x0D,0x83,0xE0,7};
    const unsigned char view[]={0x83,0xF8,1,0x8D,0x96,0xBC,0,0,0};
    const unsigned char position[]={0xD9,0x43,0x28,0xD9,0x5C,0x24,0x44,0xD9,0x43,0x2C,0xD9,0x5C,0x24,0x40,
        0xD9,0x43,0x24,0x8D,0x5C,0x24,0x0C};
    const unsigned char bind[]={0x53,0x8B,0xCE,0xE8,6,0xE8,0xFF,0xFF};
    const unsigned char exit[]={0x5F,0xD9,0x9E,0xCC,3,0,0,0x5E,0x5B,0x83,0xC4,0x30,0xC2,8,0};
    return match(0x72E00,entry)&&match(0x72E33,mode)&&match(0x72E4F,view)&&match(0x72F39,position)&&
        match(0x72F62,bind)&&match(0x72FCF,exit);
}
struct SpriteSourceInfo {
    std::uintptr_t entry=0,geometry=0,records=0;
    unsigned type=0,mode=~0u,count=0,slot=2,flags=~0u;
    bool cameraRelative=false;
};
template<class Read> bool VerifyParticleNormalCall(std::uintptr_t image,Read read){
    const unsigned char expected[]={0x8D,0x94,0x24,0xF4,0,0,0,0x52,0x8D,0x50,4,0x8D,0x8C,0x24,0x2C,1,0,0,0xE8,0x5F,0x4D,0xF9,0xFF};
    unsigned char actual[sizeof(expected)]{};
    return image&&read(image+0x99A0A,actual,sizeof(actual))&&!__builtin_memcmp(expected,actual,sizeof(expected));
}
template<class Read> bool VerifyParticleDeferredCall(std::uintptr_t image,Read read){
    if(!image||image>0xFFFFFFFFu-0x791400u)return false;
    // Native Type7 sends its already-expanded stride24 corners and separate
    // depth array to the shared queue. Never detour that shared queue globally.
    const unsigned char lead[]={0x52,0x51,0x68};
    const unsigned char tail[]={0x8D,0x88,0x2C,0x7E,0x0A,0x00,0xE8,0x87,0x01,0xFE,0xFF,0xEB,0x4F};
    unsigned char actualLead[3]{},actualTail[sizeof(tail)]{};
    std::uint32_t depth=0,vertices=0;unsigned char push=0;
    return read(image+0x9A172,actualLead,sizeof(actualLead))&&!__builtin_memcmp(lead,actualLead,sizeof(lead))&&
        read(image+0x9A175,&depth,4)&&depth==image+0x789400&&read(image+0x9A179,&push,1)&&push==0x68&&
        read(image+0x9A17A,&vertices,4)&&vertices==image+0x791400&&
        read(image+0x9A17E,actualTail,sizeof(actualTail))&&!__builtin_memcmp(tail,actualTail,sizeof(tail));
}
template<class Read> bool SpriteSource(std::uintptr_t image,std::uintptr_t binder,std::uintptr_t state,Read read,SpriteSourceInfo& output) {
    if(!state||image<0x10000u||image>0xFFFFFFFFu-0x7678A8u||binder<0x10000u)return false;
    std::uint32_t binderType=0,entry=0,entryType=0,slot=2,type=0,geometry=0;unsigned char aligned=0;
    if(!read(binder,binderType)||binderType!=image+0x53FB88||!read(binder+0xC,entry)||
       !read(entry,entryType)||entryType!=image+0x540EAC||!read(image+0x7678A4,slot)||slot>1||
       !read(entry+0x108+slot*20+0xC,type))return false;
    SpriteSourceInfo next;next.entry=entry;next.type=type;next.slot=slot;
    // Basic provenance for other native sprite types is diagnostic only.
    // Their record layouts are deliberately not interpreted here.
    if(type!=5&&type!=6&&type!=7){
        if(read(entry+0xFC,aligned))next.flags=aligned;
        if(read(entry+0x100+slot*4,geometry))next.geometry=geometry;
        output=next;return true;
    }
    if(type==7){
        if(!read(entry+0x100+slot*4,geometry)||!geometry||!read(geometry+0x30,aligned))return false;
        next.mode=aligned;if(read(entry+0xFC,aligned))next.flags=aligned;
    }
    else {
        std::uint32_t count=0,records=0;
        if(!read(entry+0xFC,aligned))return false;
        next.cameraRelative=aligned!=0;next.flags=aligned;
        if(!read(entry+0x100+slot*4,geometry)||!geometry)return false;
        // Ordinary camera-relative HUD needs no source-record reads at all.
        if(next.cameraRelative){next.geometry=geometry;output=next;return true;}
        if(!read(geometry,count)||!read(geometry+8,records)||!records||count>0x100000)return false;
        next.count=count;next.records=records;
        const std::uint64_t end=std::uint64_t(records)+std::uint64_t(count)*(type==5?40u:0x74u);
        if(records<0x10000u||end>0xFFFFFFFFu)return false;
    }
    next.geometry=geometry;output=next;return true;
}
template<class Read> bool ParticleSource(std::uintptr_t image,std::uintptr_t binder,std::uintptr_t state,Read read,unsigned& mode) {
    SpriteSourceInfo source;if(!SpriteSource(image,binder,state,read,source)||source.type!=7)return false;
    mode=source.mode;return true;
}
// BufferUnlock is a thiscall/fastcall void callback with NO stack arguments.
// __builtin_frame_address(0) forces a real callee frame: frame[0] is the saved
// native EBP, frame[1] its exact native return PC, frame+8 the native ESP after
// return. This is not a generic frame walk; offsets are pinned caller locals.
template<class Read> bool ParticlePrefix(std::uint32_t caller,std::uintptr_t frame,unsigned allocatedQuads,Read read,unsigned& live) {
    if(caller!=0x9A19B||frame<0x10000u||frame>0xFFFFFFFFu-0xA0u||!allocatedQuads||allocatedQuads>2048)return false;
    std::uint32_t actual=0,count=0;
    if(!read(frame+8+0x74,actual)||!read(frame+8+0x94,count)||count!=allocatedQuads||actual>count)return false;
    live=actual;return true;
}
template<class Read> bool ManualState(std::uint32_t caller,std::uintptr_t frame,Read read,std::uintptr_t& state) {
    if(caller!=0xA3EA4||frame<0x10000u)return false;
    std::uint32_t nativeFrame=0,source=0;
    if(!read(frame,nativeFrame)||nativeFrame<0x10000u||nativeFrame>0xFFFFFFFFu-0x3F14u||
       !read(std::uintptr_t(nativeFrame)+0x3F10,source)||source<0x10000u)return false;
    state=source;return true;
}
}
