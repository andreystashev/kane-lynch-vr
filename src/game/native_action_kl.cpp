#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_action_kl.hpp"
#include <atomic>
#include <cstring>

namespace NativeActionKL {
namespace {
std::atomic<std::uintptr_t> verifiedImage{0};
bool Read(std::uintptr_t at,void* output,std::size_t size) {
    SIZE_T copied=0;
    return Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&copied)&&copied==size;
}
template<std::size_t N> bool Match(std::uintptr_t image,std::uintptr_t rva,const unsigned char (&expected)[N]) {
    unsigned char actual[N]{};return Read(image+rva,actual,N)&&!std::memcmp(actual,expected,N);
}
}
bool InitializeVerifiedImage(std::uintptr_t image) {
    if(verifiedImage.load()==image&&image)return true;
    // Native current-state and dispatcher semantics, pinned independently of
    // the cosmetic class names in RTTI. Only called after supported EXE hash.
    const unsigned char getter[]={0x8B,0x81,0x7C,0x06,0x00,0x00,0xC3};
    const unsigned char registerState[]={0x89,0x94,0x81,0xF8,0x05,0x00,0x00};
    const unsigned char startAction[]={0x89,0x4E,0x04,0x8B,0x01,0x8B,0x50,0x08,0xFF,0xD2};
    const unsigned char endAction[]={0xC7,0x41,0x04,0x00,0x00,0x00,0x00};
    if(!image||!Match(image,0x137BD0,getter)||!Match(image,0x137B6D,registerState)||
       !Match(image,0x11C2CE,startAction)||!Match(image,0x11C3BB,endAction))return false;
    verifiedImage=image;return true;
}
State Current() {
    const auto image=verifiedImage.load();return image?Snapshot(image,Read,GetTickCount()):State{};
}
bool PreserveRig(){return Current().PreserveRig();}
bool UsesNativeMovementAxes(){return Current().NativeMovementAxes();}
}
