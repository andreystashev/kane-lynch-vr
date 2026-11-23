#include "rappel_look_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace RappelLookMath;
namespace {
unsigned checks=0;
void Check(bool okay,const char* reason){++checks;if(!okay){std::printf("FAIL: %s\n",reason);std::exit(1);}}
Quat Yaw(float degrees){const float a=degrees*Pi/360.f;return {0,-std::sin(a),0,std::cos(a)};}
float Heading(Quat q){const auto v=Rotate(q,{0,0,-1});return std::atan2(v.x,-v.z);}
bool Near(Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.001f;}
bool Near(Basis a,Basis b){return Near(a.forward,b.forward)&&Near(a.up,b.up)&&Near(a.right,b.right);}
void AttachmentTests() {
    CameraPose root;root.basis={{0,0,1},{0,1,0},{1,0,0}};root.position={200,500,-700};
    const CameraPose eye{root.basis,{200,650,-680}};
    Attachment attached;CameraPose selected;bool basis=false;
    Check(attached.Select(root,eye.position,&eye.basis,1,2,3,0,selected,basis)&&basis&&Near(selected.position,eye.position),"rappel attachment captures actual anatomical eye");
    const auto local=NativeRigMath::ToModel(root,eye);
    for(int angle=-180;angle<=180;++angle) {
        CameraPose currentRoot=root;const auto turn=Yaw(float(angle));
        currentRoot.basis={Rotate(turn,root.basis.forward),Rotate(turn,root.basis.up),Rotate(turn,root.basis.right)};
        currentRoot.position=Add(root.position,{float(angle)*.5f,-float(angle)*2.f,float(angle)*.25f});
        const auto expected=NativeRigMath::ToWorld(currentRoot,{local.basis,local.position});
        auto bob=expected;bob.position=Add(bob.position,{std::sin(float(angle))*.8f,1.5f,-.7f});
        const auto wiggle=Yaw(float(angle)*.35f);
        bob.basis={Rotate(wiggle,bob.basis.forward),Rotate(wiggle,bob.basis.up),Rotate(wiggle,bob.basis.right)};
        Check(attached.Select(currentRoot,bob.position,&bob.basis,1,2,3,0,selected,basis,&expected),"valid native body descent and yaw accepted");
        Check(Near(selected.position,expected.position)&&Near(selected.basis,eye.basis),"current anatomical position follows real actor motion while native actor/neck yaw cannot rotate the world reference");
    }
    for(int key=0;key<4;++key) {
        auto changed=attached;auto newEye=eye;newEye.position.x+=25;
        Check(changed.Select(root,newEye.position,&newEye.basis,key==0?11:1,key==1?22:2,key==2?33:3,key==3?1:0,selected,basis)&&Near(selected.position,newEye.position),"new actor camera action or recenter captures its own attachment");
    }
    Attachment late;Check(late.Select(root,eye.position,nullptr,1,2,3,0,selected,basis)&&!basis,"CPU eye anchor does not fabricate an animated orientation");
    auto lateEye=eye;lateEye.position.x+=8;
    Check(late.Select(root,lateEye.position,&eye.basis,1,2,3,0,selected,basis)&&basis&&Near(selected.position,lateEye.position),"late GPU eye replaces a CPU entry-animation eye instead of latching the old offset");
    CameraPose untouched=selected;auto badRoot=root;badRoot.position.x=std::numeric_limits<float>::infinity();
    Check(!late.Select(badRoot,eye.position,&eye.basis,1,2,3,0,selected,basis)&&Near(selected.position,untouched.position),"invalid actor root cannot mutate the attached camera");
    late.Reset();Check(!late.valid&&!late.basisValid,"leaving rappel retires its cached anatomical reference");
}
void ParentHeadTests() {
    const Basis identity{{0,0,1},{0,1,0},{1,0,0}};
    const CameraPose root{identity,{300,1200,-900}},bindParent{identity,{0,140,0}},bindHead{identity,{0,155,0}},bindEye{identity,{0,159,-8}};
    for(int pitch=0;pitch<=100;++pitch) {
        const float a=float(pitch)*Pi/360.f;const Quat tilt{std::sin(a),0,0,std::cos(a)};
        NativeRigMath::Bone parent{{Rotate(tilt,identity.forward),Rotate(tilt,identity.up),Rotate(tilt,identity.right)},
                                   {2.f,float(140-pitch)*.5f,float(pitch)}};
        const auto relativeEye=NativeRigMath::Inverse(bindParent.basis,Subtract(bindEye.position,bindParent.position));
        const auto localEye=Add(parent.position,NativeRigMath::Transform(parent.basis,relativeEye));
        CameraPose selected;
        Check(ParentHead(root,bindParent,bindHead,bindEye,parent,100.f,selected),"current head-parent plus bind anatomical eye forms a valid world pose");
        Check(Near(selected.position,Add(root.position,NativeRigMath::Transform(root.basis,localEye))),"standing-to-horizontal transition retains current anatomical eye position and real parent motion");
        const auto expectedForward=NativeRigMath::Transform(root.basis,Rotate(tilt,{0,0,-1}));
        Check(Near(selected.basis.forward,expectedForward),"authored body tilt is followed without independent head-local animation");
        if(pitch==90)Check(Dot(selected.basis.forward,{0,1,0})>.999f,"horizontal rappel looks upward as the character's face does");
        Attachment selectedAttachment;CameraPose result;bool basis=false;
        const CameraPose wrongEntry{identity,Add(selected.position,{35,-40,70})};
        Check(selectedAttachment.Select(root,wrongEntry.position,&wrongEntry.basis,1,2,3,0,result,basis,&selected)&&basis&&Near(result.position,selected.position),"bad entry eye cannot override the proven current parent-based eye");
        auto moved=selected;moved.position=Add(moved.position,{0,-120,0});
        Check(selectedAttachment.Select(root,wrongEntry.position,&wrongEntry.basis,1,2,3,0,result,basis,&moved)&&Near(result.position,moved.position),"same movement/camera identity follows authored descent instead of freezing the first reference");
    }
    NativeRigMath::Bone parent{identity,bindParent.position};CameraPose selected{identity,{4,5,6}},untouched=selected;
    auto badParent=bindParent;badParent.position.x+=100;
    Check(!ParentHead(root,badParent,bindHead,bindEye,parent,100.f,selected)&&Near(selected.position,untouched.position),"unrelated head-parent distance cannot authorize a camera");
    auto badEye=bindEye;badEye.position.z-=100;
    Check(!ParentHead(root,bindParent,bindHead,badEye,parent,100.f,selected)&&Near(selected.position,untouched.position),"invalid anatomical eye offset fails without mutating the camera");
    auto badRoot=root;badRoot.basis.forward={};
    Check(!ParentHead(badRoot,bindParent,bindHead,bindEye,parent,100.f,selected),"corrupt current root cannot authorize parent-head observation");
    Attachment attachment;bool basis=false;CameraPose invalid{{{},{},{}},{}};
    Check(!attachment.Select(root,bindEye.position,nullptr,1,2,3,0,selected,basis,&invalid)&&!attachment.valid,"invalid optional authored eye cannot fabricate an origin or alter identity");
}
void WorldOrientationTests() {
    const Basis identity{{0,0,1},{0,1,0},{1,0,0}};
    CameraPose root{identity,{200,500,-700}},head{identity,{200,660,-700}},selected;
    Attachment attachment;bool basis=false;
    Check(attachment.Select(root,head.position,&head.basis,1,2,3,0,selected,basis,&head,1)&&basis,"rappel starts from the observed anatomical eye and world heading");
    for(int pitch=0;pitch<=90;++pitch) {
        const float a=float(pitch)*Pi/360.f;const Quat tilt{std::sin(a),0,0,std::cos(a)},yaw=Yaw(float(pitch)*.2f);
        const auto rotation=Multiply(yaw,tilt);
        head.basis={Rotate(rotation,identity.forward),Rotate(rotation,identity.up),Rotate(rotation,identity.right)};
        // The native model faces -Z. Turn its face upward while the current
        // parent/actor supplies an independent authored horizontal yaw.
        head.basis.forward=Scale(head.basis.forward,-1.f);head.basis.right=Scale(head.basis.right,-1.f);
        head.position={200.f+float(pitch)*.1f,660.f-float(pitch)*2.f,-700.f+float(pitch)*.5f};
        const auto turn=Yaw(float(pitch)*.4f);
        root.basis={Rotate(turn,identity.forward),Rotate(turn,identity.up),Rotate(turn,identity.right)};
        Check(attachment.Select(root,head.position,&head.basis,1,2,3,0,selected,basis,&head,pitch==90?2:1)&&Near(selected.position,head.position),"entrance keeps the accepted current parent-head position through body turn and descent");
        Check(Near(selected.basis,head.basis),"proven entrance follows the complete current body turn and tilt before settlement without retaining the old azimuth");
    }
    const auto locked=selected.basis;
    Check(Dot(locked.forward,{0,1,0})>.9999f,"authored horizontal body reaches a fixed upward-looking base");
    Check(attachment.orientationLocked&&attachment.phaseKnown,"the first anatomically horizontal torso locks the complete world reference");
    for(int i=-180;i<=180;++i) {
        const float a=float(i%29)*Pi/360.f;
        const auto wobble=Multiply(Yaw(float(i)),Quat{std::sin(a),0,0,std::cos(a)});
        head.basis={Rotate(wobble,locked.forward),Rotate(wobble,locked.up),Rotate(wobble,locked.right)};
        head.position={float(i),500.f-float(i),-700.f+float(i)*.3f};
        const auto turn=Yaw(float(i)*.6f);root.basis={Rotate(turn,identity.forward),Rotate(turn,identity.up),Rotate(turn,identity.right)};
        Check(attachment.Select(root,head.position,&head.basis,1,2,3,0,selected,basis,&head,2)&&Near(selected.position,head.position)&&Near(selected.basis,locked),"stationary HMD reference cannot drift with any actor/neck yaw or idle nod after the upward pose");
    }
    Check(attachment.Select(root,head.position,&head.basis,1,2,3,7,selected,basis,&head)&&Near(selected.basis,locked),"recenter cannot kick the fixed world reference toward the current animated neck yaw");
    const auto before=selected;const auto state=attachment;
    Check(!attachment.Select(root,head.position,&head.basis,0,2,3,7,selected,basis,&head)&&Near(selected.position,before.position)&&Near(attachment.referenceRight,state.referenceRight),"invalid ownership cannot mutate the eye or its locked orientation");
    const CameraPose nextHead{identity,head.position};
    Check(attachment.Select(root,nextHead.position,&nextHead.basis,1,2,4,7,selected,basis,&nextHead)&&Near(selected.basis,identity),"new movement identity begins a fresh world reference instead of retaining a previous descent");
    Attachment partial;CameraPose seventyFive=nextHead;const float a=75.f*Pi/360.f;
    const Quat up{std::sin(a),0,0,std::cos(a)};
    seventyFive.basis={Rotate(up,{0,0,-1}),Rotate(up,identity.up),Rotate(up,{-1,0,0})};
    Check(partial.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,2)&&partial.orientationLocked,"a proven horizontal torso may establish its actual75degree authored gaze without inventing90degrees");
    const auto settled=selected.basis;
    auto higher=seventyFive;const float b=89.f*Pi/360.f;const Quat higherPitch{std::sin(b),0,0,std::cos(b)};
    higher.basis={Rotate(higherPitch,{0,0,-1}),Rotate(higherPitch,identity.up),Rotate(higherPitch,{-1,0,0})};
    Check(partial.Select(root,higher.position,&higher.basis,1,2,3,0,selected,basis,&higher,2)&&Near(selected.basis,settled),"later higher neck/parent pitch cannot creep a settled world camera upward");
    Check(partial.Select(root,higher.position,&higher.basis,1,2,3,1,selected,basis,&higher,1)&&Near(selected.basis,settled),"recenter and a later standing-like torso sample cannot unlock a settled descent");
    NativeRigMath::Bone pelvis{identity,{0,90,0}},neck{identity,{0,140,0}};
    Check(BodyPhase(root,pelvis,neck,100.f)==1,"native vertical pelvis-to-neck geometry proves an entrance pose");
    neck.position={0,90,100};Check(BodyPhase(root,pelvis,neck,100.f)==2,"native horizontal pelvis-to-neck geometry independently proves the settled body pose");
    neck.basis=higher.basis;Check(BodyPhase(root,pelvis,neck,100.f)==2,"neck yaw or gaze basis cannot change the torso phase derived from anatomical positions");
    neck.position=pelvis.position;Check(BodyPhase(root,pelvis,neck,100.f)==0,"missing torso extent grants no entrance tilt authority");
    Attachment latePhase;CameraPose nativeStanding{{{0,0,-1},{0,1,0},{-1,0,0}},head.position};
    Check(latePhase.Select(root,nativeStanding.position,&nativeStanding.basis,1,2,3,0,selected,basis,&nativeStanding,0)&&latePhase.orientationLocked&&!latePhase.phaseKnown,"unknown torso data holds the current native standing basis without granting ongoing animation drift");
    const auto unknownBasis=selected.basis;
    Check(latePhase.Select(root,higher.position,&higher.basis,1,2,3,0,selected,basis,&higher,0)&&Near(selected.basis,unknownBasis),"additional unknown poses cannot slowly tilt a stationary reference");
    Check(latePhase.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,1)&&latePhase.phaseKnown&&!latePhase.orientationLocked&&selected.basis.forward.y>.9f,"the first proven entrance after CPU fallback can advance from standing toward the anatomical upward view");
    Check(latePhase.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,2)&&latePhase.orientationLocked,"late proven horizontal torso finalizes the authored upward reference");
    const auto lateLocked=selected.basis;
    Check(latePhase.Select(root,higher.position,&higher.basis,1,2,3,0,selected,basis,&higher,0)&&Near(selected.basis,lateLocked),"unknown pelvis samples after horizontal settlement never unlock its view");
    Attachment unknownHigh;
    Check(unknownHigh.Select(root,higher.position,&higher.basis,1,2,3,0,selected,basis,&higher,0),"unknown high-pitch fallback begins without entrance authority");
    Check(unknownHigh.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,2)&&
          std::fabs(selected.basis.forward.y-seventyFive.basis.forward.y)<.0001f,"first proven horizontal pose replaces unknown fallback pitch rather than inheriting its higher animated angle");
    // The historical upward reference can have its screen top toward the
    // feet. Anatomical settlement corrects roll without changing forward.
    Attachment screen;const Vec3 towardCity{0,0,-1};
    Check(screen.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,2,&towardCity)&&screen.anatomicalUpLocked,"horizontal native anatomy establishes an explicit screen-top reference");
    const auto corrected=selected;
    Check(Dot(selected.basis.up,towardCity)>.96f&&Dot(Scale(selected.basis.up,-1.f),Scale(towardCity,-1.f))>.96f,"screen TOP points toward the neck/city and screen BOTTOM toward the pelvis/feet");
    Check(std::fabs(selected.basis.forward.y-seventyFive.basis.forward.y)<.0001f&&Near(selected.position,seventyFive.position),"anatomical roll correction preserves the accepted view direction and eye origin");
    for(int i=-180;i<=180;++i) {
        const auto turn=Yaw(float(i));const Vec3 movingTorso=Rotate(turn,towardCity);
        root.basis={Rotate(turn,identity.forward),Rotate(turn,identity.up),Rotate(turn,identity.right)};
        higher.position.y-=.1f;
        Check(screen.Select(root,higher.position,&higher.basis,1,2,3,i%3,selected,basis,&higher,2,&movingTorso)&&
              Near(selected.basis,corrected.basis)&&Near(selected.position,higher.position),"anatomical WORLD screen top stays locked through actor/neck yaw, descent and recenter");
    }
    Vec3 towardHead{};neck.position={0,90,-100};root.basis=identity;
    Check(BodyPhase(root,pelvis,neck,100.f,&towardHead)==2&&Near(towardHead,towardCity),"native torso phase publishes its unit WORLD pelvis-to-neck direction");
    Basis untouchedBasis=corrected.basis;
    Check(!AnatomicalScreen(corrected.basis,corrected.basis.forward,untouchedBasis)&&Near(untouchedBasis,corrected.basis),"degenerate anatomical top never invents a roll or mutates output");
    // The native entrance can turn ninety degrees before settling. The old
    // reference kept the entrance azimuth and only corrected its roll.
    Attachment entranceTurn;CameraPose wrongFacing{{{1,0,0},{0,1,0},{0,0,-1}},head.position};
    Check(entranceTurn.Select(root,wrongFacing.position,&wrongFacing.basis,1,2,3,0,selected,basis,&wrongFacing,1),"different entrance wall heading accepted provisionally");
    const Vec3 towardNeck{0,0,1};
    Check(entranceTurn.Select(root,seventyFive.position,&seventyFive.basis,1,2,3,0,selected,basis,&seventyFive,2,&towardNeck)&&
          Near(selected.basis.forward,seventyFive.basis.forward),"settlement captures CURRENT anatomical forward instead of retaining the sideways entrance reference");
    const auto actualSettled=selected.basis;
    Check(entranceTurn.Select(root,higher.position,&higher.basis,1,2,3,1,selected,basis,&higher,2,&towardNeck)&&Near(selected.basis,actualSettled),"corrected complete settled reference stays locked through later neck pose and recenter");
    pelvis.position={0,90,0};neck.position={0,90,100};
    NativeRigMath::Bone left{identity,{20,90,50}},right{identity,{-20,90,50}};Basis torso;
    Check(TorsoScreen(root,pelvis,neck,left,right,100,torso)&&Near(torso.forward,{0,1,0})&&Near(torso.up,towardNeck),"horizontal native torso plane looks straight upward with screen top toward the head");
    neck.basis=higher.basis;left.basis=right.basis=higher.basis;
    Check(TorsoScreen(root,pelvis,neck,left,right,100,torso)&&Near(torso.forward,{0,1,0}),"animated neck/shoulder gaze bases cannot alter the positional torso-plane reference");
    right.position=left.position;Check(!TorsoScreen(root,pelvis,neck,left,right,100,torso),"collapsed shoulders cannot invent a face-up body basis");
    right.position={-20,90,50};neck.position={0,140,0};Check(!TorsoScreen(root,pelvis,neck,left,right,100,torso),"standing entrance cannot prematurely publish a settled torso-plane basis");
}
}
int main(){
    AttachmentTests();
    ParentHeadTests();
    WorldOrientationTests();
    Look look;Quat selected;
    Check(look.Select(Yaw(163),1,2,3,0,selected)&&std::fabs(Heading(selected))<1e-5f,"entry cancels the current physical heading, independently of accumulated virtual turn");
    look.Reset();Check(look.Select({},1,2,3,0,selected),"neutral rappel starts");
    for(int angle=1;angle<=720;++angle){Check(look.Select(Yaw(float(angle)),1,2,3,0,selected),"full headset turn accepted as tracking");Check(std::fabs(Heading(selected)-std::min(float(angle)*Pi/180.f,YawLimit))<1e-4f,"bounded view cannot wrap or reverse at 180/360 degrees");}
    for(int angle=719;angle>=0;--angle){Check(look.Select(Yaw(float(angle)),1,2,3,0,selected),"return rotation accepted");Check(std::fabs(Heading(selected)-std::min(float(angle)*Pi/180.f,YawLimit))<2e-4f,"return unwinds toward the neutral human head range");}
    for(int angle=-1;angle>=-360;--angle){Check(look.Select(Yaw(float(angle)),1,2,3,0,selected),"opposite rotation accepted");Check(std::fabs(Heading(selected)-std::max(float(angle)*Pi/180.f,-YawLimit))<2e-4f,"left bound is stable across the negative heading pole");}
    for(auto changed:{1,2,3,4}){Look next=look;const auto p=changed==1?11u:1u,c=changed==2?22u:2u,m=changed==3?33u:3u;Check(next.Select(Yaw(-140),p,c,m,changed==4?1:0,selected)&&std::fabs(Heading(selected))<1e-5f,"new player/camera/action/recenter establishes a fresh neutral view");}
    Look pitch;Check(pitch.Select({},1,2,3,0,selected),"pitch origin");
    for(int angle=-89;angle<=89;++angle){const float a=float(angle)*Pi/360.f;Check(pitch.Select({std::sin(a),0,0,std::cos(a)},1,2,3,0,selected),"physical pitch accepted");const auto f=Rotate(selected,{0,0,-1});Check(std::fabs(std::asin(std::clamp(f.y,-1.f,1.f)))<=PitchLimit+1e-5f,"look up and down stays inside the human range");}
    Quat untouched{1,2,3,4};Check(!pitch.Select({0,0,0,0},1,2,3,0,untouched)&&untouched.x==1&&untouched.w==4,"invalid tracking does not fabricate a view");
    std::printf("Rappel bounded physical look: %u checks PASS.\n",checks);return 0;
}
