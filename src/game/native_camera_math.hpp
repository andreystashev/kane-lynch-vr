#pragma once

#include <cmath>

// Camera-to-world math for the native Contracts camera. This is deliberately
// independent of Windows, D3D, and the headset SDK so its axis conventions can
// be tested without starting the game.
namespace NativeCameraMath {

struct Vec3 { float x=0.f,y=0.f,z=0.f; };
struct Quat { float x=0.f,y=0.f,z=0.f,w=1.f; };
// Native matrix rows are forward, up, right. Native right = up x forward,
// so the F/U/R matrix has determinant -1; it is not a quaternion matrix.
struct Basis {
    Vec3 forward{1.f,0.f,0.f};
    Vec3 up{0.f,1.f,0.f};
    Vec3 right{0.f,0.f,-1.f};
};
struct CameraPose { Basis basis{}; Vec3 position{}; };

inline Vec3 Add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 Subtract(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 Scale(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a,Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline bool Finite(Vec3 a) {
    return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);
}
inline bool Finite(Quat q) {
    return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w);
}

// Reject missing/corrupt tracking while accepting ordinary sensor drift from
// unit length. No output argument is modified on failure, throughout this API.
inline bool NormalizeQuaternion(const Quat& input,Quat& output) {
    if(!Finite(input))return false;
    const float length2=input.x*input.x+input.y*input.y+input.z*input.z+input.w*input.w;
    if(!std::isfinite(length2)||length2<0.25f||length2>4.f)return false;
    const float inverseLength=1.f/std::sqrt(length2);
    output={input.x*inverseLength,input.y*inverseLength,input.z*inverseLength,input.w*inverseLength};
    return true;
}
inline Quat Conjugate(Quat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quat Multiply(Quat a,Quat b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
// q must be a unit quaternion. LibOVR xyzw quaternions rotate local vectors
// into tracking space; conjugating q here would invert all head rotations.
inline Vec3 Rotate(Quat q,Vec3 v) {
    const Vec3 imaginary{q.x,q.y,q.z};
    const Vec3 twiceCross=Scale(Cross(imaginary,v),2.f);
    return Add(v,Add(Scale(twiceCross,q.w),Cross(imaginary,twiceCross)));
}

inline bool ValidateBasis(const Basis& b,float tolerance=0.025f) {
    if(!std::isfinite(tolerance)||tolerance<=0.f||tolerance>0.1f ||
       !Finite(b.forward)||!Finite(b.up)||!Finite(b.right))return false;
    if(std::fabs(Dot(b.forward,b.forward)-1.f)>tolerance ||
       std::fabs(Dot(b.up,b.up)-1.f)>tolerance ||
       std::fabs(Dot(b.right,b.right)-1.f)>tolerance ||
       std::fabs(Dot(b.forward,b.up))>tolerance ||
       std::fabs(Dot(b.forward,b.right))>tolerance ||
       std::fabs(Dot(b.up,b.right))>tolerance)return false;
    // This intentionally accepts the negative determinant of native F/U/R.
    return Dot(Cross(b.up,b.forward),b.right)>1.f-2.f*tolerance;
}
inline bool ValidatePose(const CameraPose& p,float tolerance=0.025f) {
    return Finite(p.position)&&ValidateBasis(p.basis,tolerance);
}

// Tracking axes are +X right, +Y up, -Z forward. This mapping is a change of
// coordinate convention, including its reflection, not an inverse rotation.
inline Vec3 MapTracking(const Basis& base,Vec3 trackingVector) {
    return Add(Add(Scale(base.right,trackingVector.x),Scale(base.up,trackingVector.y)),
               Scale(base.forward,-trackingVector.z));
}

// base is the unmodified native camera pose for this render, in game units.
// relativeHead is the headset camera-to-world rotation relative to its recenter
// reference. displacementMetres is already expressed in the reference axes;
// eyeOffsetMetres is an SDK per-eye offset in head-local axes. Do not use an SDK
// eye pose position as displacement and also add eyeOffset: that doubles IPD.
inline bool Compose(const CameraPose& base,const Quat& relativeHead,
                    const Vec3& displacementMetres,const Vec3& eyeOffsetMetres,
                    float unitsPerMetre,CameraPose& output) {
    if(!ValidatePose(base)||!Finite(displacementMetres)||!Finite(eyeOffsetMetres)||
       !std::isfinite(unitsPerMetre)||unitsPerMetre<=0.f||unitsPerMetre>100000.f)return false;
    Quat q;
    if(!NormalizeQuaternion(relativeHead,q))return false;
    CameraPose result;
    result.basis.forward=MapTracking(base.basis,Rotate(q,{0.f,0.f,-1.f}));
    result.basis.up=MapTracking(base.basis,Rotate(q,{0.f,1.f,0.f}));
    result.basis.right=MapTracking(base.basis,Rotate(q,{1.f,0.f,0.f}));
    const Vec3 trackingOffset=Add(displacementMetres,Rotate(q,eyeOffsetMetres));
    result.position=Add(base.position,Scale(MapTracking(base.basis,trackingOffset),unitsPerMetre));
    if(!ValidatePose(result))return false;
    output=result;
    return true;
}

// Use a yaw-only recenter reference when the engine's base supplies body yaw.
// This zeros physical heading at recenter while retaining absolute head pitch
// and roll. A near-vertical gaze has no reliable horizontal heading: return
// false and retain the previous reference rather than jumping the view.
inline bool MakeYawReference(const Quat& head,Quat& output) {
    Quat q;
    if(!NormalizeQuaternion(head,q))return false;
    const Vec3 forward=Rotate(q,{0.f,0.f,-1.f});
    if(forward.x*forward.x+forward.z*forward.z<1e-6f)return false;
    const float yaw=std::atan2(-forward.x,-forward.z);
    output={0.f,std::sin(yaw*0.5f),0.f,std::cos(yaw*0.5f)};
    return true;
}

// Full reference orientations are supported too. A full-pose reference zeros
// pitch/roll as well, whereas MakeYawReference preserves them. Position and
// orientation must use the same reference axes to keep room-scale motion and
// per-eye rotation consistent after a recenter.
inline bool RelativeTrackingPose(const Quat& head,const Vec3& position,
                                 const Quat& referenceOrientation,const Vec3& referencePosition,
                                 Quat& relativeHead,Vec3& displacementMetres) {
    Quat q,reference;
    if(!Finite(position)||!Finite(referencePosition)||
       !NormalizeQuaternion(head,q)||!NormalizeQuaternion(referenceOrientation,reference))return false;
    const Quat inverseReference=Conjugate(reference);
    Quat relative;
    if(!NormalizeQuaternion(Multiply(inverseReference,q),relative))return false;
    const Vec3 displacement=Rotate(inverseReference,Subtract(position,referencePosition));
    if(!Finite(displacement))return false;
    relativeHead=relative;
    displacementMetres=displacement;
    return true;
}

// Positive half-angle tangents, ordered left, right, up, down as in LibOVR.
inline bool ValidFov(const float* fov) {
    if(!fov)return false;
    for(int side=0;side<4;++side)
        if(!std::isfinite(fov[side])||fov[side]<0.05f||fov[side]>5.f)return false;
    return true;
}

// Build the complete row-major, row-vector D3D LH perspective (+Z forward,
// clip depth 0..1). Pass the native camera's far distance/infinite-far flag to
// retain its depth convention while replacing its FOV and near distance.
// An infinite far plane does not consume farUnits, which may be +infinity.
inline bool ApplyEyeProjection(float* matrix16,const float* fov,
                               float nearUnits,float farUnits,bool infiniteFar) {
    if(!matrix16||!ValidFov(fov)||!std::isfinite(nearUnits)||nearUnits<=0.f)return false;
    if(!infiniteFar&&(!std::isfinite(farUnits)||farUnits<=nearUnits))return false;
    const float horizontal=fov[0]+fov[1],vertical=fov[2]+fov[3];
    const double depth=infiniteFar?1.0:static_cast<double>(farUnits)/(static_cast<double>(farUnits)-nearUnits);
    float result[16]{};
    result[0]=2.f/horizontal;
    result[5]=2.f/vertical;
    result[8]=(fov[0]-fov[1])/horizontal;
    result[9]=(fov[3]-fov[2])/vertical;
    result[10]=static_cast<float>(depth);
    result[11]=1.f;
    result[14]=static_cast<float>(-static_cast<double>(nearUnits)*depth);
    for(float value:result)if(!std::isfinite(value))return false;
    for(int element=0;element<16;++element)matrix16[element]=result[element];
    return true;
}

// The Contracts CameraSpace fields +0x104..+0x110 are FOV (radians), Pers,
// ScaleX and ScaleY. Its CPU Proj3D (0x623BF0) reconstructs frustum corners
// from screen +/-0.5 using z/Pers and 1/ScaleY. Widening just the clip planes
// leaves those independent portal/bounds corners at the old monitor FOV.
// Supply the conservative union of both eyes, including the visibility guard
// band. The native scalar projection is symmetric, so enclose every side of
// that union. The render matrix continues to use its actual asymmetric FOV.
inline bool ApplyVisibilityProjection(float* fovPersScaleXY,const float* fov) {
    if(!fovPersScaleXY||!ValidFov(fov))return false;
    const float horizontal=fov[0]>fov[1]?fov[0]:fov[1];
    const float vertical=fov[2]>fov[3]?fov[2]:fov[3];
    const float result[4]={2.f*std::atan(horizontal),.5f/horizontal,1.f,horizontal/vertical};
    for(float value:result)if(!std::isfinite(value)||value<=0.f)return false;
    for(unsigned field=0;field<4;++field)fovPersScaleXY[field]=result[field];
    return true;
}

// CameraSpace holds outward local clip planes (a,b,c,d), with inside <= 0.
// This buffer has room for 12 planes. Depth planes 0/1 and optional portal
// planes 6..11 remain exactly as supplied; only side planes 2..5 change.
inline bool ApplyClipFov(float* planes48,const float* fov) {
    if(!planes48||!ValidFov(fov))return false;
    const float normals[4][3]={{1.f,0.f,-fov[1]},{-1.f,0.f,-fov[0]},
                              {0.f,1.f,-fov[2]},{0.f,-1.f,-fov[3]}};
    for(int side=0;side<4;++side) {
        const float inverseLength=1.f/std::sqrt(1.f+normals[side][2]*normals[side][2]);
        for(int axis=0;axis<3;++axis)planes48[(side+2)*4+axis]=normals[side][axis]*inverseLength;
        planes48[(side+2)*4+3]=0.f;
    }
    return true;
}

} // namespace NativeCameraMath
