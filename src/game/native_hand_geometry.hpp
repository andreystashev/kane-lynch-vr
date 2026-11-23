#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>

// Procedural glove anatomy in native wrist-local METRES: +X fingers, +Z
// palm, thumb toward -Y left / +Y right. No controller or weapon offsets.
namespace NativeHandGeometry {
using namespace NativeCameraMath;
constexpr unsigned FingerCount=5,NativeJoints=3,PalmRings=3,RingSides=12;
struct Finger {Vec3 joint[4]{};float radius=0;};
struct Shape {
    Finger finger[FingerCount]{}; // thumb, index, middle, ring, pinky
    Vec3 palm[PalmRings][RingSides]{};
    Vec3 palmFront{};
};
inline Vec3 LocalPoint(const CameraPose& wrist,Vec3 world,float unitsPerMetre) {
    const Vec3 relative=Scale(Subtract(world,wrist.position),1.f/unitsPerMetre);
    return {Dot(relative,wrist.basis.right),Dot(relative,wrist.basis.up),Dot(relative,wrist.basis.forward)};
}
inline Vec3 LocalDirection(const CameraPose& wrist,Vec3 world) {
    return {Dot(world,wrist.basis.right),Dot(world,wrist.basis.up),Dot(world,wrist.basis.forward)};
}
inline float Length(Vec3 value){return std::sqrt(Dot(value,value));}
inline float Radius(unsigned finger) {
    constexpr float values[FingerCount]={.0105f,.009f,.0092f,.0088f,.0075f};
    return values[finger];
}
inline void Palm(Shape& shape) {
    std::array<Vec3,4> knuckles;
    shape.palmFront={};
    float low=1.f,high=-1.f;
    for(unsigned i=0;i<4;++i) {
        knuckles[i]=shape.finger[i+1].joint[0];
        low=std::min(low,knuckles[i].y-shape.finger[i+1].radius*.8f);
        high=std::max(high,knuckles[i].y+shape.finger[i+1].radius*.8f);
        shape.palmFront=Add(shape.palmFront,Scale(knuckles[i],.25f));
    }
    std::sort(knuckles.begin(),knuckles.end(),[](Vec3 a,Vec3 b){return a.y<b.y;});
    const float centre=(low+high)*.5f,width=(high-low)*.5f;
    constexpr float pi=3.14159265358979323846f;
    for(unsigned i=0;i<RingSides;++i) {
        const float angle=2*pi*i/RingSides,c=std::cos(angle),s=std::sin(angle);
        shape.palm[0][i]={-.012f,c*.025f,s*.013f};
        shape.palm[1][i]={.036f,centre*.4f+c*(.025f*.4f+width*.6f),shape.palmFront.z*.4f+s*.015f};
        const float y=centre+c*width;
        Vec3 front=y<knuckles[0].y?knuckles[0]:knuckles[3];
        for(unsigned j=0;j<3;++j)if(y>=knuckles[j].y&&y<=knuckles[j+1].y) {
            const float span=knuckles[j+1].y-knuckles[j].y;
            const float t=span>1e-6f?(y-knuckles[j].y)/span:0.f;
            front=Add(Scale(knuckles[j],1.f-t),Scale(knuckles[j+1],t));break;
        }
        // The front follows the actual MCP arch rather than ending halfway
        // along the native palm. Finger-root volumes overlap this surface.
        shape.palm[2][i]={front.x-.004f,y,front.z+s*.013f};
    }
}
inline bool Anatomy(const Shape& shape,unsigned side) {
    if(side>1)return false;
    for(unsigned f=0;f<FingerCount;++f) {
        for(unsigned j=0;j<4;++j)if(!Finite(shape.finger[f].joint[j])||Length(shape.finger[f].joint[j])>.35f)return false;
        for(unsigned j=0;j<3;++j) {
            const float length=Length(Subtract(shape.finger[f].joint[j+1],shape.finger[f].joint[j]));
            if(length<.004f||length>.09f)return false;
        }
        const auto base=shape.finger[f].joint[0];
        if(base.x<(f==0?.015f:.05f)||base.x>.16f)return false;
    }
    return shape.finger[0].joint[0].y*(side==0?-1.f:1.f)>.01f;
}
// Exact native MCP/PIP/DIP positions and distal bone axis from the SAME rig
// snapshot as the wrist. Only the unmodelled terminal tip is extrapolated.
// Inputs are world-space game units; output is local metres. Failure leaves
// output untouched, so invalid/mixed skeleton data can use the fallback.
inline bool FromNative(unsigned side,const CameraPose& wrist,
                       const CameraPose (&fingers)[FingerCount][NativeJoints],
                       float unitsPerMetre,Shape& output) {
    if(side>1||!ValidatePose(wrist,.05f)||!std::isfinite(unitsPerMetre)||unitsPerMetre<.001f||unitsPerMetre>100000.f)return false;
    Shape next;
    for(unsigned f=0;f<FingerCount;++f) {
        auto& finger=next.finger[f];finger.radius=Radius(f);
        for(unsigned j=0;j<NativeJoints;++j) {
            if(!ValidatePose(fingers[f][j],.05f))return false;
            finger.joint[j]=LocalPoint(wrist,fingers[f][j].position,unitsPerMetre);
        }
        Vec3 axis=LocalDirection(wrist,fingers[f][2].basis.right);
        const float length=Length(axis);
        if(!std::isfinite(length)||length<.95f||length>1.05f)return false;
        // Native +X is verified along every first/second phalanx. The final
        // bone supplies its own distal orientation, including held animation.
        const float tipLength=Length(Subtract(finger.joint[2],finger.joint[1]))*.65f;
        finger.joint[3]=Add(finger.joint[2],Scale(axis,tipLength/length));
    }
    if(!Anatomy(next,side))return false;
    Palm(next);output=next;return true;
}
inline Vec3 Curl(Vec3 vector,float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    return {vector.x*c-vector.z*s,vector.y,vector.x*s+vector.z*c};
}
// Measured Contracts LEFT_HAND bind anatomy (centimetres / 100), mirrored
// about wrist Y for the right hand. This preserves native palm length when
// native fingers are unavailable, and retains analog free-hand animation.
inline bool Fallback(unsigned side,float grip,float trigger,Shape& output) {
    if(side>1||!std::isfinite(grip)||!std::isfinite(trigger))return false;
    grip=std::clamp(grip,0.f,1.f);trigger=std::clamp(trigger,0.f,1.f);
    constexpr Vec3 bind[FingerCount][NativeJoints]={
        {{.0351985f,-.0321264f,.0134241f},{.0615999f,-.0615215f,.0304992f},{.0888350f,-.0796727f,.0471491f}},
        {{.1010844f,-.0298941f,-.0031973f},{.1389569f,-.0342179f,.0080903f},{.1686619f,-.0376094f,.0169436f}},
        {{.1045223f,-.0055901f,-.0008448f},{.1508211f,-.0038974f,.0146481f},{.1799596f,-.0028263f,.0249678f}},
        {{.0985419f,.0204418f,.0010594f},{.1415112f,.0249469f,.0151016f},{.1664440f,.0275608f,.0234922f}},
        {{.0899283f,.0378887f,.0042474f},{.1215829f,.0452318f,.0205154f},{.1367720f,.0487300f,.0288349f}}
    };
    Shape next;
    for(unsigned f=0;f<FingerCount;++f) {
        auto& finger=next.finger[f];finger.radius=Radius(f);
        const float amount=f==1?std::max(trigger,grip*.65f):grip;
        finger.joint[0]=bind[f][0];
        if(f==0) {
            // Thumb opposition reaches across the grip while preserving its
            // native root and both segment lengths; mirror only after posing.
            const float a=amount*.65f,c=std::cos(a),s=std::sin(a);
            for(unsigned j=1;j<3;++j) {
                Vec3 vector=Subtract(bind[f][j],bind[f][j-1]);
                vector={vector.x*c-vector.y*s,vector.x*s+vector.y*c,vector.z};
                finger.joint[j]=Add(finger.joint[j-1],vector);
            }
        } else {
            finger.joint[1]=Add(finger.joint[0],Curl(Subtract(bind[f][1],bind[f][0]),amount*1.15f));
            finger.joint[2]=Add(finger.joint[1],Curl(Subtract(bind[f][2],bind[f][1]),amount*2.55f));
        }
        Vec3 distal=Subtract(bind[f][2],bind[f][1]);
        distal=f==0?Scale(Subtract(finger.joint[2],finger.joint[1]),.65f):Scale(Curl(distal,amount*3.05f),.65f);
        finger.joint[3]=Add(finger.joint[2],distal);
        if(side==1)for(auto& joint:finger.joint)joint.y=-joint.y;
    }
    if(!Anatomy(next,side))return false;
    Palm(next);output=next;return true;
}
} // namespace NativeHandGeometry
