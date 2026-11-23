#pragma once
#include "native_hud_math.hpp"
#include <cstddef>

namespace NativeReticleKLMath {
using namespace NativeCameraMath;
// KL1 SpriteEmit 470938 jump table: kinds5/6 copy count*40/count*116
// geometry bytes. Borrowed kinds3/4 and unrelated particle formats stay native.
inline std::size_t CopiedStride(std::uint32_t kind){return kind==5?40:kind==6?116:0;}
inline bool Emitter(std::uint32_t caller,std::uint32_t type){
    // 254883 is the separate five-bar charge meter, not the aiming graphic.
    return (caller==0x254660&&type==10)||(caller==0x2546C2&&type==9);
}
inline bool Translate(const float* native,const NativeHudMath::Vertex* vertices,std::size_t count,
                      const CameraPose& eye,const float* projection,Vec3 target,float* output){
    if(!output||!projection||!ValidatePose(eye,.05f)||!Finite(target)||!vertices||!count||count>32||count%4||
       !NativeHudMath::NativeMonitorProjection(native)||!NativeHudMath::InterfacePlane(vertices,count))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
    if(projection[0]<=0||projection[5]<=0||std::fabs(projection[11]-1.f)>.0001f||std::fabs(projection[15])>.0001f)return false;
    const auto delta=Subtract(target,eye.position);
    const Vec3 camera{Dot(delta,eye.basis.right),Dot(delta,eye.basis.up),Dot(delta,eye.basis.forward)};
    const float x=camera.x*projection[0]+camera.y*projection[4]+camera.z*projection[8]+projection[12];
    const float y=camera.x*projection[1]+camera.y*projection[5]+camera.z*projection[9]+projection[13];
    const float w=camera.x*projection[3]+camera.y*projection[7]+camera.z*projection[11]+projection[15];
    if(!std::isfinite(w)||w<=1e-4f||!std::isfinite(x)||!std::isfinite(y)||std::fabs(x/w)>4||std::fabs(y/w)>4)return false;
    double centreX=0,centreY=0;
    for(std::size_t index=0;index<count;++index){const auto& vertex=vertices[index];
        const float nx=vertex.x*native[0]+vertex.y*native[1]+vertex.z*native[2]+native[3];
        const float ny=vertex.x*native[4]+vertex.y*native[5]+vertex.z*native[6]+native[7];
        const float nw=vertex.x*native[12]+vertex.y*native[13]+vertex.z*native[14]+native[15];
        if(!std::isfinite(nw)||nw<=1e-4f||!std::isfinite(nx)||!std::isfinite(ny)||std::fabs(nx/nw)>4||std::fabs(ny/nw)>4)return false;
        centreX+=nx/nw;centreY+=ny/nw;
    }
    const float across=(projection[0]/projection[5])*(native[5]/native[0]);
    if(!std::isfinite(across)||across<.25f||across>4.f)return false;
    const float dx=x/w-across*float(centreX/count),dy=y/w-float(centreY/count);
    float rows[16];std::memcpy(rows,native,sizeof(rows));
    for(unsigned i=0;i<4;++i){rows[i]=native[i]*across+native[12+i]*dx;rows[4+i]=native[4+i]+native[12+i]*dy;}
    for(float value:rows)if(!std::isfinite(value))return false;
    std::memcpy(output,rows,sizeof(rows));return true;
}
}
