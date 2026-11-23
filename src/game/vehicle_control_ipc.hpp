#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cwchar>

// Independent x86/x64 mailbox: no image ABI or pixel offsets are changed.
namespace VehicleControlIPC {
constexpr std::uint32_t Magic=0x56434B4C,Version=1;
enum class Kind {Mounted,Passenger};
struct Certificate {std::uint32_t player=0,movement=0,controller=0,entity=0,record=0,epoch=0,recenter=0,tick=0;};
struct Request {Certificate receiver{};std::uint32_t tick=0;float x=0,y=0;std::uint32_t buttons=0;};
template<class T>struct Slot {volatile LONG sequence=0;T value{};};
struct alignas(64) Shared {
    std::uint32_t magic=0,version=0,bytes=0,gamePid=0;
    Slot<Certificate> mounted,passenger;
    Slot<Request> mountedInput,passengerInput;
};
static_assert(sizeof(Certificate)==32&&sizeof(Request)==48&&sizeof(Shared)==192,"Vehicle controls ABI changed");
static_assert(offsetof(Shared,mountedInput)==88&&offsetof(Shared,passengerInput)==140,"Vehicle controls slots changed");
inline void MappingName(wchar_t* out,std::size_t count,DWORD pid){std::swprintf(out,count,L"Local\\KaneLynchVR-VehicleControls-%lu",pid);}
inline bool Header(const Shared& shared,DWORD pid){return pid&&shared.magic==Magic&&shared.version==Version&&shared.bytes==sizeof(Shared)&&shared.gamePid==pid;}
template<class T>inline bool Read(const Slot<T>& slot,T& output){
    const auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0);MemoryBarrier();
    if(!before||(before&1))return false;T value;std::memcpy(&value,&slot.value,sizeof(value));MemoryBarrier();
    if(before!=InterlockedCompareExchange(const_cast<volatile LONG*>(&slot.sequence),0,0))return false;output=value;return true;
}
template<class T>inline void Publish(Slot<T>& slot,const T& value){InterlockedIncrement(&slot.sequence);std::memcpy(&slot.value,&value,sizeof(value));MemoryBarrier();InterlockedIncrement(&slot.sequence);}
inline bool Same(const Certificate& a,const Certificate& b){return a.player==b.player&&a.movement==b.movement&&a.controller==b.controller&&a.entity==b.entity&&a.record==b.record&&a.epoch==b.epoch&&a.recenter==b.recenter;}
inline bool Fresh(const Certificate& value,DWORD now,std::uint32_t recenter,Kind kind){
    return value.player&&value.movement&&value.epoch&&value.tick&&value.recenter==recenter&&
        std::int32_t(now-value.tick)>=0&&now-value.tick<=150&&
        (kind==Kind::Mounted?(value.controller&&value.entity&&value.record):(!value.controller&&!value.entity&&!value.record));
}
inline bool Receiver(const Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,Kind kind,Certificate& output){
    Certificate value;if(!Header(shared,pid)||!Read(kind==Kind::Mounted?shared.mounted:shared.passenger,value)||!Fresh(value,now,recenter,kind)||!Header(shared,pid))return false;
    output=value;return true;
}
inline bool Input(const Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,Kind kind,const Certificate& expected,Request& output){
    Request value;Certificate current;
    if(!Receiver(shared,pid,now,recenter,kind,current)||!Same(expected,current)||
       !Read(kind==Kind::Mounted?shared.mountedInput:shared.passengerInput,value)||!Same(value.receiver,current)||
       !Fresh(value.receiver,now,recenter,kind)||!value.tick||std::int32_t(now-value.tick)<0||now-value.tick>100||
       !std::isfinite(value.x)||!std::isfinite(value.y)||std::fabs(value.x)>1||std::fabs(value.y)>1||
       (value.buttons&~1u)||(kind==Kind::Mounted&&value.buttons)||!Header(shared,pid))return false;
    Certificate after;if(!Receiver(shared,pid,now,recenter,kind,after)||!Same(current,after))return false;output=value;return true;
}
// Called only after mapper focus/calibration/UI consumption. Axes are signed
// normalized rates (mounted) or navigation axes (passenger), never mouse pixels.
inline bool Send(Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,Kind kind,float x,float y,bool crouched=false){
    Certificate receiver;if(!Receiver(shared,pid,now,recenter,kind,receiver)||!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1||std::fabs(y)>1)return false;
    Request value{receiver,now,x,y,kind==Kind::Passenger&&crouched?1u:0u};Publish(kind==Kind::Mounted?shared.mountedInput:shared.passengerInput,value);return true;
}
inline void Cancel(Shared& shared){Publish(shared.mountedInput,Request{});Publish(shared.passengerInput,Request{});}
struct Intent {bool allowed=false,mounted=false,passenger=false,crouched=false;float mountedX=0,mountedY=0,passengerX=0,passengerY=0;};
enum class Route {Native,Mounted,Passenger};
// Exact host consumer: fallback mouse is suppressed only after a fresh
// certificate accepted the rate. Passenger aiming keeps its original mouse.
inline Route Dispatch(Shared& shared,DWORD pid,DWORD now,std::uint32_t recenter,const Intent& intent,float& mouseX,float& mouseY){
    if(!Header(shared,pid))return Route::Native;
    if(!intent.allowed||(intent.mounted&&intent.passenger)){Cancel(shared);return Route::Native;}
    if(intent.mounted&&Send(shared,pid,now,recenter,Kind::Mounted,intent.mountedX,intent.mountedY)){
        mouseX=mouseY=0;Publish(shared.passengerInput,Request{});return Route::Mounted;
    }
    if(intent.passenger&&Send(shared,pid,now,recenter,Kind::Passenger,intent.passengerX,intent.passengerY,intent.crouched)){
        Publish(shared.mountedInput,Request{});return Route::Passenger;
    }
    Cancel(shared);return Route::Native;
}
}
