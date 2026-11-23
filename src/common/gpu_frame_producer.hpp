#pragma once
#include "d3d9_on12_api.hpp"
#include "gpu_frame_ipc.hpp"
#include "gpu_frame_objects.hpp"
#include "gpu_frame_texture_policy.hpp"
#include "dxvk_gpu_bridge.hpp"
#include <memory>
#include <vector>

namespace GPUFrame {
enum class PublishResult { Fallback, Busy, Published, Unavailable };
struct Producer {
  struct Generation {
    Object<Device9On12> bridge;
    Object<ID3D12Device> device;
    Object<ID3D12CommandQueue> queue;
    Object<ID3D12Fence> ready, done[SlotCount];
    Object<ID3D12Fence> dxvkReady,dxvkDone[SlotCount];
    Object<ID3D12Resource> texture[SlotCount], source[SlotCount];
    Object<ID3D12CommandAllocator> allocator[SlotCount];
    Object<ID3D12GraphicsCommandList> list[SlotCount];
    Handle readyHandle, textureHandle[SlotCount], doneHandle[SlotCount];
    Handle dxvkReadyHandle,dxvkTextureHandle[SlotCount],dxvkDoneHandle[SlotCount];
    // Destroy Vulkan imports before their native D3D12 resources/handles.
    std::unique_ptr<DXVKGPU::Bridge> dxvk;
    std::uint64_t next = 0, value[SlotCount]{};
    std::uint64_t dxvkValue=0,intermediateBytes=0;
    unsigned intermediateTextures=0;
    IDirect3DDevice9 *identity = nullptr;
    unsigned width = 0, height = 0;
    unsigned physicalTextures = 0;
    std::uint64_t textureBytes = 0;
    bool compactPanel = false, waitWithoutCPU = false;
    bool unfenced = false;
    bool Finished() const {
      if(dxvkReady.p&&dxvkReady->GetCompletedValue()<dxvkValue)return false;
      return !ready.p || (unfenced ? FAILED(device->GetDeviceRemovedReason())
                                   : ready->GetCompletedValue() >= next);
    }
  };
  Handle mapping;
  Shared *shared = nullptr;
  std::unique_ptr<Generation> active;
  std::vector<std::unique_ptr<Generation>> retired;
  IDirect3DDevice9 *rejected = nullptr;
  IDirect3DDevice9 *unavailable = nullptr;
  DWORD rejectedAt = 0;
  bool creationRetryable = false;
  bool dxvkEnabled = false; // explicit [VR] DXVKGPUTransport opt-in
  bool layoutRetirementPending = false;
  GPUFrameTexturePolicy::SceneRetirementWait sceneRetirementWait;
  HRESULT createResult = S_OK;
  std::uint32_t generation = 0;
  LONG host = 0;
  const char *reason = "disabled";
  std::uint64_t copies = 0, busy = 0, failures = 0;
  DWORD busySince[2]{}, lastBusyTick[2]{};
  bool busyPending[2]{};
  // Each eye has its own pair of slots. Progress in the other eye must not
  // hide a stalled pair. Discontinuous attempts (pause/minimize/suspend) start
  // a fresh interval rather than disabling a healthy transport on resume.
  bool RingStalled(unsigned ring, DWORD now) {
    if (!busyPending[ring] || DWORD(now - lastBusyTick[ring]) > 500) {
      busyPending[ring] = true;
      busySince[ring] = now;
    }
    lastBusyTick[ring] = now;
    return DWORD(now - busySince[ring]) >= 2000;
  }
  // The bridge owns this object for the lifetime of the game; Open/Tick do
  // not call D3D9. Invalidate/Try run only on the presentation thread.
  bool Open(DWORD pid) {
    if (shared)
      return Compatible(shared, pid);
    wchar_t name[100];
    Name(name, 100, pid);
    mapping.p = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                   PAGE_READWRITE, 0, sizeof(Shared), name);
    if (!mapping.p || GetLastError() == ERROR_ALREADY_EXISTS) {
      mapping.Reset();
      reason = "gpu_mapping_unavailable";
      return false;
    }
    shared = static_cast<Shared *>(
        MapViewOfFile(mapping.p, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared) {
      mapping.Reset();
      reason = "gpu_mapping_failed";
      return false;
    }
    std::memset(shared, 0, sizeof(*shared));
    shared->version = Version;
    shared->bytes = sizeof(*shared);
    shared->gamePid = pid;
    MemoryBarrier();
    shared->magic = Magic;
    reason = "waiting_for_gpu_consumer";
    return true;
  }
  void Collect() {
    for (auto i = retired.begin(); i != retired.end();)
      if ((*i)->Finished())
        i = retired.erase(i);
      else
        ++i;
  }
  void Invalidate() {
    layoutRetirementPending = false;
    sceneRetirementWait.Reset();
    busyPending[0] = busyPending[1] = false;
    if (shared) {
      InterlockedExchange(&shared->acceptedGeneration, 0);
      InterlockedIncrement(&shared->headerSequence);
      shared->generation = 0;
      MemoryBarrier();
      InterlockedIncrement(&shared->headerSequence);
    }
    if (active)
      retired.push_back(std::move(active));
    rejected = nullptr;
    unavailable = nullptr;
    rejectedAt = 0;creationRetryable = false;
    Collect();
    reason = "gpu_source_reset";
  }
  void DeviceUnavailable(IDirect3DDevice9* source,HRESULT result) {
    Invalidate();unavailable=source;rejected=source;createResult=result;
    reason="gpu_device_removed_waiting_for_reset";
  }
  bool HostReady() const {
    return shared && Atomic(shared->consumerEnabled) &&
           Atomic(shared->consumerPid) &&
           GetTickCount() - DWORD(Atomic(shared->consumerTick)) < 1000 &&
           !Atomic(shared->cpuRequested);
  }
  bool CanAttempt(IDirect3DDevice9* source) const {
    return rejected!=source || (shared&&Atomic(shared->consumerPid)!=host) ||
      (creationRetryable&&DWORD(GetTickCount()-rejectedAt)>=2000);
  }
  bool Create(IDirect3DDevice9 *source, const D3DSURFACE_DESC &desc,
              bool compactPanel = false,bool layoutTransition = false) {
    creationRetryable = false;createResult = S_OK;
    auto g = std::make_unique<Generation>();
    g->identity = source;
    g->width = desc.Width;
    g->height = desc.Height;
    g->compactPanel = compactPanel;g->waitWithoutCPU=compactPanel||layoutTransition;
    HRESULT hr = source->QueryInterface(
        iid9on12, reinterpret_cast<void **>(g->bridge.Put()));
    if (FAILED(hr)) {
      wchar_t optIn[4]{};
      if(!dxvkEnabled&&(GetEnvironmentVariableW(L"KL1_DXVK_GPU_TRANSPORT",optIn,4)!=1||optIn[0]!=L'1')){
        createResult = hr;reason = "source_is_not_d3d9on12";return false;
      }
      g->dxvk=std::make_unique<DXVKGPU::Bridge>();
      if(!g->dxvk->Open(source)){createResult=E_NOINTERFACE;reason=g->dxvk->reason;return false;}
      hr=g->dxvk->CreateD3D12Device(g->device.Put());
    }else{
      hr = g->bridge->GetD3D12Device(__uuidof(ID3D12Device),
                                     reinterpret_cast<void **>(g->device.Put()));
    }
    if (FAILED(hr)) {
      createResult = hr;
      reason = "d3d12_device_unavailable";
      return false;
    }
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = g->device->CreateCommandQueue(
        &q, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void **>(g->queue.Put()));
    if (SUCCEEDED(hr))
      hr = g->device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                  __uuidof(ID3D12Fence),
                                  reinterpret_cast<void **>(g->ready.Put()));
    if (SUCCEEDED(hr))
      hr = g->device->CreateSharedHandle(g->ready.p, nullptr, GENERIC_ALL,
                                         nullptr, &g->readyHandle.p);
    if(SUCCEEDED(hr)&&g->dxvk){
      hr=g->device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),reinterpret_cast<void**>(g->dxvkReady.Put()));
      if(SUCCEEDED(hr))hr=g->device->CreateSharedHandle(g->dxvkReady.p,nullptr,GENERIC_ALL,nullptr,&g->dxvkReadyHandle.p);
    }
    D3D12_RESOURCE_DESC image{};
    image.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    image.Width = desc.Width;
    image.Height = desc.Height;
    image.DepthOrArraySize = image.MipLevels = image.SampleDesc.Count = 1;
    image.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    image.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                  D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto allocation=g->device->GetResourceAllocationInfo(0,1,&image);
    for (unsigned i = 0; i < SlotCount && SUCCEEDED(hr); ++i) {
      const auto physical=GPUFrameTexturePolicy::PhysicalSlot(i,compactPanel);
      if(physical!=i){
        // Keep independent COM ownership and independently exported NT
        // handles for all four ABI slots. Right slots are never published in
        // this exact UI generation; only their unused storage aliases left.
        g->texture[i].p=g->texture[physical].p;
        if(g->texture[i].p)g->texture[i]->AddRef();else hr=E_UNEXPECTED;
      }else{
        hr = g->device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_SHARED, &image, D3D12_RESOURCE_STATE_COMMON,
          nullptr, __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(g->texture[i].Put()));
        if(SUCCEEDED(hr)){++g->physicalTextures;if(allocation.SizeInBytes!=UINT64_MAX)g->textureBytes+=allocation.SizeInBytes;}
      }
      if (SUCCEEDED(hr)){
        if(physical!=i){
          if(!DuplicateHandle(GetCurrentProcess(),g->textureHandle[physical].p,
              GetCurrentProcess(),&g->textureHandle[i].p,0,FALSE,DUPLICATE_SAME_ACCESS)){
            const auto error=GetLastError();hr=error?HRESULT_FROM_WIN32(error):E_FAIL;
          }
        }else hr = g->device->CreateSharedHandle(g->texture[i].p, nullptr, GENERIC_ALL,
                                               nullptr, &g->textureHandle[i].p);
      }
      if (SUCCEEDED(hr))
        hr = g->device->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence),
            reinterpret_cast<void **>(g->done[i].Put()));
      if (SUCCEEDED(hr))
        hr = g->device->CreateSharedHandle(g->done[i].p, nullptr, GENERIC_ALL,
                                           nullptr, &g->doneHandle[i].p);
      // Keep Vulkan's external-memory handoff inside this process. A native
      // D3D12 copy publishes into the existing cross-process D3D11 ring; it
      // never reads pixels through the CPU or imports consumer done fences.
      if(SUCCEEDED(hr)&&g->dxvk){
        if(physical!=i){g->source[i].p=g->source[physical].p;if(g->source[i].p)g->source[i]->AddRef();else hr=E_UNEXPECTED;}
        else{
          hr=g->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&image,D3D12_RESOURCE_STATE_COMMON,nullptr,
            __uuidof(ID3D12Resource),reinterpret_cast<void**>(g->source[i].Put()));
          if(SUCCEEDED(hr)){++g->intermediateTextures;if(allocation.SizeInBytes!=UINT64_MAX)g->intermediateBytes+=allocation.SizeInBytes;}
        }
        if(SUCCEEDED(hr))hr=g->device->CreateSharedHandle(g->source[i].p,nullptr,GENERIC_ALL,nullptr,&g->dxvkTextureHandle[i].p);
        if(SUCCEEDED(hr))hr=g->device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),reinterpret_cast<void**>(g->dxvkDone[i].Put()));
        if(SUCCEEDED(hr))hr=g->device->CreateSharedHandle(g->dxvkDone[i].p,nullptr,GENERIC_ALL,nullptr,&g->dxvkDoneHandle[i].p);
      }
      if (SUCCEEDED(hr))
        hr = g->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(g->allocator[i].Put()));
      if (SUCCEEDED(hr))
        hr = g->device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, g->allocator[i].p, nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void **>(g->list[i].Put()));
      if (SUCCEEDED(hr))
        hr = g->list[i]->Close();
    }
    if (FAILED(hr)) {
      createResult = hr;creationRetryable = true;
      reason = "gpu_resource_creation_failed";
      ++failures;
      const auto removed=g->device->GetDeviceRemovedReason();
      if(FAILED(removed))DeviceUnavailable(source,removed);
      return false;
    }
    if(g->dxvk){
      bool imported=g->dxvk->ImportFence(g->dxvkReadyHandle.p,g->dxvk->ready);
      for(unsigned i=0;i<SlotCount&&imported;++i)
        imported=g->dxvk->Import(i,g->dxvkTextureHandle[i].p,g->dxvkDoneHandle[i].p,desc.Width,desc.Height);
      if(!imported){createResult=E_FAIL;reason=g->dxvk->reason;++failures;return false;}
    }
    active = std::move(g);
    ++generation;
    if (!generation)
      ++generation;
    InterlockedExchange(&shared->acceptedGeneration, 0);
    InterlockedIncrement(&shared->headerSequence);
    shared->generation = generation;
    shared->width = desc.Width;
    shared->height = desc.Height;
    shared->format = 87;
    shared->adapter = active->device->GetAdapterLuid();
    shared->readyHandle =
        reinterpret_cast<std::uintptr_t>(active->readyHandle.p);
    for (unsigned i = 0; i < SlotCount; ++i) {
      shared->textureHandle[i] =
          reinterpret_cast<std::uintptr_t>(active->textureHandle[i].p);
      shared->doneHandle[i] =
          reinterpret_cast<std::uintptr_t>(active->doneHandle[i].p);
      InterlockedExchange(&shared->slots[i].state, Free);
    }
    MemoryBarrier();
    InterlockedIncrement(&shared->headerSequence);
    reason = "waiting_for_gpu_generation_ack";
    return true;
  }
  template<class Prepare>
  PublishResult Try(IDirect3DDevice9 *device, IDirect3DSurface9 *back,
                    unsigned mask, const VRIPC::EyeMetadata &meta,
                    unsigned epoch, unsigned &selected, IDirect3DResource9 *interop, Prepare prepare) {
    const bool sceneImage=meta.poseValid==VRIPC::kSceneFrame||meta.poseValid==VRIPC::kScopeFrame;
    // Even an early UI return (consumer unavailable, invalid source, etc.)
    // interrupts a continuous scene-only recovery interval.
    if(!sceneImage)sceneRetirementWait.Reset();
    if(unavailable==device)return PublishResult::Unavailable;
    Collect();
    const bool compactPanel=GPUFrameTexturePolicy::CompactPanel(mask,meta);
    // A driver/fence failure may leave an old generation pending forever.
    // Keep its in-flight resources alive, but bound subsequent allocations
    // across resize/chapter loops and use the existing CPU compatibility path.
    if (retired.size() >= 3&&!layoutRetirementPending&&
        !(active&&active->compactPanel!=compactPanel)) {
      reason = "gpu_retirement_budget_cpu_fallback";
      return PublishResult::Fallback;
    }
    if (!HostReady()) {
      busyPending[0] = busyPending[1] = false;
      reason = "cpu_requested_or_gpu_consumer_unavailable";
      return PublishResult::Fallback;
    }
    const auto pid = Atomic(shared->consumerPid);
    if (host != pid) {
      Invalidate();
      host = pid;
    }
    D3DSURFACE_DESC desc{};
    if (FAILED(back->GetDesc(&desc)) || desc.Width != meta.width ||
        desc.Height != meta.height || desc.Format != D3DFMT_A8R8G8B8 ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
        desc.Width > VRIPC::kMaxGPUWidth || desc.Height > VRIPC::kMaxGPUHeight) {
      reason = "source_requires_cpu_compatibility_path";
      return PublishResult::Fallback;
    }
    if (active && (active->identity != device || active->width != desc.Width ||
                   active->height != desc.Height||active->compactPanel!=compactPanel)){
      const bool layoutTransition=active->compactPanel!=compactPanel;
      Invalidate();layoutRetirementPending=layoutTransition;
    }
    // Do not peak at four old plus two new textures during loader entry.
    // Fence completion is observed without waiting or freeing submitted work.
    // Keeping this as Busy also avoids allocating a CPU readback/arena merely
    // to bridge this ordinary transport generation transition.
    if((layoutRetirementPending||compactPanel)&&!retired.empty()){
      layoutRetirementPending=true;++busy;
      if(sceneRetirementWait.Fallback(sceneImage,GetTickCount())){
        reason="gpu_scene_layout_retirement_cpu_fallback";return PublishResult::Fallback;
      }
      reason="gpu_panel_layout_retiring";
      return PublishResult::Busy;
    }
    sceneRetirementWait.Reset();
    if (rejected == device) {
      // Resource creation can fail transiently during a graphics Reset while
      // old shared views are retiring. Do not permanently latch CPU/stale
      // output after that one failure. Unsupported APIs and stalled rings
      // still require a Reset/new host; avoid retrying every source frame.
      if (!CanAttempt(device))
        return PublishResult::Fallback;
      rejected = nullptr;
    }
    if (!active && !Create(device, desc,compactPanel,layoutRetirementPending)) {
      if(unavailable==device)return PublishResult::Unavailable;
      rejected = device;
      rejectedAt = GetTickCount();
      return PublishResult::Fallback;
    }
    layoutRetirementPending=false;
    if (Atomic(shared->acceptedGeneration) != LONG(generation)) {
      reason = "waiting_for_gpu_generation_ack";
      if(active->waitWithoutCPU){++busy;return PublishResult::Busy;}
      return PublishResult::Fallback;
    }
    auto &g = *active;
    selected = SlotCount;
    LONG selectedState=Free;
    const unsigned first = mask == 2 ? 2 : 0;
    // When both ready slots are reusable, keep the newest complete images
    // available for the compositor instead of continuously replacing slot 0.
    const unsigned oldest=g.value[first]<=g.value[first+1]?first:first+1;
    const unsigned order[]{oldest,first+(oldest==first)};
    const auto readyValue=g.ready->GetCompletedValue();
    for (LONG state : {LONG(Free), LONG(Ready)})
      for (unsigned i : order) {
        if(selected!=SlotCount)break;
        const auto doneValue=g.done[i]->GetCompletedValue();
        if (readyValue == UINT64_MAX || doneValue == UINT64_MAX) {
          ++failures;
          auto removed=g.device->GetDeviceRemovedReason();
          if(SUCCEEDED(removed))removed=HRESULT(0x887A0005L);
          DeviceUnavailable(device,removed);
          return PublishResult::Unavailable;
        }
        if (readyValue < g.value[i] || (state == Free && doneValue < g.value[i]))
          continue;
        if (InterlockedCompareExchange(&shared->slots[i].state, Writing,
                                       state) == state) {
          selected = i;selectedState=state;
        }
      }
    if (selected == SlotCount) {
      ++busy;
      if (RingStalled(first / 2, GetTickCount())) {
        ++failures;
        Invalidate();
        // Keep resources alive until their submitted copies finish, but stop
        // retrying this device every frame. Reset or a new host permits retry.
        rejected = device;
        reason = "gpu_ring_stalled_cpu_fallback";
        return PublishResult::Fallback;
      }
      return PublishResult::Busy;
    }
    busyPending[first / 2] = false;
    const auto i = selected;
    auto &slot = shared->slots[i];
    // A full-size resolve/conversion is useful only after a consumer has
    // acknowledged this generation and a writable ring slot is available.
    // On failure the old slot's image/metadata remain untouched.
    if(FAILED(prepare())) {
      InterlockedExchange(&slot.state,selectedState);
      ++failures;return PublishResult::Fallback;
    }
    reason = "gpu_unwrap";
    HRESULT hr=S_OK;bool queued=false,checked=false;UINT64 value=0;
    if(g.dxvk){
      const auto next=g.next+1;
      if(g.dxvk->Copy(i,interop?interop:back,g.value[i],next)){
        g.dxvkValue=next;hr=g.queue->Wait(g.dxvkReady.p,next);
      }else hr=E_FAIL;
    }else{
    g.source[i].Reset();
    hr = g.bridge->UnwrapUnderlyingResource(
        interop ? interop : back, g.queue.p, __uuidof(ID3D12Resource),
        reinterpret_cast<void **>(g.source[i].Put()));
    checked = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr))
      hr = g.allocator[i]->Reset();
    if (SUCCEEDED(hr))
      hr = g.list[i]->Reset(g.allocator[i].p, nullptr);
    if (SUCCEEDED(hr)) {
      D3D12_RESOURCE_BARRIER b[2]{};
      for (auto &v : b) {
        v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        v.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        v.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      }
      b[0].Transition.pResource = g.source[i].p;
      b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      b[1].Transition.pResource = g.texture[i].p;
      b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      g.list[i]->ResourceBarrier(2, b);
      g.list[i]->CopyResource(g.texture[i].p, g.source[i].p);
      for (auto &v : b) {
        v.Transition.StateBefore = v.Transition.StateAfter;
        v.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
      }
      g.list[i]->ResourceBarrier(2, b);
      hr = g.list[i]->Close();
    }
    if (SUCCEEDED(hr)) {
      ID3D12CommandList *lists[]{g.list[i].p};
      g.queue->ExecuteCommandLists(1, lists);
      value = ++g.next;
      if(g.dxvk)hr=g.queue->Signal(g.dxvkDone[i].p,value);
      if(SUCCEEDED(hr))hr = g.queue->Signal(g.ready.p, value);
      queued = SUCCEEDED(hr);
      if (queued)
        g.value[i] = value;
      else
        g.unfenced = true;
    }
    if (checked) {
      ID3D12Fence *fences[]{g.ready.p};
      const auto returned = g.bridge->ReturnUnderlyingResource(
          interop ? interop : back, queued ? 1 : 0, queued ? &value : nullptr,
          queued ? fences : nullptr);
      if (SUCCEEDED(hr))
        hr = returned;
    }
    if (FAILED(hr)) {
      ++failures;
      const auto removed=g.device->GetDeviceRemovedReason();
      if(FAILED(removed)){DeviceUnavailable(device,removed);return PublishResult::Unavailable;}
      Invalidate();
      reason = "gpu_capture_failed";
      rejected = device;
      return PublishResult::Fallback;
    }
    slot.generation = generation;
    slot.transportEpoch = epoch;
    slot.eyeMask = mask;
    slot.value = value;
    slot.metadata = meta;
    MemoryBarrier();
    InterlockedExchange(&slot.state, Ready);
    ++copies;
    reason = g.dxvk?"gpu_active_dxvk_d3d12_copy":"gpu_active";
    return PublishResult::Published;
  }
};
} // namespace GPUFrame
