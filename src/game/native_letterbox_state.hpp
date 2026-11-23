#pragma once
#include "native_cinematic_kl.hpp"
#include <cmath>
#include <cstdint>

// Only the hash-pinned ZLetterboxDisplay draw may use this state machine.
// A brief same-owner latch covers the native 0.5 s fade after MiniCutscene
// End, without treating black HUD panels or arbitrary screen quads as bars.
namespace NativeLetterboxKL {
struct State {
    std::uintptr_t object=0,owner=0;
    std::uint32_t lastMiniTick=0;
    bool seenMini=false;
    bool Step(std::uintptr_t currentObject,std::uintptr_t currentOwner,bool exactSource,bool firstPerson,
              const NativeCinematicKL::State& cinematic,float amount,std::uint32_t now,bool stereoFull=false) {
        if(!exactSource||!currentObject||!currentOwner||!firstPerson||!cinematic.valid||(cinematic.full&&!stereoFull)||
           !std::isfinite(amount)||amount<=0.f||amount>1.01f) {*this={};return false;}
        if(object!=currentObject||owner!=currentOwner) {*this={};object=currentObject;owner=currentOwner;}
        if(cinematic.full) {seenMini=false;lastMiniTick=0;return true;}
        if(cinematic.mini) {seenMini=true;lastMiniTick=now;return true;}
        if(seenMini&&now-lastMiniTick<=1000u)return true;
        *this={};return false;
    }
};
}
