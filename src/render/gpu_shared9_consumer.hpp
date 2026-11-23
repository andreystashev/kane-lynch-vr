#pragma once
#include "gpu_shared9_ipc.hpp"
#include "gpu_frame_ipc.hpp"
#include "cpu_timer.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <algorithm>

namespace SharedD3D9 {
struct Timing {
 std::uint64_t queued=0,ready=0,claimed=0;
};
struct Consumer {
 struct Generation {
  std::shared_ptr<Section> section;GPUFrame::Object<ID3D11DeviceContext> context;
  unsigned id=0,request=0,width=0,height=0,leases=0;bool pending[Slots]{};
  GPUFrame::Object<ID3D11Texture2D> texture[Slots],image[4];GPUFrame::Object<ID3D11ShaderResourceView> srv[Slots];
  GPUFrame::Object<ID3D11RenderTargetView> rtv[4];GPUFrame::Object<ID3D11Query> done[Slots];
  void QueueReadCompletion(unsigned slot){
   context->End(done[slot].p);pending[slot]=true;
   Change(section->shared->slots[slot],id,Reading,ReadPending);
  }
  void FinishRead(unsigned slot){QueueReadCompletion(slot);context->Flush();}
  bool Poll(){bool complete=leases==0;for(unsigned i=0;i<Slots;++i)if(pending[i]){
   const auto h=context->GetData(done[i].p,nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);if(h==S_FALSE){complete=false;continue;}
   if(FAILED(h)){complete=false;continue;}pending[i]=false;Change(section->shared->slots[i],id,ReadPending,Free);
  }
  return complete;}
 };
 DWORD pid=0;std::uint64_t creation=0;GPUFrame::Handle process;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
 std::shared_ptr<Section> section;std::shared_ptr<Generation> active;std::vector<std::shared_ptr<Generation>> retired;
 LUID adapter{};unsigned generation=0,rejected=0;DWORD lastConnect=0;bool enabled=false;const char* reason="shared9_disabled";HRESULT result=S_OK;
 std::uint64_t copies=0,bytesAvoided=0,pairs=0,panels=0,olderPanels=0;
 Timing lastPairTiming[2]{};
 ~Consumer(){if(section&&Load(section->shared->offer.pid)==LONG(GetCurrentProcessId())){InterlockedExchange(&section->shared->offer.enabled,0);InterlockedCompareExchange(&section->shared->offer.pid,0,GetCurrentProcessId());}}
 bool Open(DWORD gamePid,ID3D11Device* d,ID3D11DeviceContext* c,bool use){
  enabled=use;pid=gamePid;device=d;context=c;if(!enabled)return false;
  process.p=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process.p)return false;creation=Born(process.p);
  GPUFrame::Object<IDXGIDevice> dxgi;GPUFrame::Object<IDXGIAdapter> gpu;DXGI_ADAPTER_DESC desc{};
  result=d->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.Put()));if(SUCCEEDED(result))result=dxgi->GetAdapter(gpu.Put());if(SUCCEEDED(result))result=gpu->GetDesc(&desc);adapter=desc.AdapterLuid;
  enabled=SUCCEEDED(result);Tick();return enabled;
 }
 bool Connect(){if(section)return true;const auto now=GetTickCount();if(lastConnect&&DWORD(now-lastConnect)<250)return false;lastConnect=now;section=OpenSection(pid,creation,false);if(!section)return false;
  auto& o=section->shared->offer;const LONG own=GetCurrentProcessId(),old=Load(o.pid);
  if(old&&old!=own){GPUFrame::Handle previous;previous.p=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(old));
   if(previous.p&&Born(previous.p)==o.creation&&WaitForSingleObject(previous.p,0)==WAIT_TIMEOUT){section.reset();return false;}}
  if(InterlockedCompareExchange(&o.pid,own,old)!=old){section.reset();return false;}
  Description previous;if(ReadOffer(*section->shared,previous))generation=std::max(generation,previous.generation);
  generation=std::max(generation,unsigned(Load(section->shared->source.accepted)));
  InterlockedExchange(&o.enabled,0);o.creation=Born(GetCurrentProcess());return true;
 }
 bool Active()const{return active&&section&&section->shared->offer.generation==active->id&&unsigned(Load(section->shared->source.accepted))==active->id&&
     unsigned(Load(section->shared->offer.failedRequest))!=active->request;}
 void Reject(unsigned request,const char* why){rejected=request;reason=why;if(section)InterlockedExchange(&section->shared->offer.failedRequest,LONG(request));}
 bool Tick(){
  if(!enabled||!process.p||WaitForSingleObject(process.p,0)!=WAIT_TIMEOUT||!Connect())return false;
  auto& s=*section->shared;if(Load(s.offer.pid)!=LONG(GetCurrentProcessId()))return false;
  InterlockedExchange(&s.offer.tick,LONG(GetTickCount()));InterlockedExchange(&s.offer.enabled,1);
  if(active)active->Poll();for(auto it=retired.begin();it!=retired.end();){if((*it)->Poll())it=retired.erase(it);else ++it;}
  Request request;if(!ReadRequest(s,request))return false;
  if(active&&active->request==request.id){reason="shared9_gpu_active";return Active();}
  if(active){retired.push_back(std::move(active));InterlockedIncrement(&s.offer.sequence);s.offer.generation=0;MemoryBarrier();InterlockedIncrement(&s.offer.sequence);}
  if(!request.id||!ValidSize(request.width,request.height)){reason="shared9_waiting_for_source";return false;}
  if(rejected==request.id)return false;
  if(!SameAdapter(request.adapter,adapter)){Reject(request.id,"shared9_adapter_mismatch");return false;}
  if(retired.size()>=3){Reject(request.id,"shared9_retirement_limit");return false;}
  auto next=std::make_shared<Generation>();next->section=section;next->id=++generation;next->request=request.id;next->width=request.width;next->height=request.height;next->context.p=context;context->AddRef();
  D3D11_TEXTURE2D_DESC desc{};desc.Width=request.width;desc.Height=request.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
  D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};std::uint64_t handles[Slots]{};result=S_OK;
  for(unsigned i=0;i<Slots&&SUCCEEDED(result);++i){result=device->CreateTexture2D(&desc,nullptr,next->texture[i].Put());GPUFrame::Object<IDXGIResource> resource;HANDLE handle=nullptr;
   if(SUCCEEDED(result))result=next->texture[i]->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(resource.Put()));if(SUCCEEDED(result))result=resource->GetSharedHandle(&handle);handles[i]=reinterpret_cast<std::uintptr_t>(handle);
   if(SUCCEEDED(result))result=device->CreateShaderResourceView(next->texture[i].p,nullptr,next->srv[i].Put());if(SUCCEEDED(result))result=device->CreateQuery(&query,next->done[i].Put());
  }
  auto local=desc;local.MiscFlags=0;for(unsigned i=0;i<4&&SUCCEEDED(result);++i){result=device->CreateTexture2D(&local,nullptr,next->image[i].Put());if(SUCCEEDED(result))result=device->CreateRenderTargetView(next->image[i].p,nullptr,next->rtv[i].Put());}
  Request checked;if(FAILED(result)||!ReadRequest(s,checked)||checked.id!=request.id){Reject(request.id,"shared9_host_create_failed");return false;}
  InterlockedIncrement(&s.offer.sequence);s.offer.request=request.id;s.offer.generation=next->id;s.offer.width=request.width;s.offer.height=request.height;s.offer.format=87;s.offer.adapter=adapter;std::memcpy(s.offer.handles,handles,sizeof(handles));
  for(auto& slot:s.slots){InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&slot),LONG64(std::uint64_t(next->id)<<32));}
  InterlockedExchange(&s.offer.failedRequest,0);active=std::move(next);MemoryBarrier();InterlockedIncrement(&s.offer.sequence);reason="shared9_textures_offered";return true;
 }
 bool Claim(unsigned slot,unsigned epoch,const VRIPC::EyeMetadata& expected,unsigned eye){
  if(!Active()||slot>=Slots||eye>1)return false;auto& s=section->shared->slots[slot];if(!Change(s,active->id,Ready,Reading))return false;
  if(s.epoch!=epoch||!(s.mask&(1u<<eye))||std::memcmp(&s.metadata,&expected,sizeof(expected))||expected.width!=active->width||expected.height!=active->height){Change(s,active->id,Reading,Ready);return false;}return true;
 }
 void Candidates(GPUFrame::PairCandidate (&out)[Slots],unsigned epoch){for(unsigned i=0;i<Slots;++i){auto& s=section->shared->slots[i];if(Load(s.state)!=Ready||s.generation!=active->id||s.epoch!=epoch)continue;
  const auto serial=s.serial;VRIPC::EyeMetadata m=s.metadata;const auto mask=s.mask;MemoryBarrier();if(Load(s.state)==Ready&&s.generation==active->id&&s.serial==serial&&(mask&(1u<<(i/PerEye))))out[i]={true,epoch,m};
 }}
 template<class Copy> bool CopyClaimed(unsigned eye,unsigned slot,const VRIPC::EyeMetadata& m,ID3D11Texture2D*& out,ID3D11Texture2D* preserve,Copy&& copy){
  const unsigned target=eye*2+(active->image[eye*2].p==preserve?1u:0u);
  copy(context,active->srv[slot].p,active->rtv[target].p,m.width,m.height);
  active->FinishRead(slot);
  if(FAILED(device->GetDeviceRemovedReason())){Reject(active->request,"shared9_host_device_removed");return false;}
  out=active->image[target].p;++copies;bytesAvoided+=std::uint64_t(m.stride)*m.height;reason="shared9_gpu_active";return true;
 }
 template<class Copy> bool Read(unsigned eye,unsigned slot,unsigned epoch,const VRIPC::EyeMetadata& m,ID3D11Texture2D*& out,ID3D11Texture2D* preserve,Copy&& copy){out=nullptr;if(!Tick()||!Claim(slot,epoch,m,eye))return false;return CopyClaimed(eye,slot,m,out,preserve,copy);}
 template<class Read> bool ClaimPair(std::uint64_t after,DWORD minimum,unsigned epoch,VRIPC::EyeMetadata (&metadata)[2],ID3D11Texture2D* (&out)[2],Read&& read){
  if(!Tick())return false;GPUFrame::PairCandidate c[Slots]{};Candidates(c,epoch);unsigned selected[2]{};if(!GPUFrame::LatestScenePair(c,after,minimum,selected))return false;
  if(!SamePose(c[selected[0]].metadata,c[selected[1]].metadata))return false;
  if(!Claim(selected[0],epoch,c[selected[0]].metadata,0))return false;
  if(!Claim(selected[1],epoch,c[selected[1]].metadata,1)){Change(section->shared->slots[selected[0]],active->id,Reading,Ready);return false;}
  Timing timing[2]{};
  const auto claimed=FrameMetrics::Counter();
  for(unsigned eye=0;eye<2;++eye){
   const auto& slot=section->shared->slots[selected[eye]];
   timing[eye]={slot.queuedCounter,slot.readyCounter,claimed};
  }
  ID3D11Texture2D* images[2]{};
  if(!read(selected,c,images)){
   // A submitted GPU read is ReadPending and will retire through its query.
   for(unsigned slot:selected)Change(section->shared->slots[slot],active->id,Reading,Ready);
   return false;
  }
  for(unsigned eye=0;eye<2;++eye){
   metadata[eye]=c[selected[eye]].metadata;
   out[eye]=images[eye];
   lastPairTiming[eye]=timing[eye];
  }
  ++pairs;
  return true;
 }
 template<class Copy> bool Pair(std::uint64_t after,DWORD minimum,unsigned epoch,VRIPC::EyeMetadata (&metadata)[2],ID3D11Texture2D* (&out)[2],ID3D11Texture2D* const (&preserve)[2],Copy&& copy){
  return ClaimPair(after,minimum,epoch,metadata,out,[&](const auto& slots,const auto& candidates,auto& images){
   for(unsigned eye=0;eye<2;++eye)if(!CopyClaimed(eye,slots[eye],candidates[slots[eye]].metadata,images[eye],preserve[eye],copy))return false;
   return true;
  });
 }
 template<class Copy> bool Panel(std::uint64_t after,DWORD minimum,unsigned epoch,const VRIPC::EyeMetadata& announced,VRIPC::EyeMetadata& metadata,ID3D11Texture2D*& out,ID3D11Texture2D* preserve,unsigned* chosen,Copy&& copy){
  if(!Tick())return false;GPUFrame::PairCandidate c[Slots]{};Candidates(c,epoch);GPUFrame::PairCandidate mono[GPUFrame::SlotCount]{};mono[0]=c[0];mono[1]=c[1];unsigned selected=0;if(!GPUFrame::LatestPanel(mono,after,minimum,epoch,announced,selected)||!Claim(selected,epoch,c[selected].metadata,0))return false;
  if(!CopyClaimed(0,selected,c[selected].metadata,out,preserve,copy))return false;metadata=c[selected].metadata;if(chosen)*chosen=selected;++panels;if(metadata.frameId<announced.frameId)++olderPanels;return true;
 }
};
}
