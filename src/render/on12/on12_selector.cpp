// Owned x86 selector for the Windows D3D9On12 implementation.
#define WIN32_LEAN_AND_MEAN
#include "d3d9_on12_api.hpp"
#include "on12_memory_policy.hpp"
#include <cstdio>
#include <d3d9.h>
#include <string>
#include <windows.h>
bool DiagnosticLogging() {
  wchar_t enabled[8]{};
  return GetEnvironmentVariableW(L"KL2_VR_RENDERER_LOG",enabled,8)==1&&enabled[0]==L'1';
}
HMODULE Native() {
  static HMODULE library = []() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const auto p = std::wstring(system) + L"\\d3d9.dll";
    return LoadLibraryExW(p.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }();
  return library;
}
void Log(const char *what, void *pointer) {
  if(!DiagnosticLogging())return;
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  auto file = _wfopen((std::wstring(path) + L".on12-probe.log").c_str(), L"ab");
  if (file) {
    std::fprintf(file,
                 "pid=%lu tick=%lu %s=%p; Windows D3D9On12 selector\n",
                 GetCurrentProcessId(), GetTickCount(), what, pointer);
    std::fclose(file);
  }
}
void PolicyLog(const char* event,const On12Memory::Compat* value) {
  if(!DiagnosticLogging())return;
  wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  auto* f=_wfopen((std::wstring(path)+L".on12-probe.log").c_str(),L"ab");
  if(f){std::fprintf(f,"pid=%lu tick=%lu memory_policy=%s uploadBytes=%lu trimFences=%lu\n",
    GetCurrentProcessId(),GetTickCount(),event,value?value->uploadBytes:0,value?value->trimFences:0);std::fclose(f);}
}
#ifndef KL2_ON12_MEMORY_POLICY
#define KL2_ON12_MEMORY_POLICY 1
#endif
void Policy() {
#if KL2_ON12_MEMORY_POLICY
  static const bool applied=On12Memory::Install(PolicyLog);(void)applied;
#else
  // Explicit stock comparison only. The old0611 policy could not solve the
  // independent Steam integrity/native heap failures fixed subsequently.
  static const bool logged=[](){PolicyLog("disabled_research",nullptr);return true;}();(void)logged;
#endif
}
extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk) {
  Policy();
  auto create =
      reinterpret_cast<IDirect3D9 *(WINAPI *)(UINT, Args9On12 *, UINT)>(
          GetProcAddress(Native(), "Direct3DCreate9On12"));
  Args9On12 args{};
  args.enable = TRUE;
  auto *result = create ? create(sdk, &args, 1) : nullptr;
  Log("Direct3DCreate9On12", result);
  return result;
}
extern "C" HRESULT WINAPI
Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **result) {
  Policy();
  auto create = reinterpret_cast<HRESULT(WINAPI *)(UINT, Args9On12 *, UINT,
                                                   IDirect3D9Ex **)>(
      GetProcAddress(Native(), "Direct3DCreate9On12Ex"));
  Args9On12 args{};
  args.enable = TRUE;
  return create ? create(sdk, &args, 1, result) : E_NOINTERFACE;
}
extern "C" int WINAPI
D3DPERF_BeginEvent(D3DCOLOR color, const WCHAR *name) {
  auto f = reinterpret_cast<int(WINAPI *)(D3DCOLOR, const WCHAR *)>(
      GetProcAddress(Native(), "D3DPERF_BeginEvent"));
  return f ? f(color, name) : 0;
}
extern "C" int WINAPI D3DPERF_EndEvent() {
  auto f = reinterpret_cast<int(WINAPI *)()>(
      GetProcAddress(Native(), "D3DPERF_EndEvent"));
  return f ? f() : 0;
}
extern "C" DWORD WINAPI D3DPERF_GetStatus() {
  auto f = reinterpret_cast<DWORD(WINAPI *)()>(
      GetProcAddress(Native(), "D3DPERF_GetStatus"));
  return f ? f() : 0;
}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() {
  auto f = reinterpret_cast<BOOL(WINAPI *)()>(
      GetProcAddress(Native(), "D3DPERF_QueryRepeatFrame"));
  return f ? f() : FALSE;
}
extern "C" void WINAPI
D3DPERF_SetMarker(D3DCOLOR color, const WCHAR *name) {
  auto f = reinterpret_cast<void(WINAPI *)(D3DCOLOR, const WCHAR *)>(
      GetProcAddress(Native(), "D3DPERF_SetMarker"));
  if (f)
    f(color, name);
}
extern "C" void WINAPI
D3DPERF_SetRegion(D3DCOLOR color, const WCHAR *name) {
  auto f = reinterpret_cast<void(WINAPI *)(D3DCOLOR, const WCHAR *)>(
      GetProcAddress(Native(), "D3DPERF_SetRegion"));
  if (f)
    f(color, name);
}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD options) {
  auto f = reinterpret_cast<void(WINAPI *)(DWORD)>(
      GetProcAddress(Native(), "D3DPERF_SetOptions"));
  if (f)
    f(options);
}
