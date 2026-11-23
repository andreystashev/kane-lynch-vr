#pragma once
#include "native_weapon_kl.hpp"
namespace NativeAimKL {
using LogCallback=void(*)(const char*);
struct Endpoint {
    bool valid=false;
    NativeWeaponKL::AimFrame pose{};
    NativeCameraMath::Vec3 point{};
    std::uint32_t tick=0,reference=0;
    std::uintptr_t room=0;
};
struct Stats {
    bool installed=false,enabled=false;
    std::uint64_t queries=0,hits=0,misses=0,rejected=0,selfRejected=0,threadRejected=0,rateLimited=0;
    std::uint32_t tick=0,thread=0;
};
bool Install(LogCallback log);
void SetEnabled(bool enabled);
// Must be registered with NativeCoverKL::SetSimulationObserver. Every other
// caller is rejected before touching native collision, including render code.
void SimulationTick(std::uintptr_t player);
// Read-only rendering certificate: <=50ms, same owned source/recenter, <=1cm
// current-ray error. A missed ray never produces a synthetic range dot.
bool ReadEndpoint(const NativeWeaponKL::AimFrame& current,Endpoint& output);
bool TraceReloadFloor(std::uintptr_t player,NativeCameraMath::Vec3 point,float units,float& height);
enum class GazeTrace {Unavailable,Miss,Hit};
// Independent of optional visual preview. Valid only inside the actual
// owned sniper Fire capability on that same native simulation thread.
GazeTrace TraceGaze(const NativeWeaponKL::HeadGaze& gaze,const NativeWeaponKL::AimFrame& weapon,
                   NativeCameraMath::Vec3& point);
// One-shot squad command query. Only the owned native simulation callback can
// authorize it; it is independent of the optional laser and never fires.
GazeTrace TraceCommand(const NativeWeaponKL::AimFrame& weapon,NativeCameraMath::Vec3 direction,Endpoint& output,float rangeUnits=0);
// Same native simulation/world/contact guards, with a current centre-head
// certificate and two positively empty native hands before and after query.
GazeTrace TraceHeadCommand(const NativeWeaponKL::HeadGaze& head,NativeCameraMath::Vec3 direction,Endpoint& output,float rangeUnits=0);
Stats Snapshot();
}
