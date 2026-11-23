#pragma once
#include "vr_ipc.hpp"
#include "mono_frame_policy.hpp"
#include "stereo_pair_policy.hpp"

// Optional v1 side channel. Textures/fences are NT handles, duplicated from the
// producer process; this mapping never contains pixels or process pointers.
namespace GPUFrame {
constexpr std::uint32_t Magic = 0x47564C32, Version = 1, SlotCount = 4;
enum : LONG { Free = 0, Writing = 1, Ready = 2, Reading = 3 };
inline LONG Atomic(volatile LONG &v) {
  return InterlockedCompareExchange(&v, 0, 0);
}
#pragma pack(push, 8)
struct alignas(64) Slot {
  volatile LONG state;
  std::uint32_t generation, transportEpoch, eyeMask;
  std::uint64_t value;
  VRIPC::EyeMetadata metadata;
};
struct alignas(64) Shared {
  std::uint32_t magic, version, bytes, gamePid;
  volatile LONG headerSequence, consumerPid, consumerTick, consumerEnabled;
  volatile LONG acceptedGeneration, cpuRequested;
  std::uint32_t generation, width, height, format;
  LUID adapter;
  std::uint64_t readyHandle, textureHandle[SlotCount], doneHandle[SlotCount];
  std::uint8_t reserved[56]; // Explicit padding: pack(8) caps member alignment.
  Slot slots[SlotCount];
};
struct Header {
  std::uint32_t generation, width, height, format;
  LUID adapter;
  std::uint64_t readyHandle, textureHandle[SlotCount], doneHandle[SlotCount];
};
#pragma pack(pop)
static_assert(sizeof(Slot) == 128 && offsetof(Slot, metadata) == 24,
              "GPU slot ABI");
static_assert(offsetof(Shared, slots) == 192 && sizeof(Shared) == 704,
              "GPU mapping ABI");
inline void Name(wchar_t *out, std::size_t count, DWORD pid) {
  swprintf(out, count, L"Local\\KaneLynchVR-GPU-%lu", pid);
}
inline bool Compatible(const Shared *p, DWORD pid) {
  return p && p->magic == Magic && p->version == Version &&
         p->bytes == sizeof(Shared) && p->gamePid == pid;
}
inline bool ReadHeader(Shared &p, Header &out) {
  const auto seq = Atomic(p.headerSequence);
  if (!seq || (seq & 1))
    return false;
  Header h{};
  h.generation = p.generation;
  h.width = p.width;
  h.height = p.height;
  h.format = p.format;
  h.adapter = p.adapter;
  h.readyHandle = p.readyHandle;
  std::memcpy(h.textureHandle, p.textureHandle, sizeof(h.textureHandle));
  std::memcpy(h.doneHandle, p.doneHandle, sizeof(h.doneHandle));
  MemoryBarrier();
  if (Atomic(p.headerSequence) != seq)
    return false;
  out = h;
  return true;
}
inline bool SameAdapter(const LUID &a, const LUID &b) {
  return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}
inline bool ValidHeader(const Header &h) {
  return h.generation && h.width >= 16 && h.height >= 16 &&
         h.width <= VRIPC::kMaxGPUWidth && h.height <= VRIPC::kMaxGPUHeight &&
         h.format == 87 && h.readyHandle;
}
struct PairCandidate {bool ready=false;std::uint32_t epoch=0;VRIPC::EyeMetadata metadata{};};
// A fullscreen UI/scope image announces the same token to both eyes. Refuse recovery
// while either side is a newer scene/CPU/reset publication; an old UI slot
// remaining in the ring cannot flatten a newly announced stereo scene, and
// an old UI image cannot become a native optic (or vice versa).
inline bool ReadPanelAnnouncement(const VRIPC::SharedFrames& frames,DWORD producer,unsigned epoch,
                                  VRIPC::EyeMetadata& output) {
  const auto identity=[&]{return producer&&frames.magic==VRIPC::kMagic&&frames.version==VRIPC::kVersion&&
    frames.bytes==sizeof(frames)&&frames.gamePid==producer;};
  if(!identity())return false;
  LONG sequence[2]{};VRIPC::EyeMetadata metadata[2]{};unsigned slots[2]{};
  for(unsigned side=0;side<2;++side){const auto& eye=frames.eyes[side];
    sequence[side]=Atomic(const_cast<volatile LONG&>(eye.sequence));
    if(!sequence[side]||(sequence[side]&1)||eye.transport!=VRIPC::kGPU||eye.transportEpoch!=epoch)return false;
    slots[side]=eye.gpuSlot;std::memcpy(&metadata[side],&eye.metadata,sizeof(metadata[side]));
  }
  MemoryBarrier();
  for(unsigned side=0;side<2;++side){const auto& eye=frames.eyes[side];
    if(sequence[side]!=Atomic(const_cast<volatile LONG&>(eye.sequence))||eye.transport!=VRIPC::kGPU||
       eye.transportEpoch!=epoch||eye.gpuSlot!=slots[side])return false;
  }
  if(!identity()||slots[0]>=2||slots[0]!=slots[1]||
     std::memcmp(&metadata[0],&metadata[1],sizeof(metadata[0]))||!MonoFrame::Kind(metadata[0].poseValid)||
     !metadata[0].frameId||!VRIPC::ValidImageDimensions(metadata[0].width,metadata[0].height,VRIPC::kGPU)||
     metadata[0].stride!=metadata[0].width*4)return false;
  output=metadata[0];return true;
}
inline bool PanelWithinAnnouncement(const VRIPC::EyeMetadata& panel,const VRIPC::EyeMetadata& announced){
  return MonoFrame::Kind(panel.poseValid)&&panel.poseValid==announced.poseValid&&panel.frameId&&
    panel.frameId<=announced.frameId&&static_cast<LONG>(panel.tick-announced.tick)<=0&&
    panel.width==announced.width&&panel.height==announced.height&&panel.stride==announced.stride&&
    VRIPC::ValidImageDimensions(panel.width,panel.height,VRIPC::kGPU)&&panel.stride==panel.width*4;
}
inline bool LatestPanel(const PairCandidate (&c)[SlotCount],std::uint64_t afterFrame,DWORD minimumTick,unsigned epoch,
                        const VRIPC::EyeMetadata& announced,unsigned& selected){
  bool found=false;std::uint64_t newest=0;
  for(unsigned slot=0;slot<2;++slot){const auto& candidate=c[slot];const auto& panel=candidate.metadata;
    if(!candidate.ready||candidate.epoch!=epoch||panel.frameId<=afterFrame||
       static_cast<LONG>(panel.tick-minimumTick)<0||!PanelWithinAnnouncement(panel,announced))continue;
    if(!found||panel.frameId>newest){selected=slot;newest=panel.frameId;found=true;}
  }
  return found;
}
template<std::size_t Capacity> inline bool LatestScenePair(const PairCandidate (&c)[Capacity],std::uint64_t afterFrame,DWORD minimumTick,unsigned (&selected)[2]){
  bool found=false;std::uint64_t newest=0;
  static_assert(Capacity>=2&&Capacity%2==0,"equal left and right slot rings");
  constexpr auto eyes=Capacity/2;
  for(unsigned l=0;l<eyes;++l)for(unsigned r=eyes;r<Capacity;++r){const auto& a=c[l];const auto& b=c[r];
    const auto& x=a.metadata;const auto& y=b.metadata;
    if(!a.ready||!b.ready||a.epoch!=b.epoch||x.poseValid!=VRIPC::kSceneFrame||y.poseValid!=VRIPC::kSceneFrame||
       (x.frameId&1)||!(y.frameId&1)||x.frameId/2!=y.frameId/2||y.frameId<=afterFrame||
       static_cast<LONG>(x.tick-minimumTick)<0||static_cast<LONG>(y.tick-minimumTick)<0||
       x.width!=y.width||x.height!=y.height||x.stride!=y.stride||
       !VRIPC::ValidImageDimensions(x.width,x.height,VRIPC::kGPU)||x.stride!=x.width*4)continue;
    if(!StereoPairPolicy::Compatible(x,y))continue;
    if(!found||y.frameId>newest){selected[0]=l;selected[1]=r;newest=y.frameId;found=true;}
  }
  return found;
}
} // namespace GPUFrame
