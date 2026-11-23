#pragma once

#include "native_camera_math.hpp"
#include "stereo_render_math.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NativeCameraKLMath {
using namespace NativeCameraMath;

// Layout proved by the retail CameraSpace constructor at RVA 0x32CF0.
// All pointer-sized fields in this engine structure are x86 DWORDs.
struct Space {
    Basis basis;
    Vec3 position;
    float planes[12][4]{};
    float fog[4]{};
    std::uint32_t colour=0;
    float nearUnits=0,farUnits=0;
    float horizontalFov=0,alternateFov=0,perspective=0,scaleX=0,aspect=0;
    float clipRectangle[4]{};
    float projectionScale=0;
    Vec3 positionCache;
    std::uint32_t planeCount=0,resource=0;
    std::uint8_t orthographic=0,flag149=0,flag14A=0,flag14B=0;
    std::uint32_t sourceCamera=0;
};
static_assert(offsetof(Space,position)==0x24,"K&L CameraSpace position ABI");
static_assert(offsetof(Space,planes)==0x30,"K&L CameraSpace planes ABI");
static_assert(offsetof(Space,nearUnits)==0x104,"K&L CameraSpace depth ABI");
static_assert(offsetof(Space,horizontalFov)==0x10C,"K&L CameraSpace FOV ABI");
static_assert(offsetof(Space,projectionScale)==0x130,"K&L CameraSpace projection scale ABI");
static_assert(offsetof(Space,positionCache)==0x134,"K&L CameraSpace position cache ABI");
static_assert(offsetof(Space,planeCount)==0x140,"K&L CameraSpace plane count ABI");
static_assert(offsetof(Space,sourceCamera)==0x14C&&sizeof(Space)==0x150,"K&L CameraSpace ABI");

inline Vec3 CameraVector(const Basis& basis,float x,float y,float z) {
    return Add(Add(Scale(basis.right,x),Scale(basis.up,y)),Scale(basis.forward,z));
}

// Exact native inverse-pose convention from kind1 GPU decoder +0xBC and
// world/view composition at RVA 0x71770. This is not a D3D 4x4 matrix.
inline bool ViewFromWorld(const CameraPose& world,CameraPose& view) {
    if(!ValidatePose(world))return false;
    const auto& b=world.basis;
    CameraPose result;
    result.basis={{b.right.z,b.up.z,b.forward.z},{b.right.y,b.up.y,b.forward.y},
                  {b.right.x,b.up.x,b.forward.x}};
    result.position={-Dot(b.right,world.position),-Dot(b.up,world.position),-Dot(b.forward,world.position)};
    if(!ValidatePose(result))return false;
    view=result;return true;
}
inline bool WorldFromView(const CameraPose& view,CameraPose& world) {
    if(!ValidatePose(view))return false;
    const auto& b=view.basis;
    CameraPose result;
    result.basis={{b.right.z,b.up.z,b.forward.z},{b.right.y,b.up.y,b.forward.y},
                  {b.right.x,b.up.x,b.forward.x}};
    result.position=Scale(CameraVector(result.basis,view.position.x,view.position.y,view.position.z),-1.f);
    if(!ValidatePose(result))return false;
    world=result;return true;
}
inline void EyePlane(const CameraPose& eye,float plane[4]) {
    plane[0]=-eye.basis.forward.x;plane[1]=-eye.basis.forward.y;plane[2]=-eye.basis.forward.z;
    plane[3]=Dot(eye.basis.forward,eye.position);
}

// A third-person camera's downward pitch must not become the player's neck
// pitch when moving its origin to the head. Preserve its horizontal heading;
// the tracked headset contributes all pitch and roll after this operation.
inline bool YawOnlyBase(const CameraPose& input,CameraPose& output) {
    if(!ValidatePose(input))return false;
    const Vec3 horizontal{input.basis.forward.x,0.f,input.basis.forward.z};
    const float length2=Dot(horizontal,horizontal);
    if(!std::isfinite(length2)||length2<1e-6f)return false;
    CameraPose result=input;
    result.basis.forward=Scale(horizontal,1.f/std::sqrt(length2));
    result.basis.up={0.f,1.f,0.f};
    result.basis.right=Cross(result.basis.up,result.basis.forward);
    if(!ValidatePose(result))return false;
    output=result;return true;
}

