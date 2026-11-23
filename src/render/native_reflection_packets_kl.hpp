#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// A source ZCAMERA is not a render-pass identity. The verified native
// reflector queues four reflector-entry packets. The source camera entry is
// a separate producer argument; it is never the queued packet's entry.
// Classify only the complete producer-owned sequence, never arbitrary kind1
// packets sharing that camera or a render-target size.
namespace NativeReflectionPacketsKL {
constexpr std::size_t PacketBytes=0xB0;
struct Packet {
    std::uintptr_t address=0;
    std::array<unsigned char,PacketBytes> bytes{};
    std::uint32_t Word(unsigned offset)const {std::uint32_t x=0;std::memcpy(&x,bytes.data()+offset,4);return x;}
};
struct Certificate {
    std::uintptr_t queue=0,begin=0,end=0,sourceEntry=0,reflector=0,view=0;
    std::array<Packet,4> packets{};
    bool valid=false;
};
enum class Failure:unsigned {None,Span,Header,Owner,Length,CameraCount,CameraWords,View,Flags,Entry,Payload,MissingCamera,OriginalRestore,ReflectedRestore,QueueRead};
struct Trace {
    Failure reason=Failure::None;
    unsigned commands=0,cameras=0;
    std::uintptr_t at=0;
    std::uint32_t header[6]{};
    Certificate observed;
};
// Reader is an exact bounded read into private storage. The caller verifies
// the pinned executable, reflector vtable, source-entry vtable and source
// ZCAMERA before using this parser.
template<class Reader> bool Capture(std::uintptr_t queue,std::uintptr_t begin,std::uintptr_t end,
    std::uintptr_t sourceEntry,std::uintptr_t reflector,std::uintptr_t view,Reader read,Certificate& output,Trace* trace=nullptr) {
    Certificate next;next.queue=queue;next.begin=begin;next.end=end;
    next.sourceEntry=sourceEntry;next.reflector=reflector;next.view=view;
    Trace report;
    const auto fail=[&](Failure reason){report.reason=reason;report.observed=next;if(trace)*trace=report;return false;};
    if(!queue||!begin||end<=begin||end-begin>0x1000000||!sourceEntry||!reflector||!view)return fail(Failure::Span);
    for(auto at=begin;at<end;){
        std::uint32_t h[6]{};
        report.at=at;
        if(++report.commands>65536||end-at<sizeof(h)||!read(at,h,sizeof(h)))return fail(Failure::Header);
        std::memcpy(report.header,h,sizeof(h));
        if(h[2]!=queue)return fail(Failure::Owner);
        const std::uint64_t bytes=sizeof(h)+std::uint64_t(h[5])*4;
        if(bytes>end-at)return fail(Failure::Length);
        if(h[0]==1){
            if(report.cameras==4)return fail(Failure::CameraCount);
            auto& packet=next.packets[report.cameras++];packet.address=at;
            // Keep the offending header/payload before authorization checks:
            // a rejection must still explain the real native packet shape.
            if(bytes<PacketBytes||!read(at,packet.bytes.data(),packet.bytes.size()))return fail(Failure::Payload);
            if(h[5]!=0x26)return fail(Failure::CameraWords);
            if(h[3]!=view)return fail(Failure::View);
            if(h[4])return fail(Failure::Flags);
            if(h[1]!=reflector)return fail(Failure::Entry);
        }
        at+=static_cast<std::uintptr_t>(bytes);
    }
    // The two native original-view packets are a restore pair. The mirrored
    // view is repeated after target restoration, with its own authored P.
    if(report.cameras!=4)return fail(Failure::MissingCamera);
    if(std::memcmp(next.packets[0].bytes.data()+0x18,next.packets[2].bytes.data()+0x18,0x70))return fail(Failure::OriginalRestore);
    if(std::memcmp(next.packets[1].bytes.data()+0x18,next.packets[3].bytes.data()+0x18,0x30))return fail(Failure::ReflectedRestore);
    next.valid=true;output=next;
    if(trace){report.observed=next;*trace=report;}
    return true;
}
inline int Match(const Certificate& certificate,std::uintptr_t packet,const void* bytes,std::size_t size){
    if(!certificate.valid||!bytes||size!=PacketBytes)return -1;
    for(unsigned n=0;n<4;++n)if(certificate.packets[n].address==packet&&
        !std::memcmp(certificate.packets[n].bytes.data(),bytes,size))return static_cast<int>(n);
    return -1;
}
}
