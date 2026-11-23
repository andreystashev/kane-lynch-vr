#include "native_body_math.hpp"
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace NativeCameraMath;
using namespace NativeBodyMath;
unsigned checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);}}
bool Near(float a,float b,float epsilon=.001f){return std::fabs(a-b)<epsilon;}
bool Near(Vec3 a,Vec3 b,float epsilon=.001f){return Near(a.x,b.x,epsilon)&&Near(a.y,b.y,epsilon)&&Near(a.z,b.z,epsilon);}
CameraPose AtYaw(float degrees,Vec3 position={}) {
    CameraPose result;const float angle=Radians(degrees);
    result.basis={RotateHeading(result.basis.forward,angle),RotateHeading(result.basis.up,angle),RotateHeading(result.basis.right,angle)};
    result.position=position;return result;
}
float Degrees(const CameraPose& pose){float angle=0;Heading(pose.basis.forward,angle);return angle*180.f/Pi;}
Vec3 Head(const CameraPose& root,Vec3 local){return Add(root.position,Transform(root.basis,local));}
// Existing root/pivot tests use identical original/bind shoulder lines: zero
// animation-local turn. Torso-specific tests below use the full API directly.
bool Adjust(FollowState& state,std::uintptr_t player,const CameraPose& rawRoot,Vec3 head,
            const CameraPose& eye,std::uint64_t pair,std::uint32_t tick,CameraPose& output) {
    return NativeBodyMath::Adjust(state,player,rawRoot,head,{1,0,0},{1,0,0},eye,pair,tick,output);
}
void TestFirstPersonHeadAlignment() {
    const Vec3 head{4,171,-8},shoulder{-16,144,1},wrist{-22,120,-24};
    for(float yaw:{-180.f,-90.f,-15.f,0.f,45.f,170.f}) {
        const auto root=AtYaw(yaw,{655.55f,.5f,-815.69f});const auto saved=root;
        const auto nativeHead=Head(root,head);
        for(Vec3 lean:{Vec3{},Vec3{0,35,0},Vec3{0,-35,0},Vec3{35,0,0},Vec3{-20,8,25}}) {
            const Vec3 centre=Add(nativeHead,lean);CameraPose aligned;
            Check(AlignHead(root,head,centre,100,aligned),"bounded physical lean aligns the rendered body with current first-person centre");
            const auto alignedHead=Head(aligned,head);
            Check(Near(alignedHead.x,centre.x)&&Near(alignedHead.z,centre.z)&&Near(aligned.position.y,root.position.y),"horizontal head alignment preserves native ground height even with vertical physical lean");
            Check(!std::memcmp(&root.basis,&aligned.basis,sizeof(root.basis)),"head alignment preserves body follow heading and bone basis");
            Check(Near(Head(aligned,shoulder),Add(Head(root,shoulder),{lean.x,0,lean.z})),"shoulders follow horizontal head displacement without lifting the feet");
            const auto before=Subtract(Head(root,wrist),Head(root,shoulder));
            const auto after=Subtract(Head(aligned,wrist),Head(aligned,shoulder));
            Check(Near(before,after),"body alignment does not stretch native limbs or change grip offsets");
            const auto queued=AtYaw(yaw,Add(root.position,{12,-3,9}));CameraPose next;
            Check(AlignHead(queued,head,centre,100,next)&&Near(Head(next,head).x,centre.x)&&Near(Head(next,head).z,centre.z)&&Near(next.position.y,queued.position.y),"native body queue movement cannot detach avatar horizontally or override its floor height");
        }
        Check(!std::memcmp(&root,&saved,sizeof(root)),"source gameplay root stays unchanged");
    }
    CameraPose sentinel=AtYaw(73,{7,8,9}),out=sentinel;
    Check(!AlignHead(AtYaw(0),head,{900,800,700},100,out)&&!std::memcmp(&out,&sentinel,sizeof(out)),"foreign distant camera cannot translate the first-person body across the map");
    Check(!AlignHead(AtYaw(0),head,{0,170,0},0,out)&&
          !AlignHead(AtYaw(0),{0,std::numeric_limits<float>::quiet_NaN(),0},{0,170,0},100,out),"invalid head alignment data is rejected");
}
void TestDeadzoneAndRate() {
    FollowState state;CameraPose out;const auto raw=AtYaw(0,{10,20,30});const Vec3 head{3,170,-5};
    Check(Adjust(state,123,raw,head,AtYaw(20),1,1000,out),"first pair initializes from the native body heading");
    Check(Near(Degrees(out),0),"initialization avoids an immediate rotation jump");
    Check(std::memcmp(&raw,&out,sizeof(raw))==0,"zero yaw correction preserves exact native root bytes");
    Check(Adjust(state,123,raw,head,AtYaw(20),2,1100,out)&&Near(Degrees(out),0),"35 degree neck deadzone retains native heading");
    Check(Adjust(state,123,raw,head,AtYaw(90),3,1200,out)&&Near(Degrees(out),18),"normal follow advances 180 degrees per second");
    Check(Near(Head(out,head),Head(raw,head)),"normal follow preserves original native head world position");
    Check(Adjust(state,123,raw,head,AtYaw(90),4,1300,out)&&Near(Degrees(out),36),"normal follow advances on the next pair");
    Check(Adjust(state,123,raw,head,AtYaw(90),5,1400,out)&&Near(Degrees(out),54),"normal follow approaches deadzone edge");
    Check(Adjust(state,123,raw,head,AtYaw(90),6,1500,out)&&Near(Degrees(out),55),"follow stops at 35 degree boundary without overshoot");
    Check(Adjust(state,123,raw,head,AtYaw(90),7,1600,out)&&Near(Degrees(out),55),"held gaze does not drift at the deadzone edge");
    FollowState fast;Adjust(fast,123,raw,head,AtYaw(180),1,1000,out);
    Check(Adjust(fast,123,raw,head,AtYaw(180),2,1100,out)&&Near(std::fabs(Degrees(out)),24),"large turns follow at 240 degrees per second");
    Check(Adjust(fast,123,raw,head,AtYaw(180),3,101100,out)&&Near(std::fabs(Degrees(out)),48),"pause gaps clamp smoothing elapsed time to 100 ms");
}
void TestStereoAndPivot() {
    FollowState state;CameraPose left,right;const auto raw=AtYaw(-15,{-5000,0,2100});const Vec3 head{-4,166,6};
    Adjust(state,123,raw,head,AtYaw(80),1,1000,left);
    Adjust(state,123,raw,head,AtYaw(80),2,1100,left);
    const auto once=state;
    Check(Adjust(state,123,raw,head,AtYaw(-120),2,1190,right),"other eye accepts the same pair");
    Check(Near(left.position,right.position)&&Near(left.basis.forward,right.basis.forward)&&
          Near(left.basis.up,right.basis.up)&&Near(left.basis.right,right.basis.right),"both eyes use identical visual body root for identical native input");
    Check(state.tick==once.tick&&Near(state.yaw,once.yaw),"repeated camera commands and right eye do not smooth twice");
    const auto moved=AtYaw(5,{-4990,-30,2110});const Vec3 crouchedHead{-2,120,4};
    Check(Adjust(state,123,moved,crouchedHead,AtYaw(0),2,1199,right)&&Near(Degrees(left),Degrees(right)),"native root changes do not change the cached pair heading");
    Check(Near(Head(right,crouchedHead),Head(moved,crouchedHead)),"each draw preserves its native animated head pivot while crouching or moving");
    Check(!Near(right.position,moved.position),"head-pivot rotation also relocates body root beneath the head");
    const auto rawBefore=raw;const auto headBefore=head;
    Adjust(state,123,raw,head,AtYaw(60),3,1200,right);
    Check(std::memcmp(&raw,&rawBefore,sizeof(raw))==0&&Near(head,headBefore),"source root and native head local pose remain read-only");
}
void TestWrapAndValidation() {
    FollowState state;CameraPose out;const Vec3 head{0,170,0};
    Adjust(state,123,AtYaw(179),head,AtYaw(-179),1,1000,out);
    Check(Adjust(state,123,AtYaw(179),head,AtYaw(-179),2,1100,out)&&Near(Degrees(out),179),"yaw wrap takes the short two degree error across 180");
    FollowState wrap;Adjust(wrap,123,AtYaw(0),head,AtYaw(80),1,0xfffffff0u,out);
    Check(Adjust(wrap,123,AtYaw(0),head,AtYaw(80),2,16,out)&&Near(Degrees(out),5.76f),"DWORD clock wrap preserves 32 ms smoothing interval");
    Check(Adjust(state,456,AtYaw(-45),head,AtYaw(90),1,1200,out)&&Near(Degrees(out),-45),"new native player resets heading history");
    CameraPose sentinel=out;const auto before=state;const float nan=std::numeric_limits<float>::quiet_NaN();
    auto invalid=AtYaw(0);invalid.basis.forward={0,1,0};invalid.basis.up={1,0,0};invalid.basis.right={0,0,1};
    Check(!Adjust(state,456,AtYaw(0),head,invalid,2,1300,out),"vertical tracked forward rejects undefined heading");
    Check(Near(out.position,sentinel.position)&&Near(state.yaw,before.yaw)&&state.pairId==before.pairId,"invalid heading leaves state and output unchanged");
    Check(!Adjust(state,456,AtYaw(0),{nan,0,0},AtYaw(0),2,1300,out),"nonfinite native head pivot rejected");
    Check(!Adjust(state,0,AtYaw(0),head,AtYaw(0),2,1300,out),"missing exact player rejected");
    Check(!Adjust(state,456,AtYaw(0),head,AtYaw(0),0,1300,out),"missing pair identity rejected");
}
void TestRotatedRigSweep() {
    for(int initial=-180;initial<=180;initial+=30)for(int target=-180;target<=180;target+=30) {
        FollowState state;CameraPose out;const auto raw=AtYaw(static_cast<float>(initial),{123,40,-60});
        const Vec3 head{5,static_cast<float>(100+target/6),-7};
        Adjust(state,123,raw,head,AtYaw(static_cast<float>(target)),1,1000,out);
        for(unsigned pair=2;pair<12;++pair) {
            Check(Adjust(state,123,raw,head,AtYaw(static_cast<float>(target)),pair,1000+pair*30,out),"heading sweep produces a valid draw root");
            Check(Near(Head(out,head),Head(raw,head),.002f),"heading sweep preserves native head world pivot");
            Check(ValidatePose(out,.001f),"heading sweep remains a rigid native F/U/R basis");
        }
    }
}
bool SameAzimuth(Vec3 actual,Vec3 expected) {
    const float a=std::atan2(actual.z,actual.x),b=std::atan2(expected.z,expected.x);
    return std::fabs(std::atan2(std::sin(a-b),std::cos(a-b)))<.0001f;
}
void TestAnimationTurns() {
    const Vec3 bindLines[]={{1,0,0},{-30,2,12},{7,-4,-19}};
    for(float entity:{-140.f,0.f,63.f})for(float animation:{-180.f,-90.f,90.f,180.f})
        for(Vec3 bindLine:bindLines) {
            const auto raw=AtYaw(entity,{123,12,-456});const Vec3 head{3,143,-8};
            const auto turnedLine=RotateHeading(bindLine,Radians(animation));
            const float bindFacing=entity+std::atan2(bindLine.z,bindLine.x)*180.f/Pi;
            FollowState initial;CameraPose out;
            Check(NativeBodyMath::Adjust(initial,123,raw,head,turnedLine,bindLine,AtYaw(bindFacing),1,1000,out),
                  "initial animated torso turn is measured independently of the entity root");
            Check(std::fabs(Wrap(initial.yaw-Radians(bindFacing+animation)))<.0001f,
                  "follow history initializes from actual torso yaw including local animation");
            Check(std::memcmp(&raw,&out,sizeof(raw))==0,"initial actual torso heading has zero delta and exact native root bytes");
            FollowState follow;
            NativeBodyMath::Adjust(follow,123,raw,head,bindLine,bindLine,AtYaw(bindFacing),1,1000,out);
            Check(NativeBodyMath::Adjust(follow,123,raw,head,turnedLine,bindLine,AtYaw(bindFacing),2,1100,out),
                  "animation-local 90 or 180 degree turn is compensated while the desired torso heading stays fixed");
            // Independent geometric observation: the rendered animated
            // shoulder line must point where the original bind line pointed.
            Check(SameAzimuth(Transform(out.basis,turnedLine),Transform(raw.basis,bindLine)),
                  "rendered torso follows desired heading despite internal animation turn and arbitrary bind shoulder axis");
            Check(std::fabs(Wrap(Radians(Degrees(out)-entity+animation)))<.0001f,
                  "entity draw yaw includes the opposite animation turn rather than being forced to the torso target");
            Check(Near(Head(out,head),Head(raw,head),.002f),"animation-turn compensation preserves the original head world pivot");
            Check(follow.shoulderMeasured&&follow.shoulderValid,
                  "valid torso geometry is distinguished from fallback heading");
        }
}
void TestTorsoStereoAndFallback() {
    const Vec3 bindLine{-31,1,11},head{4,155,-9};const auto raw=AtYaw(28,{-1000,17,2000});
    FollowState state;CameraPose left,right;
    NativeBodyMath::Adjust(state,123,raw,head,bindLine,bindLine,AtYaw(28),1,1000,left);
    const auto leftLine=RotateHeading(bindLine,Radians(90));
    Check(NativeBodyMath::Adjust(state,123,raw,head,leftLine,bindLine,AtYaw(98),2,1100,left),
          "left eye measures animation-local turn before the once-pair follow step");
    const float pairYaw=state.yaw;const auto pairTick=state.tick;
    const auto otherRoot=AtYaw(70,{-998,12,2001});const auto rightLine=RotateHeading(bindLine,Radians(-90));
    Check(NativeBodyMath::Adjust(state,123,otherRoot,head,rightLine,bindLine,AtYaw(-120),2,1190,right),
          "right eye can use its own original native root and animation pose");
    Check(Near(state.yaw,pairYaw)&&state.tick==pairTick,"right eye does not take another torso smoothing step");
    const float bindFacing=Radians(28)+std::atan2(bindLine.z,bindLine.x);
    const auto expectedLine=RotateHeading(Transform(raw.basis,bindLine),pairYaw-bindFacing);
    Check(SameAzimuth(Transform(left.basis,leftLine),expectedLine)&&
          SameAzimuth(Transform(right.basis,rightLine),expectedLine),
          "both eyes have the same final actual torso facing despite different entity and internal-animation headings");
    Check(Near(Head(right,head),Head(otherRoot,head),.002f),"other eye retains its original animated head pivot");

    FollowState fallback;
    Check(NativeBodyMath::Adjust(fallback,123,raw,head,{0,10,0},bindLine,AtYaw(28),1,1000,left)&&
          !fallback.shoulderMeasured&&!fallback.shoulderValid&&std::memcmp(&left,&raw,sizeof(raw))==0,
          "initial vertical shoulder line falls back to the anatomical bind heading with zero draw correction");
    Check(fallback.bindValid&&std::fabs(Wrap(fallback.yaw-bindFacing))<.0001f,
          "initial fallback retains the bind facing offset instead of assuming entity forward");
    Check(NativeBodyMath::Adjust(fallback,123,raw,head,{0,0,0},bindLine,AtYaw(28),2,1100,left)&&
          !fallback.shoulderMeasured,"zero-length shoulder line safely uses fallback");
    const auto turned=RotateHeading(bindLine,Radians(90));
    NativeBodyMath::Adjust(fallback,123,raw,head,turned,bindLine,AtYaw(28),3,1200,left);
    const float rememberedOffset=fallback.animationYaw;
    Check(NativeBodyMath::Adjust(fallback,123,otherRoot,head,{.001f,10,0},bindLine,AtYaw(28),4,1300,right)&&
          !fallback.shoulderMeasured&&fallback.shoulderValid&&Near(fallback.animationYaw,rememberedOffset),
          "nearly vertical projected line keeps the previous animation offset instead of a noisy azimuth");
    Check(std::fabs(Wrap(fallback.rawTorsoYaw-Radians(70)-rememberedOffset-fallback.bindYawOffset))<.0001f,
          "fallback keeps anatomical bind and animation offsets relative to a changing native entity heading");
    const float bindOffset=fallback.bindYawOffset;
    Check(NativeBodyMath::Adjust(fallback,123,otherRoot,head,turned,{0,10,0},AtYaw(28),5,1400,right)&&
          fallback.shoulderMeasured&&Near(fallback.bindYawOffset,bindOffset),
          "valid animated shoulders still measure absolute facing when the bind projection degenerates");
    Check(Near(Head(right,head),Head(otherRoot,head),.002f),"fallback correction still preserves the native head pivot");
    const auto before=fallback;const auto sentinel=right;const float nan=std::numeric_limits<float>::quiet_NaN();
    Check(!NativeBodyMath::Adjust(fallback,123,raw,head,{nan,0,0},bindLine,AtYaw(0),6,1500,right)&&
          Near(fallback.yaw,before.yaw)&&fallback.pairId==before.pairId&&
          std::memcmp(&right,&sentinel,sizeof(right))==0,"nonfinite shoulder input rejects without changing state or draw root");
    Check(NativeBodyMath::Adjust(fallback,456,raw,head,{0,10,0},bindLine,AtYaw(28),1,1600,right)&&
          !fallback.shoulderValid&&Near(fallback.animationYaw,0)&&std::memcmp(&right,&raw,sizeof(raw))==0,
          "new player discards the previous player's fallback animation offset");
    FollowState missing;right=sentinel;
    Check(!NativeBodyMath::Adjust(missing,789,raw,head,{},{},AtYaw(28),1,1700,right)&&
          !missing.valid&&std::memcmp(&right,&sentinel,sizeof(right))==0,
          "without current shoulders, bind direction or history no blind entity-forward fallback is invented");
}
void TestPinnedAnatomicalForward() {
    // Actual named K&L bind anatomy: LEFT +X, RIGHT -X, face/toes -Z.
    // Use the native identity root: stored F=+Z, U=+Y, R=+X.
    const auto raw=AtYaw(90,{100,20,300});const Vec3 head{0,160,-15};
    const Vec3 leftShoulder{20,140,0},rightShoulder{-20,140,0};
    const Vec3 leftWrist{30,120,-35},rightWrist{-30,120,-35};
    const Vec3 line=Subtract(rightShoulder,leftShoulder);
    float actual=0.f,animation=0.f;
    Check(TorsoHeading(raw,line,line,actual,animation)&&std::fabs(Wrap(actual-Radians(-90)))<.0001f&&Near(animation,0),
          "named left +X and right -X shoulders establish absolute model facing -Z, opposite native root.forward");
    const auto camera=AtYaw(-90);FollowState initial;CameraPose out;
    Check(NativeBodyMath::Adjust(initial,123,raw,head,line,line,camera,1,1000,out)&&
          std::fabs(Wrap(initial.yaw-Radians(-90)))<.0001f&&std::memcmp(&out,&raw,sizeof(raw))==0,
          "native bind initializes from anatomical front -Z without turning the avatar's back toward the camera");
    Check(NativeBodyMath::Adjust(initial,123,raw,head,line,line,camera,2,1100,out)&&
          std::memcmp(&out,&raw,sizeof(raw))==0,"camera aligned with anatomical front leaves the original root byte-exact");
    const auto lateral=[&](const CameraPose& body,Vec3 point,const CameraPose& eye) {
        return Dot(Subtract(Head(body,point),Head(body,head)),eye.basis.right);
    };
    Check(lateral(out,leftShoulder,camera)<0&&lateral(out,rightShoulder,camera)>0&&
          lateral(out,leftWrist,camera)<0&&lateral(out,rightWrist,camera)>0,
          "camera-aligned named left/right shoulders and wrists occupy their own sides without crossed arms");
    for(float turn:{-180.f,-90.f,-45.f,45.f,90.f,180.f}) {
        FollowState state;NativeBodyMath::Adjust(state,123,raw,head,line,line,camera,1,1000,out);
        const auto turnedEye=AtYaw(-90+turn);
        for(unsigned pair=2;pair<=20;++pair)
            Check(NativeBodyMath::Adjust(state,123,raw,head,line,line,turnedEye,pair,1000+pair*100,out),
                  "pinned anatomical model follows HMD turns through both directions and 180 degrees");
        float finalYaw=0.f;
        Check(ShoulderHeading(out,line,finalYaw)&&std::fabs(Wrap(finalYaw-Radians(-90+turn)))<=Radians(35.001f),
              "actual anatomical torso, not entity forward, reaches the HMD deadzone");
        Check(Near(Head(out,head),Head(raw,head),.002f),"all HMD turn directions preserve the exact native head world pivot");
        Check(lateral(out,leftWrist,turnedEye)<0&&lateral(out,rightWrist,turnedEye)>0,
              "named wrists stay on the physical left/right sides after HMD follow");
        CameraPose otherEye;
        Check(NativeBodyMath::Adjust(state,123,raw,head,line,line,AtYaw(123),20,3999,otherEye)&&
              std::memcmp(&out,&otherEye,sizeof(out))==0,"other eye reuses the pinned anatomical body heading without a second step");
    }
    FollowState fallback;
    Check(NativeBodyMath::Adjust(fallback,123,raw,head,{0,10,0},line,camera,1,1000,out)&&
          fallback.bindValid&&!fallback.shoulderMeasured&&std::fabs(Wrap(fallback.yaw-Radians(-90)))<.0001f,
          "degenerate animated shoulders fall back to native anatomical bind -Z rather than entity +Z");
    Check(std::fabs(std::fabs(fallback.bindYawOffset)-Pi)<.0001f,
          "the pinned model's measured 180 degree bind/entity difference is retained without a hard-coded flip");
}
}
void TestFaceViewAndVerticalLook() {
    const auto root=AtYaw(37,{20,4,-9});const Vec3 head{2,165,-7},line{1,0,0};
    FollowState follow;CameraPose draw;
    Check(NativeBodyMath::Adjust(follow,123,root,head,line,line,AtYaw(112),1,1000,draw,true)&&
          Near(Wrap(follow.yaw-Radians(112)),0),"first-person body follows HMD heading on the initial pair");
    Check(NativeBodyMath::Adjust(follow,123,root,head,line,line,AtYaw(-120),2,1016,draw,true)&&
          Near(Wrap(follow.yaw-Radians(-120)),0),"first-person body turns without smoothing lag");
    for(float pitch:{-90.f,90.f}) {
        const float half=Radians(pitch)*.5f;CameraPose eye,facing;
        Check(Compose({root.basis,Head(root,head)},{std::sin(half),0,0,std::cos(half)},{},{},100,eye)&&
              FacingView(123,follow,root,line,line,eye,facing)&&
              NativeBodyMath::Adjust(follow,123,root,head,line,line,facing,3,1032,draw,true),
              "vertical gaze retains valid body heading and arm target frame");
    }
}
int main() {
    Check(DrawContextCurrent(true,7,7),"current explicitly-owned body draw remains authorized independent of slow-frame wall time");
    Check(!DrawContextCurrent(false,7,7)&&!DrawContextCurrent(true,7,8)&&!DrawContextCurrent(true,0,0),
          "body retirement, a different pair and missing identity retain the native draw");
    TestDeadzoneAndRate();TestStereoAndPivot();TestWrapAndValidation();TestRotatedRigSweep();
    TestAnimationTurns();TestTorsoStereoAndFallback();
    TestPinnedAnatomicalForward();TestFirstPersonHeadAlignment();TestFaceViewAndVerticalLook();
    std::printf("Native visual body math: %u checks, %u failures.\n",checks,failures);
    return failures?1:0;
}