// Close to the vertical poles, the horizontal projection becomes sensor
// noise and can reverse as the player looks over their head. Keep the last
// useful world heading until the gaze is at least about six degrees away
// from vertical. Ordinary horizontal look remains direct and unsmoothed.
struct HorizontalGaze {
    bool valid=false;
    Vec3 forward{};
    bool Select(const CameraPose& eye,Vec3& output) {
        if(!ValidatePose(eye))return false;
        const Vec3 horizontal{eye.basis.forward.x,0.f,eye.basis.forward.z};
        const float length2=Dot(horizontal,horizontal);
        if(length2>=.01f) {
            forward=Scale(horizontal,1.f/std::sqrt(length2));valid=true;
        }
        if(!valid)return false;
        output=forward;return true;
    }
};

struct AuthoredHeadingInput {
    std::uintptr_t action=0;
    std::uint64_t pairId=0;
    Vec3 actorForward{};
};
struct AuthoredHeadingReference {
    bool valid=false;
    std::uintptr_t action=0;
    std::uint64_t pairId=0;
    Vec3 actorForward{};
};
struct FirstPersonHeading {
    bool valid=false;
    std::uintptr_t player=0,camera=0;
    Basis basis{};
    long recenterAck=0;
    HorizontalGaze worldGaze{};
    // Only the consumer records this after applying an owned first-person
    // view. Transient anchor loss can retain yaw, never old eye positions or
    // an authorization for another native renderer state.
    std::uintptr_t sourceState=0;
    AuthoredHeadingReference authored{};
    bool traversalHeadingPending=false;
};
inline void FollowAuthoredHeading(FirstPersonHeading& state,const AuthoredHeadingInput* sample) {
    if(!sample||!sample->action||!sample->pairId||!Finite(sample->actorForward)) {state.authored={};return;}
    Vec3 forward{sample->actorForward.x,0,sample->actorForward.z};
    const float length2=Dot(forward,forward);
    // Upright actor ROOT only. Undefined/tilted/noisy head axes are never
    // remembered for catch-up when an anatomical sample becomes valid again.
    if(!std::isfinite(length2)||length2<.25f||length2>4.f){state.authored={};return;}
    forward=Scale(forward,1.f/std::sqrt(length2));
    auto& previous=state.authored;
    if(!previous.valid||previous.action!=sample->action||sample->pairId<previous.pairId){
        previous={true,sample->action,sample->pairId,forward};return;
    }
    // Both physical eyes share one tracking/base rotation. Current anatomy
    // still supplies each eye's position, but root yaw is consumed once.
    if(previous.pairId==sample->pairId)return;
    previous.pairId=sample->pairId;
    const float angle=std::atan2(previous.actorForward.z*forward.x-previous.actorForward.x*forward.z,
                                Dot(previous.actorForward,forward));
    // A 0.2-degree play deadband suppresses small root jitter without
    // quantising a sustained turn: after entry its continuing delta is direct.
    constexpr float deadband=.00349065850f;
    if(!std::isfinite(angle)||std::fabs(angle)<=deadband)return;
    const float applied=angle-std::copysign(deadband,angle);
    const Quat turn{0,std::sin(applied*.5f),0,std::cos(applied*.5f)};
    const auto horizontal=[&](Vec3 value){
        auto rotated=Rotate(turn,value);rotated.y=0;
        const float squared=Dot(rotated,rotated);return Scale(rotated,1.f/std::sqrt(squared));
    };
    state.basis.forward=horizontal(state.basis.forward);state.basis.up={0,1,0};
    state.basis.right=Cross(state.basis.up,state.basis.forward);
    if(state.worldGaze.valid)state.worldGaze.forward=horizontal(state.worldGaze.forward);
    previous.actorForward=horizontal(previous.actorForward);
}
struct HeadingRetentionContext {
    std::uintptr_t player=0,camera=0,sourceState=0;
    long recenterAck=0;
    std::uint32_t now=0,trackingTick=0;
    bool physical=false,uiOrCinema=false,ready=false,trackingValid=false,mounted=false;
};
inline bool RetainPhysicalHeading(const FirstPersonHeading& heading,const HeadingRetentionContext& context) {
    return heading.valid&&ValidateBasis(heading.basis)&&heading.player==context.player&&context.player&&
        heading.camera==context.camera&&context.camera&&heading.sourceState==context.sourceState&&context.sourceState&&
        heading.recenterAck==context.recenterAck&&context.physical&&!context.uiOrCinema&&context.ready&&
        context.trackingValid&&context.mounted&&context.trackingTick&&
        std::int32_t(context.now-context.trackingTick)>=0&&context.now-context.trackingTick<=250;
}
inline void RetireFirstPersonHeading(FirstPersonHeading& heading,bool preserveStableHeading) {
    if(!preserveStableHeading)heading={};
}
// In virtual-turn mode the native third-person camera still animates and
// orbits during reload/interaction. Its horizontal rotation must not become
// an extra VR turn. Keep the entry heading while taking the CURRENT anchor
// position; HMD/virtual-turn rotation is composed once afterwards.
inline bool AnimatedActorCamera(const CameraPose& native,const Basis& animatedBasis,CameraPose& output) {
    if(!ValidatePose(native)||!ValidateBasis(animatedBasis,.05f))return false;
    // Both axes come from the same owned animated palette. Projecting the
    // native camera forward onto body-up flips at a horizontal rappel pose.
    CameraPose next=native;next.basis=animatedBasis;output=next;return true;
}
struct PickupHeadInput {
    AuthoredHeadingInput root{};
    Basis head{};
};
// A pickup animation bends/translates its neck away from the native actor
// centre. Keep the VR eye over that actor and carry the BODY to it, rather
// than translating the headset sideways along the third-person animation.
// Keep the same queued head Y (including crouch), never mix CPU/GPU poses.
inline bool StablePickupEye(const CameraPose& actor,Vec3 animatedEye,Vec3& output) {
    if(!ValidatePose(actor,.05f)||!Finite(animatedEye))return false;
    const auto local=Subtract(animatedEye,actor.position);const float height=Dot(local,actor.basis.up);
    if(!std::isfinite(height))return false;
    const auto next=Add(actor.position,Scale(actor.basis.up,height));
    if(!Finite(next))return false;output=next;return true;
}
inline bool PickupPosture(const Basis& stable,const PickupHeadInput& pickup,Basis& output) {
    if(!ValidateBasis(stable,.05f)||!ValidateBasis(pickup.head,.05f))return false;
    CameraPose root;root.basis.forward=pickup.root.actorForward;root.basis.up={0,1,0};
    // Root yaw supplies the local frame. Neck/body bend supplies the head
    // posture; an orbiting game camera supplies neither of them.
    // KL1's neutral face is model -Z, as in AnimatedHeadForward. Using the
    // nominal entity +forward here reverses a real neutral pickup by180deg.
    const Vec3 f{-root.basis.forward.x,0,-root.basis.forward.z};const float n=Dot(f,f);
    if(!Finite(f)||!std::isfinite(n)||n<.25f||n>4.f)return false;
    root.basis.forward=Scale(f,1/std::sqrt(n));root.basis.right=Cross(root.basis.up,root.basis.forward);
    const auto map=[&](Vec3 axis){const Vec3 local{Dot(axis,root.basis.right),Dot(axis,root.basis.up),Dot(axis,root.basis.forward)};
        return CameraVector(stable,local.x,local.y,local.z);};
    Basis next{map(pickup.head.forward),map(pickup.head.up),map(pickup.head.right)};
    if(!ValidateBasis(next,.05f))return false;output=next;return true;
}
inline bool FirstPersonBase(FirstPersonHeading& state,std::uintptr_t player,std::uintptr_t camera,
                            bool stable,const CameraPose& native,Vec3 anchor,CameraPose& output,
                            long recenterAck=0,bool nativeActionTilt=false,const Basis* animatedBasis=nullptr,
                            const AuthoredHeadingInput* authored=nullptr,const PickupHeadInput* pickup=nullptr) {
    if(!player||!camera||!Finite(anchor)||!ValidatePose(native))return false;
    CameraPose result=native;
    if(nativeActionTilt) {
        CameraPose tilted=native;
        if(animatedBasis&&!AnimatedActorCamera(native,*animatedBasis,tilted))return false;
        // Use the animated actor's up when available, otherwise preserve
        // the authored tilt. Preserve a SAME owner's upright gameplay yaw
        // separately: clearing it here captured an animated third-person
        // orbit on exit, then applied the old virtual turn on top again.
        // A fresh action with no prior gameplay heading invents none.
        FirstPersonHeading next=state;
        if(!stable||!next.valid||next.player!=player||next.camera!=camera)next={};
        next.authored={};
        next.traversalHeadingPending=stable;
        next.player=player;next.camera=camera;
        tilted.position=anchor;output=tilted;state=next;return true;
    }
    FirstPersonHeading next=state;
    if(!stable) {
        if(!YawOnlyBase(native,result))return false;
        next={};
    }
    else {
        if(!next.valid||next.player!=player||next.camera!=camera) {
            if(!YawOnlyBase(native,result))return false;
            const bool traversalPending=next.traversalHeadingPending&&next.player==player&&next.camera==camera;
            next={true,player,camera,result.basis,recenterAck,{}};
            next.traversalHeadingPending=traversalPending;
        } else if(next.recenterAck!=recenterAck) {
            // The host resets both the physical origin and virtual turn on
            // recenter. Rebase onto the last rendered world heading so that
            // centring the headset cannot turn the player back to entry yaw.
            if(next.worldGaze.valid) {
                next.basis.forward=next.worldGaze.forward;
                next.basis.up={0.f,1.f,0.f};
                next.basis.right=Cross(next.basis.up,next.basis.forward);
            }
            next.recenterAck=recenterAck;next.worldGaze={};next.authored={};
        }
        // Reloads and constrained animations can point the native camera
        // straight up/down. Its heading is no longer needed once captured;
        // rejecting that vertical pose would drop the VR view for a frame.
        if(!ValidateBasis(next.basis))return false;
        // .42 tried to drive the camera with the authored pickup head/root.
        // The VR body must instead follow the already accepted gameplay
        // heading. Only true authored mini scenes retain their .41 turn path.
        FollowAuthoredHeading(next,authored);
        result.basis=next.basis;
        (void)pickup;
    }
    result.position=anchor;output=result;state=next;return true;
}
// Native WASD is relative to the ORIGINAL game camera. Once FP yaw is stable,
// measuring gaze against the frozen base would send movement in the wrong
// world direction whenever the native camera auto-orbits.
inline bool MovementYaw(const Basis& inputBasis,Vec3 gaze,float& output) {
    if(!ValidateBasis(inputBasis)||!Finite(gaze))return false;
    const auto& r=inputBasis.right;const auto& f=inputBasis.forward;
    // Native guide bases can contain pitch and roll. Solve their actual XZ
    // projection instead of taking 3D dots (which would mix HMD pitch into
    // walk yaw) or assuming the two projected axes remain orthonormal.
    const float determinant=r.x*f.z-r.z*f.x;
    if(!std::isfinite(determinant)||std::fabs(determinant)<1e-4f||gaze.x*gaze.x+gaze.z*gaze.z<1e-6f)return false;
    const float right=(gaze.x*f.z-gaze.z*f.x)/determinant;
    const float forward=(r.x*gaze.z-r.z*gaze.x)/determinant;
    if(!std::isfinite(right)||!std::isfinite(forward)||right*right+forward*forward<1e-6f)return false;
    output=std::atan2(right,forward);return std::isfinite(output);
}
inline bool MovementYaw(const Basis& inputBasis,const CameraPose& eye,float& output) {
    return ValidatePose(eye)&&MovementYaw(inputBasis,eye.basis.forward,output);
}
struct MovementHeading {
    std::uintptr_t player=0,camera=0;
    long recenterAck=0;
    bool anchored=false,stableYaw=false;
    HorizontalGaze gaze{};
    void Reset(){*this={};}
    bool Select(std::uintptr_t currentPlayer,std::uintptr_t currentCamera,long currentAck,
                bool currentAnchored,bool currentStableYaw,const CameraPose& eye,Vec3& forward) {
        if(!currentPlayer||!currentCamera||!ValidatePose(eye)){Reset();return false;}
        if(player!=currentPlayer||camera!=currentCamera||recenterAck!=currentAck||
           anchored!=currentAnchored||stableYaw!=currentStableYaw) {
            Reset();player=currentPlayer;camera=currentCamera;recenterAck=currentAck;
            anchored=currentAnchored;stableYaw=currentStableYaw;
        }
        return gaze.Select(eye,forward);
    }
};
inline bool VisibilityCamera(std::uintptr_t source,std::uintptr_t local,std::uintptr_t cinematic,bool panel) {
    return !panel&&source&&(source==local||source==cinematic);
}
inline bool SameView(const CameraPose& actual,const CameraPose& expected,float positionTolerance=.001f) {
    if(!ValidatePose(actual)||!ValidatePose(expected)||!std::isfinite(positionTolerance)||positionTolerance<0.f)return false;
    const auto close=[&](Vec3 a,Vec3 b,float tolerance){return Dot(Subtract(a,b),Subtract(a,b))<=tolerance*tolerance;};
    return close(actual.basis.forward,expected.basis.forward,.0001f)&&
        close(actual.basis.up,expected.basis.up,.0001f)&&close(actual.basis.right,expected.basis.right,.0001f)&&
        close(actual.position,expected.position,positionTolerance);
}
inline bool RestoreQueuedProjection(bool cinematic,const CameraPose& actual,const CameraPose& expected,
                                    const float* native,const float* eye,float* current) {
    if(!cinematic||!native||!eye||!current||!SameView(actual,expected)||
       std::memcmp(current,native,16*sizeof(float))||!std::memcmp(native,eye,16*sizeof(float)))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(native[i])||!std::isfinite(eye[i]))return false;
    std::memcpy(current,eye,16*sizeof(float));return true;
}
inline bool RestoreProjectionRows(unsigned start,float* rows,unsigned count,const float* native,const float* eye) {
    if(!rows||count<4||!native||!eye)return false;
    bool any=false;
    for(unsigned reg:{0u,24u,69u}){
        if(reg<start||reg-start>count-4)continue;
        auto* matrix=rows+(reg-start)*4;
        bool direct=true,transpose=true;
        for(unsigned i=0;i<16;++i){
            if(!std::isfinite(native[i])||!std::isfinite(eye[i]))return false;
            const auto match=[&](float value,float expected){
                if(!std::isfinite(value))return false;
                // Authored crop/aspect may scale only the two focal axes.
                if(i==0||i==5)return expected>0&&value>0&&value/expected>=.25f&&value/expected<=4.f;
                return value==expected;
            };
            direct=direct&&match(matrix[i],native[i]);transpose=transpose&&match(matrix[i],native[(i%4)*4+i/4]);
        }
        if(!direct&&!transpose)continue;
        for(unsigned i=0;i<16;++i)matrix[i]=direct?eye[i]:eye[(i%4)*4+i/4];any=true;
    }
    return any;
}
inline bool SameProjection(const float* actual,const float* expected) {
    if(!actual||!expected)return false;
    for(unsigned i=0;i<16;++i)
        if(!std::isfinite(actual[i])||!std::isfinite(expected[i])||std::fabs(actual[i]-expected[i])>1e-5f)return false;
    return true;
}

