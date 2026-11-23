#pragma once
// DXVK's public D3D9 Vulkan interop ABI. No DXVK or Vulkan loader link dependency.
// https://github.com/doitsujin/dxvk/blob/master/src/d3d9/d3d9_interfaces.h
#include <d3d9.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "gpu_frame_objects.hpp"
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan_win32.h"
#include <cstring>
#include <cwchar>

namespace DXVKGPU {
struct Texture : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(VkImage*, VkImageLayout*, VkImageCreateInfo*) = 0;
};
struct Device : IUnknown {
  virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance*, VkPhysicalDevice*, VkDevice*) = 0;
  virtual void STDMETHODCALLTYPE GetSubmissionQueue(VkQueue*, uint32_t*, uint32_t*) = 0;
  virtual void STDMETHODCALLTYPE TransitionTextureLayout(Texture*, const VkImageSubresourceRange*, VkImageLayout, VkImageLayout) = 0;
  virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
  virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
  virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
  virtual void STDMETHODCALLTYPE LockDevice() = 0;
  virtual void STDMETHODCALLTYPE UnlockDevice() = 0;
};
inline constexpr GUID DeviceIID = {0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
inline constexpr GUID TextureIID = {0xd56344f5,0x8d35,0x46fd,{0x80,0x6d,0x94,0xc3,0x51,0xb4,0x72,0xc1}};

struct Bridge {
  GPUFrame::Object<Device> interop;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = 0;
  LUID adapter{};
  const char* reason = "dxvk_not_initialized";
  VkResult result = VK_SUCCESS;
  VkSemaphore ready = VK_NULL_HANDLE;
  struct Slot {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    GPUFrame::Object<IDirect3DResource9> source;
    unsigned width = 0, height = 0;
    bool initialized = false;
  } slots[4];
#define KL1_VK_DEVICE_FUNCTIONS(X) \
  X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) \
  X(FreeMemory) X(BindImageMemory) X(GetMemoryWin32HandlePropertiesKHR) \
  X(CreateSemaphore) X(DestroySemaphore) X(ImportSemaphoreWin32HandleKHR) \
  X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) \
  X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
  X(CmdPipelineBarrier) X(CmdCopyImage) X(QueueSubmit)
#define KL1_VK_MEMBER(name) PFN_vk##name name = nullptr;
  KL1_VK_DEVICE_FUNCTIONS(KL1_VK_MEMBER)
#undef KL1_VK_MEMBER
  PFN_vkGetPhysicalDeviceProperties2 Properties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties MemoryProperties = nullptr;
  PFN_vkGetPhysicalDeviceImageFormatProperties2 ImageProperties = nullptr;
  PFN_vkGetPhysicalDeviceExternalSemaphoreProperties SemaphoreProperties = nullptr;
  Bridge() = default;
  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;
  ~Bridge() {
    // Owner retires this bridge only after the shared ready fence completes.
    if (!device) return;
    for (auto& s : slots) {
      if(s.pool) DestroyCommandPool(device,s.pool,nullptr);
      if(s.done) DestroySemaphore(device,s.done,nullptr);
      if(s.image) DestroyImage(device,s.image,nullptr);
      if(s.memory) FreeMemory(device,s.memory,nullptr);
    }
    if(ready) DestroySemaphore(device,ready,nullptr);
  }
  bool Check(VkResult value,const char* operation) {
    result=value;if(value==VK_SUCCESS)return true;reason=operation;return false;
  }
  bool Open(IDirect3DDevice9* d3d) {
    if(FAILED(d3d->QueryInterface(DeviceIID,reinterpret_cast<void**>(interop.Put()))))return false;
    interop->GetVulkanHandles(&instance,&physical,&device);
    uint32_t index=0;interop->GetSubmissionQueue(&queue,&index,&family);
    if(!instance||!physical||!device||!queue){reason="dxvk_missing_handles";return false;}
    auto loader=GetModuleHandleW(L"vulkan-1.dll");
    auto getInstance=loader?reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr")):nullptr;
    auto getDevice=getInstance?reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstance(instance,"vkGetDeviceProcAddr")):nullptr;
    if(!getDevice){reason="dxvk_missing_loader";return false;}
#define KL1_VK_LOAD(name) name=reinterpret_cast<PFN_vk##name>(getDevice(device,"vk" #name));if(!name){reason="dxvk_missing_" #name;return false;}
    KL1_VK_DEVICE_FUNCTIONS(KL1_VK_LOAD)
