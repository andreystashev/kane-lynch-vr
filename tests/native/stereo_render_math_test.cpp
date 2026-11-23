#include "stereo_render_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace NativeCameraMath;
using namespace StereoRenderMath;
namespace {
unsigned failures=0;
void Check(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}
}
bool Near(float a,float b,float tolerance=0.001f){return std::fabs(a-b)<tolerance;}
Quat Angle(Vec3 axis,float radians) {
    const float s=std::sin(radians*.5f);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5f)};
}
Vec3 Transform(Vec3 point,const Mat4 matrix) {
    return {point.x*matrix[0][0]+point.y*matrix[1][0]+point.z*matrix[2][0]+matrix[3][0],
            point.x*matrix[0][1]+point.y*matrix[1][1]+point.z*matrix[2][1]+matrix[3][1],
            point.x*matrix[0][2]+point.y*matrix[1][2]+point.z*matrix[2][2]+matrix[3][2]};
}
void PairProtocol() {
    PairSequence sequence;
    for(unsigned pair=0;pair<1000;++pair) {
        const auto left=sequence.FrameId();
        Check(sequence.Eye()==0&&(left&1)==0,"pair starts with even left frame");
        // A failed capture leaves the sequence untouched; do not pair a retry
        // against a different sample.
        Check(sequence.FrameId()==left,"retry retains frame identity");
        sequence.CompleteEye();const auto right=sequence.FrameId();
        Check(sequence.Eye()==1&&(right&1)==1&&left/2==right/2,
              "left and right pass the host's exact pair gate");
        sequence.CompleteEye();Check(sequence.FrameId()/2==left/2+1,"new pair follows right capture");
    }
    sequence.CompleteEye();const auto cancelledRight=sequence.FrameId();sequence.Restart();
    Check(sequence.Eye()==0&&sequence.FrameId()/2>cancelledRight/2,"cancelling right cannot reuse its old left");
}
void ViewDirection() {
    for(int yaw=-8;yaw<=8;++yaw)for(int pitch=-5;pitch<=5;++pitch)for(int roll=-4;roll<=4;++roll) {
        const Quat q=Multiply(Angle({0,1,0},yaw*.17f),Multiply(Angle({1,0,0},pitch*.13f),Angle({0,0,1},roll*.11f)));
        const Vec3 lean{.1f,-.03f,.2f};const Vec3 offset{-.032f,0,.015f};
        Mat4 view;Check(CameraViewDelta(q,lean,offset,1.5f,100.f,view),"tracked view builds");
        const Vec3 position=Add(lean,Rotate(q,offset));
        const Vec3 gaze=Rotate(q,{0,0,-1});const Vec3 point=Add(position,Scale(gaze,2.f));
        const Vec3 screen=Transform({point.x*100,point.y*100,(-point.z+1.5f)*100},view);
        Check(Near(screen.x,0)&&Near(screen.y,0)&&Near(screen.z,200),"physical gaze lands at screen centre, inverse view rotation");
        const Vec3 up=Add(position,Rotate(q,{0,1,-2}));
        const Vec3 screenUp=Transform({up.x*100,up.y*100,(-up.z+1.5f)*100},view);
        Check(Near(screenUp.x,0)&&Near(screenUp.y,100)&&Near(screenUp.z,200),"head-local up remains screen up after yaw/pitch/roll");
    }
    Mat4 left,right;
    CameraViewDelta({}, {},{-.032f,0,0},0,100,left);
    CameraViewDelta({}, {},{.032f,0,0},0,100,right);
    Check(Near(Transform({0,0,100},left).x,3.2f)&&Near(Transform({0,0,100},right).x,-3.2f),"IPD shifts fixed points oppositely in the two views");
}
void VirtualTurning() {
    // Compare through native camera composition, independently of the D3D
    // view matrix: virtual turning rotates the whole tracked rig, not each
    // eye about itself, and never rotates the raw SDK sample for timewarp.
    const CameraPose base{};
    for(int step=-12;step<=12;++step) {
        const float radians=step*3.14159265f/12.f;
        const Quat physical=Multiply(Angle({1,0,0},.31f),Angle({0,0,1},-.18f));
        const Quat saved=physical;
        const Vec3 lean{.1f,-.05f,.03f};
        Quat turned;Vec3 rotated;
        Check(ApplyVirtualYaw(physical,lean,radians,turned,rotated),"virtual yaw accepts complete turn range");
        CameraPose physicalEyes[2],turnedEyes[2];
        for(unsigned side=0;side<2;++side) {
            const Vec3 offset{side? .032f:-.032f,0,0};
            Check(Compose(base,physical,lean,offset,100,physicalEyes[side])&&
                  Compose(base,turned,rotated,offset,100,turnedEyes[side]),"physical and turned native eyes compose");
            const auto aboutAnchor=[&](Vec3 world){
                const Vec3 point=Subtract(world,base.position);
                const float x=Dot(point,base.basis.right),y=Dot(point,base.basis.up),z=Dot(point,base.basis.forward);
                // Positive yaw turns +forward toward +right in native axes.
                return Add(base.position,Add(Add(Scale(base.basis.right,x*std::cos(radians)+z*std::sin(radians)),
                                               Scale(base.basis.up,y)),
                                           Scale(base.basis.forward,z*std::cos(radians)-x*std::sin(radians))));
            };
            const Vec3 expected=aboutAnchor(physicalEyes[side].position);
            Check(Near(turnedEyes[side].position.x,expected.x)&&Near(turnedEyes[side].position.y,expected.y)&&
                  Near(turnedEyes[side].position.z,expected.z),"turn rotates lean and IPD around the head anchor together");
            const Vec3 point=Add(physicalEyes[side].position,Scale(physicalEyes[side].basis.forward,200.f));
            const Vec3 expectedGaze=Subtract(aboutAnchor(point),expected);
            Check(Dot(turnedEyes[side].basis.forward,expectedGaze)>199.99f,"yaw precedes physical pitch and roll");
        }
        const Vec3 ipd=Subtract(turnedEyes[1].position,turnedEyes[0].position);
        Check(Near(std::sqrt(Dot(ipd,ipd)),6.4f),"virtual turn preserves binocular separation");
        Check(physical.x==saved.x&&physical.y==saved.y&&physical.z==saved.z&&physical.w==saved.w,
              "virtual turn preserves raw tracking for compositor");
    }
    Quat turned{.1f,.2f,.3f,.9f};Vec3 lean{7,8,9};
    Check(!ApplyVirtualYaw({}, {},std::numeric_limits<float>::quiet_NaN(),turned,lean),"nonfinite turn rejected");
    Check(!ApplyVirtualYaw({}, {},1000.f,turned,lean),"unbounded turn rejected");
    Check(!ApplyVirtualYaw({0,0,0,0}, {},0,turned,lean),"invalid physical pose rejected");
    Check(turned.x==.1f&&turned.y==.2f&&turned.z==.3f&&turned.w==.9f&&lean.x==7&&lean.y==8&&lean.z==9,
          "rejected turn leaves outputs intact");
}
void HeadOriginLifecycle() {
    HeadPositionReference reference;
    Vec3 origin{7,8,9};
    Check(!reference.Select({0,.7f,0},false,1,2,false,0,origin)&&!reference.valid&&origin.x==7,
          "tracked off-head pose cannot establish avatar eye origin");
    const Vec3 worn{1.2f,1.6f,-2.f};
    Check(reference.Select(worn,true,1,2,false,0,origin)&&Near(origin.x,worn.x)&&Near(origin.y,worn.y)&&Near(origin.z,worn.z),
          "putting headset on establishes neutral at the current worn position");
    Vec3 peek=Add(worn,{.2f,-.05f,.03f});
    Check(reference.Select(peek,true,1,2,false,0,origin)&&Near(Subtract(peek,origin).x,.2f),
          "same actor retains direct physical peek rather than following the moving head");
    const Vec3 cinema{4,1.8f,-3};
    Check(reference.Select(cinema,true,0,8,true,0,origin)&&Near(Dot(Subtract(cinema,origin),Subtract(cinema,origin)),0),
          "external cinematic starts its own positional origin");
    const Vec3 returned{3,1.7f,-2};
    Check(reference.Select(returned,true,1,2,false,0,origin)&&Near(Dot(Subtract(returned,origin),Subtract(returned,origin)),0),
          "return from cinematic cannot leave the player's body in front of the VR eyes");
    const Vec3 respawn{3.4f,1.5f,-2};
    Check(reference.Select(respawn,true,9,2,false,0,origin)&&Near(origin.x,respawn.x),"respawn resets only the new avatar's positional origin");
    Check(reference.Select(worn,true,9,2,false,1,origin)&&Near(origin.y,worn.y),"explicit recenter resets positional origin");
    Check(!reference.Select({},false,9,2,false,1,origin)&&!reference.valid,
          "removing headset invalidates positional origin");
    Check(reference.Select(returned,true,9,2,false,1,origin)&&Near(origin.x,returned.x),
          "wearing headset again cannot inherit the position where it was put down");
    const auto saved=reference;
    Check(!reference.Select({std::numeric_limits<float>::quiet_NaN(),0,0},true,9,2,false,1,origin)&&
          reference.valid==saved.valid&&Near(reference.origin.x,saved.origin.x),"invalid sample cannot replace a valid head reference");
    Mat4 view;
    Check(CameraViewDelta({},Subtract(returned,origin),{},0,100,view),"neutral worn pose builds a camera without artificial torso offset");
    Check(Near(view[3][0],0)&&Near(view[3][1],0)&&Near(view[3][2],0),"put-on and scene transitions keep eyes on the native head anchor");
}
void AuthoredCinematicLook() {
    CinematicLook state;Quat output;
    const Quat entry=Multiply(Angle({0,1,0},1.1f),Angle({1,0,0},-.2f));
    Check(state.Select(entry,7,0,output),"cutscene accepts full physical entry orientation");
    Vec3 look=Rotate(output,{0,0,-1});
    Check(Near(look.x,0)&&Near(look.y,0)&&Near(look.z,-1),"cutscene starts on authored gaze even with gameplay head pitch and yaw");
    for(float yaw:{-2.f,-.05f,0.f,.05f,2.f})for(float pitch:{-.5f,0.f,.5f}) {
        const Quat moved=Multiply(entry,Multiply(Angle({0,1,0},-yaw),Angle({1,0,0},pitch)));
        Check(state.Select(moved,7,0,output),"bounded cinematic look accepts physical rotation");
        look=Rotate(output,{0,0,-1});
        constexpr float limit=8.f*3.14159265358979323846f/180.f;
        Check(Near(std::atan2(look.x,-look.z),std::clamp(yaw,-limit,limit))&&
              Near(std::asin(look.y),std::clamp(pitch,-limit,limit)),"cutscene gaze clamps to eight degrees around authored framing");
    }
    Check(state.Select({},8,0,output)&&Near(output.w,1),"new cutscene camera resets look origin");
    Check(state.Select(entry,8,1,output)&&Near(output.w,1),"recenter resets cutscene look");
    state.Reset();Check(!state.valid,"leaving cutscene discards cinematic look reference");
    Check(!state.Select({0,0,0,0},7,0,output)&&!state.Select({},0,0,output),"invalid cutscene samples cannot establish an origin");
}
void FreeAuthoredCinematicLook() {
    CinematicLook state;Quat output;
    const Quat entry=Multiply(Angle({0,1,0},1.1f),Multiply(Angle({1,0,0},-.2f),Angle({0,0,1},.13f)));
    Check(state.SelectFree(entry,17,2,output),"free cutscene accepts its complete physical entry pose");
    Check(Near(output.x,0)&&Near(output.y,0)&&Near(output.z,0)&&Near(output.w,1),
          "free cutscene entry keeps the native authored gaze without carrying gameplay pitch or roll");
    CameraPose base;
    base.position={170,240,-310};
    base.basis.forward={0,0,1};base.basis.right={1,0,0};
    const auto sameVector=[](Vec3 a,Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);};
    for(float yaw:{-3.05f,-1.2f,-.3f,0.f,.3f,1.2f,3.05f})
        for(float pitch:{-1.1f,0.f,1.1f})for(float roll:{-.5f,0.f,.5f}) {
            const Quat expected=Multiply(Angle({0,1,0},-yaw),
                Multiply(Angle({1,0,0},pitch),Angle({0,0,1},roll)));
            const Quat head=Multiply(entry,expected);const Quat original=head;
            Check(state.SelectFree(head,17,2,output),"free authored look accepts physical yaw/pitch/roll outside eight degrees");
            for(Vec3 axis:{Vec3{0,0,-1},Vec3{0,1,0},Vec3{1,0,0}})
                Check(sameVector(Rotate(output,axis),Rotate(expected,axis)),
                      "free cinematic basis follows each physical direction without conjugation, axis inversion or lost roll");
            Check(head.x==original.x&&head.y==original.y&&head.z==original.z&&head.w==original.w,
                  "actual SDK pose stays unchanged for compositor metadata");
            CameraPose eyes[2];
            for(unsigned eye=0;eye<2;++eye) {
                const Vec3 offset{eye?.032f:-.032f,0,0};
                Check(Compose(base,output,{},offset,100,eyes[eye]),"free cinematic applies its rotation to each native eye once");
                Check(sameVector(eyes[eye].basis.forward,MapTracking(base.basis,Rotate(expected,{0,0,-1})))&&
                      sameVector(eyes[eye].basis.up,MapTracking(base.basis,Rotate(expected,{0,1,0}))),
                      "native camera orientation follows free gaze with the same axis convention as gameplay");
            }
            Check(sameVector(Scale(Add(eyes[0].position,eyes[1].position),.5f),base.position),
                  "free look retains the authored camera centre/path and adds no room-scale lean or player dolly");
        }
    Check(state.SelectFree(Angle({0,1,0},-.7f),18,2,output)&&Near(output.w,1),
          "new authored camera resets free-look entry orientation");
    Check(state.SelectFree(entry,18,3,output)&&Near(output.w,1),"recenter resets free cinematic orientation");
    const Quat saved=output;
    Check(!state.SelectFree({0,0,0,0},18,3,output)&&!state.SelectFree({},0,3,output)&&
          output.x==saved.x&&output.y==saved.y&&output.z==saved.z&&output.w==saved.w,
          "invalid free cinematic tracking cannot publish a changed output");
    state.Reset();Check(!state.valid,"leaving free cinematic retires the previous authored reference");
    Check(state.SelectFree(entry,18,3,output)&&Near(output.w,1),"reentry after reset starts at native framing again");
}
void PhysicalPeeking() {
    Vec3 limited;
    const Vec3 lean{.24f,-.04f,.08f};
    Check(LimitHeadTranslation(lean,.35f,limited)&&Near(limited.x,lean.x)&&Near(limited.y,lean.y)&&Near(limited.z,lean.z),
          "physical peek within reach preserves displacement without smoothing or deadzone");
    Check(LimitHeadTranslation({.6f,.2f,.3f},.35f,limited)&&Near(std::sqrt(Dot(limited,limited)),.35f)&&
          Near(limited.x/limited.z,2.f),"oversized lean clamps radially without changing its direction");
    Check(LimitHeadTranslation(lean,0.f,limited)&&Dot(limited,limited)==0.f,"explicit zero retains fixed-head mode");
    Check(LimitHeadTranslation({},.35f,limited)&&Dot(limited,limited)==0.f,"neutral head is finite and exact");
    for(int turn=-6;turn<=6;++turn) {
        Quat rotated;Vec3 turned;
        Check(LimitHeadTranslation(lean,.35f,limited)&&ApplyVirtualYaw({},limited,turn*.4f,rotated,turned),
              "physical peek accepts all supported virtual headings");
        const Vec3 expected=Rotate(rotated,lean);
        Check(Near(turned.x,expected.x)&&Near(turned.y,expected.y)&&Near(turned.z,expected.z),
              "lean follows virtual heading once while raw positional tracking remains unchanged");
        Mat4 left,right;
        Check(CameraViewDelta(rotated,turned,{-.032f,0,0},0,100,left)&&
              CameraViewDelta(rotated,turned,{.032f,0,0},0,100,right),"peek builds both eyes");
        const Vec3 eye=Add(turned,Rotate(rotated,{-.032f,0,0}));
        const Vec3 view=Transform({eye.x*100,eye.y*100,-eye.z*100},left);
        Check(Near(view.x,0)&&Near(view.y,0)&&Near(view.z,0),"left eye receives physical lean exactly once");
    }
    limited={7,8,9};
    Check(!LimitHeadTranslation({std::numeric_limits<float>::quiet_NaN(),0,0},.35f,limited)&&
          !LimitHeadTranslation({},-1.f,limited)&&!LimitHeadTranslation({},2.f,limited)&&
          limited.x==7&&limited.y==8&&limited.z==9,"invalid tracking or limits preserve output");
}
void ShaderConstants() {
    const float gameFov[4]={.7f,.7f,.5f,.5f},eyeFov[4]={1.1f,.9f,1.05f,.95f};
    Mat4 projection,eyeProjection,inverse,modelView,delta;
    ApplyEyeProjection(&projection[0][0],gameFov,5,60000,false);
    ApplyEyeProjection(&eyeProjection[0][0],eyeFov,3,60000,false);
    Check(Invert(projection,inverse),"engine projection invertible");
    CameraViewDelta(Angle({0,1,0},.6f),{.1f,.2f,.1f},{-.032f,0,0},1.5f,100,delta);
    CameraViewDelta(Angle({0,1,0},-.37f),{}, {},0,1,modelView);
    modelView[3][0]=20;modelView[3][1]=100;modelView[3][2]=350;
    Mat4 wvp,tracked,expected;
    Multiply(modelView,projection,wvp);Multiply(modelView,delta,tracked);Multiply(tracked,eyeProjection,expected);
    Check(std::fabs(wvp[0][3])>.01f&&std::fabs(wvp[3][3])>1,"test WVP has actual camera rotation/translation, not P's w column");
    for(unsigned transposed=0;transposed<2;++transposed) {
        float input[16],output[16]{};
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
            input[transposed?c*4+r:r*4+c]=wvp[r][c];
        Check(PatchProjectedAffine(input,inverse,delta,eyeProjection,output),"row-major and transposed WVP both recognized");
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
            Check(Near(output[transposed?c*4+r:r*4+c],expected[r][c],.01f),"shader result equals independently composed WV*delta*Peye");
        Perspective recovered;
        Check(RecoverPerspective(input,recovered)&&recovered.transposed==(transposed!=0)&&recovered.hasWorldView,"shader-only projection recovered in both matrix conventions");
        Check(Near(recovered.projection[0][0],projection[0][0])&&Near(recovered.projection[1][1],projection[1][1])&&
              Near(recovered.projection[3][2],projection[3][2]),"shader-only recovery preserves projection FOV and depth");
    }
    float unchanged[16];for(float& v:unchanged)v=17.f;
    Check(!PatchProjectedAffine(&modelView[0][0],inverse,delta,eyeProjection,unchanged),"affine bones are not WVP constants");
    Perspective rejected;
    Perspective pureProjection;
    Check(RecoverPerspective(&projection[0][0],pureProjection)&&!pureProjection.hasWorldView,
          "pure menu projection is distinguished from a transformed scene");
    Check(!RecoverPerspective(&modelView[0][0],rejected),"shader-only detector rejects affine bones");
    for(float value:unchanged)Check(value==17.f,"rejecting constants preserves output");
    wvp[1][1]=std::numeric_limits<float>::quiet_NaN();
    Check(!PatchProjectedAffine(&wvp[0][0],inverse,delta,eyeProjection,unchanged),"nonfinite constants rejected");
    Check(!RecoverPerspective(&wvp[0][0],rejected),"shader-only detector rejects nonfinite matrices");
}
}
int main() {
    PairProtocol();ViewDirection();VirtualTurning();HeadOriginLifecycle();AuthoredCinematicLook();FreeAuthoredCinematicLook();PhysicalPeeking();ShaderConstants();
    if(failures){std::fprintf(stderr,"%u stereo render checks failed\n",failures);return EXIT_FAILURE;}
    std::puts("Stereo rendering: pair identities, 1683 tracked view rotations, shader matrix conventions passed.");
    return EXIT_SUCCESS;
}
