#pragma once

#include "native_camera_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

// Only for a producer-certified native planar-reflection camera packet.
// A reflected view is an improper rigid transform, so it must never pass
// through the ordinary camera's handedness validator or tracking callbacks.
namespace NativeReflectionCameraMath {
using NativeCameraMath::CameraPose;

namespace Detail {
struct Matrix { double a[16]{}; };
inline Matrix Identity() { Matrix m;for(unsigned i=0;i<4;++i)m.a[i*4+i]=1.;return m; }
inline bool Finite(const Matrix& m) {
    for(double value:m.a)if(!std::isfinite(value)||std::fabs(value)>1.e12)return false;
    return true;
}
inline Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix m;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        for(unsigned k=0;k<4;++k)m.a[r*4+c]+=a.a[r*4+k]*b.a[k*4+c];
    return m;
}
inline double Determinant3(const Matrix& m) {
    return m.a[0]*(m.a[5]*m.a[10]-m.a[6]*m.a[9])-
        m.a[1]*(m.a[4]*m.a[10]-m.a[6]*m.a[8])+
        m.a[2]*(m.a[4]*m.a[9]-m.a[5]*m.a[8]);
}
inline bool Rigid(const Matrix& m,int parity) {
    if(!Finite(m)||std::fabs(m.a[3])>1.e-6||std::fabs(m.a[7])>1.e-6||
       std::fabs(m.a[11])>1.e-6||std::fabs(m.a[15]-1.)>1.e-6)return false;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
        double dot=0;for(unsigned k=0;k<3;++k)dot+=m.a[r*4+k]*m.a[c*4+k];
        if(std::fabs(dot-(r==c?1.:0.))>.025)return false;
    }
    return std::fabs(Determinant3(m)-parity)<.05;
}
// The packed inverse pose is NOT a D3D matrix: F/U/R store D3D rows 2/1/0.
inline Matrix View(const CameraPose& p) {
    Matrix m;
    const auto& b=p.basis;
    m.a[0]=b.right.x;m.a[1]=b.right.y;m.a[2]=b.right.z;
    m.a[4]=b.up.x;m.a[5]=b.up.y;m.a[6]=b.up.z;
    m.a[8]=b.forward.x;m.a[9]=b.forward.y;m.a[10]=b.forward.z;
    m.a[12]=p.position.x;m.a[13]=p.position.y;m.a[14]=p.position.z;m.a[15]=1.;
    return m;
}
inline CameraPose Packed(const Matrix& m) {
    CameraPose p;
    p.basis.right={float(m.a[0]),float(m.a[1]),float(m.a[2])};
    p.basis.up={float(m.a[4]),float(m.a[5]),float(m.a[6])};
    p.basis.forward={float(m.a[8]),float(m.a[9]),float(m.a[10])};
    p.position={float(m.a[12]),float(m.a[13]),float(m.a[14])};return p;
}
inline Matrix RigidInverse(const Matrix& m) {
    Matrix out=Identity();
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out.a[r*4+c]=m.a[c*4+r];
    for(unsigned c=0;c<3;++c)for(unsigned k=0;k<3;++k)out.a[12+c]-=m.a[12+k]*out.a[k*4+c];
    return out;
}
inline bool Inverse(const Matrix& m,Matrix& output) {
    if(!Finite(m))return false;
    double rows[4][8]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){rows[r][c]=m.a[r*4+c];rows[r][c+4]=r==c?1.:0.;}
    for(unsigned c=0;c<4;++c) {
        unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::fabs(rows[r][c])>std::fabs(rows[pivot][c]))pivot=r;
        if(std::fabs(rows[pivot][c])<1.e-10)return false;
        for(unsigned k=0;k<8;++k)std::swap(rows[c][k],rows[pivot][k]);
        const double scale=rows[c][c];for(double& value:rows[c])value/=scale;
        for(unsigned r=0;r<4;++r)if(r!=c){const double factor=rows[r][c];for(unsigned k=0;k<8;++k)rows[r][k]-=factor*rows[c][k];}
    }
    Matrix next;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)next.a[r*4+c]=rows[r][c+4];
    if(!Finite(next))return false;output=next;return true;
}
inline bool Projection(const float* input,Matrix& output) {
    if(!input)return false;
    Matrix next;for(unsigned i=0;i<16;++i)next.a[i]=input[i];
    if(!Finite(next)||next.a[0]<=1.e-5||next.a[5]<=1.e-5||std::fabs(next.a[15])>1.e-6||
       std::fabs(next.a[11]-1.)>1.e-5)return false;
    output=next;return true;
}
inline bool PlaneReflection(const Matrix& m) {
    if(!Rigid(m,-1))return false;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)if(std::fabs(m.a[r*4+c]-m.a[c*4+r])>.025)return false;
    const auto twice=Multiply(m,m);
    double translation=1.;for(unsigned c=0;c<3;++c)translation=std::max(translation,std::fabs(m.a[12+c]));
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
        const double tolerance=r==3&&c<3?.003+translation*.0001:.05;
        if(std::fabs(twice.a[r*4+c]-(r==c?1.:0.))>tolerance)return false;
    }
    // Inversion through a point also has det -1 and squares to I, but is not
    // a single reflector plane. A plane mirror has eigenvalues +1,+1,-1.
    return std::fabs(m.a[0]+m.a[5]+m.a[10]-1.)<.05;
}
}

inline bool HorizontalPlane(const CameraPose& original,const CameraPose& reflected,float& height) {
    const auto view=Detail::View(original),other=Detail::View(reflected);
    if(!Detail::Rigid(view,1)||!Detail::Rigid(other,-1))return false;
    const auto mirror=Detail::Multiply(other,Detail::RigidInverse(view));
    if(std::fabs(mirror.a[5]+1.)>.01||!Detail::PlaneReflection(mirror))return false;
    height=float(mirror.a[13]*.5);return true;
}

