#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include "native_buffer_flush_policy.hpp"

// The pinned render worker flushes its static VB/IB shadow before BeginScene
// and the first camera packet. These are NOT the dynamic DISCARD/NOOVERWRITE
// writers measured by NativeHudKL's original Lock scopes.
namespace NativeBufferFlushKL {
enum Phase : unsigned {StaticFlush,BeginScene,PhaseCount};
enum Resource : unsigned {Unknown,StaticVertex,StaticIndex};
enum class Backend : unsigned {Unknown,Original,DXVK,On12};
inline constexpr unsigned DeviceQueryInterfaceSlot=0,DeviceCapsSlot=7,DeviceCreationParametersSlot=9;
inline const char* BackendName(Backend value){
    switch(value){case Backend::Original:return "Original";case Backend::DXVK:return "DXVK";
        case Backend::On12:return "D3D9On12";default:return "Unknown";}
}
inline Backend SelectBackend(bool on12Capability,bool verifiedSystemDevice,bool verifiedDxvkDevice){
    return on12Capability?Backend::On12:verifiedSystemDevice?Backend::Original:verifiedDxvkDevice?Backend::DXVK:Backend::Unknown;
}
inline bool SupportsBatching(Backend value){return value==Backend::On12||value==Backend::Original;}
inline constexpr std::uintptr_t FlushRva=0x8E030,BeginSceneRva=0x8E530,StaticVertexCreateRva=0x8DE50;
inline constexpr unsigned EventCapacity=512;
// Whole native constructor: exact stack ABI, CreateVertexBuffer slot26,
// shadow allocation and initialization. Constructor has only one caller.
inline constexpr unsigned char StaticVertexCreateCode[]={
    0x8B,0x44,0x24,0x04,0x53,0x8B,0x5C,0x24,0x14,0x56,0x57,0x33,0xD2,0x52,0x8B,0xF1,
    0x8B,0x4C,0x24,0x18,0x56,0x53,0x52,0x89,0x56,0x04,0x89,0x96,0x10,0x40,0x00,0x00,
    0x8B,0x54,0x24,0x28,0x89,0x4E,0x08,0x8B,0x40,0x04,0x8B,0x38,0x52,0x51,0x50,0x8B,
    0x47,0x68,0xFF,0xD0,0x8B,0x4E,0x08,0x51,0xE8,0x63,0x2F,0xFB,0xFF,0x83,0xC4,0x04,
    0x89,0x46,0x0C,0x5F,0x8B,0xC6,0x5E,0x5B,0xC2,0x10,0x00};
struct Metadata {
    bool pending=false,valid=false,shadowReady=false;
    unsigned resource=Unknown,capacity=0,ranges=0,spanBytes=0,submittedRanges=0;
    std::uint64_t bytes=0;
};
struct Event {
    std::uint64_t sequence=0,startUs=0,endUs=0,wallUs=0;
    std::uint32_t tick=0,thread=0,phase=0;
    Metadata metadata{};
    std::int32_t result=0;
};
struct Totals {
    std::uint64_t calls=0,active=0,totalUs=0,maxUs=0,bytes=0,ranges=0;
};
struct Stats {
    bool installed=false,enabled=false,verified=false,batching=false,on12=false,active=false,batchingFaulted=false;
    bool batchingSupported=false;
    Backend backend=Backend::Unknown;
    std::uint64_t events=0,dropped=0,batchedCalls=0,savedLocks=0;
    std::uint64_t singleMapCalls=0,lockFailures=0,unlockFailures=0,invalidMappings=0;
    std::uint64_t uploadDeferred=0,uploadRecovered=0,recoveryAttempts=0,recoveryTimeouts=0,blockedDraws=0;
    std::uint64_t dynamicVertexCreated=0,dynamicVertexFallback=0;
    unsigned pendingUploads=0;
    std::uint32_t firstFailureTick=0,lastFailureTick=0;
    std::int32_t lastUploadResult=0;
    Event inProgress{};
    Totals phases[PhaseCount]{};
};
using LogCallback=void(*)(const char*);
bool Install(LogCallback log=nullptr);
bool InstallAt(std::uintptr_t image,LogCallback log=nullptr);
void SetEnabled(bool value);
void ConfigureBatching(bool value);
// Call when binding/resetting a device. The capability check holds no COM ref.
void SetDevice(IDirect3DDevice9* device);
// No device calls on the usual no-failure path. UP draws never use native
// static buffers and must not call this predicate.
bool ShouldBlockDraw(IDirect3DDevice9* device,bool indexed=true);
Stats Snapshot();
std::size_t ReadEvents(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten);
// Hook removal is for process shutdown/private tests; no DLL unload permitted
// while another thread may still be executing a wrapper or trampoline.
bool Shutdown();

// Only merge consecutive entries whose exact union has no hole. Input order
// is retained; never sort, widen across gaps, or reinterpret zero-byte locks.
inline unsigned CompactRanges(Range* ranges,unsigned count,unsigned capacity){
    if(!ranges||count<2||count>=2048||!capacity)return 0;
    for(unsigned i=0;i<count;++i)if(!ranges[i].bytes||ranges[i].offset>=capacity||
        ranges[i].bytes>capacity-ranges[i].offset)return 0;
    unsigned out=0;
    for(unsigned i=1;i<count;++i){auto& a=ranges[out];const auto b=ranges[i];
        const auto aEnd=a.offset+a.bytes,bEnd=b.offset+b.bytes;
        if(b.offset<=aEnd&&a.offset<=bEnd){const auto first=a.offset<b.offset?a.offset:b.offset;
            const auto last=aEnd>bEnd?aEnd:bEnd;a={first,last-first};}
        else ranges[++out]=b;}
    return out+1;
}

template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read){
    const auto get=[&](std::uintptr_t at,auto& value){return read(at,&value,sizeof(value));};
    std::uint16_t mz=0,machine=0,magic=0;std::uint32_t nt=0,signature=0,size=0;
    if(image<0x10000u||image>0xffffffffu-0x800000u||!get(image,mz)||mz!=0x5A4D||
       !get(image+0x3C,nt)||nt>0x10000||!get(image+nt,signature)||signature!=0x4550||
       !get(image+nt+4,machine)||machine!=0x14C||!get(image+nt+24,magic)||magic!=0x10B||
       !get(image+nt+0x50,size)||size<0x786674||size>0x1000000||image>0xffffffffu-size)return false;
    const auto match=[&](std::uint32_t rva,const unsigned char* expected,std::size_t count){
        unsigned char actual[96]{};return count<=sizeof(actual)&&rva<size&&count<=size-rva&&
            read(image+rva,actual,count)&&!std::memcmp(actual,expected,count);};
    const unsigned char flush[]={0x83,0xEC,0x08,0x56,0x8B,0xF1};
    const unsigned char begin[]={0x56,0x8B,0xF1,0x8B,0x46,0x04};
    if(!match(FlushRva,flush,sizeof(flush))||!match(BeginSceneRva,begin,sizeof(begin)))return false;
    const unsigned char createCall[]={0xE8,0xD8,0x65,0x00,0x00};
    if(!match(StaticVertexCreateRva,StaticVertexCreateCode,sizeof(StaticVertexCreateCode))||
       !match(0x87873,createCall,sizeof(createCall)))return false;
    // Exactly the two static shadow uploads and the following BeginScene in
    // the native dispatch worker. No inference from a similar buffer layout.
    for(const auto call:{std::uint32_t(0xB52F3),std::uint32_t(0xB5300),std::uint32_t(0xB534D)}){
        unsigned char code[5]{};if(!read(image+call,code,5)||code[0]!=0xE8)return false;
        std::int32_t displacement=0;std::memcpy(&displacement,code+1,4);
        const auto destination=static_cast<std::uint32_t>(call+5+displacement);
        if(destination!=(call==0xB534D?BeginSceneRva:FlushRva))return false;
    }
    return true;
}
}
