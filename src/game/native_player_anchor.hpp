#pragma once
#include "native_camera_math.hpp"
#include <cstdint>

namespace NativePlayerAnchor {
struct Anchor {
    bool valid=false,headBone=false,animatedHead=false;
    std::uintptr_t player=0,sourceCamera=0;
    NativeCameraMath::Vec3 position{};
    // Native actor geometry from the same eye-anchor observation. Allows a
    // constrained camera to retain a body-local eye while the actor descends.
    NativeCameraMath::CameraPose root{};
};
// Call only after NativeCameraKL has verified the image hash and signatures.
bool InitializeVerifiedImage(std::uintptr_t image);
Anchor Current(float worldUnitsPerMetre,unsigned fallbackHeightMm,bool animatedHead=false);
}
