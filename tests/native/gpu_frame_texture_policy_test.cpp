// Production producer with private COM/Win32 handles only. No GPU or game.
#include "gpu_frame_producer.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {
unsigned checks=0;
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL: %s\n",why);std::exit(1);}}
struct Fixture;
enum Kind {Source,Bridge,Device,Queue,Fence,Texture,Allocator,List,Surface};
struct Com {
    void** vtable=nullptr;unsigned refs=1;Fixture* owner=nullptr;Kind kind=Source;
    UINT64 completed=0;bool destroyed=false;
    static ULONG __stdcall Add(Com* p){Check(p&&!p->destroyed,"AddRef only on live fixture object");return ++p->refs;}
    static ULONG __stdcall Release(Com* p){Check(p&&p->refs,"balanced fixture COM Release");if(!--p->refs)p->destroyed=true;return p->refs;}
};
struct Fixture {
    std::array<void*,44> deviceTable{};std::array<void*,16> ordinary{},sourceTable{},bridgeTable{},surfaceTable{};
    std::vector<std::unique_ptr<Com>> objects;
    Com *source=nullptr,*bridge=nullptr,*device=nullptr,*back=nullptr;
    unsigned textures=0,sharedCalls=0,failShared=0,failTexture=0,peakTextures=0;
    std::vector<HANDLE> handles;
    static constexpr UINT64 TextureBytes=0x1fe0000;
    Com* Make(Kind kind,void** methods=nullptr){auto p=std::make_unique<Com>();p->owner=this;p->kind=kind;p->vtable=methods?methods:ordinary.data();auto* result=p.get();objects.push_back(std::move(p));return result;}
    static HRESULT __stdcall QuerySource(Com* p,REFIID iid,void** out){
        Check(IsEqualGUID(iid,iid9on12),"producer requests original 9On12 ABI");Com::Add(p->owner->bridge);*out=p->owner->bridge;return S_OK;}
    static HRESULT __stdcall GetDevice(Com* p,REFIID,void** out){Com::Add(p->owner->device);*out=p->owner->device;return S_OK;}
    static HRESULT __stdcall CreateQueue(Com* p,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void** out){*out=p->owner->Make(Queue);return S_OK;}
    static HRESULT __stdcall CreateFence(Com* p,UINT64 initial,D3D12_FENCE_FLAGS,REFIID,void** out){auto* f=p->owner->Make(Fence);f->completed=initial;*out=f;return S_OK;}
    static HRESULT __stdcall CreateAllocator(Com* p,D3D12_COMMAND_LIST_TYPE,REFIID,void** out){*out=p->owner->Make(Allocator);return S_OK;}
    static HRESULT __stdcall CreateList(Com* p,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void** out){*out=p->owner->Make(List);return S_OK;}
    static HRESULT __stdcall CreateTexture(Com* p,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void** out){
        auto& f=*p->owner;if(++f.textures==f.failTexture)return E_OUTOFMEMORY;*out=f.Make(Texture);
        unsigned live=0;for(const auto& o:f.objects)live+=o->kind==Texture&&o->refs;
        if(live>f.peakTextures)f.peakTextures=live;return S_OK;}
    static HRESULT __stdcall Share(Com* p,ID3D12DeviceChild*,const SECURITY_ATTRIBUTES*,DWORD,const WCHAR*,HANDLE* out){
        auto& f=*p->owner;if(++f.sharedCalls==f.failShared)return E_FAIL;
        *out=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!*out)return E_FAIL;f.handles.push_back(*out);return S_OK;}
    static HRESULT __stdcall Removed(Com*){return S_OK;}
    static HRESULT __stdcall Close(Com*){return S_OK;}
    static UINT64 __stdcall Completed(Com* p){return p->completed;}
    static HRESULT __stdcall Desc(Com*,D3DSURFACE_DESC* d){*d={};d->Width=3840;d->Height=2160;d->Format=D3DFMT_A8R8G8B8;return S_OK;}
