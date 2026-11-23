#pragma once
#include <atomic>
#include <cstdint>

// The native loader can register a standing player many seconds before any
// world-camera packet exists. Only a real native-UI transition arms this
// state; a bad tracked frame during gameplay must never turn it on.
namespace NativeLoadingLifecycle {
class State {
public:
    void ObserveMenu(bool nativeMenu) {
        if(nativeMenu&&!awaitingWorld_.exchange(true,std::memory_order_relaxed))
            entries_.fetch_add(1,std::memory_order_relaxed);
    }
    // Call from the exact owned camera provider BEFORE its panel gate. Waiting
    // for a successfully steered frame here would create a circular hold.
    bool ObserveOwnedCamera(bool nativeMenu) {
        if(nativeMenu)return false;
        const bool resumed=awaitingWorld_.exchange(false,std::memory_order_relaxed);
        if(resumed)resumes_.fetch_add(1,std::memory_order_relaxed);
        return resumed;
    }
    bool Active(bool nativeMenu,bool explicitNativeVideo=false)const {
        return awaitingWorld_.load(std::memory_order_relaxed)&&!nativeMenu&&!explicitNativeVideo;
    }
    bool AwaitingWorld()const{return awaitingWorld_.load(std::memory_order_relaxed);}
    std::uint64_t Entries()const{return entries_.load(std::memory_order_relaxed);}
    std::uint64_t Resumes()const{return resumes_.load(std::memory_order_relaxed);}
    void Reset(){awaitingWorld_.store(false,std::memory_order_relaxed);}
private:
    std::atomic<bool> awaitingWorld_{false};
    std::atomic<std::uint64_t> entries_{0},resumes_{0};
};
}
