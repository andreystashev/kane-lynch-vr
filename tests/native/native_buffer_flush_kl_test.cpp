#include "native_buffer_flush_kl.hpp"
#include "d3d9_on12_api.hpp"
#include "MinHook.h"
#include <array>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

namespace {
using namespace NativeBufferFlushKL;
unsigned checks=0,failures=0,receivedCount=0,receivedError=0;
using Control=std::array<std::uint32_t,0x4014/4>;
Control* currentControl=nullptr;
std::array<unsigned char,256> gpu{},shadow{};
unsigned nativeCalls=0,lockCalls=0,unlockCalls=0,lockOffset=0,lockBytes=0;
bool held=false,indexResource=false,nullMapping=false,badDescriptor=false;
unsigned createFailureMode=0;DWORD createdUsage=0;unsigned createCalls=0,streamQueries=0,indexQueries=0;
IDirect3DDevice9* fixtureDevice=nullptr;
void* boundVertices[16]{};void* boundIndices=nullptr;
HRESULT lockStatus=S_OK,unlockStatus=S_OK;
void* resourceTable[14]{};struct ResourceStub {void** table=resourceTable;} resource;
bool delay=false,capability=true;HANDLE entered=nullptr,gate=nullptr;
std::array<Range,2048> received{};
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
template<class T>void Put(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void __fastcall FlushOriginal(void* pointer,void*){
    ++nativeCalls;
    receivedError=GetLastError();auto* control=static_cast<std::uint32_t*>(pointer);
    receivedCount=control[0x4010/4];if(receivedCount<2048)std::memcpy(received.data(),control+4,receivedCount*sizeof(Range));
    if(entered){SetEvent(entered);WaitForSingleObject(gate,2000);}if(delay)Sleep(3);
    control[1]=control[0x4010/4]=0;SetLastError(8765);
}
HRESULT __fastcall BeginOriginal(void*,void*){SetLastError(7654);return E_FAIL;}
HRESULT STDMETHODCALLTYPE ResourceLock(void*,UINT offset,UINT bytes,void** output,DWORD flags){
    ++lockCalls;receivedCount=1;receivedError=GetLastError();lockOffset=offset;lockBytes=bytes;
    Check(!held&&!flags&&output==reinterpret_cast<void**>(currentControl->data()+1),"Lock0 uses actual native pending field with no outstanding map");
    if(FAILED(lockStatus)){*output=reinterpret_cast<void*>(1);SetLastError(6000);return lockStatus;}
    held=true;*output=nullMapping?nullptr:gpu.data()+offset;
    if(entered){SetEvent(entered);WaitForSingleObject(gate,2000);}if(delay)Sleep(3);
    SetLastError(6000);return lockStatus;
}
HRESULT STDMETHODCALLTYPE ResourceUnlock(void*){++unlockCalls;
    Check(held&&(*currentControl)[1]==(nullMapping?0:reinterpret_cast<std::uintptr_t>(gpu.data()+lockOffset)),"mapping retained in native pending field through Unlock");
    if(SUCCEEDED(unlockStatus))held=false;SetLastError(8765);return unlockStatus;
}
HRESULT STDMETHODCALLTYPE ResourceDesc(void*,void* output){
    if(indexResource){auto& d=*static_cast<D3DINDEXBUFFER_DESC*>(output);d={D3DFMT_INDEX16,D3DRTYPE_INDEXBUFFER,D3DUSAGE_WRITEONLY|D3DUSAGE_DONOTCLIP,D3DPOOL_DEFAULT,(*currentControl)[2]};}
    else{auto& d=*static_cast<D3DVERTEXBUFFER_DESC*>(output);d={D3DFMT_VERTEXDATA,D3DRTYPE_VERTEXBUFFER,D3DUSAGE_WRITEONLY,D3DPOOL_DEFAULT,(*currentControl)[2],0};}
    SetLastError(7777);return badDescriptor?E_FAIL:S_OK;
}
ULONG STDMETHODCALLTYPE ResourceRelease(void*){return 1;}
ULONG STDMETHODCALLTYPE ResourceAddRef(void*){return 2;}
HRESULT STDMETHODCALLTYPE ResourceDevice(void*,IDirect3DDevice9** out){*out=fixtureDevice;return S_OK;}
HRESULT STDMETHODCALLTYPE NoQuery(IDirect3DDevice9*,D3DQUERYTYPE,IDirect3DQuery9** out){*out=nullptr;return E_NOTIMPL;}
ULONG STDMETHODCALLTYPE DeviceRelease(void*){return 1;}
HRESULT STDMETHODCALLTYPE Caps(IDirect3DDevice9*,D3DCAPS9* out){*out={};out->MaxStreams=16;return S_OK;}
HRESULT STDMETHODCALLTYPE Stream(IDirect3DDevice9*,UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride){
    ++streamQueries;if(stream>=16)return D3DERR_INVALIDCALL;*out=static_cast<IDirect3DVertexBuffer9*>(boundVertices[stream]);*offset=0;*stride=16;return S_OK;}
HRESULT STDMETHODCALLTYPE Indices(IDirect3DDevice9*,IDirect3DIndexBuffer9** out){++indexQueries;*out=static_cast<IDirect3DIndexBuffer9*>(boundIndices);return S_OK;}
HRESULT STDMETHODCALLTYPE CreateVertex(IDirect3DDevice9*,UINT bytes,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){
    ++createCalls;createdUsage=usage;Check(bytes&&fvf==0&&pool==D3DPOOL_DEFAULT&&!shared,"native ctor retains resource size/FVF/pool/share ABI");
    if(createFailureMode&&(usage&D3DUSAGE_DYNAMIC)){
        if(createFailureMode==2)*out=nullptr;SetLastError(6789);return createFailureMode==2?S_OK:E_OUTOFMEMORY;}
    *out=reinterpret_cast<IDirect3DVertexBuffer9*>(&resource);
    SetLastError(6789);return *out?S_OK:E_OUTOFMEMORY;}
void* AllocateShadow(unsigned bytes){Check(bytes!=0,"native ctor allocates its own CPU shadow once");return shadow.data();}
struct Bridge:Device9On12 {
    ULONG references=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{return --references;}
    HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID,void**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnwrapUnderlyingResource(IDirect3DResource9*,ID3D12CommandQueue*,REFIID,void**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReturnUnderlyingResource(IDirect3DResource9*,UINT,UINT64*,ID3D12Fence**)override{return E_NOTIMPL;}
} bridge;
HRESULT STDMETHODCALLTYPE Query(IDirect3DDevice9*,REFIID iid,void** out){
    Check(IsEqualGUID(iid,iid9on12),"official On12 capability only");*out=capability?&bridge:nullptr;
    if(capability)bridge.AddRef();return capability?S_OK:E_NOINTERFACE;
}
struct Image {
    std::uintptr_t base=0;static constexpr unsigned Size=0x800000;
    Image(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,Size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!base)return;
        Put<std::uint16_t>(base,0x5A4D);Put<std::uint32_t>(base+0x3C,0x100);Put<std::uint32_t>(base+0x100,0x4550);
        Put<std::uint16_t>(base+0x104,0x14C);Put<std::uint16_t>(base+0x118,0x10B);Put<std::uint32_t>(base+0x150,Size);
        const unsigned char flush[]={0x83,0xEC,0x08,0x56,0x8B,0xF1,0x5E,0x83,0xC4,0x08,0xB8};
        std::memcpy(reinterpret_cast<void*>(base+FlushRva),flush,sizeof(flush));Put<std::uint32_t>(base+FlushRva+sizeof(flush),reinterpret_cast<std::uintptr_t>(&FlushOriginal));
        Put<std::uint16_t>(base+FlushRva+sizeof(flush)+4,0xE0FF);
        const unsigned char begin[]={0x56,0x8B,0xF1,0x8B,0x46,0x04,0x5E,0xB8};
        std::memcpy(reinterpret_cast<void*>(base+BeginSceneRva),begin,sizeof(begin));Put<std::uint32_t>(base+BeginSceneRva+sizeof(begin),reinterpret_cast<std::uintptr_t>(&BeginOriginal));
        Put<std::uint16_t>(base+BeginSceneRva+sizeof(begin)+4,0xE0FF);
        std::memcpy(reinterpret_cast<void*>(base+StaticVertexCreateRva),StaticVertexCreateCode,sizeof(StaticVertexCreateCode));
        Put<std::uint8_t>(base+0x40DF0,0xB8);Put<std::uint32_t>(base+0x40DF1,reinterpret_cast<std::uintptr_t>(&AllocateShadow));Put<std::uint16_t>(base+0x40DF5,0xE0FF);
        for(unsigned i=0;i<4;++i)Put<std::uint32_t>(base+0x87860+i*4,0x102474FF);
        for(unsigned i=0;i<3;++i)Put<std::uint8_t>(base+0x87870+i,0x90);
        Put<std::uint8_t>(base+0x87873,0xE8);Put<std::uint32_t>(base+0x87874,StaticVertexCreateRva-0x87878);
        Put<std::uint8_t>(base+0x87878,0xC2);Put<std::uint16_t>(base+0x87879,0x10);
        for(unsigned call:{0xB52F3u,0xB5300u,0xB534Du}){Put<std::uint8_t>(base+call,0xE8);
            Put<std::uint32_t>(base+call+1,(call==0xB534D?BeginSceneRva:FlushRva)-call-5);Put<std::uint8_t>(base+call+5,0xC3);}
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base),Size);
    }
    ~Image(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    bool Read(std::uintptr_t at,void* data,std::size_t size){if(at<base||at-base>=Size||size>Size-(at-base))return false;
        std::memcpy(data,reinterpret_cast<void*>(at),size);return true;}
    void Flush(void* control,unsigned call=0xB52F3){reinterpret_cast<void(__thiscall*)(void*)>(base+call)(control);}
    HRESULT Begin(void* control){return reinterpret_cast<HRESULT(__thiscall*)(void*)>(base+0xB534D)(control);}
    void* Create(void* control,void* renderer,unsigned capacity,DWORD usage,unsigned call=0x87860){
        return reinterpret_cast<void*(__thiscall*)(void*,void*,unsigned,DWORD,D3DPOOL)>(base+call)(control,renderer,capacity,usage,D3DPOOL_DEFAULT);}
};
void Prepare(Control& control,const std::vector<Range>& ranges,unsigned capacity=0x2400000){
    currentControl=&control;nativeCalls=lockCalls=unlockCalls=receivedCount=0;held=false;nullMapping=badDescriptor=false;lockStatus=unlockStatus=S_OK;
    indexResource=capacity==0x300000;for(unsigned i=0;i<256;++i){gpu[i]=static_cast<unsigned char>(i);shadow[i]=static_cast<unsigned char>(i^0xA5);}
    resourceTable[11]=reinterpret_cast<void*>(&ResourceLock);resourceTable[12]=reinterpret_cast<void*>(&ResourceUnlock);resourceTable[13]=reinterpret_cast<void*>(&ResourceDesc);
    resourceTable[1]=reinterpret_cast<void*>(&ResourceAddRef);resourceTable[2]=reinterpret_cast<void*>(&ResourceRelease);resourceTable[3]=reinterpret_cast<void*>(&ResourceDevice);
    control.fill(0);control[0]=reinterpret_cast<std::uintptr_t>(&resource);control[1]=control[3]=reinterpret_cast<std::uintptr_t>(shadow.data());control[2]=capacity;control[0x4010/4]=ranges.size();
    std::memcpy(control.data()+4,ranges.data(),ranges.size()*sizeof(Range));
}
void Policy(){
    Range ranges[]={{20,10},{0,20},{5,1},{100,10},{110,5},{75,5}};
    Check(CompactRanges(ranges,6,200)==3,"only consecutive overlap/touch groups merge");
    Check(ranges[0].offset==0&&ranges[0].bytes==30&&ranges[1].offset==100&&ranges[1].bytes==15&&ranges[2].offset==75,"exact union retains group order without sorting");
    for(const Range bad:{Range{1,0},Range{200,1},Range{0,201},Range{0xfffffff0,32}}){
        Range values[]={{0,4},bad};const auto saved=values[0];Check(!CompactRanges(values,2,200)&&!std::memcmp(&saved,values,sizeof(Range)),"invalid input rejected before any mutation");}
    Range one[]={{0,200}};Check(!CompactRanges(one,1,200)&&!CompactRanges(one,2048,200),"single and overflow full-upload modes remain native");
    // Compare the complete destination byte image, including untouched holes,
    // for randomized valid queues: native writes and compacted writes agree.
    std::uint32_t random=17;bool exact=true;for(unsigned trial=0;trial<500;++trial){
        std::array<unsigned char,256> original{},merged{},shadow{};
        for(unsigned i=0;i<256;++i){original[i]=merged[i]=static_cast<unsigned char>(i^0x55);shadow[i]=static_cast<unsigned char>(i+trial);}
        Range queue[32];for(auto& r:queue){random=random*1664525+1013904223;r.offset=random%240;random=random*1664525+1013904223;r.bytes=1+random%16;
            std::memcpy(original.data()+r.offset,shadow.data()+r.offset,r.bytes);}
        const auto count=CompactRanges(queue,32,256);for(unsigned i=0;i<count;++i)std::memcpy(merged.data()+queue[i].offset,shadow.data()+queue[i].offset,queue[i].bytes);
        exact=exact&&count&&original==merged;
    }Check(exact,"500 varied queues preserve every written and untouched byte");
}
struct Pending {Image* image;Control* control;};
DWORD WINAPI Work(void* p){auto& value=*static_cast<Pending*>(p);value.image->Flush(value.control->data());return 0;}
bool VerifyFile(const char* path){
    std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
    if(raw.size()<0x1000)return false;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if(dos->e_lfanew<0||static_cast<std::size_t>(dos->e_lfanew)>raw.size()-sizeof(IMAGE_NT_HEADERS32))return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    std::vector<unsigned char> mapped(nt->OptionalHeader.SizeOfImage);if(nt->OptionalHeader.SizeOfHeaders>raw.size()||nt->OptionalHeader.SizeOfHeaders>mapped.size())return false;
    std::memcpy(mapped.data(),raw.data(),nt->OptionalHeader.SizeOfHeaders);const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=section[i];
        if(std::uint64_t(s.PointerToRawData)+s.SizeOfRawData>raw.size()||std::uint64_t(s.VirtualAddress)+s.SizeOfRawData>mapped.size())return false;
        std::memcpy(mapped.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);}
    const auto base=nt->OptionalHeader.ImageBase;return VerifyImage(base,[&](std::uintptr_t at,void* out,std::size_t bytes){
        if(at<base||at-base>=mapped.size()||bytes>mapped.size()-(at-base))return false;std::memcpy(out,mapped.data()+at-base,bytes);return true;});
}
}
int main(int argc,char** argv){
    Check(SelectBackend(true,true,false)==Backend::On12&&SupportsBatching(Backend::On12),"official On12 capability takes precedence over system runtime provenance");
    Check(SelectBackend(false,true,false)==Backend::Original&&SupportsBatching(Backend::Original),"verified native system implementation supports sparse upload");
    Check(SelectBackend(false,false,true)==Backend::DXVK&&!SupportsBatching(Backend::DXVK),"pinned DXVK is recognized but never receives regressing wide sparse maps");
    Check(SelectBackend(false,false,false)==Backend::Unknown&&!SupportsBatching(Backend::Unknown),"unverified implementations remain native");
    Check(!std::strcmp(BackendName(Backend::Original),"Original")&&!std::strcmp(BackendName(Backend::DXVK),"DXVK")&&
        !std::strcmp(BackendName(Backend::On12),"D3D9On12")&&!std::strcmp(BackendName(Backend::Unknown),"Unknown"),"backend diagnostics have stable names");
    Policy();if(argc>1)Check(VerifyFile(argv[1]),"actual pinned EXE mapped offline verifies exact flush/begin worker calls");
    Image image;Check(image.base!=0,"private executable fixture allocated");if(!image.base)return 1;
    auto reader=[&](auto at,void* data,auto size){return image.Read(at,data,size);};
    Check(VerifyImage(image.base,reader),"exact PE32 prologues/call graph accepted");
    Put<std::uint8_t>(image.base+0xB5300,0x90);Check(!InstallAt(image.base),"changed call rejected before hooks");Put<std::uint8_t>(image.base+0xB5300,0xE8);
    Check(MH_Initialize()==MH_OK&&InstallAt(image.base),"real MinHook hooks install into executed private image");
    Control control;std::uint32_t owner[5]{};owner[3]=reinterpret_cast<std::uintptr_t>(control.data());Put<std::uint32_t>(image.base+0x786670,reinterpret_cast<std::uintptr_t>(owner));
    std::uintptr_t table[120]={reinterpret_cast<std::uintptr_t>(&Query)};auto* device=reinterpret_cast<IDirect3DDevice9*>(&table);
    // A COM object is a pointer to its table, not the table itself.
    auto* tablePointer=table;device=reinterpret_cast<IDirect3DDevice9*>(&tablePointer);fixtureDevice=device;
    table[2]=reinterpret_cast<std::uintptr_t>(&DeviceRelease);table[7]=reinterpret_cast<std::uintptr_t>(&Caps);table[26]=reinterpret_cast<std::uintptr_t>(&CreateVertex);
    table[101]=reinterpret_cast<std::uintptr_t>(&Stream);table[105]=reinterpret_cast<std::uintptr_t>(&Indices);table[118]=reinterpret_cast<std::uintptr_t>(&NoQuery);
    SetDevice(device);
    Check(Snapshot().on12&&Snapshot().backend==Backend::On12&&Snapshot().batchingSupported&&bridge.references==1,"capability/backend retained without leaking COM reference");
    struct Renderer {void* ignored;IDirect3DDevice9* device;} renderer{nullptr,device};
    for(unsigned scenario=0;scenario<6;++scenario){createCalls=0;createdUsage=0;createFailureMode=scenario==4?1:scenario==5?2:0;capability=scenario!=1;
        control[0]=0xDEADBEEF;
        const auto bytes=scenario==2?1024u:0x2400000u;const auto call=scenario==3?StaticVertexCreateRva:0x87860u;
        Check(image.Create(control.data(),&renderer,bytes,D3DUSAGE_WRITEONLY,call)==control.data(),"executed native ctor returns its actual control");
        Check(createdUsage==(scenario==0?D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC:D3DUSAGE_WRITEONLY)&&createCalls==(scenario>=4?2u:1u),
            "only exact On12 static VB gets dynamic backing; failed create retries resource alone with native usage");
        Check(control[3]==reinterpret_cast<std::uintptr_t>(shadow.data())&&control[2]==bytes&&GetLastError()==6789,"constructor shadow capacity and original LastError retained");}
    createFailureMode=0;capability=true;SetDevice(device);
    const std::vector<Range> queue={{0,4},{4,8},{100,4},{102,8}};
    Prepare(control,queue);delay=true;SetLastError(1234);image.Flush(control.data());delay=false;
    Check(receivedCount==1&&lockCalls==1&&unlockCalls==1&&!nativeCalls&&lockOffset==0&&lockBytes==110,"executed worker hook maps one enclosing range through exact COM slots");
    Check(receivedError==1234&&GetLastError()==8765,"native incoming and outgoing LastError preserved");
    Check(control[1]==0&&control[0x4010/4]==0&&!std::memcmp(control.data()+4,queue.data(),queue.size()*sizeof(Range)),"native completion state retained and original range table never modified");
    bool bytesExact=true;for(unsigned i=0;i<256;++i){bool dirty=false;for(auto r:queue)dirty=dirty||(i>=r.offset&&i<r.offset+r.bytes);bytesExact=bytesExact&&gpu[i]==(dirty?shadow[i]:static_cast<unsigned char>(i));}
    Check(bytesExact,"executed production hook copies dirty bytes and preserves all untouched holes");
    auto stats=Snapshot();Check(stats.batchedCalls==1&&stats.singleMapCalls==1&&stats.savedLocks==3&&stats.phases[StaticFlush].maxUs>=2000,"real completed scope times driver work and saved lock counters");
    Event events[EventCapacity];std::uint64_t lost=0;Check(ReadEvents(0,events,EventCapacity,lost)==1&&!lost&&events[0].metadata.submittedRanges==1,"event records original ranges and one actual map");
    for(unsigned scenario=0;scenario<10;++scenario){Prepare(control,queue);ConfigureBatching(true);capability=true;SetDevice(device);unsigned call=0xB52F3;
        if(scenario==0)ConfigureBatching(false);if(scenario==1){capability=false;SetDevice(device);
            Check(Snapshot().backend==Backend::Unknown&&!Snapshot().batchingSupported,"mock unknown wrapper with missing getters cannot qualify as Original");}if(scenario==2)call=FlushRva;
        if(scenario==3)control[1]++;if(scenario==4)owner[3]=0;if(scenario==5)control[2]=1024;
        if(scenario==6)control[5]=0;if(scenario==7){control[0x4010/4]=2048;badDescriptor=true;}if(scenario==8)control[5]=0xffffffff;
        if(scenario==9)badDescriptor=true;
        image.Flush(control.data(),call);
        if(scenario<2)Check(lockCalls==4&&unlockCalls==4&&!nativeCalls,"disabled merging and unknown renderer retain checked native per-range map order");
        else Check(receivedCount==(scenario==7?2048u:4u),"unsupported source, capacity or range keeps original queue");owner[3]=reinterpret_cast<std::uintptr_t>(control.data());}
    capability=true;SetDevice(device);ConfigureBatching(true);
    Prepare(control,queue,0x300000);owner[3]=0;owner[4]=reinterpret_cast<std::uintptr_t>(control.data());image.Flush(control.data(),0xB5300);
    Check(receivedCount==1&&lockCalls==1&&unlockCalls==1&&!nativeCalls,"exact static index worker call shares safe sparse mapping");
    Prepare(control,{{0,4},{100,3}},0x300000);image.Flush(control.data(),0xB5300);Check(nativeCalls==1&&!lockCalls,"odd index bytes preserve native path");owner[3]=owner[4];owner[4]=0;
    Prepare(control,queue);SetEnabled(false);const auto before=Snapshot().events;image.Flush(control.data());Check(receivedCount==1&&Snapshot().events==before,"disabled logging retains batching without adding events");
    Prepare(control,queue);ConfigureBatching(false);image.Flush(control.data());Check(lockCalls==4&&!nativeCalls&&Snapshot().events==before,"disabled diagnostics and merging retain checked native map semantics");SetEnabled(true);ConfigureBatching(true);
    Check(image.Begin(control.data())==E_FAIL&&GetLastError()==7654,"BeginScene HRESULT and LastError passthrough");
    Prepare(control,queue);entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Pending pending{&image,&control};HANDLE worker=CreateThread(nullptr,0,Work,&pending,0,nullptr);Check(WaitForSingleObject(entered,1000)==WAIT_OBJECT_0,"private original entered pending scope");
    stats=Snapshot();Check(stats.active&&stats.inProgress.phase==StaticFlush&&stats.inProgress.startUs&&!stats.inProgress.endUs&&stats.inProgress.metadata.submittedRanges==1,"in-progress slow flush visible before return");
    SetEvent(gate);Check(WaitForSingleObject(worker,1000)==WAIT_OBJECT_0&&!Snapshot().active,"completed flush clears active state");CloseHandle(worker);CloseHandle(entered);CloseHandle(gate);entered=gate=nullptr;
    const auto beforeFaults=Snapshot();
    for(unsigned fault=0;fault<4;++fault){Prepare(control,queue);SetDevice(device);nullMapping=fault==1||fault==3;
        if(fault==0)lockStatus=E_FAIL;if(fault>=2)unlockStatus=E_FAIL;SetLastError(1234);image.Flush(control.data());
        Check(!std::memcmp(control.data()+4,queue.data(),queue.size()*sizeof(Range)),"fault path never rewrites native dirty table");
        if(fault<2){Check(!nativeCalls&&lockCalls==(fault?1u:2u)&&unlockCalls==fault&&control[1]==reinterpret_cast<std::uintptr_t>(shadow.data())&&control[0x4010/4]==queue.size(),"failed/null map preserves native shadow and pending queue without unsafe native memcpy");
            boundVertices[5]=&resource;SetLastError(1234);Check(ShouldBlockDraw(device,false)&&GetLastError()==1234,"failed VB in any bound stream gates draw and preserves LastError");boundVertices[5]=nullptr;
            Check(!ShouldBlockDraw(device,false),"unrelated geometry and UI buffers continue while one upload is pending");
            const auto attempts=lockCalls;SetEnabled(false);ConfigureBatching(false);image.Flush(control.data());Check(lockCalls==attempts&&!nativeCalls,"pending retry backoff stays safe with diagnostics and batching both disabled");SetEnabled(true);ConfigureBatching(true);
            Sleep(260);lockStatus=S_OK;nullMapping=false;image.Flush(control.data());Check(!nativeCalls&&control[1]==0&&control[0x4010/4]==0&&!Snapshot().pendingUploads,"retry completes exact queue and clears resource draw gate");}
        else {Check(!nativeCalls&&lockCalls==1&&unlockCalls==1&&Snapshot().batchingFaulted&&Snapshot().on12,"failed Unlock never retries and blocks batching without misreporting actual On12");
            Prepare(control,queue);SetEnabled(false);ConfigureBatching(false);image.Flush(control.data());Check(!nativeCalls&&!lockCalls,"uncertain Unlock never reenters native map with diagnostics and batching disabled");SetEnabled(true);ConfigureBatching(true);}
    }
    stats=Snapshot();Check(stats.lockFailures-beforeFaults.lockFailures==2&&stats.unlockFailures-beforeFaults.unlockFailures==2&&stats.invalidMappings-beforeFaults.invalidMappings==2,"fault counters distinguish actual Lock attempts, Unlock and null mapping");
    SetDevice(device);Prepare(control,queue);Check(!ShouldBlockDraw(device,true)&&!Snapshot().pendingUploads,"fresh device releases failure identities without any ordinary draw getters");
    const auto streamBefore=streamQueries,indexBefore=indexQueries;Check(!ShouldBlockDraw(device,true)&&streamQueries==streamBefore&&indexQueries==indexBefore,"healthy fast path performs no extra device getters");
    for(unsigned count:{1u,2048u}){Prepare(control,queue);SetDevice(device);control[0x4010/4]=count;lockStatus=E_OUTOFMEMORY;image.Flush(control.data());
        Check(!nativeCalls&&lockCalls==2&&!unlockCalls&&control[0x4010/4]==count&&Snapshot().pendingUploads==1,"single/full-copy failure also preserves queue without native null-copy fallback");}
    SetDevice(device);Check(!Snapshot().batchingFaulted,"fresh device binding re-arms guarded sparse batching");
    capability=false;table[DeviceCapsSlot]=table[DeviceCreationParametersSlot]=table[0];SetDevice(device);
    Check(Snapshot().backend==Backend::Unknown&&!Snapshot().batchingSupported,"coherent getters in an untrusted module cannot impersonate system D3D9");
    capability=true;SetDevice(device);SetDevice(nullptr);
    Check(Snapshot().backend==Backend::Unknown&&!Snapshot().batchingSupported&&!Snapshot().on12,"null device binding clears capability and batching support");
    ConfigureBatching(false);for(unsigned i=0;i<EventCapacity+20;++i){Prepare(control,queue);image.Flush(control.data());}
    const auto retained=ReadEvents(0,events,EventCapacity,lost);Check(retained==EventCapacity&&retained+lost==Snapshot().events,"bounded ring reports overwritten history exactly");
    bool ordered=true;for(unsigned i=1;i<retained;++i)ordered=ordered&&events[i].sequence==events[i-1].sequence+1;Check(ordered,"retained events ordered");
    Check(Shutdown()&&MH_Uninitialize()==MH_OK,"hooks restore private image cleanly");
    std::printf("native_buffer_flush_kl_test: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
