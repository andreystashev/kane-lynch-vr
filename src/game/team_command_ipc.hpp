#pragma once
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include "team_command_types.hpp"

namespace TeamCommandIPC {
constexpr std::uint32_t Magic=0x54434B4C,Version=1;
template<class T> struct Slot {volatile LONG sequence=0;T value{};};
struct alignas(64) Shared {
    std::uint32_t magic=0,version=0,bytes=0,gamePid=0;
    Slot<ReceiverCertificate> receiver;
    Slot<Request> request;
    Slot<Result> result;
};
static_assert(sizeof(Shared)==192&&offsetof(Shared,request)==48&&offsetof(Shared,result)==96,"Team mailbox ABI changed");
inline void MappingName(wchar_t* out,std::size_t count,DWORD pid){swprintf(out,count,L"Local\\KaneLynchVR-TeamCommands-%lu",pid);}
inline bool Header(const Shared& shared,DWORD pid) {
    return pid&&shared.magic==Magic&&shared.version==Version&&shared.bytes==sizeof(Shared)&&shared.gamePid==pid;
}
template<class T> inline bool Read(const Slot<T>& slot,T& output) {
    const auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0);
    if(!before||(before&1))return false;
    T copy;std::memcpy(&copy,&slot.value,sizeof(copy));MemoryBarrier();
    const auto after=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0);
    if(before!=after||(after&1))return false;
    output=copy;return true;
}
template<class T> inline void Publish(Slot<T>& slot,const T& value) {
    InterlockedIncrement(&slot.sequence);std::memcpy(&slot.value,&value,sizeof(value));
    MemoryBarrier();InterlockedIncrement(&slot.sequence);
}
inline bool ReadReceiver(const Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,ReceiverCertificate& output) {
    ReceiverCertificate value;
    if(!Header(shared,pid)||!Read(shared.receiver,value)||!Fresh(value,now,recenter)||!Header(shared,pid))return false;
    output=value;return true;
}
inline bool ReadRequest(const Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,Request& output) {
    Request value;
    if(!Header(shared,pid)||!Read(shared.request,value)||!value.serial||!value.tick||!Known(value.command)||
       std::int32_t(now-value.tick)<0||now-value.tick>250||value.recenter!=recenter||
       !Fresh(value.selected,now,recenter)||!Header(shared,pid))return false;
    output=value;return true;
}
inline bool PublishCommand(Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,Command command,
                           DWORD navigationAfter,std::uint32_t& serial) {
    ReceiverCertificate selected;
    if(!Known(command)||!ReadReceiver(shared,pid,now,recenter,selected)||
       (navigationAfter&&std::int32_t(selected.tick-navigationAfter)<=0))return false;
    Request request;request.serial=++serial;if(!request.serial)request.serial=++serial;
    request.tick=now;request.recenter=recenter;request.command=command;request.selected=selected;
    Publish(shared.request,request);return true;
}
inline void CancelCommand(Shared& shared,DWORD pid,DWORD now,std::uint32_t serial) {
    if(!Header(shared,pid))return;
    Request cancel;cancel.serial=serial;cancel.tick=now;Publish(shared.request,cancel);
}
inline bool ReadResult(const Shared& shared,DWORD pid,Result& output) {
    Result value;
    if(!Header(shared,pid)||!Read(shared.result,value)||!value.serial||!Known(value.command)||!Known(value.code)||!Header(shared,pid))return false;
    output=value;return true;
}
}
