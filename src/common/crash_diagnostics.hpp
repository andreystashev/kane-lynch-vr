#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "diagnostic_session.hpp"

// No render-thread I/O or first-chance handler. A fatal exception is recorded,
// then chained to the application's previous unhandled filter unchanged.
// DbgHelp runs on a prestarted thread; the faulting thread waits at most 3s.
namespace CrashDiagnostics {
namespace Detail {
constexpr unsigned MaxCrumbs=16,MaxModules=192,LogLimit=65536;
struct Crumb {char stage[48]{};std::uint64_t frame=0,context=0;DWORD tick=0;};
struct alignas(64) Slot {volatile LONG owner=0,sequence=0;Crumb crumb;};
struct Module {std::uintptr_t base=0;DWORD size=0;char name[512]{};};
using WriteDump=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                            PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
constexpr MINIDUMP_TYPE DumpType(bool fullMemoryInfo=false){
    return static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|
        (fullMemoryInfo?MiniDumpWithFullMemoryInfo:0));
}
struct State {
    volatile LONG installed=0,crashing=0,closing=0,logBytes=0;
    HANDLE log=INVALID_HANDLE_VALUE,request=nullptr,complete=nullptr,thread=nullptr;
    HMODULE dbghelp=nullptr;WriteDump writeDump=nullptr;LPTOP_LEVEL_EXCEPTION_FILTER previous=nullptr;
    MINIDUMP_TYPE dumpType=DumpType();
    DWORD pid=0,faultThread=0;std::uint64_t creation=0;EXCEPTION_POINTERS* exception=nullptr;
    EXCEPTION_RECORD savedException{};CONTEXT savedContext{};EXCEPTION_POINTERS savedPointers{};
    wchar_t directory[32768]{},path[32768]{},dumpPath[32768]{};
    Module modules[MaxModules]{};unsigned moduleCount=0;Slot crumbs[MaxCrumbs]{};
};
inline State state;
struct Text {
    char bytes[8192]{};DWORD size=0;bool okay=true;
    void Add(char value){if(size+1>=sizeof(bytes)){okay=false;return;}bytes[size++]=value;}
    void Add(const char* value){if(value)while(*value)Add(*value++);}
    void Number(std::uint64_t value){char digits[32];unsigned n=0;do{digits[n++]=char('0'+value%10);value/=10;}while(value);while(n)Add(digits[--n]);}
    void Hex(std::uint64_t value,unsigned digits=0){Add('"');Add("0x");char chars[16];unsigned n=0;do{chars[n++]="0123456789abcdef"[value&15];value>>=4;}while(value);while(n<digits&&n<16)chars[n++]='0';while(n)Add(chars[--n]);Add('"');}
    void Quote(const char* value){Add('"');if(value){for(unsigned i=0;value[i]&&i<1024;++i){const auto c=static_cast<unsigned char>(value[i]);
        if(c=='"'||c=='\\'){Add('\\');Add(char(c));}else if(c<32){Add("\\u00");Add("0123456789abcdef"[c>>4]);Add("0123456789abcdef"[c&15]);}else Add(char(c));}}Add('"');}
};
inline void Write(Text& text){
    if(state.log==INVALID_HANDLE_VALUE)return;
    if(!text.okay)return;
    if(InterlockedExchangeAdd(&state.logBytes,static_cast<LONG>(text.size))+text.size>LogLimit)return;
    DWORD written=0;WriteFile(state.log,text.bytes,text.size,&written,nullptr);FlushFileBuffers(state.log);
}
inline void Identity(Text& text){text.Add(",\"pid\":");text.Number(state.pid);text.Add(",\"processCreationFt\":");text.Number(state.creation);text.Add(",\"tick\":");text.Number(GetTickCount64());}
inline bool Wide(wchar_t* destination,std::size_t count,const std::wstring& source){if(source.size()>=count)return false;std::memcpy(destination,source.c_str(),(source.size()+1)*sizeof(wchar_t));return true;}
inline void CacheModules(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,state.pid);if(snapshot==INVALID_HANDLE_VALUE)return;
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot,&entry)){do{if(state.moduleCount==MaxModules)break;auto& module=state.modules[state.moduleCount++];
        module.base=reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);module.size=entry.modBaseSize;
        const int converted=WideCharToMultiByte(CP_UTF8,0,entry.szModule,-1,module.name,sizeof(module.name),nullptr,nullptr);
        if(!converted){module.name[0]='?';module.name[1]=0;}
    }while(Module32NextW(snapshot,&entry));}CloseHandle(snapshot);
}
inline bool ReadCrumb(const Slot& slot,Crumb& crumb){
    for(unsigned attempt=0;attempt<2;++attempt){const auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0);
        if(!before||(before&1))continue;std::memcpy(&crumb,&slot.crumb,sizeof(crumb));MemoryBarrier();
        if(before==InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0))return true;
    }return false;
}
inline void ExceptionRecord(EXCEPTION_POINTERS* pointers){
    Text text;text.Add("{\"event\":\"exception\"");Identity(text);text.Add(",\"threadId\":");text.Number(GetCurrentThreadId());
    const auto* exception=pointers?pointers->ExceptionRecord:nullptr;
    const auto address=exception?reinterpret_cast<std::uintptr_t>(exception->ExceptionAddress):0;
    text.Add(",\"exceptionCode\":");text.Hex(exception?exception->ExceptionCode:0,8);text.Add(",\"exceptionFlags\":");text.Number(exception?exception->ExceptionFlags:0);
    text.Add(",\"address\":");text.Hex(address,sizeof(void*)*2);text.Add(",\"module\":");
    const Module* owner=nullptr;for(unsigned i=0;i<state.moduleCount;++i){const auto& module=state.modules[i];if(address>=module.base&&address-module.base<module.size){owner=&module;break;}}
    text.Quote(owner?owner->name:"unknown_or_loaded_after_install");text.Add(",\"moduleBase\":");text.Hex(owner?owner->base:0,sizeof(void*)*2);
    text.Add(",\"moduleRva\":");text.Hex(owner?address-owner->base:0);text.Add(",\"parameters\":[");
    if(exception)for(unsigned i=0;i<exception->NumberParameters&&i<EXCEPTION_MAXIMUM_PARAMETERS;++i){if(i)text.Add(',');text.Hex(exception->ExceptionInformation[i]);}
    text.Add("],\"breadcrumbs\":[");bool first=true;
    for(const auto& slot:state.crumbs){Crumb crumb;const auto ownerThread=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.owner),0,0);
        if(!ownerThread||!ReadCrumb(slot,crumb))continue;if(!first)text.Add(',');first=false;
        text.Add("{\"threadId\":");text.Number(static_cast<DWORD>(ownerThread));text.Add(",\"stage\":");text.Quote(crumb.stage);
        text.Add(",\"frame\":");text.Number(crumb.frame);text.Add(",\"context\":");text.Number(crumb.context);text.Add(",\"tick\":");text.Number(crumb.tick);text.Add('}');
    }text.Add("]}\n");Write(text);
}
inline DWORD WINAPI DumpWorker(void*){
    for(;;){if(WaitForSingleObject(state.request,INFINITE)!=WAIT_OBJECT_0)return 0;if(InterlockedCompareExchange(&state.closing,0,0))return 0;
        DWORD error=ERROR_PROC_NOT_FOUND;bool written=false;
        if(state.writeDump){HANDLE file=CreateFileW(state.dumpPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION exception{state.faultThread,state.exception,FALSE};
                written=state.writeDump(GetCurrentProcess(),state.pid,file,state.dumpType,&exception,nullptr,nullptr)!=FALSE;
                error=written?ERROR_SUCCESS:GetLastError();FlushFileBuffers(file);CloseHandle(file);
            }else error=GetLastError();
        }
        Text text;text.Add("{\"event\":\"minidump\"");Identity(text);text.Add(",\"written\":");text.Add(written?"true":"false");text.Add(",\"error\":");text.Number(error);text.Add("}\n");Write(text);
        SetEvent(state.complete);
    }
}
inline LONG WINAPI Filter(EXCEPTION_POINTERS* pointers){
    if(InterlockedCompareExchange(&state.installed,2,1)==1){
        InterlockedExchange(&state.crashing,1);
        ExceptionRecord(pointers);state.faultThread=GetCurrentThreadId();
        // The previous application filter may continue execution after our
        // bounded wait. Never leave the dump worker pointing into that
        // exception frame's temporary stack storage.
        if(pointers&&pointers->ExceptionRecord&&pointers->ContextRecord){
            std::memcpy(&state.savedException,pointers->ExceptionRecord,sizeof(state.savedException));
            state.savedException.ExceptionRecord=nullptr;
            std::memcpy(&state.savedContext,pointers->ContextRecord,sizeof(state.savedContext));
#ifdef CONTEXT_XSTATE
            state.savedContext.ContextFlags&=~DWORD(CONTEXT_XSTATE&0xffffu);
#endif
            state.savedPointers={&state.savedException,&state.savedContext};state.exception=&state.savedPointers;
        }
        MemoryBarrier();
        if(state.thread&&state.request&&state.complete){SetEvent(state.request);const auto waited=WaitForSingleObject(state.complete,3000);
            if(waited!=WAIT_OBJECT_0){Text text;text.Add("{\"event\":\"minidump_timeout\"");Identity(text);text.Add("}\n");Write(text);}}
    }
    const auto previous=state.previous;return previous&&previous!=Filter?previous(pointers):EXCEPTION_CONTINUE_SEARCH;
}
}
inline bool Active(){return InterlockedCompareExchange(&Detail::state.installed,0,0)!=0;}
inline const wchar_t* Path(){return Detail::state.path;}
inline const wchar_t* Directory(){return Detail::state.directory;}
inline const wchar_t* DumpPath(){return Detail::state.dumpPath;}
inline void Breadcrumb(const char* stage,std::uint64_t frame=0,std::uint64_t context=0){
    if(!Active())return;const DWORD thread=GetCurrentThreadId();
    static thread_local Detail::Slot* own=nullptr;
    if(!own){for(auto& slot:Detail::state.crumbs){const auto owner=InterlockedCompareExchange(&slot.owner,static_cast<LONG>(thread),0);
            if(!owner||owner==static_cast<LONG>(thread)){own=&slot;break;}}
        ULONG guarantee=32768;SetThreadStackGuarantee(&guarantee);}
    if(!own)return;
    InterlockedIncrement(&own->sequence);unsigned count=0;if(stage)while(stage[count]&&count+1<sizeof(own->crumb.stage)){own->crumb.stage[count]=stage[count];++count;}
    own->crumb.stage[count]=0;own->crumb.frame=frame;own->crumb.context=context;own->crumb.tick=GetTickCount();MemoryBarrier();InterlockedIncrement(&own->sequence);
}
inline bool Install(const std::wstring& package,const char* version,bool fullMemoryInfo=false){
    using namespace Detail;if(Active())return true;
    const auto directory=DiagnosticsSession::CurrentDirectory(package);if(directory.empty())return false;
    if(!Wide(state.directory,32768,directory)||!Wide(state.path,32768,directory+L"\\crash.jsonl")||!Wide(state.dumpPath,32768,directory+L"\\game.dmp"))return false;
    state.log=CreateFileW(state.path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(state.log==INVALID_HANDLE_VALUE)return false;
    state.pid=GetCurrentProcessId();state.creation=DiagnosticsSession::Creation(GetCurrentProcess());
    state.dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(state.dbghelp)state.writeDump=reinterpret_cast<WriteDump>(GetProcAddress(state.dbghelp,"MiniDumpWriteDump"));
    // Immutable for the lifetime of the prestarted worker. The game can add
    // address-space metadata without changing default host dump flags or
    // requesting a full-memory payload.
    state.dumpType=DumpType(fullMemoryInfo);
    state.request=CreateEventW(nullptr,FALSE,FALSE,nullptr);state.complete=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(state.request&&state.complete)state.thread=CreateThread(nullptr,0,DumpWorker,nullptr,0,nullptr);
    CacheModules();Text text;text.Add("{\"event\":\"crash_session\",\"formatVersion\":1,\"version\":");text.Quote(version);Identity(text);
    text.Add(",\"minidumpAvailable\":");text.Add(state.writeDump&&state.thread?"true":"false");
    text.Add(",\"fullMemoryInfo\":");text.Add(fullMemoryInfo?"true":"false");
    text.Add(",\"dumpFlags\":");text.Number(state.dumpType);
    text.Add(",\"moduleSnapshotCount\":");text.Number(state.moduleCount);text.Add("}\n");Write(text);
    state.previous=SetUnhandledExceptionFilter(Filter);InterlockedExchange(&state.installed,1);Breadcrumb("diagnostics_installed");return true;
}
// Worker-only explicit shutdown; never call from DllMain/process detach.
inline void Close(const char* reason="normal"){
    using namespace Detail;if(InterlockedCompareExchange(&state.installed,0,1)!=1)return;
    const auto replaced=SetUnhandledExceptionFilter(state.previous);if(replaced!=Filter)SetUnhandledExceptionFilter(replaced);
    Text text;text.Add("{\"event\":\"normal_end\",\"reason\":");text.Quote(reason);Identity(text);text.Add("}\n");Write(text);
    InterlockedExchange(&state.closing,1);if(state.request)SetEvent(state.request);
    if(state.thread&&WaitForSingleObject(state.thread,2000)!=WAIT_OBJECT_0)return;
    if(state.thread){CloseHandle(state.thread);state.thread=nullptr;}
    if(state.request){CloseHandle(state.request);state.request=nullptr;}
    if(state.complete){CloseHandle(state.complete);state.complete=nullptr;}
    if(state.log!=INVALID_HANDLE_VALUE){CloseHandle(state.log);state.log=INVALID_HANDLE_VALUE;}
    if(state.dbghelp){FreeLibrary(state.dbghelp);state.dbghelp=nullptr;state.writeDump=nullptr;}
}
}
