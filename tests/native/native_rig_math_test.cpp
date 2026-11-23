#include "native_rig_math.hpp"
#include "native_hands_math.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace NativeCameraMath;
using namespace NativeRigMath;
namespace {
int failures=0,checks=0;
void Check(bool okay,const char* label) {
    ++checks;if(!okay){++failures;if(failures<20)std::printf("FAIL: %s\n",label);}
}
bool Near(Vec3 a,Vec3 b,float tolerance=.002f) {return NativeArmMath::Length(Subtract(a,b))<tolerance;}
Basis Along(Vec3 direction) {
    Vec3 right,up;NativeArmMath::Unit(direction,right);
    const Vec3 seed=std::fabs(right.y)<.8f?Vec3{0,1,0}:Vec3{0,0,1};
    NativeArmMath::Unit(Subtract(seed,Scale(right,Dot(seed,right))),up);
    return {Cross(right,up),up,right};
}
void ControllerRows(Quat q,float output[9]) {
    const Vec3 axes[3]={Rotate(q,{1,0,0}),Rotate(q,{0,1,0}),Rotate(q,{0,0,1})};
    for(unsigned i=0;i<3;++i){output[i*3]=axes[i].x;output[i*3+1]=axes[i].y;output[i*3+2]=axes[i].z;}
}
Quat Yaw(float radians){return {0.f,std::sin(radians*.5f),0.f,std::cos(radians*.5f)};}
Basis RotatedBasis(Quat q){return {Rotate(q,{0,0,1}),Rotate(q,{0,1,0}),Rotate(q,{1,0,0})};}
}
int main() {
    constexpr unsigned count=12,upper=3,lower=4,wrist=5;
    const std::uint32_t parents[count]={0,0,0,2,3,4,5,6,0,8,9,10};
    std::array<Bone,count> animated{};
    for(auto& b:animated)b.basis={{0,0,1},{0,1,0},{1,0,0}};
    // Read-only native Contracts standing-pose arm lengths/coordinates.
    animated[upper].position={18.8f,144.05f,1.69f};
    animated[lower].position=Add(animated[upper].position,{2.8944f,-26.2732f,.4736f});
    animated[wrist].position=Add(animated[lower].position,{3.2268f,-25.3678f,-4.3685f});
    animated[upper].basis=Along(Subtract(animated[lower].position,animated[upper].position));
    animated[lower].basis=Along(Subtract(animated[wrist].position,animated[lower].position));
    animated[wrist].basis=Along({.2f,-.8f,-.3f});
    animated[6].basis=animated[wrist].basis;animated[7].basis=animated[wrist].basis;
    animated[6].position=Add(animated[wrist].position,Transform(animated[wrist].basis,{4.f,1.f,0}));
    animated[7].position=Add(animated[6].position,Transform(animated[wrist].basis,{3.f,0,.5f}));
    for(unsigned i=8;i<count;++i)animated[i].position={-float(i),100.f+float(i),2.f};
    const float upperLength=NativeArmMath::Length(Subtract(animated[lower].position,animated[upper].position));
    const float lowerLength=NativeArmMath::Length(Subtract(animated[wrist].position,animated[lower].position));
    const Vec3 fingerLocal=Inverse(animated[wrist].basis,Subtract(animated[7].position,animated[wrist].position));
    const Quat rotation{.18257418f,.36514836f,-.18257418f,.89442719f};
    Basis absolute;
    absolute.forward=Rotate(rotation,{0,0,1});absolute.up=Rotate(rotation,{0,1,0});absolute.right=Rotate(rotation,{1,0,0});
    Check(ValidateBasis(absolute),"test wrist orientation is native orthonormal");
    for(int x=-3;x<=3;++x)for(int y=-3;y<=3;++y)for(int z=-3;z<=3;++z) {
        CameraPose target{absolute,Add(animated[upper].position,{float(x)*19.f,float(y)*19.f,float(z)*19.f})};
        std::array<Bone,count> posed;Result result;
        const bool okay=SolveArm(animated.data(),parents,count,upper,lower,wrist,target,{.75f,-.7f,.45f},posed.data(),result);
        Check(okay,"native subtree solve succeeds for workspace target");if(!okay)continue;
        Check(Near(posed[upper].position,animated[upper].position),"shoulder remains attached to clavicle");
        Check(std::fabs(NativeArmMath::Length(Subtract(posed[lower].position,posed[upper].position))-upperLength)<.002f,"native upper-arm length preserved");
        Check(std::fabs(NativeArmMath::Length(Subtract(posed[wrist].position,posed[lower].position))-lowerLength)<.002f,"native forearm length preserved");
        Check(Near(Scale(posed[upper].basis.right,upperLength),Subtract(posed[lower].position,posed[upper].position)),"upper-arm local X follows solved segment");
        Check(Near(Scale(posed[lower].basis.right,lowerLength),Subtract(posed[wrist].position,posed[lower].position)),"forearm local X follows solved segment");
        Check(Near(posed[wrist].basis.right,absolute.right)&&Near(posed[wrist].basis.up,absolute.up)&&Near(posed[wrist].basis.forward,absolute.forward),"absolute tracked wrist rotation preserved");
        Check(Near(Inverse(posed[wrist].basis,Subtract(posed[7].position,posed[wrist].position)),fingerLocal),"finger descendants retain animated wrist-relative pose");
        Check(!std::memcmp(posed.data()+8,animated.data()+8,4*sizeof(Bone)),"opposite arm is byte-for-byte unchanged");
        Check(!std::memcmp(posed.data(),animated.data(),upper*sizeof(Bone)),"head and clavicle are byte-for-byte unchanged");
        const float distance=NativeArmMath::Length(Subtract(target.position,animated[upper].position));
        Check(distance>upperLength+lowerLength?result.clamped:true,"unreachable controller position reports anatomical clamp");
        if(distance>1.f&&distance<upperLength+lowerLength-.01f)
            Check(Near(posed[wrist].position,target.position),"reachable target is reached exactly");
    }
    // Native root is a proper R/U/F coordinate transform despite stored F/U/R
    // rows having negative determinant. Include a translated, rotated root.
    CameraPose root{absolute,{48.f,22.f,9900.f}},local{animated[wrist].basis,animated[wrist].position};
    const CameraPose world=ToWorld(root,{local.basis,local.position});
    const CameraPose roundtrip=ToModel(root,world);
    Check(Near(roundtrip.position,local.position,.003f)&&Near(roundtrip.basis.right,local.basis.right)&&Near(roundtrip.basis.up,local.basis.up),"native model/world wrist roundtrip handles translated reflected storage basis");
    std::array<Bone,count> untouched=animated;Result result;
    CameraPose invalid{absolute,{1,2,3}};invalid.basis.right.x=std::numeric_limits<float>::quiet_NaN();
    Check(!SolveArm(animated.data(),parents,count,upper,lower,wrist,invalid,{0,1,0},untouched.data(),result)&&!std::memcmp(untouched.data(),animated.data(),sizeof(animated)),"invalid wrist rejects without mutating bones");
    auto cyclic=std::array<std::uint32_t,count>{};std::memcpy(cyclic.data(),parents,sizeof(parents));cyclic[7]=7;
    Check(!SolveArm(animated.data(),cyclic.data(),count,upper,lower,wrist,local,{0,1,0},untouched.data(),result)&&!std::memcmp(untouched.data(),animated.data(),sizeof(animated)),"cyclic hierarchy rejects without mutating bones");
    auto aliased=animated;
    Check(SolveArm(aliased.data(),parents,count,upper,lower,wrist,{absolute,{20,130,20}},{0,-1,0},aliased.data(),result),"in-place native bone array is supported");
    {
        // Actual K&L bind positions/name lookup: anatomical LEFT is model +X,
        // RIGHT is -X; the toes and facial bones establish forward as -Z.
        // Rotate the animation independently inside its root, then compensate
        // that root so the anatomical body still faces camera/world +Z.
        constexpr float pi=3.14159265358979323846f;
        const NativeArmMath::Joints arms[2]={
            {{19.7395f,144.4428f,3.2309f},{36.1610f,125.4871f,4.7658f},{51.1421f,107.4992f,-1.1701f}},
            {{-19.7395f,144.4428f,3.2309f},{-36.1610f,125.4871f,4.7658f},{-51.1421f,107.4992f,-1.1701f}}};
        const float angles[]={0.f,pi*.5f,-pi*.5f,pi,-pi};
        for(float angle:angles) {
            const Quat animation=Yaw(angle);
            const CameraPose renderRoot{RotatedBasis(Yaw(pi-angle)),{400.f,12.f,-900.f}};
            const Vec3 left=Rotate(animation,arms[0].shoulder),right=Rotate(animation,arms[1].shoulder);
            const Vec3 down=Inverse(renderRoot.basis,{0,-1,0});
            Check(Near(Transform(renderRoot.basis,Rotate(animation,{0,0,-1})),{0,0,1}),
                  "compensated K&L body faces world forward with model negative Z");
            NativeArmMath::Joints solved[2];
            for(int side=0;side<2;++side) {
                Vec3 pole;
                const bool validPole=NativeArmMath::ElbowPole(left,right,down,side,pole);
                Check(validPole,"animated anatomical shoulder line supplies both elbow poles");
                if(!validPole)continue;
                const Vec3 expectedWorld=Scale(Vec3{side?1.f:-1.f,-.5f,0},1.f/std::sqrt(1.25f));
                Check(Near(Transform(renderRoot.basis,pole),expectedWorld),
                      "elbow pole remains outward and downward through internal animation turns");
                Check(std::fabs(NativeArmMath::Length(pole)-1.f)<.00001f,"elbow pole is a unit direction");
                const NativeArmMath::Joints animatedArm{Rotate(animation,arms[side].shoulder),
                    Rotate(animation,arms[side].elbow),Rotate(animation,arms[side].wrist)};
                // Targets are separated in front of the correctly facing body,
                // at realistic reach, independent of animation-local heading.
                const Vec3 targetWorld=Add(renderRoot.position,{side?15.f:-15.f,125.f,35.f});
                const Vec3 target=Inverse(renderRoot.basis,Subtract(targetWorld,renderRoot.position));
                bool clamped=true;
                const bool okay=NativeArmMath::Solve(animatedArm,target,pole,solved[side],&clamped);
                Check(okay&&!clamped,"both forward controller targets lie inside native arm reach");
                if(!okay)continue;
                const Vec3 elbowWorld=Add(renderRoot.position,Transform(renderRoot.basis,solved[side].elbow));
                const float signedElbow=(elbowWorld.x-renderRoot.position.x)*(side?1.f:-1.f);
                Check(signedElbow>19.f,"each elbow stays outside its shoulder and sagittal plane");
                Check(Near(Add(renderRoot.position,Transform(renderRoot.basis,solved[side].wrist)),targetWorld),
                      "dynamic elbow pole preserves forward wrist target");
                Check(std::fabs(NativeArmMath::Length(Subtract(solved[side].elbow,solved[side].shoulder))-
                                NativeArmMath::Length(Subtract(animatedArm.elbow,animatedArm.shoulder)))<.002f&&
                      std::fabs(NativeArmMath::Length(Subtract(solved[side].wrist,solved[side].elbow))-
                                NativeArmMath::Length(Subtract(animatedArm.wrist,animatedArm.elbow)))<.002f,
                      "dynamic pole preserves both native arm segment lengths");
            }
            const Vec3 leftElbow=Transform(renderRoot.basis,solved[0].elbow);
            const Vec3 rightElbow=Transform(renderRoot.basis,solved[1].elbow);
            Check(Near(leftElbow,{-rightElbow.x,rightElbow.y,rightElbow.z}),
                  "native mirrored arms have mirrored elbow endpoints after internal animation turns");
        }
        Vec3 sentinel{7,8,9},pole=sentinel;
        const Vec3 nan{std::numeric_limits<float>::quiet_NaN(),0,0};
        const Vec3 inf{0,std::numeric_limits<float>::infinity(),0};
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,arms[1].shoulder,{0,-1,0},-1,pole)&&Near(pole,sentinel),
              "negative elbow pole side rejects without modifying output");
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,arms[1].shoulder,{0,-1,0},2,pole)&&Near(pole,sentinel),
              "out of range elbow pole side rejects without modifying output");
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,arms[0].shoulder,{0,-1,0},0,pole)&&Near(pole,sentinel),
              "coincident shoulders reject without modifying output");
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,arms[1].shoulder,{0,0,0},0,pole)&&Near(pole,sentinel),
              "zero model down rejects without modifying output");
        Check(!NativeArmMath::ElbowPole(nan,arms[1].shoulder,{0,-1,0},0,pole)&&Near(pole,sentinel),
              "nonfinite left shoulder rejects without modifying output");
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,inf,{0,-1,0},1,pole)&&Near(pole,sentinel),
              "nonfinite right shoulder rejects without modifying output");
        Check(!NativeArmMath::ElbowPole(arms[0].shoulder,arms[1].shoulder,nan,0,pole)&&Near(pole,sentinel),
              "nonfinite model down rejects without modifying output");
        Check(NativeArmMath::ElbowPole(arms[0].shoulder,arms[1].shoulder,{0,-30,0},0,pole)&&
              Near(pole,Scale(Vec3{1,-.5f,0},1.f/std::sqrt(1.25f))),
              "model down magnitude does not change outward downward balance");
        const Basis tiltedRoot=RotatedBasis(rotation);
        const Vec3 tiltedLeft=Inverse(tiltedRoot,{-20,140,0});
        const Vec3 tiltedRight=Inverse(tiltedRoot,{20,140,0});
        for(int side=0;side<2;++side)
            Check(NativeArmMath::ElbowPole(tiltedLeft,tiltedRight,Inverse(tiltedRoot,{0,-1,0}),side,pole)&&
                  Near(Transform(tiltedRoot,pole),Scale(Vec3{side?1.f:-1.f,-.5f,0},1.f/std::sqrt(1.25f))),
                  "tilted render roots keep elbow down bias aligned to world gravity");
        pole=sentinel;
        Check(!NativeArmMath::ElbowPole({0,0,0},{1e-8f,0,0},{0,-1,0},0,pole)&&Near(pole,sentinel),
              "degenerate shoulder separation rejects without modifying output");
        const float maximum=std::numeric_limits<float>::max();
        Check(!NativeArmMath::ElbowPole({maximum,0,0},{-maximum,0,0},{0,-1,0},1,pole)&&Near(pole,sentinel),
              "overflowing shoulder difference rejects without modifying output");
    }
    {
        // Camera basis and its raw HMD reference must cancel exactly, so a
        // head turn cannot rotate world-stable controllers. Position deltas
        // use hand/head from one HandsIPC sample, independent of its origin.
        const CameraPose centre{{{0,0,1},{0,1,0},{1,0,0}},{100,165,-200}};
        const Vec3 headPosition{0,1.6f,0},handOffset{.2f,-.35f,-.4f};
        const Quat handOrientation{.18257418f,.36514836f,-.18257418f,.89442719f};
        float originalRows[9];ControllerRows(handOrientation,originalRows);
        for(unsigned side=0;side<2;++side) {
            CameraPose expected;
            Check(NativeHandsMath::ControllerTarget(centre,centre.position,{},originalRows,
                Add(headPosition,handOffset),headPosition,side,100,1.5f,expected),"same-sample controller target builds at frozen render head");
            for(unsigned sample=0;sample<72;++sample) {
                const float half=float(sample)*.0436332313f;
                const Quat common{std::sin(half)*.26726124f,std::sin(half)*.53452248f,std::sin(half)*.80178373f,std::cos(half)};
                const Vec3 newerHead{.3f,1.4f,-.2f};
                const Vec3 newerHand=Add(newerHead,handOffset);
                CameraPose target,trackedCamera;
                Check(Compose(centre,common,{0,0,0},{0,0,0},100,trackedCamera)&&
                    NativeHandsMath::ControllerTarget(trackedCamera,trackedCamera.position,common,originalRows,newerHand,newerHead,
                    side,100,1.5f,target),"actual camera and its raw head reference produce a valid controller target");
                Check(Near(target.position,expected.position)&&Near(target.basis.forward,expected.basis.forward)&&
                      Near(target.basis.up,expected.basis.up)&&Near(target.basis.right,expected.basis.right),
                      "world-stable controller is invariant to camera head rotation and same-sample tracking origin translation");
            }
        }
    }
    std::printf("Native rig pose math: %d checks, %s\n",checks,failures?"FAILED":"passed");
    return failures?1:0;
}
