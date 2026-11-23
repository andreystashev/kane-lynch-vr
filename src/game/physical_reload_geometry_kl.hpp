// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "physical_reload_kl.hpp"
#include <vector>

namespace PhysicalReloadGeometryKL {
using namespace NativeCameraMath;
struct Vertex {Vec3 point{};std::uint32_t color=0;};
struct Mesh {std::vector<Vertex> vertices;};
// Magazines are native KL meshes. A missing or culled native packet must
// never turn one into an untextured procedural box as the camera moves.
// This empty overlay adapter preserves existing DLL consumers, allocates no
// vertices, and leaves public reload/host structures unchanged.
inline Mesh Build(const PhysicalReloadKL::Visual&,std::uint32_t){return {};}
}
