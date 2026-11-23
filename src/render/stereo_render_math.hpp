#pragma once

#include "native_camera_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// D3D9 fallback stereo: independent of Windows and the headset runtime.
namespace StereoRenderMath {
using Mat4=float[4][4];

class PairSequence {
    unsigned eye_=0;
    std::uint64_t pair_=1;
public:
    unsigned Eye() const { return eye_; }
    std::uint64_t FrameId() const { return pair_*2+eye_; }
    void CompleteEye() {
        if(eye_==1){++pair_;eye_=0;}else eye_=1;
    }
    // Cancel a partial pair without ever reusing its published left frame.
    void Restart() { ++pair_;eye_=0; }
};

// Positional lean starts at the current worn pose for each actor/camera mode.
// An off-head tracked pose, or room movement during a cinematic, must never
// become a persistent offset between the first-person eyes and the avatar.
struct HeadPositionReference {
    bool valid=false,cinematic=false;
    std::uintptr_t player=0,camera=0;
    long recenterAck=0;
    NativeCameraMath::Vec3 origin{};
    void Reset(){*this={};}
    bool Select(NativeCameraMath::Vec3 current,bool mounted,std::uintptr_t currentPlayer,
                std::uintptr_t currentCamera,bool currentCinematic,long ack,
                NativeCameraMath::Vec3& output) {
        if(!NativeCameraMath::Finite(current))return false;
        if(!mounted){Reset();return false;}
        if(!valid||player!=currentPlayer||camera!=currentCamera||cinematic!=currentCinematic||recenterAck!=ack) {
            valid=true;player=currentPlayer;camera=currentCamera;cinematic=currentCinematic;recenterAck=ack;origin=current;
        }
        output=origin;return true;
    }
};

// Authored cutscene camera. Full entry orientation is neutral, including
// pitch/roll; physical look is relative to that origin. The active stereo
// path uses SelectFree: native camera position/path is retained without
// adding gameplay turns or room-scale translation. Select keeps the former
// bounded policy available as an explicit utility, rather than a hidden
// eight-degree limit on ordinary cinematic head tracking.
struct CinematicLook {
    bool valid=false;
    std::uintptr_t camera=0;
    long ack=0;
    NativeCameraMath::Quat origin{};
    void Reset(){*this={};}
    bool SelectFree(NativeCameraMath::Quat head,std::uintptr_t currentCamera,long currentAck,
                    NativeCameraMath::Quat& output) {
        using namespace NativeCameraMath;
        Quat normalized;
        if(!currentCamera||!NormalizeQuaternion(head,normalized))return false;
        if(!valid||camera!=currentCamera||ack!=currentAck) {
            valid=true;camera=currentCamera;ack=currentAck;origin=normalized;
        }
        return NormalizeQuaternion(NativeCameraMath::Multiply(Conjugate(origin),normalized),output);
    }
    bool Select(NativeCameraMath::Quat head,std::uintptr_t currentCamera,long currentAck,
                NativeCameraMath::Quat& output) {
        using namespace NativeCameraMath;
        Quat relative;
        if(!SelectFree(head,currentCamera,currentAck,relative))return false;
        const Vec3 look=Rotate(relative,{0,0,-1});
        constexpr float maximum=8.f*3.14159265358979323846f/180.f;
        const float yaw=std::clamp(std::atan2(look.x,-look.z),-maximum,maximum);
        const float pitch=std::clamp(std::asin(std::clamp(look.y,-1.f,1.f)),-maximum,maximum);
        const Quat horizontal{0,-std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
        const Quat vertical{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
        return NormalizeQuaternion(NativeCameraMath::Multiply(horizontal,vertical),output);
    }
};

inline void Identity(Mat4 matrix) {
    std::memset(matrix,0,sizeof(Mat4));
    for(unsigned i=0;i<4;++i)matrix[i][i]=1.f;
}
inline void Multiply(const Mat4 a,const Mat4 b,Mat4 output) {
    Mat4 result{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        for(unsigned k=0;k<4;++k)result[r][c]+=a[r][k]*b[k][c];
    std::memcpy(output,result,sizeof(result));
}
inline bool Invert(const Mat4 matrix,Mat4 output) {
    float rows[4][8]{};
    for(unsigned r=0;r<4;++r) {
        for(unsigned c=0;c<4;++c)rows[r][c]=matrix[r][c];
        rows[r][4+r]=1.f;
    }
    for(unsigned col=0;col<4;++col) {
        unsigned pivot=col;
        for(unsigned r=col+1;r<4;++r)
            if(std::fabs(rows[r][col])>std::fabs(rows[pivot][col]))pivot=r;
        if(!std::isfinite(rows[pivot][col])||std::fabs(rows[pivot][col])<1e-10f)return false;
        if(pivot!=col)for(unsigned c=0;c<8;++c)std::swap(rows[pivot][c],rows[col][c]);
        const float divisor=rows[col][col];
        for(unsigned c=0;c<8;++c)rows[col][c]/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=col) {
            const float factor=rows[r][col];
            for(unsigned c=0;c<8;++c)rows[r][c]-=factor*rows[col][c];
        }
    }
    Mat4 result;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
        result[r][c]=rows[r][4+c];
        if(!std::isfinite(result[r][c]))return false;
    }
    std::memcpy(output,result,sizeof(result));return true;
}

// Turn the tracking reference about the head anchor. Positive virtual yaw
// looks right in LibOVR (-Z forward). Lean shares the reference rotation;
// per-eye offsets remain head-local and are added by CameraViewDelta once.
// Keep raw SDK orientations untouched for compositor timewarp and hands.
inline bool LimitHeadTranslation(NativeCameraMath::Vec3 displacement,float maximumMetres,
                                 NativeCameraMath::Vec3& output) {
    using namespace NativeCameraMath;
    if(!Finite(displacement)||!std::isfinite(maximumMetres)||maximumMetres<0.f||maximumMetres>1.5f)return false;
    const float squared=Dot(displacement,displacement);
    if(!std::isfinite(squared))return false;
    if(maximumMetres==0.f){output={};return true;}
    if(squared<=maximumMetres*maximumMetres){output=displacement;return true;}
    output=Scale(displacement,maximumMetres/std::sqrt(squared));return true;
}

inline bool ApplyVirtualYaw(const NativeCameraMath::Quat& physical,
                            NativeCameraMath::Vec3 lean,float radians,
                            NativeCameraMath::Quat& turned,
                            NativeCameraMath::Vec3& turnedLean) {
    using namespace NativeCameraMath;
    Quat normalized;
    if(!std::isfinite(radians)||std::fabs(radians)>3.142f||!Finite(lean)||
       !NormalizeQuaternion(physical,normalized))return false;
    const Quat yaw{0.f,-std::sin(radians*.5f),0.f,std::cos(radians*.5f)};
    Quat result;
    if(!NormalizeQuaternion(NativeCameraMath::Multiply(yaw,normalized),result))return false;
    const Vec3 rotated=Rotate(yaw,lean);
    if(!Finite(rotated))return false;
    turned=result;turnedLean=rotated;return true;
}

// Transform engine camera coordinates into the tracked eye. Tracking q is
// camera-to-world; a VIEW needs its inverse, after reflecting tracking -Z to
// D3D +Z. In particular a point along the actual gaze must land at x=y=0.
inline bool CameraViewDelta(const NativeCameraMath::Quat& relativeHead,
                            NativeCameraMath::Vec3 displacement,
                            NativeCameraMath::Vec3 eyeOffset,float dollyMetres,
                            float unitsPerMetre,Mat4 output) {
    using namespace NativeCameraMath;
    Quat q;
    if(!NormalizeQuaternion(relativeHead,q)||!Finite(displacement)||!Finite(eyeOffset)||
       !std::isfinite(dollyMetres)||!std::isfinite(unitsPerMetre)||unitsPerMetre<=0.f)return false;
    const Vec3 right=Rotate(q,{1,0,0}),up=Rotate(q,{0,1,0}),back=Rotate(q,{0,0,1});
    Mat4 result;
    Identity(result);
    result[0][0]=right.x;result[1][0]=right.y;result[2][0]=-right.z;
    result[0][1]=up.x;result[1][1]=up.y;result[2][1]=-up.z;
    result[0][2]=-back.x;result[1][2]=-back.y;result[2][2]=back.z;
    const Vec3 offset=Add(displacement,Rotate(q,eyeOffset));
    const float position[3]={offset.x*unitsPerMetre,offset.y*unitsPerMetre,
                             (-offset.z+dollyMetres)*unitsPerMetre};
    for(unsigned c=0;c<3;++c)
        result[3][c]=-(position[0]*result[0][c]+position[1]*result[1][c]+position[2]*result[2][c]);
    std::memcpy(output,result,sizeof(result));return true;
}

// WV*P does not retain P's (0,0,1,0) w-column. Factor the known engine
// projection out and require an affine, orthogonal model-view instead. This
// accepts scaled model matrices, including transposed shader constant blocks,
// while rejecting bone palettes, vectors, and unrelated 4-register windows.
inline bool ProjectedAffine(const Mat4 candidate,const Mat4 inverseProjection) {
    Mat4 affine;Multiply(candidate,inverseProjection,affine);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        if(!std::isfinite(affine[r][c])||std::fabs(affine[r][c])>1e8f)return false;
    for(unsigned r=0;r<3;++r)if(std::fabs(affine[r][3])>0.002f)return false;
    if(std::fabs(affine[3][3]-1.f)>0.002f)return false;
    float lengths[3]{};
    for(unsigned r=0;r<3;++r) {
        for(unsigned c=0;c<3;++c)lengths[r]+=affine[r][c]*affine[r][c];
        if(lengths[r]<1e-6f||lengths[r]>1e6f)return false;
    }
    for(unsigned r=0;r<3;++r)for(unsigned s=r+1;s<3;++s) {
        float dot=0.f;for(unsigned c=0;c<3;++c)dot+=affine[r][c]*affine[s][c];
        if(std::fabs(dot)>0.02f*std::sqrt(lengths[r]*lengths[s]))return false;
    }
    return true;
}
inline bool PatchProjectedAffine(const float* input,const Mat4 inverseProjection,
                                 const Mat4 delta,const Mat4 eyeProjection,float* output) {
    for(unsigned transpose=0;transpose<2;++transpose) {
        Mat4 candidate;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
            candidate[r][c]=input[transpose?c*4+r:r*4+c];
        if(!ProjectedAffine(candidate,inverseProjection))continue;
        Mat4 affine,tracked,projected;
        Multiply(candidate,inverseProjection,affine);
        Multiply(affine,delta,tracked);Multiply(tracked,eyeProjection,projected);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(projected[r][c]))return false;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
            output[transpose?c*4+r:r*4+c]=projected[r][c];
        return true;
    }
    return false;
}

struct Perspective {
    Mat4 projection{},inverse{};
    float nearUnits=0.f,farUnits=0.f;
    bool transposed=false,infiniteFar=false,hasWorldView=false;
};

// Shader-only engines do not necessarily call SetTransform(PROJECTION).
// Recover P from a rigid/uniformly-scaled WV*P: its depth and w columns are
// parallel in the upper 3 rows, and its x/y axes are mutually perpendicular
// after removing the asymmetric-frustum offsets. Bone matrices and arbitrary
// float4 blocks fail these independent geometric constraints.
inline bool RecoverPerspective(const float* input,Perspective& output) {
    for(unsigned transpose=0;transpose<2;++transpose) {
        Mat4 m;
        bool finite=true;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
            m[r][c]=input[transpose?c*4+r:r*4+c];
            finite=finite&&std::isfinite(m[r][c])&&std::fabs(m[r][c])<1e9f;
        }
        if(!finite)continue;
        float wLength=0.f,depthDot=0.f,xDot=0.f,yDot=0.f;
        for(unsigned r=0;r<3;++r) {
            wLength+=m[r][3]*m[r][3];depthDot+=m[r][2]*m[r][3];
            xDot+=m[r][0]*m[r][3];yDot+=m[r][1]*m[r][3];
        }
        if(wLength<1e-8f||wLength>1e8f)continue;
        const float a=depthDot/wLength;
        if(a<1.f-1e-6f||a>1.2f)continue;
        float residual=0.f;
        for(unsigned r=0;r<3;++r) {
            const float d=m[r][2]-a*m[r][3];residual+=d*d;
        }
        if(residual>wLength*1e-9f)continue;
        const float b=m[3][2]-a*m[3][3];
        if(!(b<-.005f))continue;
        const float offsetX=xDot/wLength,offsetY=yDot/wLength;
        float xLength=0.f,yLength=0.f,xyDot=0.f;
        for(unsigned r=0;r<3;++r) {
            const float x=m[r][0]-offsetX*m[r][3],y=m[r][1]-offsetY*m[r][3];
            xLength+=x*x;yLength+=y*y;xyDot+=x*y;
        }
        if(xLength<1e-8f||yLength<1e-8f||std::fabs(xyDot)>.001f*std::sqrt(xLength*yLength))continue;
        const float scaleX=std::sqrt(xLength/wLength),scaleY=std::sqrt(yLength/wLength);
        if(scaleX<.2f||scaleX>8.f||scaleY<.2f||scaleY>8.f||
           std::fabs(offsetX)>.8f||std::fabs(offsetY)>.8f)continue;
        const float fov[4]={(1.f+offsetX)/scaleX,(1.f-offsetX)/scaleX,
                            (1.f-offsetY)/scaleY,(1.f+offsetY)/scaleY};
        if(!NativeCameraMath::ValidFov(fov))continue;
        Perspective result;
        result.nearUnits=-b/a;
        result.infiniteFar=std::fabs(a-1.f)<1e-6f;
        result.farUnits=result.infiniteFar?1e9f:-b/(a-1.f);
        if(result.nearUnits<.005f||result.nearUnits>50.f||
           result.farUnits<100.f||result.farUnits>1e9f)continue;
        if(!NativeCameraMath::ApplyEyeProjection(&result.projection[0][0],fov,
              result.nearUnits,result.farUnits,result.infiniteFar)||
           !Invert(result.projection,result.inverse)||!ProjectedAffine(m,result.inverse))continue;
        Mat4 affine;Multiply(m,result.inverse,affine);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            if(r!=c&&std::fabs(affine[r][c])>.01f)result.hasWorldView=true;
        for(unsigned c=0;c<3;++c)
            if(std::fabs(affine[3][c])>1.f)result.hasWorldView=true;
        result.transposed=transpose!=0;output=result;return true;
    }
    return false;
}
} // namespace StereoRenderMath
