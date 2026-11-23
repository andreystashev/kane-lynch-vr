#pragma once
#include "native_camera_math.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

// Native tracer corners are expanded in the camera supplied to 696400.
// Recover that exact camera at the already-owned native SpriteEmit call.
// This changes deferred visual offsets, never projectile state, and needs no
// separate tracer Draw detour or generic third-person camera substitute.
namespace NativeTracerVisualKL {
using LogCallback=void(*)(const char*);
constexpr std::uintptr_t DrawRva=0x296400,EmitCallerRva=0x2966D3;
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t at,const auto& expected){unsigned char actual[48]{};return
        read(image+at,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));};
    const unsigned char draw[]={0x81,0xEC,0x90,0,0,0,0x53,0x55,0x56,0x8B,0xD9,0x57,0x8D,0x4C,0x24,0x7C,0x89,0x5C,0x24,0x30};
    const unsigned char camera[]={0x8B,0xB4,0x24,0xA8,0,0,0};
    const unsigned char view[]={0x8B,0xD6,0x8D,0x4C,0x24,0x34,0xE8,0xF4,0x87,0xD9,0xFF};
    const unsigned char emit[]={0x8B,0x8C,0x24,0xA4,0,0,0,0x8B,0x5C,0x24,0x1C,0x8B,0x11,0x8B,0x52,0x14,
        0x6A,0,0x6A,0,0x6A,9,0x8D,0x44,0x24,0x70,0x50,0x8D,0x84,0x24,0x8C,0,0,0,0x50,0x8B,0x47,0x3C,0x53,0x50,0xFF,0xD2};
    return image&&match(DrawRva,draw)&&match(0x296475,camera)&&match(0x2964F1,view)&&match(0x2966A9,emit);
}
struct StackRange {
    std::uintptr_t low=0,high=0;
    bool Contains(std::uintptr_t at,std::size_t bytes)const {
        return low>=0x10000u&&low<high&&at>=low&&at<high&&bytes<=high-at;
    }
};
struct Producer {
    bool valid=false;std::uintptr_t manager=0;NativeCameraMath::CameraPose camera{};
    std::uintptr_t transform=0,position=0,sourceCamera=0;
};
// At the exact native return2966D3, stable native ESP is S. Its transform
// argument is S+7C (LEA after four pushes), position is S+64 (after three),
// original manager is [S+A4], and camera is [S+A8]. Using that declared
// transform pointer avoids compiler-dependent addresses of C++ parameters.
template<class Reader> Producer CaptureProducer(std::uintptr_t manager,std::uint32_t caller,
    unsigned order,unsigned flags,unsigned extra,std::uintptr_t transform,std::uintptr_t position,
    const StackRange& stack,Reader read) {
    Producer out;
    if(caller!=EmitCallerRva||order!=9||flags||extra||!manager||transform<0x18u||position!=transform-0x18u||
       !stack.Contains(position,12)||!stack.Contains(transform,0x30))return out;
    std::uint32_t slots[2]{};
    if(!read(transform+0x28,slots,sizeof(slots))||slots[0]!=manager||slots[1]<0x10000u||
       slots[1]>0xFFFFFFFFu-sizeof(out.camera)||!read(slots[1],&out.camera,sizeof(out.camera))||
       !NativeCameraMath::ValidatePose(out.camera,.05f))return {};
    out.valid=true;out.manager=manager;out.transform=transform;out.position=position;out.sourceCamera=slots[1];return out;
}
template<class Reader> bool ProducerStillCurrent(const Producer& before,std::uintptr_t manager,std::uint32_t caller,
    unsigned order,unsigned flags,unsigned extra,const StackRange& stack,Reader read) {
    if(!before.valid)return false;
    const auto after=CaptureProducer(manager,caller,order,flags,extra,before.transform,before.position,stack,read);
    return after.valid&&after.manager==before.manager&&after.sourceCamera==before.sourceCamera&&
        !std::memcmp(&after.camera,&before.camera,sizeof(after.camera));
}
struct Packet {
    bool valid=false;std::uintptr_t entry=0,geometry=0,records=0;
    std::uint32_t slot=2,stamp=0,type=0,header[5]{};
    NativeCameraMath::CameraPose camera{};std::array<unsigned char,116> record{};
};
class Packets {
public:
    void Clear(){packets_={};}
    template<class Reader> bool Observe(std::uintptr_t image,const Producer& producer,std::uintptr_t entry,
        std::uintptr_t manager,std::uint32_t caller,unsigned order,unsigned flags,unsigned extra,Reader read) {
        std::uint32_t slot=2;if(!entry||!read(image+0x7678A0,&slot,4)||slot>1)return false;
        auto& packet=packets_[Index(entry,slot)];
        // Every foreign emit on this native entry/slot retires the old tracer
        // even when native memory reuses the same geometry/stamp/material.
        if(packet.entry==entry&&packet.slot==slot)packet={};
        if(!producer.valid||producer.manager!=manager||caller!=EmitCallerRva||order!=9||flags||extra||
           !NativeCameraMath::ValidatePose(producer.camera,.05f))return false;
        Packet next;next.entry=entry;next.slot=slot;std::uint32_t vt=0,geometry=0,records=0,count=0;
        unsigned char relative=1;
        if(!read(entry,&vt,4)||vt!=image+0x540EAC||!read(entry+0xFC,&relative,1)||relative||
           !read(entry+0x100+slot*4,&geometry,4)||!geometry||!read(entry+0x130+slot*4,&next.stamp,4)||
           !read(entry+0x108+slot*20,next.header,sizeof(next.header)))return false;
        next.type=next.header[3];if(next.type!=5&&next.type!=6)return false;
        if(!read(geometry,&count,4)||count!=1||!read(geometry+8,&records,4)||!records||
           !read(records,next.record.data(),next.type==5?40u:116u))return false;
        next.geometry=geometry;next.records=records;next.camera=producer.camera;next.valid=true;packet=next;return true;
    }
    template<class Reader> bool Reference(std::uintptr_t image,std::uintptr_t entry,std::uintptr_t geometry,
        std::uintptr_t records,unsigned type,unsigned first,unsigned count,Reader read,NativeCameraMath::CameraPose& output)const {
        std::uint32_t slot=2;if(first||count!=1||!read(image+0x7678A4,&slot,4)||slot>1)return false;
        const auto& p=packets_[Index(entry,slot)];if(!p.valid||p.entry!=entry||p.slot!=slot||p.geometry!=geometry||p.records!=records||p.type!=type)return false;
        std::uint32_t actual=0,header[5]{},live=0;unsigned char relative=1;std::array<unsigned char,116> bytes{};
        if(!read(entry,&actual,4)||actual!=image+0x540EAC||!read(entry+0xFC,&relative,1)||relative||
           !read(entry+0x100+slot*4,&actual,4)||actual!=geometry||!read(entry+0x130+slot*4,&actual,4)||actual!=p.stamp||
           !read(entry+0x108+slot*20,header,sizeof(header))||std::memcmp(header,p.header,sizeof(header))||
           !read(geometry,&live,4)||live!=1||!read(geometry+8,&actual,4)||actual!=records||
           !read(records,bytes.data(),type==5?40u:116u)||std::memcmp(bytes.data(),p.record.data(),type==5?40u:116u))return false;
        output=p.camera;return true;
    }
private:
    static unsigned Index(std::uintptr_t entry,unsigned slot){return unsigned((entry>>4)^(entry>>12)^slot)&255u;}
    std::array<Packet,256> packets_{};
};
bool Install(std::uintptr_t image,LogCallback log);
struct Stats {bool installed=false;std::uint64_t drawScopes=0,emits=0,captured=0,referenceMatched=0,rejected=0,producerReads=0,producerRejected=0;};
Stats Snapshot();
// The existing common SpriteEmit hook calls this after original emission,
// including foreign producers so recycled entries cannot retain a tracer tag.
using ProducerReader=Producer(*)(std::uintptr_t,std::uint32_t,unsigned,unsigned,unsigned,std::uintptr_t,std::uintptr_t);
inline std::atomic<ProducerReader> producerReader{nullptr};
using EmissionObserver=void(*)(std::uintptr_t,std::uintptr_t,std::uint32_t,unsigned,unsigned,unsigned,const Producer&);
inline std::atomic<EmissionObserver> emissionObserver{nullptr};
template<class Emit> std::uintptr_t ForwardSpriteEmission(std::uintptr_t manager,std::uint32_t caller,
    unsigned order,unsigned flags,unsigned extra,const void* transform,const void* position,Emit emit){
    Producer producer;
    if(const auto capture=producerReader.load(std::memory_order_relaxed))
        producer=capture(manager,caller,order,flags,extra,reinterpret_cast<std::uintptr_t>(transform),reinterpret_cast<std::uintptr_t>(position));
    const auto entry=emit();
    if(const auto observe=emissionObserver.load(std::memory_order_relaxed))observe(entry,manager,caller,order,flags,extra,producer);
    return entry;
}
// Exact native type5/6 pre-Unlock context; returns a copied producer camera.
using ReferenceReader=bool(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,unsigned,unsigned,unsigned,NativeCameraMath::CameraPose&);
inline std::atomic<ReferenceReader> referenceReader{nullptr};
inline bool ReadReference(std::uintptr_t entry,std::uintptr_t geometry,std::uintptr_t records,unsigned type,unsigned first,unsigned count,NativeCameraMath::CameraPose& output){
    const auto read=referenceReader.load(std::memory_order_relaxed);return read&&read(entry,geometry,records,type,first,count,output);
}
}
