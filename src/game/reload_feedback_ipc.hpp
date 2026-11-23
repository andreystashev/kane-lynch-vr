#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cmath>
#include <algorithm>
namespace ReloadFeedbackIPC {
constexpr std::uint32_t Magic=0x3146524b,Version=1;
struct alignas(64) Shared {std::uint32_t magic,version,bytes,pid;volatile LONG sequence[2];DWORD tick[2];float amplitude[2];DWORD duration[2];std::uint64_t creation;};
static_assert(sizeof(Shared)==64,"Reload feedback ABI");
inline void Name(wchar_t* name,DWORD pid){std::swprintf(name,100,L"Local\\KL1ReloadFeedback-%lu",pid);}
struct Channel {
 HANDLE handle=nullptr;Shared* data=nullptr;LONG consumed[2]{};DWORD until[2]{};bool vibrating[2]{};
 std::uint64_t creation=0;
 ~Channel(){if(data)UnmapViewOfFile(data);if(handle)CloseHandle(handle);}
 bool Open(DWORD pid,bool producer){
  if(data)return data->pid==pid&&data->magic==Magic&&data->version==Version&&data->bytes==sizeof(Shared)&&data->creation==creation;
  wchar_t name[100]{};Name(name,pid);
  handle=producer?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name):OpenFileMappingW(FILE_MAP_READ,FALSE,name);
  if(!handle)return false;const bool fresh=producer&&GetLastError()!=ERROR_ALREADY_EXISTS;
  data=static_cast<Shared*>(MapViewOfFile(handle,producer?FILE_MAP_ALL_ACCESS:FILE_MAP_READ,0,0,sizeof(Shared)));
  if(!data){CloseHandle(handle);handle=nullptr;return false;}
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);FILETIME made{},ended{},kernel{},user{};if(process){if(GetProcessTimes(process,&made,&ended,&kernel,&user))creation=(std::uint64_t(made.dwHighDateTime)<<32)|made.dwLowDateTime;CloseHandle(process);}
  if(fresh){*data={};data->magic=Magic;data->version=Version;data->bytes=sizeof(Shared);data->pid=pid;data->creation=creation;}
  return creation&&data->magic==Magic&&data->version==Version&&data->bytes==sizeof(Shared)&&data->pid==pid&&data->creation==creation;
 }
 bool Send(unsigned side,float amplitude,DWORD duration,DWORD now){
  if(!data||side>1||!std::isfinite(amplitude)||amplitude<=0||amplitude>1||duration<1||duration>150)return false;
  InterlockedIncrement(&data->sequence[side]);data->tick[side]=now;data->amplitude[side]=amplitude;data->duration[side]=duration;MemoryBarrier();InterlockedIncrement(&data->sequence[side]);return true;
 }
 template<class Pulse>void Poll(DWORD now,bool active,Pulse pulse){
  for(unsigned side=0;side<2;++side){
   if(vibrating[side]&&(!active||LONG(now-until[side])>=0)){pulse(side,0.f,0u);vibrating[side]=false;}
   if(!data||data->magic!=Magic||data->version!=Version||data->bytes!=sizeof(Shared)||data->creation!=creation)continue;const auto seq=data->sequence[side];MemoryBarrier();if(!seq||(seq&1)||seq==consumed[side])continue;
   const auto tick=data->tick[side],duration=data->duration[side];const float amplitude=data->amplitude[side];MemoryBarrier();
   if(seq!=data->sequence[side])continue;consumed[side]=seq;
   if(!active||!tick||LONG(now-tick)<0||now-tick>200||duration<1||duration>150||!std::isfinite(amplitude)||amplitude<=0||amplitude>1)continue;
   pulse(side,amplitude,duration);until[side]=now+duration;vibrating[side]=true;
  }
 }
};
}
