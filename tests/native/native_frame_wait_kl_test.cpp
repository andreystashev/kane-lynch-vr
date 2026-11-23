#include "native_frame_wait_kl.hpp"
#include "native_wait_trace_policy.hpp"
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

namespace {
using namespace NativeFrameWaitKL;
using SleepFunction=void(WINAPI*)(DWORD);
using SingleFunction=DWORD(WINAPI*)(HANDLE,DWORD);
using MultipleFunction=DWORD(WINAPI*)(DWORD,const HANDLE*,BOOL,DWORD);
using Stub=DWORD(*)();
unsigned checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);}}
template<class T>void Put(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Original(unsigned api){
    const char* names[ApiCount]={"Sleep","WaitForSingleObject","WaitForMultipleObjects"};
    return reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),names[api]));
}
struct Image {
    std::uintptr_t base=0;static constexpr std::uint32_t Size=0x800000;
    Image(){
        base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,Size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        if(!base)return;
        Put<std::uint16_t>(base,0x5A4D);Put<std::uint32_t>(base+0x3C,0x100);Put<std::uint32_t>(base+0x100,0x4550);
        Put<std::uint16_t>(base+0x104,0x14C);Put<std::uint16_t>(base+0x118,0x10B);Put<std::uint32_t>(base+0x150,Size);
        Put<std::uint32_t>(base+0x180,0x200);Put<std::uint32_t>(base+0x184,40);
        const std::uint32_t descriptor[]={0x400,0,0,0x300,0x532000};std::memcpy(reinterpret_cast<void*>(base+0x200),descriptor,sizeof(descriptor));
        std::memcpy(reinterpret_cast<void*>(base+0x300),"KERNEL32.dll",13);
        for(unsigned i=0;i<=114;++i)Put<std::uint32_t>(base+0x400+4*i,0x80000001);
        const char* names[ApiCount]={"Sleep","WaitForSingleObject","WaitForMultipleObjects"};
        for(unsigned api=0;api<ApiCount;++api){const auto name=0x800+0x100*api;
            std::memcpy(reinterpret_cast<void*>(base+name+2),names[api],std::strlen(names[api])+1);
            Put<std::uint32_t>(base+0x400+(Slots[api]-0x532000),name);Put<std::uint32_t>(base+Slots[api],Original(api));}
        struct Call {std::uint32_t rva;Api api;};
        for(const auto call:{Call{0xB731A,WaitSingleApi},Call{0xB52D3,WaitMultipleApi},Call{0xB47AE,WaitSingleApi},
            Call{0xB7102,WaitSingleApi},Call{0x4D2D8,SleepApi},Call{0x351E70,SleepApi}}){
            Put<std::uint16_t>(base+call.rva,0x15FF);Put<std::uint32_t>(base+call.rva+2,base+Slots[call.api]);
            Put<std::uint8_t>(base+call.rva+6,0xC3);}
    }
    ~Image(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    bool Read(std::uintptr_t at,void* out,std::size_t count)const{
        if(at<base||at-base>=Size||count>Size-(at-base))return false;std::memcpy(out,reinterpret_cast<void*>(at),count);return true;
    }
    auto Reader(){return [&](auto at,void* out,auto count){return Read(at,out,count);};}
    Stub Single(HANDLE object,DWORD timeout,std::uint32_t call=0xB731A){
        Put<std::uint8_t>(base+call-10,0x68);Put<std::uint32_t>(base+call-9,timeout);
        Put<std::uint8_t>(base+call-5,0x68);Put<std::uint32_t>(base+call-4,reinterpret_cast<std::uintptr_t>(object));
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+call-10),17);return reinterpret_cast<Stub>(base+call-10);
    }
    Stub UnknownSingle(HANDLE object,DWORD timeout){
        constexpr std::uint32_t call=0x200000;
        Put<std::uint16_t>(base+call,0x15FF);Put<std::uint32_t>(base+call+2,base+Slots[WaitSingleApi]);
        Put<std::uint8_t>(base+call+6,0xC3);return Single(object,timeout,call);
    }
    Stub Multiple(DWORD count,const HANDLE* objects,BOOL all,DWORD timeout){
        const std::uint32_t args[]={timeout,static_cast<std::uint32_t>(all),reinterpret_cast<std::uintptr_t>(objects),count};
        for(unsigned i=0;i<4;++i){Put<std::uint8_t>(base+0xB52D3-20+5*i,0x68);Put<std::uint32_t>(base+0xB52D3-19+5*i,args[i]);}
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0xB52D3-20),27);return reinterpret_cast<Stub>(base+0xB52D3-20);
    }
    void SleepStub(DWORD timeout){
        Put<std::uint8_t>(base+0x4D2D8-5,0x68);Put<std::uint32_t>(base+0x4D2D8-4,timeout);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0x4D2D8-5),12);reinterpret_cast<Stub>(base+0x4D2D8-5)();
    }
};
void CheckLastEvent(std::uint64_t after,Api api,Site site,DWORD timeout,DWORD result,DWORD error,DWORD count=0,BOOL all=FALSE){
    Event events[4]{};std::uint64_t lost=0;const auto n=ReadEvents(after,events,4,lost);
    Check(n==1&&!lost,"one completed API call produces one event");if(n!=1)return;
    const auto& e=events[0];Check(e.api==api&&e.site==site,"executed native FF15 return RVA classifies exact native wait site");
    Check(e.timeout==timeout&&e.result==result&&e.lastError==error&&e.objectCount==count&&e.waitAll==static_cast<DWORD>(all),"API arguments/result/LastError retained exactly");
    Check(e.thread==GetCurrentThreadId()&&e.startUs<=e.endUs&&e.wallUs==e.endUs-e.startUs,"wall timing and thread belong to actual completed call");
}
struct Pending {Stub call=nullptr;HANDLE entered=nullptr;DWORD result=WAIT_FAILED,error=0;};
DWORD WINAPI PendingThread(void* argument){auto& p=*static_cast<Pending*>(argument);SetEvent(p.entered);SetLastError(9876);p.result=p.call();p.error=GetLastError();return 0;}
void RetentionPolicy(){
    using namespace NativeWaitTracePolicy;
    Event event{};event.site=ProducerCamera;event.api=WaitSingleApi;event.wallUs=1999;
    Check(Select(event)==FrameFast,"named sub-2ms frame wait uses its own bounded lane");
    event.wallUs=2000;Check(Select(event)==FrameSlow,"named 2ms wait reaches protected slow lane");
    event.site=OtherWait;Check(Select(event)==Auxiliary,"unknown 2ms wait remains detailed");
    event.wallUs=1999;Check(Select(event)==Filtered,"fast unknown polling is omitted from detailed ring only");
    event.result=WAIT_FAILED;Check(Select(event)==Auxiliary,"fast failed unknown wait stays detailed");
    event.result=WAIT_OBJECT_0;event.api=SleepApi;event.site=SystemTimerSleep;event.wallUs=0;
    Check(Select(event)==Auxiliary,"pinned timer caller is retained even when immediate");
    Ring trace;event.api=WaitSingleApi;event.site=ProducerCamera;event.wallUs=500000;
    for(unsigned i=0;i<20;++i){event.callerRva=i+1;Check(trace.Push(event),"slow frame wait accepted");}
    const auto before=trace.Total();event.site=OtherWait;event.wallUs=1;
    bool omitted=true;for(unsigned i=0;i<20000;++i)omitted=omitted&&!trace.Push(event);
    Check(omitted&&trace.Total()==before,"twenty thousand quick polls cannot displace protected frame waits or consume event IDs");
    Event output[Capacity]{};std::uint64_t lost=0;
    Check(trace.CopySince(0,output,Capacity,lost)==20&&!lost,"all earlier long frame waits survive unknown polling flood");
    event.wallUs=3000;
    for(unsigned i=0;i<5000;++i)trace.Push(event);
    const auto afterAux=trace.CopySince(0,output,Capacity,lost);
    bool frameRetained=afterAux==20+LaneCapacity[Auxiliary];
    for(unsigned i=0;i<20&&frameRetained;++i)frameRetained=output[i].site==ProducerCamera&&output[i].callerRva==i+1;
    Check(frameRetained&&lost==trace.Total()-afterAux,"unknown slow flood loses only its own lane with exact missing event count");
    event.site=WorkerDispatch;event.wallUs=100;
    for(unsigned i=0;i<500;++i)trace.Push(event);
    const auto afterFast=trace.CopySince(0,output,Capacity,lost);
    Check(afterFast==20+LaneCapacity[FrameFast]+LaneCapacity[Auxiliary]&&output[0].callerRva==1,
        "short frame-wait flood cannot evict older half-second producer waits");
    event.site=RenderJoin;event.wallUs=40000;
    for(unsigned i=0;i<300;++i)trace.Push(event);
    const auto filled=trace.CopySince(0,output,Capacity,lost);
    Check(filled==Capacity&&lost==trace.Total()-filled,"all priority lanes have the original 256-event total budget");
    bool ordered=true;for(std::size_t i=1;i<filled;++i)ordered=ordered&&output[i-1].sequence<output[i].sequence;
    Check(ordered,"merged retention lanes are returned in strict global sequence order");
    std::uint64_t cursor=0,totalLost=0,totalRead=0;
    for(;;){const auto count=trace.CopySince(cursor,output,7,lost);totalLost+=lost;if(!count)break;
        totalRead+=count;cursor=output[count-1].sequence;}
    Check(totalRead==Capacity&&totalRead+totalLost==trace.Total()&&cursor==trace.Total(),
        "small diagnostic reads count every eviction exactly once and drain without duplicates");
    Check(!trace.CopySince(0,nullptr,5,lost)&&!lost,"null reader cannot consume retained events or claim gaps");
}
void ActiveCapacity(Image& image,HANDLE gate){
    ResetEvent(gate);const auto call=image.Single(gate,INFINITE);
    const auto before=Snapshot().activeUntracked;
    Pending pending[ActiveWaitCapacity+1]{};HANDLE threads[ActiveWaitCapacity+1]{};
    bool created=true;
    for(unsigned i=0;i<ActiveWaitCapacity+1;++i){pending[i]={call,CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        threads[i]=CreateThread(nullptr,0,PendingThread,&pending[i],0,nullptr);created=created&&threads[i]&&pending[i].entered;}
    Check(created,"private wait-stress workers created");
    ActiveWait waiting[ActiveWaitCapacity]{};const auto started=GetTickCount();std::size_t count=0;
    do{count=ReadActiveWaits(waiting,ActiveWaitCapacity);
        if(count==ActiveWaitCapacity&&Snapshot().activeUntracked>before)break;Sleep(1);
    }while(GetTickCount()-started<1000);
    Check(count==ActiveWaitCapacity&&Snapshot().activeUntracked==before+1,
        "active table preserves sixteen concurrent waits and explicitly counts overflow without blocking native calls");
    bool exact=true;for(std::size_t i=0;i<count;++i)exact=exact&&waiting[i].event.site==ProducerCamera&&
        waiting[i].event.api==WaitSingleApi&&waiting[i].event.timeout==INFINITE&&waiting[i].event.objectCount==1&&
        waiting[i].event.startUs&&waiting[i].event.thread&&!waiting[i].event.endUs;
    Check(exact,"in-progress records expose original caller, thread and arguments before completion");
    SetEvent(gate);const auto waited=created?WaitForMultipleObjects(ActiveWaitCapacity+1,threads,TRUE,2000):WAIT_FAILED;
    Check(waited==WAIT_OBJECT_0,"all native calls finish normally despite diagnostic active-table overflow");
    bool unchanged=true;for(unsigned i=0;i<ActiveWaitCapacity+1;++i){unchanged=unchanged&&pending[i].result==WAIT_OBJECT_0&&pending[i].error==9876;
        if(threads[i])CloseHandle(threads[i]);if(pending[i].entered)CloseHandle(pending[i].entered);}
    Check(unchanged&&!ReadActiveWaits(waiting,ActiveWaitCapacity),"completion releases active slots and retains native result/LastError for every worker");
}
bool VerifyPinnedFile(const char* path){
    std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
    if(raw.size()<0x1000)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||
        static_cast<std::size_t>(dos->e_lfanew)>raw.size()-sizeof(IMAGE_NT_HEADERS32))return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    std::vector<unsigned char> image(nt->OptionalHeader.SizeOfImage);
    if(nt->OptionalHeader.SizeOfHeaders>raw.size()||nt->OptionalHeader.SizeOfHeaders>image.size())return false;
    std::memcpy(image.data(),raw.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        const auto& s=section[i];if(std::uint64_t(s.PointerToRawData)+s.SizeOfRawData>raw.size()||std::uint64_t(s.VirtualAddress)+s.SizeOfRawData>image.size())return false;
        std::memcpy(image.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    const auto base=nt->OptionalHeader.ImageBase;
    const auto read=[&](std::uintptr_t at,void* output,std::size_t count){
        if(at<base||at-base>=image.size()||count>image.size()-(at-base))return false;
        std::memcpy(output,image.data()+at-base,count);return true;
    };
    return VerifyImage(base,read);
}
}
int main(int argc,char** argv){
    using namespace NativeFrameWaitKL;
    RetentionPolicy();
    Check(Classify(WaitSingleApi,0xB7320)==ProducerCamera&&Classify(WaitMultipleApi,0xB52D9)==WorkerDispatch,"producer single and worker multiple are distinct");
    Check(Classify(WaitSingleApi,0xB47B4)==RenderJoin&&Classify(WaitSingleApi,0xB7108)==WorkerStart,"join/start sites are pinned");
    Check(Classify(SleepApi,0x4D2DE)==SystemTimerSleep&&Classify(SleepApi,0x351E76)==TaskPollSleep,"known Sleep100 sites are distinct");
    Check(Classify(WaitSingleApi,0xB52D9)==OtherWait&&Classify(SleepApi,0)==OtherSleep,"mismatched API or external callers never impersonate known site");
    Event dispatch{};dispatch.site=WorkerDispatch;dispatch.objectCount=2;dispatch.handle0=123;dispatch.handle1=124;
    dispatch.startUs=1000;dispatch.endUs=101000;
    RenderSignal drawStart{};drawStart.handle=123;drawStart.thread=12;drawStart.result=TRUE;drawStart.startUs=100900;
    Check(MatchesDrawStart(dispatch,drawStart),"producer signal with exact handle belongs to the slow dispatch wait");
    dispatch.result=WAIT_OBJECT_0+1;
    Check(!MatchesDrawStart(dispatch,drawStart),"worker shutdown cannot be attributed to a producer signal");
    dispatch.result=WAIT_OBJECT_0;drawStart.startUs=999;
    Check(!MatchesDrawStart(dispatch,drawStart),"a previous producer signal cannot explain a new wait");
    Image image;Check(image.base!=0,"allocate private fake PE");if(!image.base)return 1;
    std::uint32_t size=0;Check(VerifyImage(image.base,image.Reader(),&size)&&size==Image::Size,"complete pinned import/call proof accepts fake mapped PE");
    const auto callOperand=*reinterpret_cast<std::uint32_t*>(image.base+0xB731C);
    Put<std::uint32_t>(image.base+0xB731C,callOperand+4);Check(!VerifyImage(image.base,image.Reader()),"changed FF15 operand rejected");Put(image.base+0xB731C,callOperand);
    Put<std::uint32_t>(image.base+0x400+4*40,0x80000999);Check(!VerifyImage(image.base,image.Reader()),"ordinal substituted for Sleep identity rejected");Put<std::uint32_t>(image.base+0x400+4*40,0x800);
    const auto single=Original(WaitSingleApi);Put<std::uint32_t>(image.base+Slots[WaitSingleApi],single+1);
    Check(!InstallAt(image.base),"already hooked target rejected before mutations");
    Check(*reinterpret_cast<std::uint32_t*>(image.base+Slots[SleepApi])==Original(SleepApi),"preflight rejection leaves earlier slot unchanged");Put<std::uint32_t>(image.base+Slots[WaitSingleApi],single);
    DWORD oldProtection=0;Check(VirtualProtect(reinterpret_cast<void*>(image.base+0x532000),4096,PAGE_READONLY,&oldProtection)!=FALSE,"private IAT made read-only like loader imports");
    Check(InstallAt(image.base),"verified private IAT transaction installs");Check(InstallAt(image.base),"same installed image is idempotent");
    MEMORY_BASIC_INFORMATION region{};VirtualQuery(reinterpret_cast<void*>(image.base+Slots[SleepApi]),&region,sizeof(region));
    Check(region.Protect==PAGE_READONLY,"transaction restores original IAT page protection");
    DWORD ignoredProtection=0;VirtualProtect(reinterpret_cast<void*>(image.base+0x532000),4096,oldProtection,&ignoredProtection);
    auto stats=Snapshot();Check(stats.installed&&stats.enabled&&stats.verified,"snapshot exposes install/diagnostic state");
    HANDLE first=CreateEventW(nullptr,TRUE,FALSE,nullptr),second=CreateEventW(nullptr,TRUE,TRUE,nullptr);const HANDLE objects[]={first,second};
    Check(first&&second,"private real event handles created");
    const auto direct=reinterpret_cast<SingleFunction>(single);
    SetLastError(123456);const auto baseline=direct(first,0);const auto baselineError=GetLastError();auto after=Snapshot().events;
    SetLastError(123456);const auto actual=image.Single(first,0)();const auto error=GetLastError();
    Check(actual==baseline&&error==baselineError,"timeout result and LastError match native API");CheckLastEvent(after,WaitSingleApi,ProducerCamera,0,actual,error,1);
    SetEvent(first);after=Snapshot().events;SetLastError(99);const auto signaled=image.Single(first,INFINITE,0xB47AE)();const auto signaledError=GetLastError();
    Check(signaled==WAIT_OBJECT_0,"INFINITE signaled wait remains immediate");CheckLastEvent(after,WaitSingleApi,RenderJoin,INFINITE,signaled,signaledError,1);
    after=Snapshot().events;SetLastError(55);const auto failed=image.Single(reinterpret_cast<HANDLE>(0x12345),0,0xB7102)();const auto failedError=GetLastError();
    Check(failed==WAIT_FAILED&&failedError==ERROR_INVALID_HANDLE,"failed API error remains visible to caller");CheckLastEvent(after,WaitSingleApi,WorkerStart,0,failed,failedError,1);
    ResetEvent(first);after=Snapshot().events;SetLastError(456);const auto any=image.Multiple(2,objects,FALSE,0)();const auto anyError=GetLastError();
    Check(any==WAIT_OBJECT_0+1,"wait-any retains second-object index");CheckLastEvent(after,WaitMultipleApi,WorkerDispatch,0,any,anyError,2,FALSE);
    after=Snapshot().events;const auto all=image.Multiple(2,objects,TRUE,1)();const auto allError=GetLastError();
    Check(all==WAIT_TIMEOUT,"wait-all retains unsignaled first object");CheckLastEvent(after,WaitMultipleApi,WorkerDispatch,1,all,allError,2,TRUE);
    SetEvent(first);after=Snapshot().events;const auto both=image.Multiple(2,objects,TRUE,INFINITE)();const auto bothError=GetLastError();
    Check(both==WAIT_OBJECT_0,"wait-all returns success when both signaled");CheckLastEvent(after,WaitMultipleApi,WorkerDispatch,INFINITE,both,bothError,2,TRUE);
    SetLastError(789);reinterpret_cast<SleepFunction>(Original(SleepApi))(0);const auto sleepError=GetLastError();after=Snapshot().events;
    SetLastError(789);image.SleepStub(0);Check(GetLastError()==sleepError,"Sleep preserves native LastError behavior");CheckLastEvent(after,SleepApi,SystemTimerSleep,0,0,sleepError);
    const auto unknownBefore=Snapshot();SetLastError(54321);const auto unknown=image.UnknownSingle(first,0)();const auto unknownError=GetLastError();
    const auto unknownAfter=Snapshot();
    Check(unknown==WAIT_OBJECT_0&&unknownError==54321,"unknown polling callers retain native API result and LastError");
    Check(unknownAfter.sites[OtherWait].calls==unknownBefore.sites[OtherWait].calls+1&&
        unknownAfter.events-unknownBefore.events+unknownAfter.filteredFastEvents-unknownBefore.filteredFastEvents+
        unknownAfter.droppedEvents-unknownBefore.droppedEvents==1,"filtered polling still increments complete site counters with explicitly accounted detail disposition");
    after=Snapshot().events;const auto unknownFailed=image.UnknownSingle(reinterpret_cast<HANDLE>(0x12345),0)();const auto unknownFailedError=GetLastError();
    CheckLastEvent(after,WaitSingleApi,OtherWait,0,unknownFailed,unknownFailedError,1);
    after=Snapshot().events;SetEnabled(false);image.SleepStub(0);image.Single(first,0)();Check(Snapshot().events==after,"disabled wrappers call originals without events");SetEnabled(true);
    for(unsigned i=0;i<300;++i)image.SleepStub(0);
    Event events[256]{};std::uint64_t lost=0;const auto count=ReadEvents(0,events,256,lost);
    Check(count<256&&count>=NativeWaitTracePolicy::LaneCapacity[NativeWaitTracePolicy::Auxiliary]&&lost==Snapshot().events-count,"bounded priority ring reports exact lane evictions");
    bool ordered=true;for(unsigned i=1;i<count;++i)ordered=ordered&&events[i].sequence>events[i-1].sequence;Check(ordered,"retained events merge in increasing sequence order despite lane gaps");
    std::uint64_t ignored=123;Check(ReadEvents(events[count-1].sequence,events,256,ignored)==0&&!ignored,"reader cursor deduplicates drained events");
    ActiveCapacity(image,first);
    ResetEvent(first);Pending pending{image.Single(first,INFINITE),CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    HANDLE thread=CreateThread(nullptr,0,PendingThread,&pending,0,nullptr);WaitForSingleObject(pending.entered,1000);Sleep(25);
    ActiveWait active[ActiveWaitCapacity]{};auto activeCount=ReadActiveWaits(active,ActiveWaitCapacity);
    Check(activeCount==1&&active[0].event.thread==GetThreadId(thread)&&active[0].event.callerRva==0xB7320,
        "active slots are reusable after prior concurrent workers finish");
    after=Snapshot().events;Check(Shutdown(),"owned IAT slots restored while an original wait is pending");
    Check(*reinterpret_cast<std::uint32_t*>(image.base+Slots[WaitSingleApi])==single,"Shutdown restores original single-object import");
    Check(WaitForSingleObject(thread,0)==WAIT_TIMEOUT,"Shutdown never cancels or completes pending game wait");
    Check(ReadActiveWaits(active,ActiveWaitCapacity)==1,"shutdown retains the descriptor of an already executing original wait");
    SetEvent(first);Check(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0&&pending.result==WAIT_OBJECT_0,"in-flight wrapper safely completes using preserved original/state");
    Check(pending.error==9876,"in-flight wait preserves caller error through shutdown");Check(Snapshot().events==after+1,"completion begun while enabled remains traceable after disable");
    Check(!ReadActiveWaits(active,ActiveWaitCapacity),"completed post-shutdown wait is no longer reported active");
    CloseHandle(thread);CloseHandle(pending.entered);
    Check(InstallAt(image.base),"verified restored imports can be reinstalled");
    Put<std::uint32_t>(image.base+Slots[WaitSingleApi],single+1);Check(!Shutdown(),"foreign IAT replacement makes ownership-safe restore report failure");
    Check(*reinterpret_cast<std::uint32_t*>(image.base+Slots[WaitSingleApi])==single+1,"Shutdown does not overwrite a foreign replacement");Put<std::uint32_t>(image.base+Slots[WaitSingleApi],single);
    Check(*reinterpret_cast<std::uint32_t*>(image.base+Slots[SleepApi])==Original(SleepApi),"other owned imports restored despite foreign slot");
    SetEnabled(true);Check(!Snapshot().enabled,"diagnostics cannot re-enable without installed imports");
    CloseHandle(first);CloseHandle(second);
    if(argc>1)Check(VerifyPinnedFile(argv[1]),"actual pinned EXE PE import and call proof");
    std::printf("native frame wait: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
