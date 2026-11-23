#pragma once
#include <cmath>

// OpenXR grip gives the handle position; aim gives the pointing direction.
// The caller must locate both in the same space at the same display time.
namespace OpenXRControllerPose {
struct Vector { float x, y, z; };
struct Quaternion { float x, y, z, w; };
struct Pose { Quaternion orientation{}; Vector position{}; };
inline bool Combine(bool gripValid, Vector grip, bool aimValid, Quaternion aim, Pose& out) {
    out = {};
    if (!gripValid || !aimValid || !std::isfinite(grip.x) || !std::isfinite(grip.y) || !std::isfinite(grip.z) ||
        !std::isfinite(aim.x) || !std::isfinite(aim.y) || !std::isfinite(aim.z) || !std::isfinite(aim.w)) return false;
    const float norm = aim.x*aim.x + aim.y*aim.y + aim.z*aim.z + aim.w*aim.w;
    if (!std::isfinite(norm) || norm <= .9f || norm >= 1.1f) return false;
    out = {aim, grip};
    return true;
}
}
