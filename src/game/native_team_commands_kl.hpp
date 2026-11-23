#pragma once
#include "team_command_types.hpp"
#include "native_weapon_kl.hpp"
#include <cstdint>

namespace NativeTeamCommandsKL {
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
void PublishHeadRay(const NativeWeaponKL::HeadGaze& head);
void InvalidateHeadRay();
// Current mounted centre-head certificate plus positive native empty hands.
// Read-only; native collision remains owned by the simulation callback.
bool ReadHeadRay(std::uintptr_t player,NativeWeaponKL::HeadGaze& output);
// Read-only current native receiver; the root publishes it only when the
// corresponding VR roster has actually been drawn.
TeamCommandIPC::ReceiverCertificate ReceiverSnapshot(std::uint32_t now);
// Runs after the native local-player detector, through its existing observer.
void SimulationTick(std::uintptr_t player);
struct Stats {
    bool installed=false;
    std::uint64_t requests=0,accepted=0,rejected=0,noTarget=0,selectionChanged=0,stale=0,queries=0,targetOverrides=0;
    std::uint64_t weaponRayRequests=0,headRayRequests=0,heldRayUnavailable=0,headRayUnavailable=0;
    std::uint64_t assistRequests=0,assistSelected=0,assistRejected=0,assistOccluded=0;
    float assistLastAngle=0;
    std::uint32_t lastSerial=0,lastCommand=0,lastResult=0,lastReceiver=0,lastTarget=0,lastThread=0;
    float lastPoint[3]{};
};
Stats Snapshot();
}
