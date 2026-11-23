#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_postprocess_kl.hpp"
#include "profile.hpp"
#include <atomic>
#include <cstring>

namespace NativePostprocessKL {
namespace {
static_assert(sizeof(void*)==4,"K&L post-processing hook is x86 only");
using HistoryStage=void(__thiscall*)(void*);
HistoryStage originalHistory=nullptr;
IsStereoScene stereoScene=nullptr;
LogCallback writeLog=nullptr;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> calls{0},historySkipped{0};

void Log(const char* message) {if(writeLog)writeLog(message);}

void __fastcall HistoryHook(void* postFilter,void*) {
    ++calls;
    if(!postFilter||!stereoScene||!stereoScene()) {
        originalHistory(postFilter);return;
    }
    // This is the history accumulation stage, not the complete post-filter.
    // With amount=0 its native branch marks history invalid (+0x32B4=1) and
    // returns before sampling the previous texture (+0x3260). On returning
    // to a panel the engine therefore initializes history from a fresh frame.
    // Restore the game's animated amount immediately after this one call.
    auto* amount=reinterpret_cast<float*>(static_cast<unsigned char*>(postFilter)+0x18);
    struct Restore {
        float* amount;
        float previous;
        ~Restore(){std::memcpy(amount,&previous,sizeof(previous));}
    } restore{amount,0.f};
    std::memcpy(&restore.previous,amount,sizeof(restore.previous));
    const float zero=0.f;
    std::memcpy(amount,&zero,sizeof(zero));
    originalHistory(postFilter);
    ++historySkipped;
}
}

bool Install(IsStereoScene isStereoScene,LogCallback log) {
    if(installed.load())return true;
    writeLog=log;
    if(!isStereoScene)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest)) {
        Log("Native K&L history filter: EXE SHA256 differs; no hook installed.");return false;
    }
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr std::uintptr_t historyRva=0xB0620;
    // Complete zero-amount branch, including its history-invalidating store.
    const unsigned char signature[]={0xD9,0xEE,0x83,0xEC,0x24,0x56,0x8B,0xF1,
        0xD8,0x5E,0x18,0xDF,0xE0,0xF6,0xC4,0x44,0x7A,0x0F,0xC7,0x86,
        0xB4,0x32,0x00,0x00,0x01,0x00,0x00,0x00,0x5E,0x83,0xC4,0x24,0xC3};
    void* target=reinterpret_cast<void*>(image+historyRva);
    if(std::memcmp(target,signature,sizeof(signature))) {
        Log("Native K&L history filter: stage signature differs; no hook installed.");return false;
    }
    stereoScene=isStereoScene;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&HistoryHook),
                     reinterpret_cast<void**>(&originalHistory))!=MH_OK) {
        stereoScene=nullptr;Log("Native K&L history filter: hook creation failed.");return false;
    }
    if(MH_EnableHook(target)!=MH_OK) {
        MH_RemoveHook(target);stereoScene=nullptr;
        Log("Native K&L history filter: hook activation failed.");return false;
    }
    installed=true;
    Log("Native K&L temporal history is disabled for tracked scene frames; native panel/history reset retained.");
    return true;
}

Stats Snapshot() {
    Stats result{};result.installed=installed.load();result.calls=calls.load();
    result.historySkipped=historySkipped.load();return result;
}
}