inline bool BuildView(const CameraPose& originalQueuedView,const CameraPose& reflectedQueuedView,
                      const CameraPose& currentEyeView,CameraPose& output) {
    const auto native=Detail::View(originalQueuedView),reflected=Detail::View(reflectedQueuedView),eye=Detail::View(currentEyeView);
    if(!Detail::Rigid(native,1)||!Detail::Rigid(reflected,-1)||!Detail::Rigid(eye,1))return false;
    // Row vectors: Vreflection=M*Vnative, so M=Vreflection*inverse(Vnative).
    const auto mirror=Detail::Multiply(reflected,Detail::RigidInverse(native));
    if(!Detail::PlaneReflection(mirror))return false;
    const auto next=Detail::Multiply(mirror,eye);
    if(!Detail::Rigid(next,-1))return false;
    output=Detail::Packed(next);return true;
}

// Native 4B8A90 makes P'=P*C. C is identity except column2, containing the
// normalized four-component world clip plane transformed by inverse(V*P).
// Reusing that native column after changing the eye/lens moves the plane.
inline bool BuildProjection(const CameraPose& reflectedNativeView,const float* nativeObliqueProjection,
                            const CameraPose& reflectedEyeView,const float* vrProjection,float* output) {
    if(!output)return false;
    const auto native=Detail::View(reflectedNativeView),eye=Detail::View(reflectedEyeView);
    Detail::Matrix nativeP,eyeP;
    if(!Detail::Rigid(native,-1)||!Detail::Rigid(eye,-1)||
       !Detail::Projection(nativeObliqueProjection,nativeP)||!Detail::Projection(vrProjection,eyeP))return false;
    const auto nativeVP=Detail::Multiply(native,nativeP);
    double worldPlane[4]{};for(unsigned r=0;r<4;++r)worldPlane[r]=nativeVP.a[r*4+2];
    const double normal2=worldPlane[0]*worldPlane[0]+worldPlane[1]*worldPlane[1]+worldPlane[2]*worldPlane[2];
    if(!std::isfinite(normal2)||normal2<1.e-14)return false;
    Detail::Matrix inverse;
    if(!Detail::Inverse(Detail::Multiply(eye,eyeP),inverse))return false;
    double clip[4]{},length2=0;
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)clip[r]+=inverse.a[r*4+c]*worldPlane[c];length2+=clip[r]*clip[r];}
    if(!std::isfinite(length2)||length2<1.e-20)return false;
    const double length=std::sqrt(length2);for(double& value:clip)value/=length;
    // Preserve the original half-space sign. No eye-dependent sign flip:
    // changing which side is visible after head motion would reveal geometry.
    auto next=eyeP;
    for(unsigned r=0;r<4;++r){next.a[r*4+2]=0;for(unsigned k=0;k<4;++k)next.a[r*4+2]+=eyeP.a[r*4+k]*clip[k];}
    if(!Detail::Finite(next))return false;
    float result[16];for(unsigned i=0;i<16;++i)result[i]=float(next.a[i]);
    std::memcpy(output,result,sizeof(result));return true;
}

inline bool Build(const CameraPose& originalQueuedView,const CameraPose& reflectedQueuedView,
                  const float* nativeBaseProjection,const float* nativeObliqueProjection,const CameraPose& currentEyeView,
                  const float* vrProjection,CameraPose& outputView,float* outputProjection) {
    if(!outputProjection)return false;
    CameraPose view;float projection[16];Detail::Matrix base,oblique,vr;
    if(!BuildView(originalQueuedView,reflectedQueuedView,currentEyeView,view)||
       !Detail::Projection(nativeBaseProjection,base)||!Detail::Projection(nativeObliqueProjection,oblique)||
       !Detail::Projection(vrProjection,vr))return false;
    bool clipped=false;
    for(unsigned i=0;i<16;++i)if(std::fabs(base.a[i]-oblique.a[i])>1.e-5) {
        if(i%4!=2)return false;
        clipped=true;
    }
    if(clipped) {
        const auto native=Detail::View(originalQueuedView),reflected=Detail::View(reflectedQueuedView);
        const auto mirror=Detail::Multiply(reflected,Detail::RigidInverse(native));
        // I-M has rank one for a plane mirror. Its largest row gives the
        // physical plane normal without relying on any particular floor axis.
        double normal[3]{},best=0;
        for(unsigned r=0;r<3;++r) {
            double row[3]{},squared=0;
            for(unsigned c=0;c<3;++c){row[c]=(r==c?1.:0.)-mirror.a[r*4+c];squared+=row[c]*row[c];}
            if(squared>best){best=squared;std::memcpy(normal,row,sizeof(normal));}
        }
        const auto nativeVP=Detail::Multiply(reflected,oblique);
        double squared=0,dot=0;
        for(unsigned i=0;i<3;++i){const double value=nativeVP.a[i*4+2];squared+=value*value;dot+=normal[i]*value;}
        // The game's bias may move the clipping plane, but cannot rotate it
        // away from its own mirror surface. Reject unrelated depth matrices.
        if(best<1.e-10||squared<1.e-14||dot*dot<.99*best*squared||
           !BuildProjection(reflectedQueuedView,nativeObliqueProjection,view,vrProjection,projection))return false;
    } else {
        // 4B8A90 intentionally leaves P untouched when its plane degenerates.
        // Do not turn an ordinary near plane into a world-fixed oblique clip.
        std::memcpy(projection,vrProjection,sizeof(projection));
    }
    outputView=view;std::memcpy(outputProjection,projection,sizeof(projection));return true;
}
}