#ifdef WIDL_EXPLICIT_AGGREGATE_RETURNS
    static D3D12_RESOURCE_ALLOCATION_INFO* __stdcall Allocation(Com*,D3D12_RESOURCE_ALLOCATION_INFO* out,UINT,UINT,const D3D12_RESOURCE_DESC*){*out={TextureBytes,65536};return out;}
    static LUID* __stdcall Adapter(Com*,LUID* out){*out={42,13};return out;}
#else
    static D3D12_RESOURCE_ALLOCATION_INFO __stdcall Allocation(Com*,UINT,UINT,const D3D12_RESOURCE_DESC*){return {TextureBytes,65536};}
    static LUID __stdcall Adapter(Com*){return {42,13};}
#endif
    Fixture(){
        for(auto* t:{ordinary.data(),sourceTable.data(),bridgeTable.data(),surfaceTable.data(),deviceTable.data()}){t[1]=reinterpret_cast<void*>(Com::Add);t[2]=reinterpret_cast<void*>(Com::Release);}
        ordinary[8]=reinterpret_cast<void*>(Completed);ordinary[9]=reinterpret_cast<void*>(Close);
        sourceTable[0]=reinterpret_cast<void*>(QuerySource);bridgeTable[3]=reinterpret_cast<void*>(GetDevice);surfaceTable[12]=reinterpret_cast<void*>(Desc);
        deviceTable[8]=reinterpret_cast<void*>(CreateQueue);deviceTable[9]=reinterpret_cast<void*>(CreateAllocator);deviceTable[12]=reinterpret_cast<void*>(CreateList);
        deviceTable[25]=reinterpret_cast<void*>(Allocation);deviceTable[27]=reinterpret_cast<void*>(CreateTexture);deviceTable[31]=reinterpret_cast<void*>(Share);
        deviceTable[36]=reinterpret_cast<void*>(CreateFence);deviceTable[37]=reinterpret_cast<void*>(Removed);deviceTable[43]=reinterpret_cast<void*>(Adapter);
        source=Make(Source,sourceTable.data());bridge=Make(Bridge,bridgeTable.data());device=Make(Device,deviceTable.data());back=Make(Surface,surfaceTable.data());
    }
    IDirect3DDevice9* SourceAPI(){return reinterpret_cast<IDirect3DDevice9*>(source);}
    IDirect3DSurface9* BackAPI(){return reinterpret_cast<IDirect3DSurface9*>(back);}
    void Balanced(){for(const auto& p:objects)Check(p->refs==unsigned(p.get()==source||p.get()==bridge||p.get()==device||p.get()==back),"all generation references released exactly once, including aliases");
        for(const auto h:handles){DWORD flags=0;Check(!GetHandleInformation(h,&flags),"all independently exported fixture handles closed");}}
};
VRIPC::EyeMetadata Metadata(unsigned kind){VRIPC::EyeMetadata m{};m.width=3840;m.height=2160;m.stride=3840*4;m.frameId=100;m.tick=GetTickCount();m.poseValid=kind;return m;}
GPUFrame::Shared Shared(){GPUFrame::Shared s{};s.magic=GPUFrame::Magic;s.version=GPUFrame::Version;s.bytes=sizeof(s);s.gamePid=GetCurrentProcessId();s.consumerPid=7;s.consumerEnabled=1;s.consumerTick=GetTickCount();return s;}
void Creation(bool compact){Fixture f;auto shared=Shared();{
    GPUFrame::Producer p;p.shared=&shared;p.host=7;
    D3DSURFACE_DESC desc{};Fixture::Desc(f.back,&desc);
    Check(p.Create(f.SourceAPI(),desc,compact),"real production generation creates against fake COM");
    const auto& g=*p.active;Check(g.physicalTextures==(compact?2u:4u)&&g.textureBytes==g.physicalTextures*Fixture::TextureBytes,"actual production allocation metrics count unique physical textures");
    Check((g.texture[0].p==g.texture[2].p)==compact&&(g.texture[1].p==g.texture[3].p)==compact,"only compact right slots alias corresponding left resources");
    std::set<HANDLE> handles;std::set<const void*> done;
    for(unsigned i=0;i<4;++i){handles.insert(g.textureHandle[i].p);done.insert(g.done[i].p);Check(g.allocator[i].p&&g.list[i].p&&g.doneHandle[i].p,"all four allocator/list/fence slots survive unchanged");
        if(compact&&i>=2)f.handles.push_back(g.textureHandle[i].p);}
    Check(handles.size()==4&&done.size()==4,"four independent exported texture handles and four independent done fences retained");
    GPUFrame::Header header{};Check(GPUFrame::ReadHeader(shared,header)&&GPUFrame::ValidHeader(header)&&header.generation==p.generation,"unchanged public ABI header remains readable");
    p.Invalidate();Check(!p.active&&p.retired.empty(),"unused completed generation collected without waits");
 }f.Balanced();}