struct GpuCameraContext {
    std::uintptr_t source=0,player=0,action=0;
    bool fullCinematic=false,actionKnown=false;
    std::uintptr_t movement=0;
    bool movementKnown=false;
};
inline bool SameCameraContext(const GpuCameraContext& a,const GpuCameraContext& b) {
    return a.source==b.source&&a.player==b.player&&a.fullCinematic==b.fullCinematic&&
        a.actionKnown==b.actionKnown&&(!a.actionKnown||a.action==b.action)&&
        a.movementKnown==b.movementKnown&&(!a.movementKnown||a.movement==b.movement);
}
inline bool RequiresFreshPair(unsigned eye,std::uint64_t previousPair,std::uint64_t currentPair,bool sameContext) {
    return eye==1&&previousPair&&previousPair==currentPair&&!sameContext;
}
// Authored close camera shots can use a larger near plane to exclude the
// actor's own face. Lowering it to the gameplay VR value reveals its inside.
inline bool CinematicNearPlane(const float* native,float requested,float& output) {
    if(!native||!std::isfinite(requested)||requested<=0.f||!std::isfinite(native[10])||
       !std::isfinite(native[14])||native[10]<=0.f||native[14]>=0.f)return false;
    const float authored=-native[14]/native[10];
    if(!std::isfinite(authored)||authored<=0.f)return false;
    output=std::max(requested,authored);return true;
}
inline bool SelectCameraProjection(bool nativeScope,const float* native,const float* fov,
                                   float nearUnits,float farUnits,bool infinite,float* output) {
    if(!output)return false;
    if(!nativeScope)return ApplyEyeProjection(output,fov,nearUnits,farUnits,infinite);
    if(!native||native[0]<=.01f||native[5]<=.01f||std::fabs(native[15])>1e-5f)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(native[i]))return false;
    float checkedNear=0;
    if(!CinematicNearPlane(native,1.f,checkedNear))return false;
    // Optic masks and their crosshair were authored for this zoom/aspect.
    // Replacing this with the headset FOV skews them even with zero IPD.
    std::memcpy(output,native,16*sizeof(float));return true;
}

