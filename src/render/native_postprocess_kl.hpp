#pragma once
#include <cstdint>

namespace NativePostprocessKL {
using IsStereoScene=bool(*)();
using LogCallback=void(*)(const char* message);
struct Stats {
    bool installed=false;
    std::uint64_t calls=0,historySkipped=0;
};

// Call after MinHook initialization. The predicate describes this exact
// render frame, so menu/panel passes keep their native post-processing.
bool Install(IsStereoScene isStereoScene,LogCallback log);
Stats Snapshot();
}
