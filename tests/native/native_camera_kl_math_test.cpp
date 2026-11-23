#include "native_camera_kl_math.hpp"
#include "native_visual_policy.hpp"
#include <cstdio>
#include <cstring>

using namespace NativeCameraMath;
using namespace NativeCameraKLMath;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* what) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
bool Close(Vec3 a,Vec3 b,float epsilon=0.002f) {return std::fabs(a.x-b.x)<epsilon&&std::fabs(a.y-b.y)<epsilon&&std::fabs(a.z-b.z)<epsilon;}
Space Initial() {
    Space space{};
    // Observed K&L camera: F/U/R, determinant -1.
    space.basis={{.55271155f,-.11014334f,.82606196f},
                 {.06125016f,.99391568f,.09154219f},
                 {.83111870f,0.f,-.55609500f}};
    space.position={-3090.9072f,168.1513f,-9060.0322f};
    space.positionCache=space.position;
    space.nearUnits=1;space.farUnits=40000;space.planeCount=6;
    space.horizontalFov=1.169370532f;space.alternateFov=1.178097248f;
    space.perspective=.7554177046f;space.scaleX=.875f;space.aspect=1.777777791f;
    space.projectionScale=1.25f;
    space.planes[0][2]=-1;space.planes[0][3]=1;
    space.planes[1][2]=1;space.planes[1][3]=-40000;
    // An optional portal plane must survive widening.
    space.planes[6][0]=.3f;space.planes[6][3]=123.f;
    return space;
}
Vec3 ViewPoint(const CameraPose& view,Vec3 point) {
    // Native entity binder multiplies the world point by the packed view
    // basis, then adds packed view translation (RVA 0x71770).
    return Add(CameraVector(view.basis,point.x,point.y,point.z),view.position);
}
void TestGpuViewConvention() {
    const Space observed=Initial();
    const CameraPose cameras[]={
        {Basis{},{10.f,20.f,30.f}},
        {observed.basis,observed.position},
        {{{0.f,0.f,1.f},{0.f,1.f,0.f},{1.f,0.f,0.f}},{-15.f,180.f,30.f}}
    };
    for(const auto& world:cameras) {
        CameraPose view,recovered;
        Check(ViewFromWorld(world,view),"GPU packed view accepts native world pose");
        Check(Close(ViewPoint(view,world.position),{}),"GPU packed view maps the world eye to origin");
        Check(Close(ViewPoint(view,Add(world.position,Scale(world.basis.forward,10.f))),{0.f,0.f,10.f}),
              "GPU packed view maps world forward to camera positive Z");
        Check(Close(ViewPoint(view,Add(world.position,Scale(world.basis.right,10.f))),{10.f,0.f,0.f}),
              "GPU packed view maps world right to camera positive X");
        Check(Close(ViewPoint(view,Add(world.position,Scale(world.basis.up,10.f))),{0.f,10.f,0.f}),
              "GPU packed view maps world up to camera positive Y");
        Check(WorldFromView(view,recovered)&&Close(recovered.position,world.position)&&
              Close(recovered.basis.forward,world.basis.forward)&&Close(recovered.basis.up,world.basis.up)&&
              Close(recovered.basis.right,world.basis.right),"GPU packed view/world round trip retains full tilted pose");
        float plane[4]{};EyePlane(world,plane);
        const auto distance=[&](Vec3 point) {
            return plane[0]*point.x+plane[1]*point.y+plane[2]*point.z+plane[3];
        };
        Check(std::fabs(distance(world.position))<.002f,"GPU eye plane contains the rendered eye");
        Check(std::fabs(distance(Add(world.position,Scale(world.basis.forward,10.f)))+10.f)<.002f,
              "GPU eye plane has negative distance for points in front");
        Check(std::fabs(distance(Add(world.position,Scale(world.basis.right,10.f))))<.002f&&
              std::fabs(distance(Add(world.position,Scale(world.basis.up,10.f))))<.002f,
              "GPU eye plane contains right and up eye-plane offsets");
    }
    CameraPose explicitView;
    Check(ViewFromWorld(cameras[0],explicitView)&&Close(explicitView.basis.forward,{-1.f,0.f,0.f})&&
          Close(explicitView.basis.up,{0.f,1.f,0.f})&&Close(explicitView.basis.right,{0.f,0.f,1.f})&&
          Close(explicitView.position,{30.f,-20.f,-10.f}),"GPU native default basis has independently known packed inverse view");
    CameraPose invalid=cameras[0],sentinel=cameras[2];invalid.basis.forward={0.f,0.f,0.f};
    CameraPose output=sentinel;
    Check(!ViewFromWorld(invalid,output)&&std::memcmp(&output,&sentinel,sizeof(output))==0,
          "invalid GPU world pose leaves view output unchanged");
    Check(!WorldFromView(invalid,output)&&std::memcmp(&output,&sentinel,sizeof(output))==0,
          "invalid packed GPU view leaves world output unchanged");
}
void TestYawOnlyBase() {
    const Space observed=Initial();
    const CameraPose pitched{observed.basis,observed.position};
    CameraPose yaw;
    Check(YawOnlyBase(pitched,yaw)&&ValidatePose(yaw),"first-person yaw base accepts observed pitched camera");
    Check(Close(yaw.position,pitched.position),"first-person yaw base preserves the supplied head anchor");
    Check(Close(yaw.basis.forward,{.55609500f,0.f,.83111870f})&&Close(yaw.basis.up,{0.f,1.f,0.f})&&
          Close(yaw.basis.right,{.83111870f,0.f,-.55609500f}),"first-person yaw base removes native camera pitch without changing heading");
    const CameraPose ninety{{{0.f,0.f,1.f},{0.f,1.f,0.f},{1.f,0.f,0.f}},{1.f,2.f,3.f}};
    Check(YawOnlyBase(ninety,yaw)&&Close(yaw.basis.forward,ninety.basis.forward)&&
          Close(yaw.basis.up,ninety.basis.up)&&Close(yaw.basis.right,ninety.basis.right)&&Close(yaw.position,ninety.position),
          "first-person yaw base preserves a horizontal 90 degree heading");
    const CameraPose vertical{{{0.f,1.f,0.f},{1.f,0.f,0.f},{0.f,0.f,1.f}},{1.f,2.f,3.f}};
    yaw=ninety;
    Check(!YawOnlyBase(vertical,yaw)&&std::memcmp(&yaw,&ninety,sizeof(yaw))==0,
          "first-person yaw base rejects undefined vertical heading without changing output");
}
void TestFirstPersonStability() {
    const CameraPose start{{{0,0,1},{0,1,0},{1,0,0}},{4,5,6}};
    FirstPersonHeading heading;
    CameraPose result;
    Check(FirstPersonBase(heading,1,2,true,start,{10,170,20},result),"stable first-person heading initializes from native entry yaw");
    for(int orbit=-180;orbit<=180;orbit+=15) {
        const float angle=orbit*3.14159265359f/180.f;
        CameraPose native{{{std::sin(angle),0,std::cos(angle)},{0,1,0},{std::cos(angle),0,-std::sin(angle)}},{3,4,5}};
        const Vec3 currentAnchor{float(orbit),168.f+orbit*.02f,42.f};
        Check(FirstPersonBase(heading,1,2,true,native,currentAnchor,result),"native reload/orbit accepts a current head anchor");
        Check(Close(result.basis.forward,start.basis.forward)&&Close(result.position,currentAnchor),"native orbit cannot rotate or lag first-person view; current position remains unsmoothed");
        for(int gaze=-120;gaze<=120;gaze+=30) {
            const float gazeAngle=gaze*3.14159265359f/180.f;
            CameraPose eye{{{std::sin(gazeAngle),0,std::cos(gazeAngle)},{0,1,0},{std::cos(gazeAngle),0,-std::sin(gazeAngle)}},{0,170,0}};
            float yaw=999;
            Check(MovementYaw(native.basis,eye,yaw),"input-basis movement yaw accepts original native WASD basis");
            const Vec3 walked=Add(Scale(native.basis.right,std::sin(yaw)),Scale(native.basis.forward,std::cos(yaw)));
            Check(Close(walked,eye.basis.forward),"forward stick follows rendered gaze across native camera yaw changes");
        }
    }
    const CameraPose vertical{{{0,1,0},{1,0,0},{0,0,1}},{3,4,5}};
    Check(FirstPersonBase(heading,1,2,true,vertical,{22,145,33},result)&&
          Close(result.basis.forward,start.basis.forward)&&Close(result.position,{22,145,33}),
          "captured VR heading survives a vertical native camera without dropping the current anchor");
    CameraPose down=vertical;down.basis.forward={0,-1,0};down.basis.right={0,0,-1};
    Check(FirstPersonBase(heading,1,2,true,down,{23,144,34},result)&&
          Close(result.basis.forward,start.basis.forward)&&Close(result.position,{23,144,34}),
          "captured VR heading survives a downward native camera without inheriting its pitch");
    const auto headingBefore=heading;const auto resultBefore=result;
    Check(!FirstPersonBase(heading,7,8,true,vertical,{0,170,0},result)&&
          std::memcmp(&heading,&headingBefore,sizeof(heading))==0&&
          std::memcmp(&result,&resultBefore,sizeof(result))==0,
          "new player/camera with undefined heading retains the last valid state until initialization is possible");
    Check(!FirstPersonBase(heading,1,2,false,vertical,{0,170,0},result)&&
          std::memcmp(&heading,&headingBefore,sizeof(heading))==0,
          "legacy mode still rejects an undefined native horizontal heading");
    CameraPose corrupt=start;corrupt.basis.up={0,0,0};
    Check(!FirstPersonBase(heading,1,2,true,corrupt,{0,170,0},result)&&
          std::memcmp(&heading,&headingBefore,sizeof(heading))==0,
          "captured heading cannot authorize a corrupt native camera pose");
    CameraPose native{{{1,0,0},{0,1,0},{0,0,-1}},{0,0,0}};
    Check(FirstPersonBase(heading,1,2,false,native,{1,2,3},result)&&Close(result.basis.forward,native.basis.forward)&&!heading.valid,
          "legacy mouse mode retains native heading rather than freezing it");
    Check(FirstPersonBase(heading,7,8,true,native,{1,2,3},result)&&heading.player==7&&heading.camera==8&&Close(result.basis.forward,native.basis.forward),
          "replaced player/camera initializes a new first-person heading");
    CameraPose view;
    Check(ViewFromWorld(result,view)&&SameView(view,view),"authorized inverse view matches its exact written camera");
    auto changed=view;changed.position.x+=1;
    Check(!SameView(changed,view),"shadow/reflection eye position cannot inherit player palette authorization");
    changed=view;changed.basis.forward=Scale(view.basis.forward,-1);changed.basis.right=Scale(view.basis.right,-1);
    Check(!SameView(changed,view),"different shadow/reflection camera rotation retains native player head");
    float p[16]{},other[16]{};p[0]=p[5]=1;p[10]=1;p[11]=1;p[14]=-5;
    std::memcpy(other,p,sizeof(p));
    Check(SameProjection(p,other),"authorized projection is matched without mutating renderer state");
    other[0]=2;
    Check(!SameProjection(p,other),"different shadow/reflection projection cannot inherit head hiding");
    using namespace NativeVisualPolicy;
    NativeActionKL::State action;action.valid=true;action.player=7;action.action=10;
    action.actionKind=NativeActionKL::Action::Reload;action.movementKind=NativeActionKL::Movement::BaseMove;
    const auto reload=ForAction(action,7);
    Check(reload.nativeArms&&!reload.nativeBody,"manual or empty reload restores authored hands while keeping accumulated visual body heading");
    action.actionKind=NativeActionKL::Action::MiniAct;
    Check(ForAction(action,7).nativeArms&&ForAction(action,7).nativeBody,"scripted native actions still own the complete body animation");
    action.action=0;action.actionKind=NativeActionKL::Action::None;action.movementKind=NativeActionKL::Movement::Cover;
    Check(!ForAction(action,7).nativeArms&&ForAction(action,7).nativeBody,"native cover preserves wall-facing body while allowing tracked arms");
    Check(!ForAction(action,8).nativeBody,"foreign actor action cannot change current player body policy");
    Check(HideHead(true,true,{0,170,0},{35,170,0},100),"bounded physical head lean keeps first-person head clipping suppressed");
    Check(!HideHead(false,true,{0,170,0},{0,170,0},100),"side cinematic view keeps the player's native head");
    Check(!HideHead(true,true,{0,170,0},{100,170,0},100),"an external camera cannot erase a visibly separate character's head");
}
void TestGuideMovementAndVisibility() {
    const CameraPose base{{{0,0,1},{0,1,0},{1,0,0}},{0,0,0}};
    constexpr float radians=3.14159265359f/180.f;
    for(float pitch:{-60.f,-4.f,0.f,4.f,60.f})for(float roll:{-30.f,0.f,30.f})for(float yaw:{-150.f,0.f,73.f}) {
        const Quat qp{std::sin(pitch*radians/2),0,0,std::cos(pitch*radians/2)};
        const Quat qr{0,0,std::sin(roll*radians/2),std::cos(roll*radians/2)};
        const Quat qy{0,std::sin(yaw*radians/2),0,std::cos(yaw*radians/2)};
        CameraPose guide;
        Check(Compose(base,Multiply(qy,Multiply(qp,qr)),{0,0,0},{0,0,0},100,guide),"native tilted guide basis accepts parent-composed pitch/roll");
        for(float lookYaw:{-120.f,15.f,140.f})for(float lookPitch:{-65.f,0.f,65.f}) {
            const Quat lp{std::sin(lookPitch*radians/2),0,0,std::cos(lookPitch*radians/2)};
            const Quat ly{0,std::sin(lookYaw*radians/2),0,std::cos(lookYaw*radians/2)};
            CameraPose eye;
            Check(Compose(base,Multiply(ly,lp),{0,0,0},{0,0,0},100,eye),"test rendered HMD gaze accepts independent pitch");
            float movementYaw=0;
            Check(MovementYaw(guide.basis,eye,movementYaw),"tilted native guide input permits stable horizontal movement conversion");
            auto movement=Add(Scale(guide.basis.right,std::sin(movementYaw)),Scale(guide.basis.forward,std::cos(movementYaw)));
            const float ml=std::sqrt(movement.x*movement.x+movement.z*movement.z);
            const float gl=std::sqrt(eye.basis.forward.x*eye.basis.forward.x+eye.basis.forward.z*eye.basis.forward.z);
            Check(ml>0&&gl>0&&Close({movement.x/ml,0,movement.z/ml},{eye.basis.forward.x/gl,0,eye.basis.forward.z/gl}),
                  "native guide horizontal walking follows gaze regardless of guide or HMD pitch/roll");
        }
    }
    Check(VisibilityCamera(10,10,0,false),"owned gameplay camera retains tracked visibility union");
    Check(VisibilityCamera(20,10,20,false),"verified authored full-cut camera receives tracked visibility union");
    Check(!VisibilityCamera(30,10,20,false),"foreign shadow/reflection camera cannot receive HMD visibility");
    Check(!VisibilityCamera(20,10,20,true),"native cinematic panel fallback retains original visibility");
    Check(!VisibilityCamera(0,0,0,false),"missing camera cannot authorize visibility mutation");
}
void TestVerticalGazeMovementAndRecenter() {
    constexpr float radians=3.14159265359f/180.f;
    const CameraPose base{{{0,0,1},{0,1,0},{1,0,0}},{10,170,20}};
    const auto gaze=[&](float yaw,float pitch) {
        const Quat y{0,std::sin(yaw*radians/2),0,std::cos(yaw*radians/2)};
        const Quat p{std::sin(pitch*radians/2),0,0,std::cos(pitch*radians/2)};
        CameraPose result;
        Check(Compose(base,Multiply(y,p),{},{},100,result),"test gaze has a valid tracked pose");
        return result;
    };
    MovementHeading movement;Vec3 forward;
    const auto initial=gaze(57,0);
    Check(movement.Select(1,2,3,true,true,initial,forward)&&Close(forward,initial.basis.forward),
          "world movement heading captures the horizontal rendered gaze");
    const auto saved=forward;
    for(float pitch:{85.f,89.99f,90.f,90.01f,95.f,-85.f,-90.f,-95.f}) {
        const auto eye=gaze(120,pitch);
        Check(movement.Select(1,2,3,true,true,eye,forward)&&Close(forward,saved),
              "near-vertical HMD gaze retains world movement direction across the pole");
        for(float nativeYaw:{-150.f,0.f,113.f}) {
            const auto native=gaze(nativeYaw,0);float yaw=999;
            Check(MovementYaw(native.basis,forward,yaw),"held world gaze solves against current native input basis");
            const auto walked=Add(Scale(native.basis.right,std::sin(yaw)),Scale(native.basis.forward,std::cos(yaw)));
            Check(Close(walked,saved),"vertical gaze cannot turn walking when the character/native camera rotates");
        }
    }
    const auto returned=gaze(-23,80);
    Check(movement.Select(1,2,3,true,true,returned,forward)&&
          Close(forward,Scale(Vec3{returned.basis.forward.x,0,returned.basis.forward.z},
                              1.f/std::cos(80*radians))),
          "leaving the vertical pole immediately follows the current gaze without smoothing");
    const auto vertical=gaze(0,90);
    Check(!movement.Select(1,2,4,true,true,vertical,forward),"recenter cannot reuse old world movement heading");
    Check(movement.Select(1,2,4,true,true,initial,forward),"new recenter context initializes from a usable gaze");
    Check(!movement.Select(7,2,4,true,true,vertical,forward),"new player cannot inherit old movement heading");
    Check(movement.Select(7,2,4,true,true,initial,forward)&&
          !movement.Select(7,8,4,true,true,vertical,forward),"new camera cannot inherit old movement heading");
    Check(movement.Select(7,8,4,true,true,initial,forward)&&
          !movement.Select(7,8,4,false,true,vertical,forward),"first-person mode transition clears movement heading");
    Check(movement.Select(7,8,4,false,true,initial,forward)&&
          !movement.Select(7,8,4,false,false,vertical,forward),"turn mode transition clears movement heading");
    movement.Reset();
    Check(!movement.Select(7,8,4,false,false,vertical,forward),"tracking/lifecycle reset cannot reuse a previous gaze");

    FirstPersonHeading heading;CameraPose beforeBase;
    Check(FirstPersonBase(heading,11,22,true,base,base.position,beforeBase,1),"recenter test initializes frozen native yaw");
    const Quat physical{0,std::sin(37*radians/2),0,std::cos(37*radians/2)};
    Quat relative;Vec3 displacement;
    Check(StereoRenderMath::ApplyVirtualYaw(physical,{},71*radians,relative,displacement),
          "recenter test combines nonzero physical and virtual turns");
    CameraPose beforeEye;
    Check(Compose(beforeBase,relative,displacement,{},100,beforeEye)&&
          Dot(beforeEye.basis.forward,beforeBase.basis.forward)<.9f&&
          heading.worldGaze.Select(beforeEye,forward),"last rendered world gaze includes both kinds of turn");
    CameraPose afterBase,afterEye;
    Check(FirstPersonBase(heading,11,22,true,base,base.position,afterBase,2)&&
          Compose(afterBase,{}, {}, {},100,afterEye)&&Close(afterEye.basis.forward,beforeEye.basis.forward),
          "recenter preserves world direction when physical origin and virtual yaw reset to zero");
    Check(heading.recenterAck==2&&!heading.worldGaze.valid,
          "recenter updates the frozen heading context and clears the old gaze sample");
    CameraPose repeated;
    Check(FirstPersonBase(heading,11,22,true,initial,base.position,repeated,2)&&
          Close(repeated.basis.forward,afterBase.basis.forward),
          "native auto-orbit after recenter cannot rebase the frozen heading a second time");
    Check(heading.worldGaze.Select(beforeEye,forward)&&
          FirstPersonBase(heading,99,22,true,base,base.position,afterBase,3)&&
          Close(afterBase.basis.forward,base.basis.forward)&&!heading.worldGaze.valid,
          "recenter during a player replacement captures new native yaw instead of carrying old world direction");
    Check(heading.worldGaze.Select(beforeEye,forward)&&
          FirstPersonBase(heading,99,77,true,base,base.position,afterBase,4)&&
          Close(afterBase.basis.forward,base.basis.forward)&&!heading.worldGaze.valid,
          "recenter during a camera replacement cannot carry old world direction");
    Check(heading.worldGaze.Select(beforeEye,forward)&&
          FirstPersonBase(heading,99,77,false,base,base.position,afterBase,5)&&
          !heading.valid&&!heading.worldGaze.valid&&
          FirstPersonBase(heading,99,77,true,base,base.position,afterBase,5)&&
          Close(afterBase.basis.forward,base.basis.forward),
          "legacy turn mode discards frozen and rendered headings before first-person yaw is enabled again");
}
void TestCinematicCameraCoherence() {
    GpuCameraContext before{10,20,30,false,true},after=before;
    Check(SameCameraContext(before,after),"same native camera/action packet may reuse one rendered eye");
    after.source=11;Check(!SameCameraContext(before,after),"a new native camera source cannot reuse the previous eye within a GPU frame");
    after=before;after.player=21;Check(!SameCameraContext(before,after),"a replaced local player cannot inherit another actor's camera frame");
    after=before;after.action=31;Check(!SameCameraContext(before,after),"scripted actor action entry/exit invalidates a cached first-person eye");
    after=before;after.fullCinematic=true;Check(!SameCameraContext(before,after),"a full-cut transition cannot retain a cached gameplay eye");
    after=before;after.actionKnown=false;Check(!SameCameraContext(before,after),"losing verified action-token availability refreshes camera classification");
    before.actionKnown=false;before.action=100;after.action=200;
    Check(SameCameraContext(before,after),"unavailable action-token output bytes never become an ownership key");
    Check(RequiresFreshPair(1,10,10,false),"a right eye crossing an authored camera/action boundary cannot be paired with the earlier left eye");
    Check(!RequiresFreshPair(0,10,10,false),"a new camera can start a left eye normally without a redundant pair restart");
    Check(!RequiresFreshPair(1,9,10,false)&&!RequiresFreshPair(1,0,10,false),"another pair or an uninitialized context cannot discard a fresh right eye");
    Check(!RequiresFreshPair(1,10,10,true),"matching camera and action context preserves ordinary alternating-eye pairing");
    before={10,20,0,false,true,40,true};after=before;after.movement=41;
    Check(!SameCameraContext(before,after),"entering or leaving a rappel movement invalidates the current GPU eye even with no discrete action");
    after=before;after.movementKnown=false;
    Check(!SameCameraContext(before,after),"lost movement provenance cannot reuse an upright eye during a constrained action transition");
    float p[16]{};p[10]=1;p[14]=-25;float near=0;
    Check(CinematicNearPlane(p,5,near)&&near==25,"authored cinematic near plane is retained instead of exposing the actor's face inside a close shot");
    Check(CinematicNearPlane(p,30,near)&&near==30,"a larger user VR near plane remains respected");
    p[10]=40000.f/39975.f;p[14]=-25*p[10];
    Check(CinematicNearPlane(p,5,near)&&std::fabs(near-25)<.001f,"finite native perspective near plane is decoded without changing the authored world pose");
    p[14]=1;near=123;
    Check(!CinematicNearPlane(p,5,near)&&near==123,"invalid cinematic projection leaves the accepted near plane unchanged");
    Check(!CinematicNearPlane(nullptr,5,near)&&!CinematicNearPlane(p,0,near),"missing projection or invalid requested near plane cannot authorize a cinematic override");
}
void TestRappelCameraTilt() {
    const CameraPose standing{{{0,0,1},{0,1,0},{1,0,0}},{12,170,24}};
    const Vec3 actionHead{87,73,-19};
    constexpr float radians=3.14159265358979323846f/180.f;
    FirstPersonHeading state;CameraPose output;
    Check(FirstPersonBase(state,1,2,true,standing,standing.position,output)&&state.valid,
          "ordinary first-person heading is active before constrained animation");
    for(float pitch:{-90.f,-45.f,0.f,45.f,90.f})for(float roll:{-90.f,0.f,90.f}) {
        const Quat p{std::sin(pitch*radians*.5f),0,0,std::cos(pitch*radians*.5f)};
        const Quat r{0,0,std::sin(roll*radians*.5f),std::cos(roll*radians*.5f)};
        const Quat rotation=Multiply(r,p);
        const CameraPose authored{{Rotate(rotation,standing.basis.forward),Rotate(rotation,standing.basis.up),
                                   Rotate(rotation,standing.basis.right)},standing.position};
        Check(FirstPersonBase(state,1,2,true,authored,actionHead,output,0,true)&&
              Close(output.basis.forward,authored.basis.forward)&&Close(output.basis.up,authored.basis.up)&&
              Close(output.basis.right,authored.basis.right)&&Close(output.position,actionHead),
              "rappel preserves authored pitch and roll at the full animated head anchor, including horizontal and vertical poses");
        Check(state.valid&&Close(state.basis.forward,standing.basis.forward)&&!state.worldGaze.valid,
              "constrained camera orientation preserves the prior upright heading without replacing it with native action tilt");
        StereoRenderMath::Mat4 delta;StereoRenderMath::Identity(delta);
        delta[3][0]=-3.2f;
        CameraPose eye;
        Check(ApplyPose(output,&delta[0][0],eye)&&Close(eye.position,Add(actionHead,Scale(authored.basis.right,3.2f))),
              "per-eye separation stays on the actual tilted camera right axis");
    }
    Check(FirstPersonBase(state,1,2,true,standing,standing.position,output)&&state.valid&&
          Close(output.basis.up,{0,1,0}),"leaving rappel restores the previous upright heading without retaining action roll");
    const auto before=output;const auto history=state;auto bad=standing;bad.basis.up={0,0,0};
    Check(!FirstPersonBase(state,1,2,true,bad,actionHead,output,0,true)&&
          !std::memcmp(&output,&before,sizeof(output))&&state.valid==history.valid,
          "invalid action camera cannot modify the accepted camera or heading state");
}
void TestFreeCinematicViewDelta() {
    const auto turn=[](Vec3 axis,float angle){const float s=std::sin(angle*.5f);return Quat{axis.x*s,axis.y*s,axis.z*s,std::cos(angle*.5f)};};
    const Quat entry=Multiply(turn({0,1,0},.8f),Multiply(turn({1,0,0},-.3f),turn({0,0,1},.1f)));
    StereoRenderMath::CinematicLook state;Quat relative;
    Check(state.SelectFree(entry,10,0,relative),"free cinematic establishes its neutral physical origin");
    const auto initial=Initial();const CameraPose base{initial.basis,initial.position};
    for(float yaw:{-2.f,-.5f,.5f,2.f})for(float pitch:{-.8f,0.f,.8f})for(float roll:{-.3f,0.f,.3f}) {
        const Quat delta=Multiply(turn({0,1,0},-yaw),Multiply(turn({1,0,0},pitch),turn({0,0,1},roll)));
        Check(state.SelectFree(Multiply(entry,delta),10,0,relative),"cinematic accepts unconstrained physical gaze");
        CameraPose eye[2];
        for(unsigned side=0;side<2;++side){
            const Vec3 offset{side?.032f:-.032f,0,0};
            StereoRenderMath::Mat4 view;CameraPose expected;
            Check(StereoRenderMath::CameraViewDelta(relative,{},offset,0.f,100.f,view)&&
                  ApplyPose(base,&view[0][0],eye[side])&&Compose(base,delta,{},offset,100.f,expected),
                  "the actual cinematic view-delta consumer accepts the free-look camera");
            Check(Close(eye[side].position,expected.position)&&Close(eye[side].basis.forward,expected.basis.forward)&&
                  Close(eye[side].basis.up,expected.basis.up)&&Close(eye[side].basis.right,expected.basis.right),
                  "native cinematic VIEW agrees with physical camera composition without inverse yaw/pitch/roll");
        }
        Check(Close(Scale(Add(eye[0].position,eye[1].position),.5f),base.position),
              "a free cinematic keeps its authored native path and adds IPD once around the centre");
    }
}
void TestCinematicVisibilityCurrentPath() {
    const Space initial=Initial();const CameraPose previous{initial.basis,initial.position};
    for(float authoredYaw:{-1.4f,0.f,1.4f})for(float headYaw:{-2.f,0.f,2.f})for(float headPitch:{-.8f,0.f,.8f}) {
        const Quat authored{0,std::sin(authoredYaw*.5f),0,std::cos(authoredYaw*.5f)};
        const Quat head=Multiply({0,std::sin(headYaw*.5f),0,std::cos(headYaw*.5f)},
                                {std::sin(headPitch*.5f),0,0,std::cos(headPitch*.5f)});
        CameraPose current;Check(Compose(previous,authored,{10,2,-7},{},100,current),"moving authored path fixture");
        for(float offset:{-.032f,.032f}) {
            StereoRenderMath::Mat4 delta;CameraPose previousEye,expected,actual;
            Check(StereoRenderMath::CameraViewDelta(head,{},{offset,0,0},0,100,delta)&&
                  ApplyPose(previous,&delta[0][0],previousEye)&&ApplyPose(current,&delta[0][0],expected),"cinematic visibility fixture composes independent native frames");
            Check(VisibilityPose(current,previousEye,&delta[0][0],true,actual)&&
                  Close(actual.position,expected.position)&&Close(actual.basis.forward,expected.basis.forward)&&
                  Close(actual.basis.up,expected.basis.up)&&Close(actual.basis.right,expected.basis.right),
                  "cinematic culling follows current native path with the same tracked orientation and one IPD");
            Check(VisibilityPose(current,previousEye,&delta[0][0],false,actual)&&SameView(actual,previousEye),
                  "anchored gameplay visibility retains its independently positioned accepted eye");
            const auto centre=GameplayVisibilityCentre(previousEye,{offset,0,0},100);
            Check(Close(centre.position,Subtract(previous.position,Scale(centre.basis.forward,12.f))),
                  "both views give one backed-off culling origin for nearby geometry");
            const auto moved=GameplayVisibilityCentre(previousEye,{offset,0,0},100,{17,0,4});
            Check(Close(moved.position,Add(centre.position,{17,0,4}))&&
                  Close(GameplayVisibilityCentre(previousEye,{offset,0,0},100,{200,0,0}).position,centre.position),
                  "nearby visibility follows small camera movement and does not follow native cuts");
        }
    }
    CameraPose untouched=previous;StereoRenderMath::Mat4 invalid{};
    Check(!VisibilityPose(previous,previous,&invalid[0][0],true,untouched)&&SameView(untouched,previous),
          "invalid cinematic view delta cannot change visibility pose");
}
void TestPhysicalActionHeadingHistory() {
    constexpr float rad=3.14159265358979323846f/180.f;
    const auto pose=[](float yaw,Vec3 position){return CameraPose{{{std::sin(yaw),0,std::cos(yaw)},
        {0,1,0},{std::cos(yaw),0,-std::sin(yaw)}},position};};
    for(float initialYaw:{-73.f,0.f,42.f})for(float virtualYaw:{-90.f,71.f})for(float orbit:{90.f,180.f}) {
        const auto start=pose(initialYaw*rad,{1,170,2});FirstPersonHeading heading;CameraPose base,accepted;
        Check(FirstPersonBase(heading,11,22,true,start,start.position,base,1),"action history starts at an accepted owned upright yaw");
        const Quat physical{0,std::sin(37.f*rad*.5f),0,std::cos(37.f*rad*.5f)};Quat relative;Vec3 displacement;
        Check(StereoRenderMath::ApplyVirtualYaw(physical,{},virtualYaw*rad,relative,displacement)&&
              Compose(base,relative,displacement,{},100,accepted),"physical and preexisting virtual yaw are composed once before a body action");
        Vec3 unused;heading.worldGaze.Select(accepted,unused);heading.sourceState=33;
        const auto saved=heading;const auto native=pose((initialYaw+orbit)*rad,{999,-999,999});
        for(const Vec3 currentAnchor: {Vec3{42,81,-19},Vec3{-25,17,72},Vec3{54,116,-88}}) {
            Check(FirstPersonBase(heading,11,22,true,native,currentAnchor,base,1)&&Close(base.position,currentAnchor)&&
                  Close(base.basis.forward,start.basis.forward),"climb/fall/pickup use complete current XYZ anatomy while native camera orbits90/180 degrees");
            CameraPose during;Check(Compose(base,relative,{}, {},100,during)&&Close(during.basis.forward,accepted.basis.forward),
                  "native third-person rotation never adds another turn to the user's already accepted VR gaze");
        }
        const Quat roll{0,0,std::sin(89.f*rad*.5f),std::cos(89.f*rad*.5f)};
        const Basis tilted{Rotate(roll,native.basis.forward),Rotate(roll,native.basis.up),Rotate(roll,native.basis.right)};
        Check(FirstPersonBase(heading,11,22,true,native,{61,44,-82},base,1,true,&tilted)&&
              Close(base.position,{61,44,-82})&&Close(base.basis.up,tilted.up)&&
              heading.valid&&Close(heading.basis.forward,saved.basis.forward)&&Close(heading.worldGaze.forward,saved.worldGaze.forward),
              "tilted native action keeps its current head but cannot erase the separate upright heading or accepted world-gaze history");
        HeadingRetentionContext context{11,22,33,1,1020,1000,true,false,true,true,true};
        Check(RetainPhysicalHeading(heading,context),"fresh owned physical anchor gap may retain only yaw history");
        RetireFirstPersonHeading(heading,RetainPhysicalHeading(heading,context));
        Check(heading.valid&&heading.sourceState==33&&Close(heading.basis.forward,saved.basis.forward),
              "closing a transient unavailable anatomical eye does not recapture the third-person base");
        CameraPose after;
        Check(FirstPersonBase(heading,11,22,true,native,{2,172,3},base,1)&&Compose(base,relative,{}, {},100,after)&&
              Close(after.basis.forward,accepted.basis.forward)&&Close(base.position,{2,172,3}),
              "leaving the action preserves VR world direction despite native orbit and existing virtual turn");
        for(unsigned fault=0;fault<10;++fault) {
            auto bad=context;
            switch(fault){case 0:++bad.player;break;case 1:++bad.camera;break;case 2:++bad.sourceState;break;case 3:++bad.recenterAck;break;
            case 4:bad.physical=false;break;case 5:bad.uiOrCinema=true;break;case 6:bad.ready=false;break;case 7:bad.trackingValid=false;break;
            case 8:bad.mounted=false;break;default:bad.now=1251;break;}
            auto retired=heading;RetireFirstPersonHeading(retired,RetainPhysicalHeading(retired,bad));
            Check(!retired.valid&&!retired.sourceState&&!retired.worldGaze.valid,
                  "UI/cinema/player/source/recenter/tracking retirement discards yaw instead of retaining a stale camera capability");
        }
        // Recenter during tilt consumes the last accepted UPRIGHT gaze only
        // after returning; action/seat axes must not overwrite that sample.
        heading=saved;Check(FirstPersonBase(heading,11,22,true,native,{62,42,-81},base,2,true,&tilted)&&heading.recenterAck==1,
              "tilt keeps pre-action recenter history until upright tracking resumes");
        Check(FirstPersonBase(heading,11,22,true,native,{2,170,4},base,2)&&Compose(base,{}, {}, {},100,after)&&
              Close(after.basis.forward,accepted.basis.forward),"recenter in a native action restores accepted world gaze without reapplying old virtual yaw");
    }
}
}
void TestAuthoredActorHeading() {
    const auto pose=[](float yaw,Vec3 at=Vec3{}){return CameraPose{{{std::sin(yaw),0,std::cos(yaw)},{0,1,0},{std::cos(yaw),0,-std::sin(yaw)}},at};};
    constexpr float rad=.01745329252f;
    FirstPersonHeading state;CameraPose base;
    const auto entry=pose(0,{0,170,0});
    Check(FirstPersonBase(state,11,22,true,entry,entry.position,base,7),"authored turn starts with ordinary accepted stable camera");
    AuthoredHeadingInput sample{33,1,pose(40*rad).basis.forward};
    Check(FirstPersonBase(state,11,22,true,pose(-120*rad),{1,170,2},base,7,false,nullptr,&sample)&&Close(base.basis.forward,entry.basis.forward),
          "MiniAct entry captures actor root reference without snapping to its facing or external camera orbit");
    for(unsigned pair=2;pair<12;++pair){sample.pairId=pair;sample.actorForward=pose((40+(pair%2?.1f:-.1f))*rad).basis.forward;
        Check(FirstPersonBase(state,11,22,true,pose(float(pair)),{float(pair),170,2},base,7,false,nullptr,&sample)&&Close(base.basis.forward,entry.basis.forward),
              "small root jitter and arbitrary neck/native camera turns cannot move the stable authored camera");}
    for(unsigned step=1;step<=9;++step){sample.pairId=20+step;sample.actorForward=pose((40+step*10)*rad).basis.forward;
        Check(FirstPersonBase(state,11,22,true,pose(-float(step)),{float(step),165,4},base,7,false,nullptr,&sample),"deliberate actor root turn follows through its upright authored sequence");}
    Check(Close(base.basis.forward,pose(89.8f*rad).basis.forward,.0003f),"authored90-degree turn reaches retained heading with only bounded0.2-degree jitter play");
    const auto turned=base;sample.actorForward=pose(170*rad).basis.forward;
    Check(FirstPersonBase(state,11,22,true,pose(0),{9,164,4},base,7,false,nullptr,&sample)&&Close(base.basis.forward,turned.basis.forward),
          "same stereo pair cannot consume a second root turn while its current anatomical position still updates");
    const Quat physical{0,std::sin(15*rad),0,std::cos(15*rad)};Quat relative;Vec3 displacement;CameraPose eye;
    Check(StereoRenderMath::ApplyVirtualYaw(physical,{},35*rad,relative,displacement)&&Compose(base,relative,{}, {},100,eye)&&
          Close(eye.basis.forward,pose(94.8f*rad).basis.forward,.0003f),"authored base composes signed physical30 and virtual35 degrees exactly once");
    Vec3 gaze;state.worldGaze.Select(eye,gaze);
    Check(FirstPersonBase(state,11,22,true,pose(-160*rad),{10,170,6},base,7)&&!state.authored.valid&&Close(base.basis.forward,turned.basis.forward),
          "normal or skipped authored exit retains the final gameplay heading and discards only its script root reference");
    Check(Compose(base,relative,{}, {},100,eye)&&Close(eye.basis.forward,pose(94.8f*rad).basis.forward,.0003f),"exit cannot apply native third-person yaw or existing HMD turn again");
    sample={44,40,pose(-170*rad).basis.forward};
    Check(FirstPersonBase(state,11,22,true,pose(100*rad),{11,170,6},base,7,false,nullptr,&sample)&&Close(base.basis.forward,turned.basis.forward),
          "replacement MiniAct starts a fresh root reference without inheriting another action's turn");
    sample.pairId=41;sample.actorForward=pose(170*rad).basis.forward;
    Check(FirstPersonBase(state,11,22,true,pose(100*rad),{12,170,6},base,7,false,nullptr,&sample)&&
          Close(base.basis.forward,pose(70*rad).basis.forward,.0003f),"root heading wraps through180 degrees along the continuous short signed turn");
    sample.pairId=42;sample.actorForward={0,1,0};const auto stable=base;
    Check(FirstPersonBase(state,11,22,true,entry,{13,170,6},base,7,false,nullptr,&sample)&&!state.authored.valid&&Close(base.basis.forward,stable.basis.forward),
          "undefined non-upright root retires only its reference instead of catching up from a noisy or missing axis");
    sample={44,43,pose(0).basis.forward};state.worldGaze.Select(eye,gaze);
    Check(FirstPersonBase(state,11,22,true,entry,{14,170,6},base,8,false,nullptr,&sample)&&
          Close(base.basis.forward,gaze)&&state.authored.valid,"recenter rebases to accepted world gaze and reseeds actor reference without double root yaw");
}
void TestRappelExitHeading() {
    constexpr float rad=.01745329252f;
    const CameraPose identity{{{0,0,1},{0,1,0},{1,0,0}},{10,170,20}};
    for(int actor=-180;actor<=180;actor+=30)for(int tracking=-170;tracking<=170;tracking+=34) {
        FirstPersonHeading state;CameraPose base,eye;
        Check(FirstPersonBase(state,1,2,true,identity,identity.position,base),"exit scenario starts with owned stable heading");
        const Basis upward{{0,1,0},{0,0,-1},{1,0,0}};
        Check(FirstPersonBase(state,1,2,true,identity,{10,40,20},base,0,true,&upward)&&state.traversalHeadingPending,
              "anatomical traversal marks its later upright heading for synchronization");
        Check(FirstPersonBase(state,1,2,true,identity,identity.position,base),"first upright exit retains current eye position");
        StereoRenderMath::Mat4 delta;
        const Quat turn{0,std::sin(tracking*rad*.5f),0,std::cos(tracking*rad*.5f)};
        const Vec3 root{std::sin(actor*rad),0,std::cos(actor*rad)};
        Check(StereoRenderMath::CameraViewDelta(turn,{},{},0,100,delta)&&ResumeActorHeading(state,root,&delta[0][0],base)&&
              ApplyPose(base,&delta[0][0],eye)&&Close(eye.basis.forward,Scale(root,-1.f))&&!state.traversalHeadingPending,
              "rappel exit faces current actor face even with old virtual or physical tracking yaw");
        const auto once=base.basis.forward;
        Check(ResumeActorHeading(state,Scale(root,-1),&delta[0][0],base)&&Close(base.basis.forward,once),
              "second eye and ordinary gameplay cannot repeat the exit alignment");
    }
    FirstPersonHeading invalid;invalid.valid=invalid.traversalHeadingPending=true;CameraPose base=identity;
    StereoRenderMath::Mat4 delta;StereoRenderMath::Identity(delta);
    Check(!ResumeActorHeading(invalid,{0,1,0},&delta[0][0],base)&&invalid.traversalHeadingPending&&Close(base.basis.forward,identity.basis.forward),
          "undefined actor face cannot consume traversal synchronization or mutate camera");
    FirstPersonHeading loadedOnRope;const Basis upward{{0,1,0},{0,0,-1},{1,0,0}};
    Check(FirstPersonBase(loadedOnRope,5,6,true,identity,identity.position,base,0,true,&upward)&&
          !loadedOnRope.valid&&loadedOnRope.traversalHeadingPending&&
          FirstPersonBase(loadedOnRope,5,6,true,identity,identity.position,base)&&
          ResumeActorHeading(loadedOnRope,{0,0,1},&delta[0][0],base)&&Close(base.basis.forward,{0,0,-1}),
          "loading directly onto the rope still synchronizes its first upright exit to the current actor");
    for(float sign:{-1.f,1.f}) {
        auto vertical=invalid;vertical.basis=identity.basis;CameraPose eye;
        const Quat pitch{sign*std::sqrt(.5f),0,0,std::sqrt(.5f)};
        Check(StereoRenderMath::CameraViewDelta(pitch,{},{},0,100,delta)&&ResumeActorHeading(vertical,{0,0,1},&delta[0][0],base)&&
              ApplyPose(base,&delta[0][0],eye)&&std::fabs(eye.basis.forward.y)>.999f,
              "a vertical physical gaze at rope exit remains valid and preserves the tracked head pitch");
    }
}
int main() {
    TestGpuViewConvention();
    TestYawOnlyBase();
    TestFirstPersonStability();
    TestGuideMovementAndVisibility();
    TestVerticalGazeMovementAndRecenter();
    TestCinematicCameraCoherence();
    TestRappelCameraTilt();
    TestPhysicalActionHeadingHistory();
    TestFreeCinematicViewDelta();
    TestAuthoredActorHeading();
    TestRappelExitHeading();
    TestCinematicVisibilityCurrentPath();
    const float fov[4]={1.1f,.9f,1.2f,.8f},culling[4]={1.5f,1.3f,1.4f,1.2f};
    const Space original=Initial();
    Check(ValidatePose({original.basis,original.position}),"observed native basis convention");
    for(int yaw=-80;yaw<=80;yaw+=10)for(int pitch=-60;pitch<=60;pitch+=10) {
        const float y=yaw*.00872664626f,p=pitch*.00872664626f;
        const Quat q=Multiply({0,std::sin(y),0,std::cos(y)},{std::sin(p),0,0,std::cos(p)});
        const Vec3 displacement{.12f,.04f,-.07f},eyeOffset{-.032f,0,0};
        StereoRenderMath::Mat4 delta;
        Check(StereoRenderMath::CameraViewDelta(q,displacement,eyeOffset,1.5f,100.f,delta),"view delta constructed");
        Space result=original;
        Check(Apply(result,&delta[0][0],fov,culling,5.f),"native delta accepted");
        CameraPose expected;
        CameraPose dollyBase{original.basis,Add(original.position,Scale(original.basis.forward,150.f))};
        Check(Compose(dollyBase,q,displacement,eyeOffset,100.f,expected),"reference camera composition");
        Check(Close(result.position,expected.position),"native translation matches Compose with single IPD");
        Check(Close(result.basis.forward,expected.basis.forward)&&Close(result.basis.up,expected.basis.up)&&Close(result.basis.right,expected.basis.right),"native rotation matches Compose");
        const CameraPose parent{original.basis,original.position};
        CameraPose local;
        Check(WorldToLocal(expected,parent,local),"tracked world eye can be stored under a camera geometry parent");
        const auto point=[&](Vec3 value){return CameraVector(parent.basis,value.x,value.y,value.z);};
        const CameraPose recovered{{point(local.basis.forward),point(local.basis.up),point(local.basis.right)},
                                  Add(parent.position,point(local.position))};
        Check(Close(recovered.position,expected.position)&&Close(recovered.basis.forward,expected.basis.forward)&&
              Close(recovered.basis.up,expected.basis.up)&&Close(recovered.basis.right,expected.basis.right),
              "direct GetWorldTransform consumers see the same eye as the cached render entry");
        Check(Close(result.positionCache,result.position),"native visibility position cache follows eye");
        Check(result.planes[0][3]==5&&result.planes[1][3]==-40000,"near updated and far retained");
        Check(std::memcmp(result.planes[6],original.planes[6],sizeof(result.planes[6]))==0,"optional portal plane retained");
        Check(result.horizontalFov==original.horizontalFov&&result.alternateFov==original.alternateFov&&
              result.perspective==original.perspective&&result.scaleX==original.scaleX&&
              result.aspect==original.aspect&&result.projectionScale==original.projectionScale,
              "conservative culling retains every native projection/LOD scalar byte-exact");
        float expectedPlanes[12][4]{};std::memcpy(expectedPlanes,original.planes,sizeof(expectedPlanes));
        Check(ApplyClipFov(&expectedPlanes[0][0],culling)&&
              !std::memcmp(result.planes[2],expectedPlanes[2],4*sizeof(expectedPlanes[0])),
              "CPU side planes still enclose the complete headset union");
    }
    Space invalid=original;
    StereoRenderMath::Mat4 delta;StereoRenderMath::Identity(delta);delta[0][0]=0;
    Check(!Apply(invalid,&delta[0][0],fov,culling,5.f)&&std::memcmp(&invalid,&original,sizeof(invalid))==0,"singular delta leaves native state untouched");
    invalid.orthographic=1;Space before=invalid;StereoRenderMath::Identity(delta);
    Check(!Apply(invalid,&delta[0][0],fov,culling,5.f)&&std::memcmp(&invalid,&before,sizeof(invalid))==0,"orthographic camera untouched");
    {
        const CameraPose native{};const Vec3 anchor{15,80,-25};
        for(float pole:{-1.57079633f,1.57079633f}) {
            Vec3 previous;bool havePrevious=false;
            for(int step=-20;step<=20;++step) {
                const float angle=pole+step*.0001f;const Quat rotation{0,0,std::sin(angle*.5f),std::cos(angle*.5f)};
                const Basis animated{Rotate(rotation,native.basis.forward),Rotate(rotation,native.basis.up),Rotate(rotation,native.basis.right)};
                FirstPersonHeading state;CameraPose result;
                Check(FirstPersonBase(state,11,22,true,native,anchor,result,0,true,&animated)&&
                      ValidatePose(result)&&Close(result.basis.up,animated.up)&&Close(result.basis.forward,animated.forward)&&
                      Close(result.position,anchor)&&!state.valid,
                      "rappel keeps the full actual animated frame across both near-parallel native-heading poles");
                if(havePrevious)Check(Dot(previous,result.basis.forward)>.999f,"horizontal rappel cannot flip its view by 180 degrees across the heading pole");
                previous=result.basis.forward;havePrevious=true;
            }
        }
        CameraPose unchanged=native;Basis bad;bad.up={};
        Check(!AnimatedActorCamera(native,bad,unchanged)&&Close(unchanged.basis.up,native.basis.up),
              "missing animated body axes cannot silently change the camera");
    }
    {
        const float headsetFov[4]={1.1f,.9f,1.05f,1.f};
        float optic[16]{},left[16]{},right[16]{},regular[16]{};
        const float zoom[4]={.12f,.12f,.0675f,.0675f};
        Check(ApplyEyeProjection(optic,zoom,3.f,40000.f,false),"native sniper zoom fixture has a real perspective matrix");
        Check(SelectCameraProjection(true,optic,headsetFov,5.f,40000.f,false,left)&&
              SelectCameraProjection(true,optic,headsetFov,5.f,40000.f,false,right)&&
              !std::memcmp(left,optic,sizeof(optic))&&!std::memcmp(right,optic,sizeof(optic)),
              "both mono optic eyes preserve every original zoom/aspect/depth bit independently of the headset projection");
        Check(SelectCameraProjection(false,optic,headsetFov,5.f,40000.f,false,regular)&&
              std::fabs(regular[0]-optic[0])>1.f,"ordinary first-person frames retain headset projection instead of sniper zoom");
        float untouched[16];for(auto& value:untouched)value=17.f;optic[15]=1.f;
        Check(!SelectCameraProjection(true,optic,headsetFov,5.f,40000.f,false,untouched)&&untouched[0]==17.f,
              "an orthographic or corrupt optic packet cannot authorize a tracked scope image");
    }
    {
        CameraPose eye,view;Check(ViewFromWorld(eye,view),"projection-only decoder replay has a valid actual native view");
        float native[16]{},tracked[16]{},current[16]{},rows[16]{};
        const float cinema[4]={.32f,.32f,.18f,.18f},headset[4]={1.1f,.9f,1.2f,1.f};
        Check(ApplyEyeProjection(native,cinema,5,40000,true)&&ApplyEyeProjection(tracked,headset,5,40000,true),
              "authored 16:9 lens and asymmetric VR projection are distinct before kind2 restore");
        std::memcpy(current,native,sizeof(current));
        Check(RestoreQueuedProjection(true,view,view,native,tracked,current)&&!std::memcmp(current,tracked,sizeof(current)),
              "exact projection-only native restore in the same tracked cinematic view returns to the paired eye projection");
        std::memcpy(current,native,sizeof(current));auto foreign=view;foreign.position.x+=10;
        Check(!RestoreQueuedProjection(true,foreign,view,native,tracked,current)&&!std::memcmp(current,native,sizeof(current)),
              "another camera or UI view cannot inherit an old tracked projection");
        Check(!RestoreQueuedProjection(false,view,view,native,tracked,current),"ordinary native gameplay never enters this cinema-only recovery");
        current[0]+=1;Check(!RestoreQueuedProjection(true,view,view,native,tracked,current),"unknown alternate projections remain entirely native");
        for(unsigned i=0;i<16;++i)rows[i]=native[(i%4)*4+i/4];
        Check(RestoreProjectionRows(0,rows,4,native,tracked),"native transposed c0 lens restored after queued original projection");
        bool same=true;for(unsigned i=0;i<16;++i)same=same&&rows[i]==tracked[(i%4)*4+i/4];
        Check(same,"eye shear/aspect/depth survive native row transpose exactly");
        std::memcpy(rows,native,sizeof(rows));
        Check(!RestoreProjectionRows(1,rows,4,native,tracked)&&!RestoreProjectionRows(0,rows,3,native,tracked),
              "unrelated registers and short constant writes are never reinterpreted as lens matrices");
        rows[7]=3;Check(!RestoreProjectionRows(0,rows,4,native,tracked),"world-view or UI combinations cannot be overwritten by the cinematic lens repair");
        for(unsigned reg:{24u,69u}){
            std::memcpy(rows,native,sizeof(rows));rows[0]*=2;rows[5]*=.5f;
            Check(RestoreProjectionRows(reg,rows,4,native,tracked)&&!std::memcmp(rows,tracked,sizeof(rows)),"authored aspect-scaled world projection restores SDK FOV at its actual shader register");
        }
    }
    std::printf("K&L native camera math: %u checks, %u failures.\n",checks,failures);
    return failures?1:0;
}