// ZGeomRef::GetWorldTransform walks local geometry and its parents. Editing
// only a render-entry cache does not change those independent consumers.
inline bool WorldToLocal(const CameraPose& world,const CameraPose& parent,CameraPose& output) {
    if(!ValidatePose(world)||!ValidatePose(parent))return false;
    const auto inverse=[&](Vec3 vector) {
        return Vec3{Dot(vector,parent.basis.right),Dot(vector,parent.basis.up),Dot(vector,parent.basis.forward)};
    };
    CameraPose result;
    result.position=inverse(Subtract(world.position,parent.position));
    result.basis={inverse(world.basis.forward),inverse(world.basis.up),inverse(world.basis.right)};
    if(!ValidatePose(result))return false;
    output=result;return true;
}

// The provider's delta transforms original camera coordinates into eye
// coordinates. Its inverse therefore expresses the eye's pose in the original
// camera. Native rows are F/U/R; ordinary D3D camera rows are R/U/F.
inline bool ApplyPose(const CameraPose& base,const float* viewDelta,CameraPose& output) {
    if(!viewDelta||!ValidatePose(base))return false;
    StereoRenderMath::Mat4 delta,inverse;
    std::memcpy(delta,viewDelta,sizeof(delta));
    if(!StereoRenderMath::Invert(delta,inverse))return false;
    for(unsigned row=0;row<3;++row)
        if(std::fabs(inverse[row][3])>1e-4f)return false;
    if(std::fabs(inverse[3][3]-1.f)>1e-4f)return false;
    CameraPose result;
    result.basis.forward=CameraVector(base.basis,inverse[2][0],inverse[2][1],inverse[2][2]);
    result.basis.up=CameraVector(base.basis,inverse[1][0],inverse[1][1],inverse[1][2]);
    result.basis.right=CameraVector(base.basis,inverse[0][0],inverse[0][1],inverse[0][2]);
    result.position=Add(base.position,CameraVector(base.basis,inverse[3][0],inverse[3][1],inverse[3][2]));
    if(!ValidatePose(result))return false;
    output=result;return true;
}

