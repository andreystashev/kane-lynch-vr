#include "native_skin_math.hpp"
#include "native_arm_volume_fixture.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace NativeCameraMath;
using NativeRigMath::Bone;
using NativeRigMath::ArmOptions;
using NativeRigMath::Result;
constexpr float Pi=3.14159265358979323846f;
constexpr unsigned Count=15,Clavicle=1,Upper=2,Lower=3,Wrist=4,Twist=5,Twist1=6,Muscle=7;
using Bones=std::array<Bone,Count>;
const std::uint32_t Parents[Count]={0,0,1,2,3,2,5,1,0,8,9,0,4,12,4};
unsigned checks=0,failures=0;
void Check(bool okay,const char* label) {
    ++checks;
    if(!okay){++failures;if(failures<=24)std::printf("FAIL: %s\n",label);}
}
float Length(Vec3 value){return std::sqrt(Dot(value,value));}
Vec3 Unit(Vec3 value){return Scale(value,1.f/Length(value));}
bool Near(Vec3 a,Vec3 b,float tolerance=.003f){return Length(Subtract(a,b))<=tolerance;}
bool Near(const Basis& a,const Basis& b,float tolerance=.003f) {
    return Near(a.right,b.right,tolerance)&&Near(a.up,b.up,tolerance)&&Near(a.forward,b.forward,tolerance);
}
bool Near(const Bone& a,const Bone& b,float tolerance=.003f) {
    return Near(a.position,b.position,tolerance)&&Near(a.basis,b.basis,tolerance);
}
Basis Along(Vec3 direction) {
    const Vec3 right=Unit(direction);
    const Vec3 seed=std::fabs(right.y)<.8f?Vec3{0,1,0}:Vec3{0,0,1};
    const Vec3 up=Unit(Subtract(seed,Scale(right,Dot(seed,right))));
    return {Cross(right,up),up,right};
}
Basis Turn(Basis basis,Vec3 axis,float angle) {
    axis=Unit(axis);const float sine=std::sin(angle*.5f);
    const Quat q{axis.x*sine,axis.y*sine,axis.z*sine,std::cos(angle*.5f)};
    return {Rotate(q,basis.forward),Rotate(q,basis.up),Rotate(q,basis.right)};
}
Vec3 LocalVector(const Basis& basis,Vec3 value) {
    return {Dot(value,basis.right),Dot(value,basis.up),Dot(value,basis.forward)};
}
Vec3 FrameVector(const Basis& basis,Vec3 value) {
    return Add(Add(Scale(basis.right,value.x),Scale(basis.up,value.y)),Scale(basis.forward,value.z));
}
Vec3 FramePoint(const Bone& frame,Vec3 local){return Add(frame.position,FrameVector(frame.basis,local));}
Vec3 RigidPoint(const Bone& from,const Bone& to,Vec3 point) {
    return FramePoint(to,LocalVector(from.basis,Subtract(point,from.position)));
}
bool SameLocal(const Bone& oldFrame,const Bone& oldBone,const Bone& frame,const Bone& bone) {
    return Near(LocalVector(oldFrame.basis,Subtract(oldBone.position,oldFrame.position)),
                LocalVector(frame.basis,Subtract(bone.position,frame.position)))&&
        Near(LocalVector(oldFrame.basis,oldBone.basis.right),LocalVector(frame.basis,bone.basis.right))&&
        Near(LocalVector(oldFrame.basis,oldBone.basis.up),LocalVector(frame.basis,bone.basis.up))&&
        Near(LocalVector(oldFrame.basis,oldBone.basis.forward),LocalVector(frame.basis,bone.basis.forward));
}
Vec3 SkinPoint(const std::array<float,12>& matrix,Vec3 p) {
    return {matrix[0]*p.x+matrix[1]*p.y+matrix[2]*p.z+matrix[3],
            matrix[4]*p.x+matrix[5]*p.y+matrix[6]*p.z+matrix[7],
            matrix[8]*p.x+matrix[9]*p.y+matrix[10]*p.z+matrix[11]};
}
std::array<float,12> Skin(const Bone& bind,const Bone& posed) {
    std::array<float,12> matrix{};
    Check(NativeSkinMath::Palette({bind.basis,bind.position},posed,matrix.data()),"helper produces a valid native skin matrix");
    return matrix;
}
Vec3 BlendPoint(const Bones& bind,const Bones& posed,unsigned a,unsigned b,Vec3 vertex) {
    return Scale(Add(SkinPoint(Skin(bind[a],posed[a]),vertex),SkinPoint(Skin(bind[b],posed[b]),vertex)),.5f);
}
float RadialDistance(Vec3 point,Vec3 origin,Vec3 axis) {
    const Vec3 offset=Subtract(point,origin);
    return Length(Subtract(offset,Scale(axis,Dot(offset,axis))));
}
// Minimum radius of a 50/50 linear blend of two proper rotation deltas is
// cos(relativeAngle/2). It measures flattening independent of axis choice;
// checking each single bone's unit basis alone cannot expose this failure.
float BlendWidth(const Bones& input,const Bones& posed,unsigned a,unsigned b) {
    Quat first,second;
    if(!NativeRigMath::RelativeRotation(input[a].basis,posed[a].basis,first)||
       !NativeRigMath::RelativeRotation(input[b].basis,posed[b].basis,second))return 0.f;
    return std::fabs(first.x*second.x+first.y*second.y+first.z*second.z+first.w*second.w);
}
ArmOptions Options(bool distribute) {
    ArmOptions options;options.helpers={Twist,Twist1,Muscle};options.distributeTwist=distribute;return options;
}
Bones Animated(unsigned side) {
    Bones bones{};
    for(auto& bone:bones)bone.basis={{0,0,1},{0,1,0},{1,0,0}};
    const float sign=side?-1.f:1.f;
    bones[0].position={0,-80,0};bones[Clavicle].position={0,0,0};
    bones[Upper].position={0,0,0};bones[Lower].position={sign*25,0,0};bones[Wrist].position={sign*45,15,0};
    bones[Upper].basis=Along(Subtract(bones[Lower].position,bones[Upper].position));
    bones[Lower].basis=Along(Subtract(bones[Wrist].position,bones[Lower].position));
    const Vec3 axis=Unit(Subtract(bones[Wrist].position,bones[Lower].position));
    // Measured native wrist frames have mirrored +/-90-degree authored roll.
    // Helpers have their own animated local roll, which must not be erased.
    bones[Wrist].basis=Turn(bones[Lower].basis,axis,sign*Pi*.5f);
    bones[Twist]={Turn(bones[Lower].basis,axis,sign*.12f),bones[Lower].position};
    bones[Twist1]={Turn(bones[Lower].basis,axis,sign*.24f),
        Add(bones[Lower].position,Scale(Subtract(bones[Wrist].position,bones[Lower].position),.5f))};
    bones[Muscle]={Turn(bones[Upper].basis,{0,0,1},sign*.17f),FramePoint(bones[Upper],{.5f,1,2})};
    bones[8].position={0,-20,-8};bones[9].position={-sign*25,-5,0};bones[10].position={-sign*40,-20,4};
    bones[11].position={0,35,-4};
    bones[12]={bones[Wrist].basis,FramePoint(bones[Wrist],{4,.8f,0})};
    bones[13]={bones[Wrist].basis,FramePoint(bones[Wrist],{7,.8f,.2f})};
    bones[14]={bones[Wrist].basis,FramePoint(bones[Wrist],{1,-1,2})};
    return bones;
}
bool Solve(const Bones& input,const CameraPose& target,Vec3 pole,const ArmOptions& options,Bones& output,Result& result) {
    return NativeRigMath::SolveArm(input.data(),Parents,Count,Upper,Lower,Wrist,target,pole,output.data(),result,options);
}
void CheckUntouched(const Bones& input,const Bones& output) {
    for(unsigned index:{0u,1u,8u,9u,10u,11u})
        Check(!std::memcmp(&input[index],&output[index],sizeof(Bone)),"ground clavicle torso opposite arm and face remain byte-identical");
}
void RigidFollow(unsigned side) {
    const Bones input=Animated(side),copy=input;
    const float sign=side?-1.f:1.f;
    const Vec3 requests[]={{25,-15,15},{15,-30,25},{35,15,-10},{5,35,10},{-10,20,30}};
    float oldForearmError=0.f,oldMuscleError=0.f;
    for(Vec3 request:requests) {
        request.x*=sign;
        const CameraPose target{Turn(input[Wrist].basis,{0,1,0},.3f),request};
        Bones output{},old{};Result result,oldResult;
        const bool okay=Solve(input,target,{sign,-.5f,0},Options(false),output,result);
        Check(okay,"native sibling helper topology solves reachable arm target");if(!okay)continue;
        Check(!result.clamped&&Near(output[Wrist].position,target.position),"helper repair retains exact reachable wrist position");
        Check(std::fabs(Length(Subtract(output[Lower].position,output[Upper].position))-25.f)<.003f&&
              std::fabs(Length(Subtract(output[Wrist].position,output[Lower].position))-25.f)<.003f,"helper repair never stretches anatomical segments");
        Check(Near(output[Twist].position,output[Lower].position),"sibling FORETWIST stays at solved elbow");
        Check(Near(output[Twist1].position,Scale(Add(output[Lower].position,output[Wrist].position),.5f)),"FORETWIST1 stays at solved forearm midpoint");
        Check(SameLocal(input[Lower],input[Twist],output[Lower],output[Twist])&&
              SameLocal(input[Lower],input[Twist1],output[Lower],output[Twist1]),"forearm helpers preserve their animated lower-arm-relative pose under lower swing");
        Check(SameLocal(input[Upper],input[Muscle],output[Upper],output[Muscle]),"muscle helper outside upper subtree follows upper-arm rigid delta");
        Check(SameLocal(input[Wrist],input[13],output[Wrist],output[13])&&
              SameLocal(input[Wrist],input[14],output[Wrist],output[14]),"finger and weapon-attacher animation stays wrist-relative");
        CheckUntouched(input,output);
        const Vec3 forearmVertex=Add(input[Twist1].position,Scale(input[Lower].basis.up,2.f));
        const Vec3 muscleVertex=FramePoint(input[Upper],{12,2,1});
        const Vec3 expectedForearm=RigidPoint(input[Lower],output[Lower],forearmVertex);
        const Vec3 expectedMuscle=RigidPoint(input[Upper],output[Upper],muscleVertex);
        Check(Near(BlendPoint(input,output,Lower,Twist1,forearmVertex),expectedForearm),"LBS point shared by forearm and sibling helper follows one rigid segment without midpoint smear");
        Check(Near(BlendPoint(input,output,Upper,Muscle,muscleVertex),expectedMuscle),"LBS point shared by upper arm and external muscle helper follows one rigid segment");
        ArmOptions absent;absent.distributeTwist=false;
        if(Solve(input,target,{sign,-.5f,0},absent,old,oldResult)) {
            oldForearmError=std::max(oldForearmError,Length(Subtract(BlendPoint(input,old,Lower,Twist1,forearmVertex),expectedForearm)));
            oldMuscleError=std::max(oldMuscleError,Length(Subtract(BlendPoint(input,old,Upper,Muscle,muscleVertex),expectedMuscle)));
        }
    }
    Check(oldForearmError>2.f,"the previous subtree-only solve demonstrably displaces a forearm LBS point by centimetres");
    Check(oldMuscleError>2.f,"the previous subtree-only solve demonstrably displaces an upper-arm muscle LBS point");
    Check(!std::memcmp(input.data(),copy.data(),sizeof(input)),"native input skeleton remains completely read-only");
    Bones separate{},aliased=input;Result separateResult,aliasedResult;
    const CameraPose target{input[Wrist].basis,{sign*25,-15,15}};
    Check(Solve(input,target,{sign,-.5f,0},Options(false),separate,separateResult)&&
          Solve(aliased,target,{sign,-.5f,0},Options(false),aliased,aliasedResult)&&
          !std::memcmp(separate.data(),aliased.data(),sizeof(separate)),"helper reconstruction supports aliased input and output without corrupting original helper offsets");
}
void TwistSkin(unsigned side) {
    const Bones input=Animated(side);const Vec3 axis=Unit(Subtract(input[Wrist].position,input[Lower].position));
    const CameraPose target{Turn(input[Wrist].basis,axis,Pi),input[Wrist].position};
    ArmOptions options=Options(true);options.twistReferenceValid=true;options.twistReferenceRadians=Pi;
    Bones output{},withoutRoll{};Result result,noRoll;
    Check(Solve(input,target,{0,-1,0},options,output,result),"pure axial half-turn with native authored wrist offset solves");
    Check(result.twistDefined&&std::fabs(result.residualTwistRadians-Pi)<.003f,"residual half-turn cancels authored mirrored ninety-degree wrist frame");
    Check(Near(output[Wrist].basis,target.basis)&&Near(output[Wrist].position,target.position),"distributed twist preserves exact absolute tracked wrist pose");
    Check(Near(output[Lower].basis,Turn(input[Lower].basis,axis,Pi)),"anatomical forearm carries the axial half-turn instead of leaving all rotation at the wrist");
    Check(Near(output[Twist].basis,Turn(input[Twist].basis,axis,Pi))&&
          Near(output[Twist1].basis,Turn(input[Twist1].basis,axis,Pi)),"native helper offsets follow forearm pronation without a half-turn skinning gap");
    Check(Near(output[Twist].position,input[Twist].position)&&Near(output[Twist1].position,input[Twist1].position),"axial helper twist does not move the elbow or forearm midpoint");
    Check(SameLocal(input[Wrist],input[13],output[Wrist],output[13]),"tracked half-turn preserves native finger animation relative to wrist");
    CheckUntouched(input,output);
    options.distributeTwist=false;
    Check(Solve(input,target,{0,-1,0},options,withoutRoll,noRoll),"comparison solve without roll distribution remains valid");
    const Vec3 radial=input[Lower].basis.up;
    const unsigned pairs[3][2]={{Lower,Twist},{Twist,Twist1},{Twist1,Wrist}};
    for(unsigned i=0;i<3;++i) {
        const Vec3 vertex=Add(Add(input[Lower].position,Scale(Subtract(input[Wrist].position,input[Lower].position),(.25f+.25f*i))),Scale(radial,2.f));
        const float radius=RadialDistance(BlendPoint(input,output,pairs[i][0],pairs[i][1],vertex),output[Lower].position,axis)/2.f;
        Check(std::fabs(radius-1.f)<.003f,"a pure axial half-turn preserves the complete forearm radial section");
        if(i==2)Check(RadialDistance(BlendPoint(input,withoutRoll,Twist1,Wrist,vertex),withoutRoll[Lower].position,axis)<.003f,
                      "undistributed half-turn demonstrably collapses the forearm/wrist LBS cross-section");
    }
}
void Continuity(unsigned side,float direction) {
    const Bones input=Animated(side);const Vec3 axis=Unit(Subtract(input[Wrist].position,input[Lower].position));
    const float degrees[]={150,165,175,179,181,185,190,205,225};
    ArmOptions options=Options(true);float previous=0,previousDegree=0;Bones prior{};bool first=true;
    for(float degree:degrees) {
        const float angle=direction*degree*Pi/180.f;
        options.twistReferenceValid=!first;options.twistReferenceRadians=previous;
        const CameraPose target{Turn(input[Wrist].basis,axis,angle),input[Wrist].position};
        Bones output{},repeat{};Result result,repeatResult;
        Check(Solve(input,target,{0,-1,0},options,output,result),"continuous roll sequence solves through the principal-angle boundary");
        Check(result.twistDefined&&std::fabs(result.residualTwistRadians-angle)<.003f,"residual twist unwraps continuously through plus or minus one hundred eighty degrees");
        const float remainder=(Pi*.25f)*std::sin(angle),forearm=angle-remainder;
        Check(Near(output[Twist].basis,Turn(input[Twist].basis,axis,forearm+remainder/3))&&
              Near(output[Twist1].basis,Turn(input[Twist1].basis,axis,forearm+2*remainder/3)),"helper roll remains bounded relative to forearm and follows physical orientation continuously");
        Check(Solve(input,target,{0,-1,0},options,repeat,repeatResult)&&!std::memcmp(output.data(),repeat.data(),sizeof(output)),
              "same frozen input and twist reference produce identical output for repeated stereo-pair draws");
        if(!first)Check(Dot(prior[Twist1].basis.up,output[Twist1].basis.up)>
            std::cos((degree-previousDegree)*1.27f*Pi/180.f)-.002f,
            "helper orientation never flips at the principal-angle branch boundary");
        previous=result.residualTwistRadians;previousDegree=degree;prior=output;first=false;
    }
}
void FullRevolutionVolume(unsigned side) {
    const Bones input=Animated(side);const Vec3 axis=Unit(Subtract(input[Wrist].position,input[Lower].position));
    const unsigned regions[]={Lower,Twist,Twist1,Wrist};
    float minimum=1.f,elbowMinimum=1.f;
    for(int degree=-720;degree<=720;degree+=15) {
        const float radians=degree*Pi/180.f;
        ArmOptions options=Options(true);options.twistReferenceValid=true;options.twistReferenceRadians=radians;
        const CameraPose target{Turn(input[Wrist].basis,axis,radians),input[Wrist].position};
        Bones output{},sameOrientation{};Result result,other;
        Check(Solve(input,target,{0,-1,0},options,output,result),"both anatomical arms solve through several complete wrist rotations");
        Check(Near(output[Wrist].basis,target.basis)&&Near(output[Wrist].position,target.position),"pronation preserves exact tracked wrist through full rotations");
        for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b) {
            const float width=BlendWidth(input,output,regions[a],regions[b]);minimum=std::min(minimum,width);
            Check(width>std::cos(Pi/8)-.003f,"all forearm region pairs including direct LOWER/WRIST retain at least cos-22.5 cross-section");
        }
        const float elbow=BlendWidth(input,output,Upper,Lower);elbowMinimum=std::min(elbowMinimum,elbow);
        Check(elbow>.79f,"humeral compensation avoids moving this forearm collapse to the elbow");
        Check(SameLocal(input[Upper],input[Muscle],output[Upper],output[Muscle]),"upper muscle follows pronation compensation rigidly");
        options.twistReferenceRadians+=2*Pi;
        Check(Solve(input,target,{0,-1,0},options,sameOrientation,other),"same physical orientation with another winding history solves");
        for(unsigned index:regions)Check(Near(output[index],sameOrientation[index]),"helper skin pose cannot depend on the number of completed controller revolutions");
    }
    std::printf("Pronation side %u: minimum forearm blend width %.5f, elbow %.5f\n",side,minimum,elbowMinimum);
}
void RetainedPoseVolume(unsigned side) {
    Bones input=Animated(side),before=input;
    const unsigned roles[]={Upper,Lower,Wrist,Twist,Twist1,Muscle};
    for(unsigned role=0;role<6;++role)
        std::memcpy(&input[roles[role]],NativeArmVolumeFixture::Original[side][role],sizeof(Bone));
    before=input;
    for(unsigned role=0;role<3;++role)
        std::memcpy(&before[roles[role]],NativeArmVolumeFixture::Solved[side][role],sizeof(Bone));
    const CameraPose target{before[Wrist].basis,before[Wrist].position};
    const Vec3 along=Unit(Subtract(target.position,input[Upper].position));
    const Vec3 upper=Subtract(before[Lower].position,input[Upper].position);
    const Vec3 pole=Subtract(upper,Scale(along,Dot(upper,along)));
    Bones output{};Result result;
    Check(Solve(input,target,pole,Options(true),output,result),"retained live deformed arm pose solves with original source frames");
    Check(Near(output[Wrist].basis,target.basis)&&Near(output[Wrist].position,target.position),"retained live wrist pose is unchanged by volume repair");
    Check(Near(output[Lower].position,before[Lower].position),"volume repair retains the live anatomical elbow position");
    const float oldWidth=BlendWidth(input,before,Lower,Wrist),newWidth=BlendWidth(input,output,Lower,Wrist);
    const float elbowWidth=BlendWidth(input,output,Upper,Lower);
    Check(oldWidth<.4f&&newWidth>.75f,"reported live lower/wrist 50-50 blend improves from severe flattening to a stable cross-section");
    Check(elbowWidth>.6f,"reported pose does not relocate forearm collapse into the upper/lower blend");
    Check(SameLocal(input[Upper],input[Muscle],output[Upper],output[Muscle]),"live native muscle offset remains humerus-relative after compensation");
    CheckUntouched(input,output);
    std::printf("Retained side %u: lower/wrist %.5f -> %.5f, upper/lower %.5f\n",side,oldWidth,newWidth,elbowWidth);
}
void SingularAndFailure(unsigned side) {
    const Bones input=Animated(side);const Vec3 axis=Unit(Subtract(input[Wrist].position,input[Lower].position));
    const Vec3 perpendicular=Unit(Cross(axis,{0,0,1}));
    const CameraPose target{Turn(input[Wrist].basis,perpendicular,Pi),input[Wrist].position};
    ArmOptions options=Options(true);options.twistReferenceValid=true;options.twistReferenceRadians=.9f;
    Bones output{};Result result;
    Check(Solve(input,target,{0,-1,0},options,output,result),"perpendicular half-turn does not reject the whole tracked wrist");
    Check(!result.twistDefined&&std::fabs(result.residualTwistRadians-.9f)<.003f,"undefined swing-twist decomposition holds its continuous reference");
    Check(Near(output[Wrist].basis,target.basis),"singular roll handling still preserves exact tracked wrist orientation");
    options.twistReferenceValid=false;
    Check(Solve(input,target,{0,-1,0},options,output,result)&&!result.twistDefined&&std::fabs(result.residualTwistRadians)<.003f,
          "undefined twist without history starts at zero instead of inventing forearm roll");
    const CameraPose ordinary{input[Wrist].basis,{25,-15,15}};
    const auto rejects=[&](ArmOptions invalid,const std::uint32_t* hierarchy,const Bones& source,const char* label) {
        Bones untouched=input;Result before{};
        Check(!NativeRigMath::SolveArm(source.data(),hierarchy,Count,Upper,Lower,Wrist,ordinary,{1,-.5f,0},untouched.data(),before,invalid)&&
              !std::memcmp(untouched.data(),input.data(),sizeof(input)),label);
    };
    options=Options(true);auto hierarchy=std::array<std::uint32_t,Count>{};std::memcpy(hierarchy.data(),Parents,sizeof(Parents));
    hierarchy[Twist]=Clavicle;rejects(options,hierarchy.data(),input,"invalid sibling FORETWIST parent rejects without mutating output");
    std::memcpy(hierarchy.data(),Parents,sizeof(Parents));hierarchy[Twist1]=Upper;
    rejects(options,hierarchy.data(),input,"invalid FORETWIST1 ancestry rejects without mutating output");
    std::memcpy(hierarchy.data(),Parents,sizeof(Parents));hierarchy[Muscle]=0;
    rejects(options,hierarchy.data(),input,"invalid external muscle ancestry rejects without mutating output");
    options=Options(true);options.helpers.foreTwist=Count;rejects(options,Parents,input,"out-of-palette helper index rejects without any output write");
    options=Options(true);options.helpers.foreTwist=Lower;options.helpers.foreTwist1=NativeRigMath::MaximumBones;
    rejects(options,Parents,input,"a helper cannot alias the main forearm bone");
    options=Options(true);options.twistReferenceValid=true;options.twistReferenceRadians=std::numeric_limits<float>::quiet_NaN();
    rejects(options,Parents,input,"nonfinite continuous twist reference rejects without output mutation");
    options=Options(true);Bones malformed=input;malformed[Muscle].basis.right=malformed[Muscle].basis.forward;
    rejects(options,Parents,malformed,"invalid optional helper basis rejects without output mutation");
    ArmOptions missing;
    std::array<Bone,Count+2> guarded{};guarded.front().position={1234,5678,91011};guarded.back().position={-1234,-5678,-91011};
    const Bone first=guarded.front(),last=guarded.back();
    Check(NativeRigMath::SolveArm(input.data(),Parents,Count,Upper,Lower,Wrist,ordinary,{1,-.5f,0},guarded.data()+1,result,missing)&&
          !std::memcmp(&guarded.front(),&first,sizeof(first))&&!std::memcmp(&guarded.back(),&last,sizeof(last)),
          "missing optional helper lookup safely solves only the bounded palette output");
}
}
int main() {
    for(unsigned side=0;side<2;++side) {
        RigidFollow(side);TwistSkin(side);Continuity(side,1.f);Continuity(side,-1.f);SingularAndFailure(side);
        FullRevolutionVolume(side);RetainedPoseVolume(side);
    }
    std::printf("Native arm helper skin tests: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