#undef KL1_VK_LOAD
    Properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getInstance(instance,"vkGetPhysicalDeviceProperties2"));
    MemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(getInstance(instance,"vkGetPhysicalDeviceMemoryProperties"));
    ImageProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(getInstance(instance,"vkGetPhysicalDeviceImageFormatProperties2"));
    SemaphoreProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(getInstance(instance,"vkGetPhysicalDeviceExternalSemaphoreProperties"));
    if(!Properties||!MemoryProperties||!ImageProperties||!SemaphoreProperties){reason="dxvk_missing_physical_properties";return false;}
    VkPhysicalDeviceExternalSemaphoreInfo semaphoreInfo{};semaphoreInfo.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    semaphoreInfo.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    VkExternalSemaphoreProperties semaphoreProperties{};semaphoreProperties.sType=VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    SemaphoreProperties(physical,&semaphoreInfo,&semaphoreProperties);
    if(!(semaphoreProperties.externalSemaphoreFeatures&VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)){reason="dxvk_d3d12_fence_not_importable";return false;}
    VkPhysicalDeviceIDProperties id{};id.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;properties.pNext=&id;
    Properties(physical,&properties);
    if(!id.deviceLUIDValid){reason="dxvk_adapter_luid_unavailable";return false;}
    std::memcpy(&adapter,id.deviceLUID,sizeof(adapter));
    reason="dxvk_interop_available";return true;
  }
  HRESULT CreateD3D12Device(ID3D12Device** out) {
    // Absolute system modules: never load a game's DXVK dxgi.dll here.
    static HMODULE dxgi=SystemModule(L"dxgi.dll");
    static HMODULE d3d12=SystemModule(L"d3d12.dll");
    using Factory=HRESULT(WINAPI*)(REFIID,void**);
    using Create=HRESULT(WINAPI*)(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
    auto factory=dxgi?reinterpret_cast<Factory>(GetProcAddress(dxgi,"CreateDXGIFactory1")):nullptr;
    auto create=d3d12?reinterpret_cast<Create>(GetProcAddress(d3d12,"D3D12CreateDevice")):nullptr;
    if(!factory||!create)return E_NOINTERFACE;
    GPUFrame::Object<IDXGIFactory4> f;GPUFrame::Object<IDXGIAdapter1> a;
    HRESULT hr=factory(__uuidof(IDXGIFactory4),reinterpret_cast<void**>(f.Put()));
    if(SUCCEEDED(hr))hr=f->EnumAdapterByLuid(adapter,__uuidof(IDXGIAdapter1),reinterpret_cast<void**>(a.Put()));
    if(SUCCEEDED(hr))hr=create(a.p,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),reinterpret_cast<void**>(out));
    return hr;
  }
  static HMODULE SystemModule(const wchar_t* name) {
    wchar_t directory[MAX_PATH]{};const auto length=GetSystemDirectoryW(directory,MAX_PATH);
    if(!length||length>=MAX_PATH-16)return nullptr;
    directory[length]=L'\\';std::wcsncpy(directory+length+1,name,MAX_PATH-length-2);
    return LoadLibraryExW(directory,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  bool ImportFence(HANDLE handle,VkSemaphore& out) {
    // Use the original D3D12-fence interop submission ABI, including its
    // explicit values. Native D3D11/12 consumers share this fence payload.
    VkSemaphoreCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if(!Check(CreateSemaphore(device,&info,nullptr,&out),"dxvk_create_d3d12_semaphore"))return false;
    VkImportSemaphoreWin32HandleInfoKHR import{};import.sType=VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    import.semaphore=out;import.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;import.handle=handle;
    return Check(ImportSemaphoreWin32HandleKHR(device,&import),"dxvk_import_d3d12_fence");
  }
  bool Import(unsigned index,HANDLE texture,HANDLE done,unsigned width,unsigned height) {
    if(index>=4||!device||!ready)return false;
    auto& s=slots[index];s.width=width;s.height=height;
    VkPhysicalDeviceExternalImageFormatInfo external{};external.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 query{};query.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;query.pNext=&external;
    query.format=VK_FORMAT_B8G8R8A8_UNORM;query.type=VK_IMAGE_TYPE_2D;query.tiling=VK_IMAGE_TILING_OPTIMAL;
    query.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkExternalImageFormatProperties externalProperties{};externalProperties.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};properties.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;properties.pNext=&externalProperties;
    if(!Check(ImageProperties(physical,&query,&properties),"dxvk_external_image_properties"))return false;
    if(!(externalProperties.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)){reason="dxvk_d3d12_resource_not_importable";return false;}
    VkExternalMemoryImageCreateInfo shared{};shared.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;shared.handleTypes=external.handleType;
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.pNext=&shared;
    image.imageType=query.type;image.format=query.format;image.extent={width,height,1};image.mipLevels=image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=query.tiling;image.usage=query.usage;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(!Check(CreateImage(device,&image,nullptr,&s.image),"dxvk_create_external_image"))return false;
    VkMemoryRequirements requirements{};GetImageMemoryRequirements(device,s.image,&requirements);
    VkMemoryWin32HandlePropertiesKHR handleProperties{};handleProperties.sType=VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    if(!Check(GetMemoryWin32HandlePropertiesKHR(device,external.handleType,texture,&handleProperties),"dxvk_d3d12_memory_properties"))return false;
    VkPhysicalDeviceMemoryProperties memory{};MemoryProperties(physical,&memory);
    uint32_t type=~0u;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
      if((requirements.memoryTypeBits&handleProperties.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
    if(type==~0u){reason="dxvk_no_shared_memory_type";return false;}
    VkMemoryDedicatedAllocateInfo dedicated{};dedicated.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;dedicated.image=s.image;
    VkImportMemoryWin32HandleInfoKHR import{};import.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    import.pNext=&dedicated;import.handleType=external.handleType;import.handle=texture;
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.pNext=&import;
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    if(!Check(AllocateMemory(device,&allocation,nullptr,&s.memory),"dxvk_import_d3d12_resource")||
       !Check(BindImageMemory(device,s.image,s.memory,0),"dxvk_bind_d3d12_image")||!ImportFence(done,s.done))return false;
    VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=family;
    if(!Check(CreateCommandPool(device,&pool,nullptr,&s.pool),"dxvk_create_copy_pool"))return false;
    VkCommandBufferAllocateInfo command{};command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command.commandPool=s.pool;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
    return Check(AllocateCommandBuffers(device,&command,&s.command),"dxvk_allocate_copy_command");
  }
  bool Copy(unsigned index,IDirect3DResource9* source,uint64_t doneValue,uint64_t readyValue) {
    if(index>=4||!source)return false;
    // The native game can upload resources from its simulation thread while
    // the render worker presents. Protect the complete interop transaction,
    // not just vkQueueSubmit, with DXVK's documented multithreaded device lock.
    interop->LockDevice();
    struct Unlock {Device* device;~Unlock(){device->UnlockDevice();}} unlock{interop.p};
    auto& s=slots[index];GPUFrame::Object<Texture> texture;
    if(FAILED(source->QueryInterface(TextureIID,reinterpret_cast<void**>(texture.Put())))){reason="dxvk_source_interop_unavailable";return false;}
    VkImage src=VK_NULL_HANDLE;VkImageLayout layout{};VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    if(FAILED(texture->GetVulkanImageInfo(&src,&layout,&info))||!src||info.extent.width!=s.width||info.extent.height!=s.height||
       info.format!=VK_FORMAT_B8G8R8A8_UNORM||info.samples!=VK_SAMPLE_COUNT_1_BIT||!(info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)){
      reason="dxvk_source_image_incompatible";return false;
    }
    // The owner's ready-fence test guarantees the previous command in this
    // slot is complete. Retain the D3D9 source until this copy completes too.
    s.source.Reset();s.source.p=source;source->AddRef();
    if(!Check(ResetCommandPool(device,s.pool,0),"dxvk_reset_copy_pool"))return false;
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!Check(BeginCommandBuffer(s.command,&begin),"dxvk_begin_copy"))return false;
    VkImageMemoryBarrier b[2]{};
    for(auto& barrier:b){barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;}
    b[0].image=src;b[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    b[1].image=s.image;b[1].oldLayout=s.initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;b[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;b[1].dstQueueFamilyIndex=family;b[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    CmdPipelineBarrier(s.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,b);
    VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={s.width,s.height,1};
    CmdCopyImage(s.command,src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    b[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b[1].newLayout=VK_IMAGE_LAYOUT_GENERAL;
    b[1].srcQueueFamilyIndex=family;b[1].dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;b[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b[1].dstAccessMask=0;
    CmdPipelineBarrier(s.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b[1]);
    if(!Check(EndCommandBuffer(s.command),"dxvk_end_copy"))return false;
    VkD3D12FenceSubmitInfoKHR timeline{};timeline.sType=VK_STRUCTURE_TYPE_D3D12_FENCE_SUBMIT_INFO_KHR;
    timeline.signalSemaphoreValuesCount=1;timeline.pSignalSemaphoreValues=&readyValue;
    VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.pNext=&timeline;
    submit.commandBufferCount=1;submit.pCommandBuffers=&s.command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&ready;
    VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_TRANSFER_BIT;
    if(doneValue){timeline.waitSemaphoreValuesCount=1;timeline.pWaitSemaphoreValues=&doneValue;submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&s.done;submit.pWaitDstStageMask=&waitStage;}
    // D3D9 rendering must be submitted before the external command; never
    // invoke a D3D9 method while the documented DXVK queue lock is held.
    // Its transition API also materializes deferred clears/render-pass writes.
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    interop->TransitionTextureLayout(texture.p,&range,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    interop->FlushRenderingCommands();
    interop->LockSubmissionQueue();
    const auto submitted=QueueSubmit(queue,1,&submit,VK_NULL_HANDLE);
    interop->ReleaseSubmissionQueue();
    // Restore through DXVK too: a raw Vulkan barrier would leave its internal
    // resource tracker in TRANSFER_SRC, breaking later native draw/uploads.
    if(submitted!=VK_ERROR_DEVICE_LOST){
      interop->TransitionTextureLayout(texture.p,&range,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout);
      interop->FlushRenderingCommands();
    }
    if(!Check(submitted,"dxvk_submit_gpu_copy")){
      // No copy was accepted. Restore the source's public D3D9 layout before
      // falling back, except when the Vulkan device itself has been lost.
      return false;
    }
    s.initialized=true;reason="dxvk_gpu_active";return true;
  }
};
#undef KL1_VK_DEVICE_FUNCTIONS
} // namespace DXVKGPU
