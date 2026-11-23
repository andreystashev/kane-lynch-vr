#pragma once
#include "native_camera_math.hpp"
#include <cstdint>
#include <cstddef>

namespace NativePlayerPose {
using namespace NativeCameraMath;
struct Geometry {
    CameraPose pose{};
    std::uint8_t properties[0x20]{};
    std::uint32_t parent=0;
};
static_assert(offsetof(Geometry,parent)==0x50,"Native geometry parent layout");
inline Vec3 Transform(const Basis& basis,Vec3 point) {
    return Add(Add(Scale(basis.right,point.x),Scale(basis.up,point.y)),Scale(basis.forward,point.z));
}
inline bool EyeLocal(Vec3 animatedEye,float units,bool followAnimation,Vec3& output) {
    if(!Finite(animatedEye)||!std::isfinite(units)||units<1.f||units>1000.f)return false;
    if(followAnimation) {
        const float distance2=Dot(animatedEye,animatedEye);
        if(distance2<=.0025f*units*units||distance2>=9.f*units*units)return false;
        output=animatedEye;
    } else {
        if(animatedEye.y<=.35f*units||animatedEye.y>=2.3f*units||
           std::fabs(animatedEye.x)>=units||std::fabs(animatedEye.z)>=units)return false;
        output={0.f,animatedEye.y,0.f};
    }
    return true;
}
// Equivalent to the engine's local-to-world walk at RVA 0x2A1F0/0x2A240.
// The reader makes stale or invalid geometry fail without dereferencing it.
template<class Reader> bool World(std::uintptr_t address,Reader read,CameraPose& output) {
    CameraPose result{{{0,0,1},{0,1,0},{1,0,0}},{0,0,0}};
    std::uintptr_t visited[32]{};
    for(unsigned count=0;address&&count<32;++count) {
        for(unsigned i=0;i<count;++i)if(visited[i]==address)return false;
        visited[count]=address;
        Geometry geometry{};
        if(!read(address,&geometry,sizeof(geometry))||!ValidatePose(geometry.pose))return false;
        result.position=Add(Transform(geometry.pose.basis,result.position),geometry.pose.position);
        result.basis={Transform(geometry.pose.basis,result.basis.forward),
                      Transform(geometry.pose.basis,result.basis.up),
                      Transform(geometry.pose.basis,result.basis.right)};
        address=geometry.parent;
        if(!address){output=result;return ValidatePose(output);}
    }
    return false;
}
}
