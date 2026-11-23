#pragma once
// Diagnostic-only creation/reset/final-release on a hidden input-desktop window.
// Rendering stays on the game's private desktop. No desktop switch or input API.
#include <windows.h>
#include <d3d9.h>
#include <memory>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdio>
namespace BackgroundRendererBroker {
using CreateFn=HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using ResetFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using ReleaseFn=ULONG(WINAPI*)(IDirect3DDevice9*);
struct Request {HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::function<void()> work;const char* label="unknown";~Request(){if(done)CloseHandle(done);}};
inline SRWLOCK queueLock=SRWLOCK_INIT,recordLock=SRWLOCK_INIT;
inline std::deque<std::shared_ptr<Request>> queue;
inline HANDLE ready=nullptr,thread=nullptr;inline DWORD threadId=0;inline HWND window=nullptr;
struct Record {void** table=nullptr;ResetFn reset=nullptr;ReleaseFn release=nullptr;};
inline std::vector<Record> records;inline std::vector<IDirect3DDevice9*> owned;
inline std::atomic<unsigned> creates{0},resets{0},releases{0},finalReleases{0};
inline std::atomic<unsigned long long> releaseTicks{0};
inline std::atomic<DWORD> lastFinalReleaseThread{0};
inline bool immediate=false;
inline void (*logger)(const char*)=nullptr;inline std::atomic<const char*> activeOperation{nullptr};
inline void Drain(){for(;;){std::shared_ptr<Request> r;AcquireSRWLockExclusive(&queueLock);if(!queue.empty()){r=queue.front();queue.pop_front();}ReleaseSRWLockExclusive(&queueLock);if(!r)return;const auto previous=activeOperation.load();activeOperation=r->label;r->work();activeOperation=previous;SetEvent(r->done);}}
inline LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM a,LPARAM b){if(m==WM_APP+71){Drain();return 0;}return DefWindowProcW(w,m,a,b);}
inline DWORD WINAPI Main(void*){
 ULONG stackGuarantee=65536;SetThreadStackGuarantee(&stackGuarantee);
 HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS);
 if(input&&SetThreadDesktop(input)){
  WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"KL1-Background-Render-Broker";RegisterClassW(&cls);
  window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"KL1 background rendering",WS_POPUP,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
 }SetEvent(ready);if(!window)return 1;
 MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