// Rappel uses a separate anatomical camera and bounded physical neutral.
// Its exit must not resurrect the old pre-rope gameplay yaw/virtual turn.
// Resolve the first current actor pose into a base which renders its face
// heading with today's tracking delta; subsequent physical look stays free.
inline bool ResumeActorHeading(FirstPersonHeading& heading,Vec3 actorForward,
                               const float* delta,CameraPose& base) {
    if(!heading.traversalHeadingPending)return true;
    if(!heading.valid||!ValidatePose(base)||!Finite(actorForward))return false;
    Vec3 face{-actorForward.x,0,-actorForward.z};const float squared=Dot(face,face);
    CameraPose relative;
    const CameraPose identity{{{0,0,1},{0,1,0},{1,0,0}},{}};
    if(!std::isfinite(squared)||squared<.25f||squared>4.f||!ApplyPose(identity,delta,relative))return false;
    const float horizontal=relative.basis.forward.x*relative.basis.forward.x+relative.basis.forward.z*relative.basis.forward.z;
    if(!std::isfinite(horizontal))return false;
    // Looking straight up at the rope is a normal exit pose. Its right axis
    // still defines tracking yaw when the forward XZ projection degenerates.
    const float trackingYaw=horizontal>=.01f?std::atan2(relative.basis.forward.x,relative.basis.forward.z):
        std::atan2(-relative.basis.right.z,relative.basis.right.x);
    face=Scale(face,1.f/std::sqrt(squared));
    const float yaw=std::atan2(face.x,face.z)-trackingYaw;
    const Basis aligned{{std::sin(yaw),0,std::cos(yaw)},{0,1,0},{std::cos(yaw),0,-std::sin(yaw)}};
    if(!ValidateBasis(aligned))return false;
    heading.basis=aligned;heading.worldGaze={};heading.authored={};heading.traversalHeadingPending=false;
    base.basis=aligned;return true;
}

