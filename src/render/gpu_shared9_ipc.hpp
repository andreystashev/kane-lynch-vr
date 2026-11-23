#pragma once
#include "vr_ipc.hpp"
#include "gpu_frame_objects.hpp"
#include <array>
#include <memory>

// Separate protocol from GPUFrame v1 (D3D9On12/NT fences). Native D3D11 owns
// legacy KMT textures; DXVK imports them. No pointers/pixels/NT handle duplication.
namespace SharedD3D9 {
constexpr unsigned Magic=0x3953474b,Version=2,PerEye=3,Slots=PerEye*2;
enum : LONG {Free=0,Writing=1,Ready=2,Reading=3,ReadPending=4};
inline LONG Load(const volatile LONG& v){return InterlockedCompareExchange(const_cast<volatile LONG*>(&v),0,0);}
inline std::uint64_t Born(HANDLE process){FILETIME c{},e{},k{},u{};return GetProcessTimes(process,&c,&e,&k,&u)?(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime:0;}
inline bool SameAdapter(LUID a,LUID b){return a.HighPart==b.HighPart&&a.LowPart==b.LowPart;}
inline bool SamePose(const VRIPC::EyeMetadata& a,const VRIPC::EyeMetadata& b){
 if(!std::isfinite(a.sensorTime)||a.sensorTime<=0||a.sensorTime!=b.sensorTime||std::memcmp(a.headOrientation,b.headOrientation,sizeof(a.headOrientation))||std::memcmp(a.headPosition,b.headPosition,sizeof(a.headPosition)))return false;
 double q=0;for(float v:a.headOrientation){if(!std::isfinite(v))return false;q+=double(v)*v;}if(q<.9||q>1.1)return false;
 for(float v:a.headPosition)if(!std::isfinite(v))return false;
 for(float v:a.fov)if(!std::isfinite(v)||v<=.01f||v>10.f)return false;for(float v:b.fov)if(!std::isfinite(v)||v<=.01f||v>10.f)return false;return true;
}
#pragma pack(push,8)
struct Identity {std::uint32_t magic,version,bytes,pid;std::uint64_t creation;std::uint8_t reserved[40];};
struct Source {volatile LONG sequence;std::uint32_t request;volatile LONG tick;std::uint32_t width,height;LUID adapter;volatile LONG accepted;volatile LONG earlyPoll;std::uint8_t reserved[28];};
struct Offer {volatile LONG sequence,pid,tick,enabled;std::uint64_t creation;std::uint32_t request,generation,width,height,format;LUID adapter;std::uint32_t pad;std::uint64_t handles[Slots];volatile LONG failedRequest;std::uint8_t reserved[20];};
struct alignas(64) Slot {
 volatile LONG state;
 std::uint32_t generation;
 std::uint64_t serial;
 std::uint32_t epoch,mask;
 VRIPC::EyeMetadata metadata;
 // Optional QPC observations in the original reserved bytes. Read only
 // while the consumer owns Reading. A v2 producer without timestamps writes zero.
 std::uint64_t queuedCounter,readyCounter;
 std::uint8_t reserved[8];
};
struct alignas(64) Shared {Identity identity;Source source;Offer offer;Slot slots[Slots];};
#pragma pack(pop)
static_assert(sizeof(Identity)==64&&sizeof(Source)==64&&sizeof(Offer)==128,"Shared9 header ABI");
static_assert(sizeof(Slot)==128&&offsetof(Slot,metadata)==24&&offsetof(Shared,slots)==256&&sizeof(Shared)==1024,"Shared9 wire ABI");
static_assert(offsetof(Slot,queuedCounter)==104&&offsetof(Slot,readyCounter)==112,"Shared9 timing offsets");
struct Request {unsigned id=0,width=0,height=0;LUID adapter{};};
struct Description {unsigned request=0,generation=0,width=0,height=0,format=0;LUID adapter{};std::uint64_t handles[Slots]{};};
inline bool ValidSize(unsigned w,unsigned h){return w>=16&&h>=16&&w<=VRIPC::kMaxGPUWidth&&h<=VRIPC::kMaxGPUHeight;}
inline bool Compatible(const Shared* p,DWORD pid,std::uint64_t creation){return p&&p->identity.magic==Magic&&p->identity.version==Version&&p->identity.bytes==sizeof(Shared)&&p->identity.pid==pid&&creation&&p->identity.creation==creation;}
inline void Name(wchar_t* out,std::size_t count,DWORD pid){std::swprintf(out,count,L"Local\\KaneLynchVR-Shared9-v2-%lu",pid);}
inline bool ReadRequest(const Shared& s,Request& r){const auto seq=Load(s.source.sequence);if(!seq||(seq&1))return false;Request n{s.source.request,s.source.width,s.source.height,s.source.adapter};MemoryBarrier();if(seq!=Load(s.source.sequence))return false;r=n;return true;}
inline bool ReadOffer(const Shared& s,Description& d){const auto seq=Load(s.offer.sequence);if(!seq||(seq&1))return false;Description n{s.offer.request,s.offer.generation,s.offer.width,s.offer.height,s.offer.format,s.offer.adapter,{}};std::memcpy(n.handles,s.offer.handles,sizeof(n.handles));MemoryBarrier();if(seq!=Load(s.offer.sequence))return false;d=n;return true;}
inline bool Change(Slot& s,unsigned generation,LONG from,LONG to){const auto prefix=std::uint64_t(generation)<<32;return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(&s),LONG64(prefix|unsigned(to)),LONG64(prefix|unsigned(from)))==LONG64(prefix|unsigned(from));}
struct Section {GPUFrame::Handle handle;Shared* shared=nullptr;~Section(){if(shared)UnmapViewOfFile(shared);}};
inline std::shared_ptr<Section> OpenSection(DWORD pid,std::uint64_t creation,bool create){
 wchar_t name[100]{};Name(name,100,pid);auto s=std::make_shared<Section>();
 s->handle.p=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name):OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
 if(!s->handle.p)return {};const bool existed=create&&GetLastError()==ERROR_ALREADY_EXISTS;
 s->shared=static_cast<Shared*>(MapViewOfFile(s->handle.p,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!s->shared)return {};
 if(create&&!existed){std::memset(s->shared,0,sizeof(Shared));s->shared->identity={0,Version,sizeof(Shared),pid,creation,{}};MemoryBarrier();InterlockedExchange(reinterpret_cast<volatile LONG*>(&s->shared->identity.magic),Magic);}
 if(!Compatible(s->shared,pid,creation))return {};return s;
}
}
