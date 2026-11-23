#pragma once
#include "native_camera_math.hpp"

// Reload-local right/up/forward coordinates, matching the KL2 mechanism.
// Keep this independent of either game's inventory and renderer adapters.
namespace ReloadMathKL {
using namespace NativeCameraMath;
inline Vec3 Transform(const Basis& basis,Vec3 p) {
    return Add(Add(Scale(basis.right,p.x),Scale(basis.up,p.y)),Scale(basis.forward,p.z));
}
inline Vec3 Inverse(const Basis& basis,Vec3 p) {
    return {Dot(p,basis.right),Dot(p,basis.up),Dot(p,basis.forward)};
}
inline CameraPose ToLocal(const CameraPose& root,const CameraPose& world) {
    return {{Inverse(root.basis,world.basis.forward),Inverse(root.basis,world.basis.up),
             Inverse(root.basis,world.basis.right)},Inverse(root.basis,Subtract(world.position,root.position))};
}
inline CameraPose ToWorld(const CameraPose& root,const CameraPose& local) {
    return {{Transform(root.basis,local.basis.forward),Transform(root.basis,local.basis.up),
             Transform(root.basis,local.basis.right)},Add(root.position,Transform(root.basis,local.position))};
}
}