inline BOOL CALLBACK Initialize(PINIT_ONCE,PVOID,PVOID*){ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready)return FALSE;thread=CreateThread(nullptr,2*1024*1024,Main,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,&threadId);return thread&&WaitForSingleObject(ready,10000)==WAIT_OBJECT_0&&window;}
inline bool Start(){
 static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
 return InitOnceExecuteOnce(&once,Initialize,nullptr,nullptr)!=FALSE;
}
inline bool Call(std::function<void()> work,const char* label="unknown"){
 if(GetCurrentThreadId()==threadId){work();return true;}if(!Start())return false;
 auto r=std::make_shared<Request>();if(!r->done)return false;r->work=std::move(work);r->label=label;
 AcquireSRWLockExclusive(&queueLock);queue.push_back(r);ReleaseSRWLockExclusive(&queueLock);
 bool completed=false;const auto began=GetTickCount64();DWORD_PTR messageResult=0;
 // A sent message permits nested Direct3D window callbacks to reach the caller.
 // A posted message can deadlock that cycle.
 const bool posted=SendMessageTimeoutW(window,WM_APP+71,0,0,SMTO_NORMAL,15000,&messageResult)!=0;
 while(posted&&GetTickCount64()-began<15000){
  const auto remaining=DWORD(15000-(GetTickCount64()-began));
  const auto wait=MsgWaitForMultipleObjects(1,&r->done,FALSE,remaining,QS_SENDMESSAGE);
  if(wait==WAIT_OBJECT_0){completed=true;break;}
  if(wait==WAIT_OBJECT_0+1){MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);continue;}
  break;
 }
 if(!completed){
  if(logger){char text[256];const auto active=activeOperation.load();snprintf(text,sizeof(text),"renderer broker timeout: waiting=%s active=%s caller=%lu broker=%lu",label,active?active:"none",GetCurrentThreadId(),threadId);logger(text);}
  if(logger){HANDLE observed=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,threadId);if(observed){const DWORD prior=SuspendThread(observed);CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;const bool read=prior!=DWORD(-1)&&GetThreadContext(observed,&context);if(prior!=DWORD(-1))ResumeThread(observed);CloseHandle(observed);char text[384];snprintf(text,sizeof(text),"broker thread snapshot: priorSuspend=%lu read=%u eip=%08lx esp=%08lx ebp=%08lx ecx=%08lx",prior,unsigned(read),context.Eip,context.Esp,context.Ebp,context.Ecx);logger(text);}}
  // A pending driver operation cannot safely outlive its caller arguments.
  // Only this diagnostic process is stopped; the runner owns its job.
  TerminateProcess(GetCurrentProcess(),0xe00b9071);return false;
 }return true;
}
inline bool Patch(void** slot,void* fn){DWORD old=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;InterlockedExchangePointer(slot,fn);DWORD unused=0;return VirtualProtect(slot,sizeof(void*),old,&unused)!=FALSE;}
inline Record Find(IDirect3DDevice9* device,bool* ours=nullptr){
 auto** table=*reinterpret_cast<void***>(device);Record out;
 AcquireSRWLockShared(&recordLock);for(const auto& r:records)if(r.table==table){out=r;break;}
 if(ours){*ours=false;for(const auto p:owned)if(p==device){*ours=true;break;}}ReleaseSRWLockShared(&recordLock);return out;
}
inline HRESULT WINAPI Reset(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* parameters){
 bool ours=false;const auto record=Find(device,&ours);if(!record.reset)return D3DERR_INVALIDCALL;if(!ours)return record.reset(device,parameters);
 HRESULT result=E_FAIL;Call([&](){auto p=*parameters;p.Windowed=TRUE;p.FullScreen_RefreshRateInHz=0;p.hDeviceWindow=window;if(immediate)p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;result=record.reset(device,&p);if(SUCCEEDED(result))*parameters=p;++resets;},"reset");return result;
}
inline ULONG WINAPI Release(IDirect3DDevice9* device){
 bool ours=false;const auto record=Find(device,&ours);if(!record.release)return 0;if(!ours)return record.release(device);
 LARGE_INTEGER began{},end{};QueryPerformanceCounter(&began);
 // Track owns one guard reference. Ordinary Release cannot destroy the device
 // and is legal on the multithreaded render device. When only the guard remains,
 // no legitimate caller owns a reference from which to start another operation.
 // Drop that final guard on the focus-window thread, as Direct3D requires.
 ULONG count=record.release(device);++releases;
 if(count==1){
  Call([&](){AcquireSRWLockExclusive(&recordLock);for(auto i=owned.begin();i!=owned.end();++i)if(*i==device){owned.erase(i);break;}ReleaseSRWLockExclusive(&recordLock);
   lastFinalReleaseThread=GetCurrentThreadId();count=record.release(device);++finalReleases;
   if(count){if(logger)logger("renderer broker guard release left unexpected references");TerminateProcess(GetCurrentProcess(),0xe00b9072);}
  },"device_final_release");
 }else if(count>1){--count;}
 else{if(logger)logger("renderer broker guard reference was lost");TerminateProcess(GetCurrentProcess(),0xe00b9072);}
 QueryPerformanceCounter(&end);releaseTicks.fetch_add(end.QuadPart-began.QuadPart,std::memory_order_relaxed);return count;
}
inline bool Track(IDirect3DDevice9* device){
 auto** table=*reinterpret_cast<void***>(device);AcquireSRWLockExclusive(&recordLock);
 bool known=false;for(const auto& r:records)if(r.table==table)known=true;
 if(!known){Record record{table,reinterpret_cast<ResetFn>(table[16]),reinterpret_cast<ReleaseFn>(table[2])};records.push_back(record);
  if(!Patch(&table[16],reinterpret_cast<void*>(&Reset))||!Patch(&table[2],reinterpret_cast<void*>(&Release))){ReleaseSRWLockExclusive(&recordLock);return false;}}
 device->AddRef();owned.push_back(device);ReleaseSRWLockExclusive(&recordLock);return true;
}
inline IDirect3D9* Api(IDirect3D9*(WINAPI*fn)(UINT),UINT version){
 IDirect3D9* out=nullptr;Call([&](){out=fn(version);},"factory");return out;
}
inline HRESULT Create(CreateFn fn,IDirect3D9* api,UINT adapter,D3DDEVTYPE type,DWORD flags,D3DPRESENT_PARAMETERS* parameters,IDirect3DDevice9** out){
 if(!parameters||!out)return D3DERR_INVALIDCALL;*out=nullptr;HRESULT result=E_FAIL;
 Call([&](){auto p=*parameters;p.Windowed=TRUE;p.FullScreen_RefreshRateInHz=0;p.hDeviceWindow=window;
  SetWindowPos(window,nullptr,0,0,int(p.BackBufferWidth),int(p.BackBufferHeight),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOMOVE);
  result=fn(api,adapter,type,window,flags|D3DCREATE_NOWINDOWCHANGES|D3DCREATE_MULTITHREADED,&p,out);
  if(SUCCEEDED(result)&&*out){if(!Track(*out)){(*out)->Release();*out=nullptr;result=E_FAIL;}else *parameters=p;}++creates;
 },"device_create");return result;
}
}
