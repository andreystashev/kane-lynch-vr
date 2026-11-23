#pragma once
#include "capture_frame_policy.hpp"
#include "gpu_shared9_ipc.hpp"
#include "cpu_timer.hpp"
#include <d3d9.h>

namespace SharedD3D9 {
// DXVK 3.1.1 public interop slot3 + Vulkan1.1 ID properties. No queue mutation.
// The unused fixed VkPhysicalDeviceProperties payload is overallocated, aligned
// and never interpreted. Its Vulkan1.x ABI fits well inside this storage.
inline HRESULT DXVKAdapter(IDirect3DDevice9* device,LUID& out){
 const GUID iid={0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
 GPUFrame::Object<IUnknown> bridge;auto h=device->QueryInterface(iid,reinterpret_cast<void**>(bridge.Put()));if(FAILED(h))return h;
 void *instance=nullptr,*physical=nullptr,*logical=nullptr;using Handles=void(STDMETHODCALLTYPE*)(IUnknown*,void**,void**,void**);
 auto** v=*reinterpret_cast<void***>(bridge.p);reinterpret_cast<Handles>(v[3])(bridge.p,&instance,&physical,&logical);
 using GetProc=void*(WINAPI*)(void*,const char*);const auto loader=GetModuleHandleW(L"vulkan-1.dll");
 const auto get=loader?reinterpret_cast<GetProc>(GetProcAddress(loader,"vkGetInstanceProcAddr")):nullptr;
 using Properties=void(WINAPI*)(void*,void*);const auto properties=get?reinterpret_cast<Properties>(get(instance,"vkGetPhysicalDeviceProperties2")):nullptr;if(!properties||!physical)return E_NOINTERFACE;
 struct ID {std::uint32_t type=1000071004;void* next=nullptr;std::uint8_t device[16]{},driver[16]{},luid[8]{};std::uint32_t node=0,valid=0;} id;
 struct alignas(8) Block {std::uint32_t type=1000059001;void* next=nullptr;alignas(8) std::uint8_t unused[2048]{};std::uint64_t sentinel=0x18273645abcdef02ull;} block;
 static_assert(offsetof(Block,unused)==(sizeof(void*)==4?8:16),"Vulkan properties2 ABI");
 static_assert(sizeof(ID)==(sizeof(void*)==4?56:64),"Vulkan ID properties ABI");
 block.next=&id;properties(physical,&block);if(block.sentinel!=0x18273645abcdef02ull||!id.valid)return E_NOINTERFACE;
 std::memcpy(&out,id.luid,sizeof(out));return S_OK;
}
struct Producer {
 struct Generation {
  unsigned id=0,request=0,width=0,height=0;IDirect3DDevice9* identity=nullptr;
  GPUFrame::Object<IDirect3DTexture9> texture[Slots];GPUFrame::Object<IDirect3DSurface9> surface[Slots];GPUFrame::Object<IDirect3DQuery9> event[Slots];bool pending[Slots]{};
  GPUFrame::Object<IDirect3DSurface9> fullSizeResolve;
  D3DSURFACE_DESC resolveSource{};
  CaptureFramePolicy::PanelCopyCadence panelCadence;

  HRESULT Copy(IDirect3DSurface9* back,const D3DSURFACE_DESC& desc,unsigned slot) {
   const bool scaled = desc.Width!=width||desc.Height!=height;
   const bool canStage = desc.MultiSampleType!=D3DMULTISAMPLE_NONE&&(scaled||desc.Format!=D3DFMT_A8R8G8B8);
   const bool sameSource = desc.Width==resolveSource.Width&&desc.Height==resolveSource.Height&&
       desc.Format==resolveSource.Format&&desc.MultiSampleType==resolveSource.MultiSampleType&&
       desc.MultiSampleQuality==resolveSource.MultiSampleQuality;
   if(fullSizeResolve.p&&(!canStage||!sameSource))fullSizeResolve.Reset();
   if(!fullSizeResolve.p) {
    // DXVK resolves same-size MSAA directly and can also scale it in one blit.
    const auto direct = identity->StretchRect(back,nullptr,surface[slot].p,nullptr,
        scaled?D3DTEXF_LINEAR:D3DTEXF_NONE);
    const bool unsupported = direct==D3DERR_INVALIDCALL||direct==D3DERR_NOTAVAILABLE||direct==E_NOTIMPL;
    if(!canStage||!unsupported)return direct;

    // Some drivers require a same-size resolve before scaling. Cache the RT
    // only after the direct operation reports that it is unsupported.
    const auto created = identity->CreateRenderTarget(desc.Width,desc.Height,desc.Format,
        D3DMULTISAMPLE_NONE,0,FALSE,fullSizeResolve.Put(),nullptr);
    if(FAILED(created)||!fullSizeResolve.p) {
     fullSizeResolve.Reset();
     return FAILED(created)?created:D3DERR_INVALIDCALL;
    }
    resolveSource = desc;
   }
   const auto resolved = identity->StretchRect(back,nullptr,fullSizeResolve.p,nullptr,D3DTEXF_NONE);
   if(FAILED(resolved))return resolved;
   return identity->StretchRect(fullSizeResolve.p,nullptr,surface[slot].p,nullptr,scaled?D3DTEXF_LINEAR:D3DTEXF_NONE);
  }
 };
 std::shared_ptr<Section> section;std::unique_ptr<Generation> active;
 DWORD host=0;std::uint64_t hostCreation=0,serial=0,copies=0,ready=0,busy=0,failures=0,panelCopiesPaced=0;
 unsigned request=0,width=0,height=0,nextSlot[2]{},rejectedOffer=0;IDirect3DDevice9* source=nullptr;LUID adapter{};
 bool supported=false,lost=false;DWORD busySince[2]{};HRESULT result=S_OK;const char* reason="shared9_disabled";
 bool Open(){if(section)return true;section=OpenSection(GetCurrentProcessId(),Born(GetCurrentProcess()),true);return bool(section);}
 void Describe(unsigned w,unsigned h){auto& s=*section->shared;InterlockedIncrement(&s.source.sequence);s.source.request=++request;s.source.width=w;s.source.height=h;s.source.adapter=adapter;MemoryBarrier();InterlockedIncrement(&s.source.sequence);width=w;height=h;}
 // Presentation thread only: DEFAULT-pool imports MUST leave before Reset.
 void Invalidate(){active.reset();if(section)Describe(0,0);source=nullptr;supported=false;lost=false;rejectedOffer=0;busySince[0]=busySince[1]=0;reason="shared9_source_reset";}
 bool Host(){auto& s=*section->shared;const DWORD pid=DWORD(Load(s.offer.pid));const auto created=s.offer.creation;
  if(!pid||!Load(s.offer.enabled)||GetTickCount()-DWORD(Load(s.offer.tick))>1000)return false;
  if(host!=pid||hostCreation!=created){GPUFrame::Handle process;process.p=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
   if(!process.p||Born(process.p)!=created||WaitForSingleObject(process.p,0)!=WAIT_TIMEOUT)return false;host=pid;hostCreation=created;}
  return true;
 }
 void Poll(){if(!active)return;auto& s=*section->shared;for(unsigned i=0;i<Slots;++i)if(active->pending[i]){
  const auto h=active->event[i]->GetData(nullptr,0,0);if(h==S_FALSE)continue;active->pending[i]=false;
  if(FAILED(h)){++failures;result=h;lost=true;reason="shared9_event_failed_waiting_reset";}
  else {
   auto& slot=s.slots[i];
   // A replacement host may have published a new generation while this
   // device was busy. Do not stamp or publish its slot.
   if(slot.generation==active->id){
    slot.readyCounter=FrameMetrics::Counter();
    if(Change(slot,active->id,Writing,Ready))++ready;
   }
  }
 }}
 // -1 CPU fallback,0 capture skipped,1 GPU copy accepted (readiness is separate).
 int Try(IDirect3DDevice9* device,IDirect3DSurface9* back,unsigned mask,const VRIPC::EyeMetadata& metadata,unsigned epoch,unsigned& selected){
  if(!section||!device||!back||mask<1||mask>3)return -1;auto& s=*section->shared;InterlockedExchange(&s.source.tick,LONG(GetTickCount()));Poll();
  const bool panel=mask==3&&(metadata.poseValid==VRIPC::kUiFrame||metadata.poseValid==VRIPC::kContextFrame);
  if(!panel&&active)active->panelCadence.Reset();
  if(source!=device){Invalidate();source=device;result=DXVKAdapter(device,adapter);supported=SUCCEEDED(result);if(!supported){reason="shared9_not_dxvk_or_luid_unavailable";return -1;}}
  if(!supported)return -1;
  if(lost)return 0;
  if(width!=metadata.width||height!=metadata.height){active.reset();Describe(metadata.width,metadata.height);}
  if(!Host()){reason="shared9_waiting_for_host";return -1;}
  if(unsigned(Load(s.offer.failedRequest))==request){reason="shared9_host_rejected_request";return -1;}
  Description offer;if(!ReadOffer(s,offer)||offer.request!=request||!offer.generation){reason="shared9_waiting_for_textures";return -1;}
  if(offer.width!=width||offer.height!=height||offer.format!=87||!SameAdapter(offer.adapter,adapter)){reason="shared9_offer_mismatch";return -1;}
  if(offer.generation==rejectedOffer)return -1;
  if(!active||active->id!=offer.generation){
   auto next=std::make_unique<Generation>();next->id=offer.generation;next->request=request;next->width=width;next->height=height;next->identity=device;
   for(unsigned i=0;i<Slots;++i){if(!offer.handles[i]||offer.handles[i]>0xffffffffull){result=E_INVALIDARG;break;}auto handle=reinterpret_cast<HANDLE>(std::uintptr_t(offer.handles[i]));
    result=device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,next->texture[i].Put(),&handle);
    if(SUCCEEDED(result))result=next->texture[i]->GetSurfaceLevel(0,next->surface[i].Put());if(SUCCEEDED(result))result=device->CreateQuery(D3DQUERYTYPE_EVENT,next->event[i].Put());if(FAILED(result))break;
   }
   Description checked;if(FAILED(result)||!ReadOffer(s,checked)||checked.generation!=offer.generation||checked.request!=request){++failures;rejectedOffer=offer.generation;reason="shared9_import_failed";return -1;}
   active=std::move(next);
  }
  if(unsigned(Load(s.source.accepted))!=active->id)InterlockedExchange(&s.source.accepted,LONG(active->id));
  std::uint64_t queued=0;
  if(panel){
   queued=FrameMetrics::Counter();
   if(active->panelCadence.Skip({metadata.width,metadata.height,epoch,metadata.poseValid},queued)){++panelCopiesPaced;return 0;}
  }
  const unsigned side=mask==2?1:0,start=side*PerEye,count=mask==3?2:PerEye;unsigned slot=Slots;
  for(const LONG wanted:{Free,Ready}){for(unsigned n=0;n<count;++n){const unsigned i=start+(nextSlot[side]+n)%count;if(active->pending[i])continue;
   if(Load(s.slots[i].state)==wanted&&Change(s.slots[i],active->id,wanted,Writing)){slot=i;break;}}if(slot!=Slots)break;}
  if(slot==Slots){++busy;reason="shared9_ring_busy";auto& since=busySince[side];const auto now=GetTickCount();if(!since)since=now;
   if(DWORD(now-since)>=2000){rejectedOffer=active->id;reason="shared9_stalled_ring_cpu_fallback";return -1;}return 0;}
  busySince[side]=0;
  if(!queued)queued=FrameMetrics::Counter();
  D3DSURFACE_DESC desc{};result=back->GetDesc(&desc);
  if(SUCCEEDED(result))result=active->Copy(back,desc,slot);
  const bool copyQueued=SUCCEEDED(result);
  if(copyQueued)result=active->event[slot]->Issue(D3DISSUE_END);
  if(FAILED(result)) {
   ++failures;
   // If the fence failed after the copy, the GPU may still be writing to this
   // texture. Leave it in Writing and reject this offer until its generation changes.
   if(!copyQueued)Change(s.slots[slot],active->id,Writing,Free);
   reason=copyQueued?"shared9_copy_event_failed":"shared9_copy_failed";
   if(result==D3DERR_DEVICELOST||result==D3DERR_DEVICENOTRESET) {
    lost=true;
    return 0;
   }
   rejectedOffer=active->id;
   return -1;
  }
  auto& token=s.slots[slot];token.serial=++serial;token.epoch=epoch;token.mask=mask;token.metadata=metadata;
  token.queuedCounter=queued;token.readyCounter=0;active->pending[slot]=true;
  if(panel){
   static const auto period=[] {
    constexpr auto rate=CaptureFramePolicy::PanelCopyCadence::RateHz;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    return (std::uint64_t(frequency.QuadPart)+rate-1)/rate;
   }();
   active->panelCadence.Copied(queued,period);
  }
  MemoryBarrier();nextSlot[side]=(slot-start+1)%count;selected=slot;++copies;reason="shared9_gpu_queued";return 1;
 }
};
}