inline bool VisibilityPose(const CameraPose& currentNative,const CameraPose& previousEye,
                           const float* previousDelta,bool authoredCinematic,CameraPose& output) {
    // An authored camera moves independently of tracking. Its previous WORLD
    // eye belongs to an older point on the path. Reapply only the tracked
    // view delta to today's native camera, just as the GPU consumer does.
    if(authoredCinematic)return ApplyPose(currentNative,previousDelta,output);
    if(!ValidatePose(previousEye))return false;
    output=previousEye;return true;
}

inline CameraPose GameplayVisibilityCentre(CameraPose eye,Vec3 offset,float units,Vec3 nativeMotion={}) {
    // The next native visibility pass serves the pair. Allow small tracked
    // translations near a character, including across the near plane.
    eye.position=Subtract(eye.position,Scale(MapTracking(eye.basis,offset),units));
    // Rebase the previous tracking sample to the current camera movement,
    // but ignore native camera cuts and large third-person offsets.
    if(Finite(nativeMotion)&&Dot(nativeMotion,nativeMotion)<=.6f*.6f*units*units)
        eye.position=Add(eye.position,nativeMotion);
    eye.position=Subtract(eye.position,Scale(eye.basis.forward,.12f*units));
    return eye;
}

inline bool Apply(Space& space,const float* viewDelta,const float* renderFov,
                  const float* cullingFov,float nearUnits) {
    if(space.orthographic||space.planeCount<6||space.planeCount>12||
       !ValidFov(renderFov)||!ValidFov(cullingFov)||!std::isfinite(nearUnits)||
       nearUnits<=0.f||!std::isfinite(space.farUnits)||space.farUnits<=nearUnits)return false;
    CameraPose pose;
    if(!ApplyPose({space.basis,space.position},viewDelta,pose))return false;
    Space result=space;
    result.basis=pose.basis;result.position=pose.position;
    result.positionCache=result.position;
    // Only widen visibility. RVA 0xBF6FE consumes horizontalFov immediately
    // after this copy to compute the camera entry's native LOD scale:
    // .01 * tan(FOV/2) / tan(.5881759524). Substituting the conservative
    // culling union here incorrectly makes the whole scene less detailed.
    // Visibility setup at RVA 0x74CD0 consumes the pose and clip planes, not
    // these projection scalars. Keep all of them native and let the exact
    // GPU camera consumer install the actual per-eye view/projection.
    if(!ApplyClipFov(&result.planes[0][0],cullingFov))return false;
    result.nearUnits=nearUnits;
    // Depth planes are already local, outward, normalized by the constructor.
    if(result.planes[0][2]<-0.9f)result.planes[0][3]=nearUnits;
    space=result;
    return true;
}
}
