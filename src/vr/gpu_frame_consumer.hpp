#pragma once
#include "gpu_frame_ipc.hpp"
#include "gpu_frame_objects.hpp"
#include "gpu_read_lifetime.hpp"
#include "gpu_shared9_consumer.hpp"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <string>
#include <memory>

namespace GPUFrame {
// The copy shader preserves BGRA bytes and makes alpha opaque, matching CPU
// publication. It uses Load at integer pixel coordinates: no scaling/filtering.
struct OpaqueCopy {
  Object<ID3D11VertexShader> vs;
  Object<ID3D11PixelShader> ps;
  bool Create(ID3D11Device *device) {
    if (vs.p && ps.p)
      return true;
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH))
      return false;
    const auto path = std::wstring(system) + L"\\d3dcompiler_47.dll";
    HMODULE dll =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll)
      return false;
    auto compile = reinterpret_cast<decltype(&D3DCompile)>(
        GetProcAddress(dll, "D3DCompile"));
    constexpr char text[] =
        "Texture2D<float4> image:register(t0);float4 VS(uint "
        "id:SV_VertexID):SV_POSITION{float2 p=float2((id<<1)&2,id&2);return "
        "float4(p*float2(2,-2)+float2(-1,1),0,1);}float4 PS(float4 "
        "p:SV_POSITION):SV_Target{return "
        "float4(image.Load(int3(int2(p.xy),0)).rgb,1);}";
    Object<ID3DBlob> vertex, pixel, errors;
    HRESULT hr =
        compile ? compile(text, sizeof(text) - 1, nullptr, nullptr, nullptr,
                          "VS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                          vertex.Put(), errors.Put())
                : E_FAIL;
    if (SUCCEEDED(hr))
      hr = compile(text, sizeof(text) - 1, nullptr, nullptr, nullptr, "PS",
                   "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, pixel.Put(),
                   errors.Put());
    if (SUCCEEDED(hr))
      hr = device->CreateVertexShader(vertex->GetBufferPointer(),
                                      vertex->GetBufferSize(), nullptr,
                                      vs.Put());
    if (SUCCEEDED(hr))
      hr = device->CreatePixelShader(pixel->GetBufferPointer(),
                                     pixel->GetBufferSize(), nullptr, ps.Put());
    // Release compiler-owned blobs before unloading their implementation.
    errors.Reset();
    pixel.Reset();
    vertex.Reset();
    FreeLibrary(dll);
    return SUCCEEDED(hr);
  }
  void Draw(ID3D11DeviceContext *context, ID3D11ShaderResourceView *source,
            ID3D11RenderTargetView *target, unsigned width, unsigned height) {
    D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.p, nullptr, 0);
    context->PSSetShader(ps.p, nullptr, 0);
    context->PSSetShaderResources(0, 1, &source);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->Draw(3, 0);
    ID3D11ShaderResourceView *empty = nullptr;
    context->PSSetShaderResources(0, 1, &empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
  }
};
struct Section {
  Handle mapping;
  Shared *shared = nullptr;
  ~Section() {
    if (shared)
      UnmapViewOfFile(shared);
  }
};
// Keep the shared slot claimed until the last GPU read has been queued. A
// lease owns its handles/context/view independently of Consumer generations.
struct ImageLease {
  std::shared_ptr<Section> section;
  std::shared_ptr<SharedD3D9::Consumer::Generation> shared9;
  std::shared_ptr<OpaqueCopy> opaque;
  Object<ID3D11Texture2D> texture;
  Object<ID3D11ShaderResourceView> srv;
  Object<ID3D11Fence> done;
  Object<ID3D11DeviceContext4> context;
  unsigned slotIndex = 0, generation = 0;
  std::uint64_t value = 0;
  GPUReadLifetime::Completion completion;
  bool released = false;
  bool shared9NeedsFlush = false;
  // State+generation occupy the first aligned eight bytes. Compare both in
  // one operation: releasing an old lease must never free a new generation's
  // Reading slot, even when Reset races this destructor.
  static bool ChangeSlot(Slot &slot, unsigned generation, LONG from, LONG to) {
    static_assert(offsetof(Slot, state) == 0 && offsetof(Slot, generation) == 4,
                  "atomic slot identity");
    const auto identity = std::uint64_t(generation) << 32;
    const auto expected = LONG64(identity | std::uint32_t(from));
    return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64 *>(&slot),
                                        LONG64(identity | std::uint32_t(to)),
                                        expected) == expected;
  }
  static void ReleaseSlot(Slot &slot, unsigned generation, LONG state) {
    ChangeSlot(slot, generation, Reading, state);
  }
  void QueueFinishRead() {
    if(shared9){
      if(released)return;
      released=true;
      if(completion.used){shared9->QueueReadCompletion(slotIndex);shared9NeedsFlush=true;}
      else SharedD3D9::Change(shared9->section->shared->slots[slotIndex],shared9->id,SharedD3D9::Reading,SharedD3D9::Ready);
      --shared9->leases;
      return;
    }
    if (!completion.Open()) return;
    if (!completion.used) {completion.FinishUnused();return;}
    const HRESULT hr = context->Signal(done.p, value);
    completion.Queue(SUCCEEDED(hr));
    if (FAILED(hr)) {
      auto &s = *section->shared;
      if (s.generation == generation &&
          Atomic(s.consumerPid) == LONG(GetCurrentProcessId()))
        InterlockedExchange(&s.cpuRequested, 1);
    }
  }
  bool NeedsReadFlush()const{return shared9?shared9NeedsFlush:completion.NeedsFlush();}
  ID3D11DeviceContext* ReadContextIdentity()const{return shared9?shared9->context.p:context.p;}
  void FlushReadContext(){ReadContextIdentity()->Flush();}
  void CompleteFinishRead() {
    if(shared9){shared9NeedsFlush=false;return;}
    completion.Flushed();
    if (released || !completion.Reusable()) return;
    released = true;
    ReleaseSlot(section->shared->slots[slotIndex], generation,
                completion.used ? Free : Ready);
  }
  static void BatchRelease(ImageLease* const* leases,std::size_t count) {
    GPUReadLifetime::FinishBatch(leases,count);
  }
  void FinishRead(){ImageLease* lease=this;BatchRelease(&lease,1);}
  ~ImageLease(){FinishRead();}
  void Draw(ID3D11RenderTargetView *target, unsigned width, unsigned height) {
    completion.used = true;
    if(shared9)opaque->Draw(shared9->context.p,shared9->srv[slotIndex].p,target,width,height);
    else opaque->Draw(context.p,srv.p,target,width,height);
  }
};
struct UploadTarget {
  Object<ID3D11RenderTargetView> rtv;
  Object<ID3D11Texture2D> intermediate;
  bool Create(ID3D11Device *device, ID3D11Texture2D *target) {
    D3D11_TEXTURE2D_DESC desc{};
    target->GetDesc(&desc);
    if (desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))
      return false;
    // Source RGB already contains the game's display-encoded bytes. An UNORM
    // view prevents a second sRGB encode when the runtime uses an sRGB chain.
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (SUCCEEDED(device->CreateRenderTargetView(target, &view, rtv.Put())))
      return true;
    // Retain the two-pass path if a runtime disallows the required view.
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = desc.MiscFlags = 0;
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, intermediate.Put())) &&
           SUCCEEDED(device->CreateRenderTargetView(intermediate.p, &view, rtv.Put()));
  }
};
struct Consumer {
  SharedD3D9::Consumer shared9;
  void RefreshNativeCounters(){
    copies=shared9.copies;bytesAvoided=shared9.bytesAvoided;scenePairsRead=shared9.pairs;
    panelFramesRead=shared9.panels;olderPanelFramesRead=shared9.olderPanels;reason=shared9.reason;
  }
  bool NativeTick(){if(!shared9.Tick()||!shared9.Active())return false;RefreshNativeCounters();return true;}
  auto NativeCopy(){return [this](ID3D11DeviceContext* c,ID3D11ShaderResourceView* s,ID3D11RenderTargetView* t,unsigned w,unsigned h){opaque->Draw(c,s,t,w,h);};}
  Handle process;
  std::shared_ptr<Section> section;
  Shared *shared = nullptr;
  DWORD pid = 0;
  Object<ID3D11Device1> device1;
  Object<ID3D11Device5> device5;
  Object<ID3D11DeviceContext4> context4;
  Object<ID3D11Fence> ready, done[SlotCount];
  Object<ID3D11Texture2D> texture[SlotCount], image[4];
  Object<ID3D11ShaderResourceView> srv[SlotCount];
  Object<ID3D11RenderTargetView> rtv[4];
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  LUID adapter{};
  std::shared_ptr<OpaqueCopy> opaque = std::make_shared<OpaqueCopy>();
  std::uint32_t generation = 0, rejected = 0;
  bool capable = false, direct = false;
  const char *reason = "disabled";
  std::uint64_t copies = 0, bytesAvoided = 0, rejections = 0, directReads = 0, scenePairsRead = 0;
  std::uint64_t panelFramesRead = 0, olderPanelFramesRead = 0;
  ~Consumer() {
    if (shared && Atomic(shared->consumerPid) == LONG(GetCurrentProcessId())) {
      InterlockedExchange(&shared->consumerEnabled, 0);
      InterlockedCompareExchange(&shared->consumerPid, 0,
                                 GetCurrentProcessId());
    }
  }
  bool Open(DWORD gamePid, ID3D11Device *d, ID3D11DeviceContext *c,
            bool enabled, bool directUpload = false,bool nativeShared=false) {
    const bool native=enabled&&nativeShared&&opaque->Create(d)&&shared9.Open(gamePid,d,c,true);
    return OpenLegacy(gamePid,d,c,enabled,directUpload)||native;
  }
  bool OpenLegacy(DWORD gamePid,ID3D11Device* d,ID3D11DeviceContext* c,bool enabled,bool directUpload){
    direct = directUpload;
    if (!enabled) {
      reason = "disabled";
      return false;
    }
    device = d;
    context = c;
    pid = gamePid;
    wchar_t name[100];
    Name(name, 100, pid);
    section = std::make_shared<Section>();
    section->mapping.p = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!section->mapping.p) {
      reason = "gpu_source_not_enabled";
      return false;
    }
    shared = static_cast<Shared *>(
        MapViewOfFile(section->mapping.p, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    section->shared = shared;
    if (!Compatible(shared, pid)) {
      reason = "gpu_protocol_mismatch";
      return false;
    }
    const LONG own = LONG(GetCurrentProcessId()),
               previous = Atomic(shared->consumerPid);
    if (previous && previous != own) {
      Handle prior;
      prior.p = OpenProcess(SYNCHRONIZE, FALSE, DWORD(previous));
      const bool dead = prior.p
                            ? WaitForSingleObject(prior.p, 0) == WAIT_OBJECT_0
                            : GetLastError() == ERROR_INVALID_PARAMETER;
      if (!dead) {
        reason = "gpu_consumer_already_owned";
        return false;
      }
    }
    if (InterlockedCompareExchange(&shared->consumerPid, own, previous) !=
        previous) {
      reason = "gpu_consumer_claim_failed";
      return false;
    }
    process.p = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, pid);
    if (!process.p) {
      reason = "gpu_source_process_unavailable";
      return false;
    }
    Object<IDXGIDevice> dxgi;
    Object<IDXGIAdapter> gpu;
    DXGI_ADAPTER_DESC desc{};
    HRESULT hr = d->QueryInterface(__uuidof(IDXGIDevice),
                                   reinterpret_cast<void **>(dxgi.Put()));
    if (SUCCEEDED(hr))
      hr = dxgi->GetAdapter(gpu.Put());
    if (SUCCEEDED(hr))
      hr = gpu->GetDesc(&desc);
    adapter = desc.AdapterLuid;
    if (SUCCEEDED(hr))
      hr = d->QueryInterface(__uuidof(ID3D11Device1),
                             reinterpret_cast<void **>(device1.Put()));
    if (SUCCEEDED(hr))
      hr = d->QueryInterface(__uuidof(ID3D11Device5),
                             reinterpret_cast<void **>(device5.Put()));
    if (SUCCEEDED(hr))
      hr = c->QueryInterface(__uuidof(ID3D11DeviceContext4),
                             reinterpret_cast<void **>(context4.Put()));
    capable = SUCCEEDED(hr) && opaque->Create(d);
    reason = capable ? "waiting_for_gpu_source"
                     : "gpu_fence_or_copy_shader_unavailable";
    Tick();
    return capable;
  }
  void RequestCPU(const char *why) {
    reason = why;
    ++rejections;
    if (shared)
      InterlockedExchange(&shared->cpuRequested, 1);
  }
  bool Duplicate(std::uint64_t remote, Handle &local) {
    return remote &&
           DuplicateHandle(
               process.p, reinterpret_cast<HANDLE>(std::uintptr_t(remote)),
               GetCurrentProcess(), &local.p, 0, FALSE, DUPLICATE_SAME_ACCESS);
  }
  void ClearImages() {
    ready.Reset();
    for (unsigned i = 0; i < SlotCount; ++i) {
      srv[i].Reset();
      texture[i].Reset();
      done[i].Reset();
      rtv[i].Reset();
      image[i].Reset();
    }
    generation = 0;
  }
  bool Tick() {return NativeTick()||LegacyTick();}
  bool LegacyTick() {
    if (!shared || Atomic(shared->consumerPid) != LONG(GetCurrentProcessId()))
      return false;
    InterlockedExchange(&shared->consumerTick, GetTickCount());
    Header h{};
    if (!ReadHeader(*shared, h)) {
      // Before the first generation the header seqlock is still zero. Keep
      // capability advertised so producer creation can start; a torn header
      // must neither clear resources nor forget an existing rejection.
      InterlockedExchange(&shared->consumerEnabled, capable && !rejected);
      return false;
    }
    if (!h.generation) {
      // A stable zero generation explicitly retires the old source at Reset.
      // Drop our duplicate handles/views before the x86 producer recreates its
      // working set. Outstanding ImageLeases retain their own GPU ownership.
      if (generation) ClearImages();
      if (rejected) {
        rejected = 0;
        InterlockedExchange(&shared->cpuRequested, 0);
      }
      InterlockedExchange(&shared->consumerEnabled, capable);
      return false;
    }
    InterlockedExchange(&shared->consumerEnabled,
                        capable && rejected != h.generation);
    if (!capable || rejected == h.generation)
      return false;
    if (generation == h.generation)
      return true;
    ClearImages();
    if (!ValidHeader(h) || !SameAdapter(h.adapter, adapter)) {
      rejected = h.generation;
      RequestCPU("gpu_adapter_or_description_mismatch");
      return false;
    }
    Handle handle;
    HRESULT hr =
        Duplicate(h.readyHandle, handle)
            ? device5->OpenSharedFence(handle.p, __uuidof(ID3D11Fence),
                                       reinterpret_cast<void **>(ready.Put()))
            : E_FAIL;
    for (unsigned i = 0; i < SlotCount && SUCCEEDED(hr); ++i) {
      handle.Reset();
      if (!Duplicate(h.textureHandle[i], handle)) {
        hr = E_FAIL;
        break;
      }
      hr = device1->OpenSharedResource1(
          handle.p, __uuidof(ID3D11Texture2D),
          reinterpret_cast<void **>(texture[i].Put()));
      D3D11_TEXTURE2D_DESC desc{};
      if (SUCCEEDED(hr)) {
        texture[i]->GetDesc(&desc);
        if (desc.Width != h.width || desc.Height != h.height ||
            desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.MipLevels != 1 ||
            desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
          hr = E_INVALIDARG;
      }
      if (SUCCEEDED(hr))
        hr = device->CreateShaderResourceView(texture[i].p, nullptr,
                                              srv[i].Put());
      handle.Reset();
      if (SUCCEEDED(hr) && !Duplicate(h.doneHandle[i], handle))
        hr = E_FAIL;
      if (SUCCEEDED(hr))
        hr = device5->OpenSharedFence(handle.p, __uuidof(ID3D11Fence),
                                      reinterpret_cast<void **>(done[i].Put()));
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = h.width;
    desc.Height = h.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    for (unsigned i = 0; !direct && i < 4 && SUCCEEDED(hr); ++i) {
      hr = device->CreateTexture2D(&desc, nullptr, image[i].Put());
      if (SUCCEEDED(hr))
        hr = device->CreateRenderTargetView(image[i].p, nullptr, rtv[i].Put());
    }
    Header verified{};
    if (FAILED(hr) || !ReadHeader(*shared, verified) ||
        verified.generation != h.generation) {
      ClearImages();
      rejected = h.generation;
      RequestCPU("gpu_resource_open_failed");
      return false;
    }
    generation = h.generation;
    InterlockedExchange(&shared->cpuRequested, 0);
    MemoryBarrier();
    InterlockedExchange(&shared->acceptedGeneration, generation);
    reason = "gpu_resources_ready";
    return true;
  }
  // Snapshot four metadata records, select the newest COMPLETE pair, then
  // claim both through the normal fence/epoch/metadata checks. No GPU waits,
  // new copies or ABI changes. A failed second claim returns the unused
  // direct lease to Ready and never exposes half a new pair to the host.
  bool ReadLatestScenePair(std::uint64_t afterFrame,DWORD minimumTick,unsigned epoch,
                          VRIPC::EyeMetadata (&metadata)[2],ID3D11Texture2D* (&out)[2],
                          std::shared_ptr<ImageLease> (&leases)[2],ID3D11Texture2D* const (&preserve)[2]){
    if(NativeTick()){
      leases[0].reset();leases[1].reset();
      bool allocationFailed=false;
      const auto borrow=[&](const auto& slots,const auto& candidates,auto& images){
        // Both allocations precede adoption of either slot. ClaimPair returns
        // both unsubmitted slots to Ready if allocation fails.
        std::shared_ptr<ImageLease> held[2];
        try{for(auto& lease:held)lease=std::make_shared<ImageLease>();}
        catch(const std::bad_alloc&){allocationFailed=true;return false;}
        for(unsigned eye=0;eye<2;++eye){
          auto& lease=*held[eye];lease.shared9=shared9.active;lease.opaque=opaque;lease.slotIndex=slots[eye];
          ++lease.shared9->leases;images[eye]=lease.shared9->texture[slots[eye]].p;
          leases[eye]=std::move(held[eye]);++shared9.copies;++directReads;
          const auto& m=candidates[slots[eye]].metadata;shared9.bytesAvoided+=std::uint64_t(m.stride)*m.height;
        }
        return true;
      };
      const bool useSharedImages=direct;
      bool okay=useSharedImages&&shared9.ClaimPair(afterFrame,minimumTick,epoch,metadata,out,borrow);
      if(!useSharedImages||allocationFailed)okay=shared9.Pair(afterFrame,minimumTick,epoch,metadata,out,preserve,NativeCopy());
      RefreshNativeCounters();return okay;
    }
    if(!LegacyTick())return false;
    PairCandidate candidates[SlotCount]{};
    const auto ownGeneration=generation;const auto completed=ready->GetCompletedValue();
    if(completed==UINT64_MAX)return false;
    for(unsigned i=0;i<SlotCount;++i){const auto& s=shared->slots[i];
      if(Atomic(const_cast<volatile LONG&>(s.state))!=Ready)continue;
      const auto value=s.value;const auto gen=s.generation,slotEpoch=s.transportEpoch,mask=s.eyeMask;
      VRIPC::EyeMetadata m{};std::memcpy(&m,&s.metadata,sizeof(m));MemoryBarrier();
      if(Atomic(const_cast<volatile LONG&>(s.state))!=Ready||s.value!=value||s.generation!=gen||
         gen!=ownGeneration||slotEpoch!=epoch||completed<value||!(mask&(1u<<(i/2))))continue;
      candidates[i]={true,slotEpoch,m};
    }
    unsigned selected[2]{};if(!LatestScenePair(candidates,afterFrame,minimumTick,selected))return false;
    ID3D11Texture2D* images[2]{};std::shared_ptr<ImageLease> held[2];
    for(unsigned side=0;side<2;++side)if(!Read(side,selected[side],epoch,candidates[selected[side]].metadata,
        images[side],preserve[side],&held[side]))return false;
    if(generation!=ownGeneration)return false;
    for(unsigned side=0;side<2;++side){metadata[side]=candidates[selected[side]].metadata;out[side]=images[side];leases[side]=std::move(held[side]);}
    ++scenePairsRead;return true;
  }
  // The newest announced mono UI/scope token may still be waiting on its ready fence.
  // Recover the newest COMPLETED same-kind image from the left ring instead of chasing
  // that unfinished token while the producer replaces the other ready slot.
  bool ReadLatestPanel(std::uint64_t afterFrame,DWORD minimumTick,unsigned epoch,
                       const VRIPC::EyeMetadata& announced,VRIPC::EyeMetadata& metadata,
                       ID3D11Texture2D*& output,std::shared_ptr<ImageLease>& lease,
                       ID3D11Texture2D* preserve=nullptr,unsigned* selectedSlot=nullptr){
    output=nullptr;lease.reset();
    if(NativeTick()){
      const bool okay=shared9.Panel(afterFrame,minimumTick,epoch,announced,metadata,output,preserve,selectedSlot,NativeCopy());
      RefreshNativeCounters();return okay;
    }
    if(!LegacyTick()||!Compatible(shared,pid))return false;
    const auto* ownShared=shared;const auto ownGeneration=generation;const auto ownProducer=pid;
    const auto completed=ready->GetCompletedValue();if(completed==UINT64_MAX)return false;
    PairCandidate candidates[SlotCount]{};
    for(unsigned i=0;i<2;++i){const auto& slot=shared->slots[i];
      if(Atomic(const_cast<volatile LONG&>(slot.state))!=Ready)continue;
      const auto value=slot.value;const auto gen=slot.generation,slotEpoch=slot.transportEpoch,mask=slot.eyeMask;
      VRIPC::EyeMetadata panel{};std::memcpy(&panel,&slot.metadata,sizeof(panel));MemoryBarrier();
      if(Atomic(const_cast<volatile LONG&>(slot.state))!=Ready||slot.value!=value||slot.generation!=gen||
         gen!=ownGeneration||slotEpoch!=epoch||mask!=3||completed<value)continue;
      candidates[i]={true,slotEpoch,panel};
    }
    unsigned selected=0;if(!LatestPanel(candidates,afterFrame,minimumTick,epoch,announced,selected))return false;
    ID3D11Texture2D* image=nullptr;std::shared_ptr<ImageLease> held;
    if(!Read(0,selected,epoch,candidates[selected].metadata,image,preserve,&held))return false;
    if(shared!=ownShared||pid!=ownProducer||!Compatible(shared,ownProducer)||generation!=ownGeneration||
       shared->generation!=ownGeneration||Atomic(shared->consumerPid)!=LONG(GetCurrentProcessId()))return false;
    metadata=candidates[selected].metadata;output=image;lease=std::move(held);
    ++panelFramesRead;if(metadata.frameId<announced.frameId)++olderPanelFramesRead;
    if(selectedSlot)*selectedSlot=selected;return true;
  }
  bool Read(unsigned eye, unsigned slotIndex, unsigned epoch,
            const VRIPC::EyeMetadata &expected, ID3D11Texture2D *&output,
            ID3D11Texture2D *preserve = nullptr,
            std::shared_ptr<ImageLease> *lease = nullptr) {
    output = nullptr;
    if (lease)
      lease->reset();
    if(NativeTick()){
      const bool okay=shared9.Read(eye,slotIndex,epoch,expected,output,preserve,NativeCopy());
      RefreshNativeCounters();return okay;
    }
    if (direct && !lease)
      return false;
    if (eye > 1 || slotIndex >= SlotCount || !LegacyTick())
      return false;
    auto &slot = shared->slots[slotIndex];
    if (!ImageLease::ChangeSlot(slot, generation, Ready, Reading))
      return false;
    const auto ownGeneration = generation;
    const auto release = [&](LONG state) {
      ImageLease::ReleaseSlot(slot, ownGeneration, state);
    };
    if (slot.generation != generation || slot.transportEpoch != epoch ||
        !(slot.eyeMask & (1u << eye)) ||
        std::memcmp(&slot.metadata, &expected, sizeof(expected))) {
      release(Ready);
      return false;
    }
    const auto value = slot.value;
    const auto complete = ready->GetCompletedValue();
    if (complete == UINT64_MAX) {
      release(Free);
      RequestCPU("gpu_source_device_removed");
      return false;
    }
    if (complete < value) {
      release(Ready);
      return false;
    }
    // A CPU-completed fence is not a GPU-side external-memory acquire.
    // Queue the shared ready fence on the D3D11 context before either copy
    // or direct use, including when Vulkan produced the image. This does
    // not block the CPU: the completion test above already passed.
    if(FAILED(context4->Wait(ready.p,value))){
      release(Ready);RequestCPU("gpu_consumer_ready_wait_failed");return false;
    }
    if (direct) {
      std::shared_ptr<ImageLease> held;
      try { held = std::make_shared<ImageLease>(); }
      catch (...) {
        release(Ready);
        RequestCPU("gpu_lease_allocation_failed");
        return false;
      }
      held->section = section;
      held->opaque = opaque;
      held->slotIndex = slotIndex;
      held->generation = generation;
      held->value = value;
      held->texture.p = texture[slotIndex].p; held->texture->AddRef();
      held->srv.p = srv[slotIndex].p; held->srv->AddRef();
      held->done.p = done[slotIndex].p; held->done->AddRef();
      held->context.p = context4.p; held->context->AddRef();
      output = held->texture.p;
      *lease = std::move(held);
      ++directReads;
      ++copies;
      bytesAvoided += std::uint64_t(expected.stride) * expected.height;
      reason = "gpu_direct_ready";
      return true;
    }
    const auto target = eye * 2 + (image[eye * 2].p == preserve ? 1 : 0);
    opaque->Draw(context, srv[slotIndex].p, rtv[target].p, expected.width,
                expected.height);
    HRESULT hr = context4->Signal(done[slotIndex].p, value);
    context->Flush();
    if (FAILED(hr) || FAILED(device->GetDeviceRemovedReason())) {
      release(Free);
      RequestCPU("gpu_consumer_copy_failed");
      return false;
    }
    MemoryBarrier();
    release(Free);
    output = image[target].p;
    ++copies;
    bytesAvoided += std::uint64_t(expected.stride) * expected.height;
    reason = "gpu_active";
    return true;
  }
};
} // namespace GPUFrame