void Transition(){Fixture f;auto shared=Shared();{
    GPUFrame::Producer p;p.shared=&shared;p.host=7;D3DSURFACE_DESC desc{};Fixture::Desc(f.back,&desc);Check(p.Create(f.SourceAPI(),desc),"scene starts with full generation");
    auto* fence=reinterpret_cast<Com*>(p.active->ready.p);p.active->next=9;fence->completed=8;
    unsigned selected=99,prepared=0;auto meta=Metadata(VRIPC::kContextFrame);const auto attempt=[&](unsigned mask){shared.consumerTick=GetTickCount();return p.Try(f.SourceAPI(),f.BackAPI(),mask,meta,1,selected,nullptr,[&]{++prepared;return E_FAIL;});};
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&!p.active&&p.retired.size()==1&&f.textures==4,"panel transition retires but does not allocate while prior GPU copy is in flight");
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&p.retired.size()==1&&f.textures==4&&!fence->destroyed,"repeated pending panel never churns generations or destroys in-flight resources");
    meta=Metadata(VRIPC::kSceneFrame);p.sceneRetirementWait.pending=true;p.sceneRetirementWait.started=GetTickCount()-2100;p.sceneRetirementWait.last=GetTickCount();
    Check(attempt(1)==GPUFrame::PublishResult::Fallback&&p.retired.size()==1&&f.textures==4&&!fence->destroyed&&prepared==0,"stalled retired fence restores scene CPU compatibility after bounded recovery without destroying resources");
    meta=Metadata(VRIPC::kContextFrame);shared.consumerEnabled=0;
    Check(attempt(3)==GPUFrame::PublishResult::Fallback&&!p.sceneRetirementWait.pending,"early UI consumer-fallback also interrupts scene recovery timer");
    shared.consumerEnabled=1;meta=Metadata(VRIPC::kSceneFrame);
    Check(attempt(1)==GPUFrame::PublishResult::Busy,"scene after an early UI return receives its fresh grace interval");
    meta=Metadata(VRIPC::kContextFrame);
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&!p.sceneRetirementWait.pending&&f.textures==4,"return to loading resets scene timeout and never allocates CPU fallback for retirement");
    fence->completed=9;
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&p.retired.empty()&&p.active&&p.active->physicalTextures==2&&f.textures==6,"completed old generation collects before two compact allocations; new acknowledgement avoids CPU fallback");
    const auto panel=p.generation;Check(attempt(3)==GPUFrame::PublishResult::Busy&&p.generation==panel&&f.textures==6&&prepared==0,"pending compact acknowledgement neither allocates nor resolves/copies");
    shared.acceptedGeneration=LONG(p.generation);shared.slots[0].state=shared.slots[1].state=GPUFrame::Reading;
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&prepared==0&&shared.slots[2].state==GPUFrame::Free&&shared.slots[3].state==GPUFrame::Free,"busy panel never falls through to unused aliased right slots");
    shared.slots[0].state=shared.slots[1].state=GPUFrame::Free;
    Check(attempt(3)==GPUFrame::PublishResult::Fallback&&prepared==1&&selected<2,"acknowledged panel uses only original left ring slots before deliberate fixture prepare failure");prepared=0;
    shared.acceptedGeneration=LONG(p.generation);meta=Metadata(VRIPC::kScopeFrame);
    Check(attempt(3)==GPUFrame::PublishResult::Busy&&p.active->physicalTextures==4&&!p.active->compactPanel&&f.textures==10,"mask3 sniper scope recreates four unique scene textures");
    const auto scope=p.generation;Check(attempt(3)==GPUFrame::PublishResult::Busy&&p.generation==scope&&f.textures==10,"scope mask3 never causes an endless UI layout flip");
    meta=Metadata(VRIPC::kSceneFrame);Check(attempt(1)==GPUFrame::PublishResult::Busy&&p.generation==scope&&f.textures==10,"stereo scene reuses full scope generation, not aliased storage");
    shared.acceptedGeneration=LONG(p.generation);
    Check(attempt(2)==GPUFrame::PublishResult::Fallback&&prepared==1&&selected>=2&&selected<4,"right scene eye retains its distinct original ring and physical texture");prepared=0;
    shared.consumerEnabled=0;Check(attempt(1)==GPUFrame::PublishResult::Fallback,"ordinary unavailable-host CPU compatibility semantics preserved");
    p.Invalidate();Check(p.retired.empty()&&prepared==0,"fixture transition never starts pixel work before generation acknowledgement");
    Check(f.peakTextures==4,"layout transitions never peak above four original physical textures");
 }f.Balanced();}
}
int main(){
    using namespace GPUFrameTexturePolicy;
    for(unsigned kind:{VRIPC::kSceneFrame,VRIPC::kUiFrame,VRIPC::kScopeFrame,VRIPC::kContextFrame,0u,5u})for(unsigned mask=0;mask<=4;++mask){auto m=Metadata(kind);
        Check(CompactPanel(mask,m)==(mask==3&&(kind==VRIPC::kUiFrame||kind==VRIPC::kContextFrame)),"only exact UI/context mask3 can use two physical textures");}
    auto invalid=Metadata(VRIPC::kUiFrame);invalid.frameId=0;Check(!CompactPanel(3,invalid),"zero frame cannot authorize compact storage");invalid=Metadata(VRIPC::kUiFrame);invalid.stride+=4;Check(!CompactPanel(3,invalid),"invalid layout cannot authorize compact storage");
    SceneRetirementWait recovery;
    Check(!recovery.Fallback(true,0),"scene retirement timer accepts tick zero");
    for(DWORD now:{500u,1000u,1500u,1999u})Check(!recovery.Fallback(true,now),"normal short scene retirement remains nonblocking GPU wait");
    Check(recovery.Fallback(true,2000),"continuous stalled scene returns to CPU after two seconds");
    Check(!recovery.Fallback(false,60000)&&!recovery.pending,"long UI/loading interval never causes a CPU retirement fallback");
    Check(!recovery.Fallback(true,60001),"resumed scene starts its own two-second grace");
    Check(!recovery.Fallback(true,60700)&&recovery.started==60700,"discontinuous scene attempts restart the recovery interval");
    recovery.Reset();const DWORD first=0xfffffa00u;
    for(DWORD elapsed:{0u,500u,1000u,1500u})Check(!recovery.Fallback(true,first+elapsed),"scene timer remains bounded through tick wrap");
    Check(recovery.Fallback(true,first+2000),"wrapped continuous scene interval recovers at exact two seconds");
    Creation(false);Creation(true);Transition();
    for(unsigned failure:{2u,6u}){Fixture f;auto shared=Shared();f.failShared=failure;DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);{
        GPUFrame::Producer p;p.shared=&shared;D3DSURFACE_DESC desc{};Fixture::Desc(f.back,&desc);
        Check(!p.Create(f.SourceAPI(),desc,true)&&!p.active,"failure before/after aliasing does not publish a partial generation");
      }GetProcessHandleCount(GetCurrentProcess(),&after);Check(before==after,"failure after NT-handle duplication leaks no exported handle");f.Balanced();}
    std::printf("Compact panel producer policy/lifetime: %u checks PASS. No GPU/game opened.\n",checks);
}
