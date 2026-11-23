#pragma once
#include "native_effects_math_kl.hpp"
#include <cmath>
#include <cstring>
namespace NativeCarriedPersonVisibilityKLMath {
// Carried actors use a third-person shoulder pose. Clipping only the nearest
// 38cm leaves a disconnected head and legs in front of the first-person eye.
// Reject the whole, separately ownership-certified actor in the main eye.
// Programmable user planes operate in homogeneous clip space: -clipW is
// negative for every vertex that could survive the normal perspective clip.
// Projection/depth mapping, the actor pose, shadows and foreign views remain
// unchanged; the caller scopes and restores this one plane around the draw.
inline bool Plane(const float* projection,float units,float output[4]){
    if(!projection||!output||!std::isfinite(units)||units<1||units>1000)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
    if(projection[0]<=0||projection[5]<=0||projection[14]>=-1e-5f||
       std::fabs(projection[11]-1)>1e-5f||std::fabs(projection[15])>1e-5f||
       std::fabs(projection[2])+std::fabs(projection[6])>1e-6f)return false;
    const float next[]{0,0,0,-1};
    std::memcpy(output,next,sizeof(next));return true;
}
inline bool MatchesMVP(const float* world,const NativeCameraMath::CameraPose& eye,const float* projection,const float* actual){
    float wv[16]{};if(!actual||!NativeEffectsKLMath::WorldViewShader(world,eye,wv))return false;
    bool columns=true,rows=true;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double expected=0;for(unsigned k=0;k<4;++k)expected+=double(wv[r*4+k])*projection[k*4+c];
        const double tolerance=r==3?.02:4e-5;
        if(!std::isfinite(expected))return false;
        columns=columns&&std::isfinite(actual[c*4+r])&&std::fabs(actual[c*4+r]-expected)<=tolerance;
        rows=rows&&std::isfinite(actual[r*4+c])&&std::fabs(actual[r*4+c]-expected)<=tolerance;
    }
    return columns||rows;
}
}
