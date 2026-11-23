#include "hands_math.hpp"
#include "native_hand_geometry.hpp"
#include "vr_ipc.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace NativeCameraMath;
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<1e-5f;}
bool Near(Vec3 a,Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
HandsIPC::Pose Pose(Vec3 position) {
    HandsIPC::Pose pose{};pose.orientation[3]=1;
    pose.position[0]=position.x;pose.position[1]=position.y;pose.position[2]=position.z;
    pose.flags=HandsIPC::kOrientation|HandsIPC::kPosition;return pose;
}
}
int main() {
    const float head[4]={0,0,0,1},position[3]={0,1.7f,0},leftEye[3]={-.032f,0,0},rightEye[3]={.032f,0,0};
    auto wrist=Pose({-.2f,1.3f,-.5f});Vec3 left{},right{},finger{};
    Check(HandsMath::EyePoint(wrist,0,{},head,position,leftEye,left),"left wrist projects");
    Check(HandsMath::EyePoint(wrist,0,{},head,position,rightEye,right),"right eye projects");
    Check(Near(left,{-.168f,-.4f,.5f})&&Near(right,{-.232f,-.4f,.5f}),"SDK eye offset is applied once with correct stereo disparity");
    Check(HandsMath::EyePoint(wrist,0,{.1f,0,0},head,position,leftEye,finger)&&Near(finger,Add(left,{0,0,.1f})),"fingers point along controller minus Z");
    Vec3 thumbLeft{},thumbRight{};
    Check(HandsMath::EyePoint(wrist,0,{0,-.05f,0},head,position,leftEye,thumbLeft)&&
          HandsMath::EyePoint(wrist,1,{0,.05f,0},head,position,leftEye,thumbRight)&&Near(thumbLeft,thumbRight)&&
          Near(thumbLeft.y,left.y+.05f),"mirrored thumbs both point toward controller up");
    const float s=std::sqrt(.5f),yaw[4]={0,s,0,s};
    const Quat rotation{0,s,0,s};const Vec3 translated{10,2,-7};
    const Vec3 headWorld=Add(translated,{0,1.7f,0});
    const Vec3 wristWorld=Add(headWorld,Rotate(rotation,{-.2f,-.4f,-.5f}));
    auto rotated=Pose(wristWorld);for(unsigned i=0;i<4;++i)rotated.orientation[i]=yaw[i];
    const float rotatedHeadPosition[3]={headWorld.x,headWorld.y,headWorld.z};
    Vec3 afterRotation{};
    Check(HandsMath::EyePoint(rotated,0,{.1f,0,0},yaw,rotatedHeadPosition,leftEye,afterRotation)&&Near(afterRotation,finger),
          "tracking origin translation and shared head/wrist yaw preserve physical hand placement");
    auto invalid=wrist;invalid.flags=HandsIPC::kOrientation;
    Check(!HandsMath::ValidPose(invalid),"orientation-only wrist remains invalid for the strict hand role");
    Check(!HandsMath::EyePoint(invalid,0,{},head,position,leftEye,left),"orientation-only wrist never creates a floating positional hand");
    auto estimatedHead=Pose({0,1.7f,0});estimatedHead.flags=HandsIPC::kOrientation;
    Check(HandsMath::ValidHeadReference(estimatedHead),"finite orientation-only head reference does not disable tracked wrists");
    estimatedHead.position[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!HandsMath::ValidHeadReference(estimatedHead),"nonfinite estimated head position is rejected");
    invalid=wrist;invalid.orientation[3]=0;
    Check(!HandsMath::EyePoint(invalid,0,{},head,position,leftEye,left),"zero quaternion rejected");
    invalid=wrist;invalid.position[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!HandsMath::EyePoint(invalid,0,{},head,position,leftEye,left),"NaN wrist rejected");
    Check(!HandsMath::EyePoint(wrist,2,{},head,position,leftEye,left),"unknown side rejected");
    NativeHandGeometry::Shape open{},closed{},mirrored{};
    Check(NativeHandGeometry::Fallback(0,0,0,open)&&NativeHandGeometry::Fallback(0,1,1,closed)&&
          NativeHandGeometry::Fallback(1,0,0,mirrored),"both procedural anatomical hands build");
    Check(!Near(open.finger[2].joint[3],closed.finger[2].joint[3]),"grip visibly bends the finger geometry");
    Check(Near(open.finger[0].joint[1].y,-mirrored.finger[0].joint[1].y),"left and right anatomy mirror thumbs");

    const DWORD pid=GetCurrentProcessId(),now=GetTickCount();
    HandsIPC::Producer producer,duplicate;HandsIPC::Consumer consumer;
    Check(producer.Open(pid)&&consumer.Open(pid),"PID-scoped producer and read-only consumer connect");
    Check(!duplicate.Open(pid),"a second writer cannot reset the active channel");
    HandsIPC::Frame output{};output.tick=777;
    Check(!consumer.Read(output,now)&&output.tick==777,"unpublished channel is rejected without altering output");
    HandsIPC::Frame frame{};frame.tick=now;frame.head=Pose({0,1.7f,0});frame.hands[0]=wrist;
    frame.present=frame.mounted=1;frame.sensorTime=12.5;frame.grip[0]=.75f;
    frame.inputValid[0]=HandsIPC::kGripInput|HandsIPC::kTriggerInput;
    frame.recenterAck=7;frame.head.flags=HandsIPC::kOrientation;
    Check(HandsMath::ValidFrame(frame,now,7),"position-tracked wrists remain usable with an estimated positional head reference");
    Check(!HandsMath::ValidFrame(frame,now,8),"a controller sample from another recenter cannot steer this camera");
    auto nextFrame=frame;nextFrame.tick=now+1;nextFrame.grip[0]=.1f;nextFrame.recenterAck=8;
    auto paired=HandsMath::PairedFrame(frame,true,nextFrame,true,now+1,7);
    Check(paired.tick==frame.tick&&Near(paired.grip[0],frame.grip[0]),
          "host recenter between eyes retains the first eye sample in the camera's origin");
    paired=HandsMath::PairedFrame(frame,true,{},false,now+1,7);
    Check(paired.tick==frame.tick,"a failed IPC read between eyes does not discard the paired wrists");
    paired=HandsMath::PairedFrame({},true,frame,true,now,7);
    Check(!paired.tick,"a pair beginning without hand tracking cannot grow different hands in its second eye");
    paired=HandsMath::PairedFrame(frame,false,nextFrame,true,now+1,8);
    Check(paired.tick==nextFrame.tick&&paired.recenterAck==8,"next camera origin consumes the matching new controller sample");
    Check(!HandsMath::PairedFrame(frame,false,nextFrame,true,now+1,7).tick,
          "a new pair rejects a controller origin newer than its camera");
    Check(!HandsMath::PairedFrame(frame,true,nextFrame,true,now+HandsIPC::kFreshMs+1,7).tick,
          "a stale pair never extends the lifetime of positional tracking");
    auto unmounted=frame;unmounted.mounted=0;
    Check(!HandsMath::PairedFrame({},false,unmounted,true,now,7).tick,"unmounted headset cannot authorize new arm IK");
    producer.Publish(frame);
    Check(consumer.Read(output,now)&&output.tick==now&&output.sensorTime==12.5&&Near(output.grip[0],.75f),"seqlock round-trip preserves full pose timestamp and analog input");
    Check(output.inputValid[0]==3&&output.inputValid[1]==0,"grip/trigger availability is independent from tracked pose and zero analog value");
    auto unavailable=frame;unavailable.grip[0]=0;unavailable.inputValid[0]=0;
    producer.Publish(unavailable);
    Check(consumer.Read(output,now)&&output.hands[0].flags==wrist.flags&&output.inputValid[0]==0,
          "inactive analog action can retain a tracked hand without claiming physical release");
    unavailable.inputValid[0]=8;producer.Publish(unavailable);
    Check(!consumer.Read(output,now),"unsupported input-availability bits reject a malformed hands packet");
    producer.Publish(frame);
    Check(!consumer.Read(output,now+HandsIPC::kFreshMs+1),"stale frame is rejected");
    Check(!consumer.Read(output,now-1),"future/wrapped negative-age frame is rejected");
    wchar_t name[96];HandsIPC::Name(name,96,pid);
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    Check(mapping!=nullptr,"fixture opens mapping");
    auto* shared=static_cast<HandsIPC::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(HandsIPC::Shared)));
    Check(shared!=nullptr,"fixture maps channel");
    const LONG sequence=shared->sequence;InterlockedExchange(&shared->sequence,sequence|1);
    Check(!consumer.Read(output,now),"in-progress write cannot be read");
    InterlockedExchange(&shared->sequence,sequence);
    shared->version=HandsIPC::kVersion+1;
    Check(!consumer.Read(output,now),"mismatched protocol rejected");shared->version=HandsIPC::kVersion;
    shared->gamePid=pid+1;Check(!consumer.Read(output,now),"wrong game PID rejected");shared->gamePid=pid;
    UnmapViewOfFile(shared);CloseHandle(mapping);
    producer.Close();Check(!consumer.Read(output,now),"producer shutdown immediately invalidates retained consumer mapping");
    Check(producer.Open(pid),"same game can restart its host while consumer retains mapping");
    producer.Publish(frame);Check(consumer.Read(output,now),"restarted host resumes publishing");
    VRIPC::SharedFrames intent{};intent.magic=VRIPC::kMagic;intent.version=VRIPC::kVersion;
    intent.bytes=sizeof(intent);intent.gamePid=pid;intent.captureEnabled=intent.hostReady=1;
    intent.inputContext=VRIPC::kInputGame;
    VRIPC::PublishHudIntent(intent,true,now,true,0,true);
    Check(VRIPC::ReadTeamHudIntent(intent,now)&&VRIPC::ReadHudIntent(intent,now)&&
          VRIPC::ReadSecondaryFireIntent(intent,now).pressed&&VRIPC::ReadPickupIntent(intent,now).hand==0,
          "team panel shares one fresh intent snapshot without losing HUD, secondary fire or pickup hand");
    Check(!VRIPC::ReadTeamHudIntent(intent,now+101)&&!VRIPC::ReadTeamHudIntent(intent,now-1),
          "stale and future team panel intents are rejected");
    for(LONG context:{VRIPC::kInputMenu,VRIPC::kInputSuspended,VRIPC::kInputInventory,VRIPC::kInputMap,VRIPC::kInputCinematic,VRIPC::kInputLoading}) {
        intent.inputContext=context;
        Check(!VRIPC::ReadTeamHudIntent(intent,now),"team panel intent never survives a native UI or cinematic context");
    }
    intent.inputContext=VRIPC::kInputGame;
    const auto intentSequence=intent.hudSequence;intent.hudSequence|=1;
    Check(!VRIPC::ReadTeamHudIntent(intent,now),"an unfinished host intent write cannot open the team panel");
    intent.hudSequence=intentSequence;intent.hudFlags|=4096;
    Check(!VRIPC::ReadTeamHudIntent(intent,now),"unknown intent bits are not interpreted as a valid team panel");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,false,false,true);
    Check(VRIPC::ReadGrenadeVisualIntent(intent,now)&&!VRIPC::ReadGrenadeVisualIntent(intent,now+101),"grenade visual intent is explicit and expires with its actual host snapshot");
    intent.inputContext=VRIPC::kInputMenu;
    Check(!VRIPC::ReadGrenadeVisualIntent(intent,now),"grenade firearm hiding cannot survive a native menu transition");
    intent.inputContext=VRIPC::kInputGame;VRIPC::PublishHudIntent(intent,false,now,false,2,true,false,false,true);
    Check(!VRIPC::ReadGrenadeVisualIntent(intent,now),"team selection cannot be reinterpreted as a hidden weapon gesture");
    VRIPC::PublishHudIntent(intent,false,now,false,1,true);
    Check(VRIPC::ReadTeamHudIntent(intent,now)&&VRIPC::ReadPickupIntent(intent,now).hand==1,
          "right-hand pickup and team panel flags remain independent");
    intent.hudFlags|=4;
    Check(!VRIPC::ReadTeamHudIntent(intent,now),"contradictory pickup flags invalidate the whole input snapshot");
    VRIPC::PublishHudIntent(intent,true,now);
    Check(!VRIPC::ReadTeamHudIntent(intent,now)&&VRIPC::ReadHudIntent(intent,now),
          "legacy HUD publication safely clears team intent when grip is released");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,true);
    Check(VRIPC::ReadWeaponMenuIntent(intent,now)&&!VRIPC::ReadTeamHudIntent(intent,now),"weapon menu intent is separate from team selection");
    Check(!VRIPC::ReadWeaponMenuIntent(intent,now+101),"weapon menu expires after host loss");
    intent.inputContext=VRIPC::kInputMenu;
    Check(!VRIPC::ReadWeaponMenuIntent(intent,now),"native menu transition clears weapon menu help");
    intent.inputContext=VRIPC::kInputGame;
    VRIPC::PublishHudIntent(intent,false,now);
    Check(!VRIPC::ReadWeaponMenuIntent(intent,now),"release clears weapon menu intent");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,false,true);
    Check(VRIPC::ReadCoverIntent(intent,now),"fresh filtered B hold crosses the existing intent seqlock");
    Check(!VRIPC::ReadCoverIntent(intent,now+101),"cover entry hold expires after a lost host");
    intent.inputContext=VRIPC::kInputMenu;
    Check(!VRIPC::ReadCoverIntent(intent,now),"UI transition retires cover entry intent");
    intent.inputContext=VRIPC::kInputGame;
    VRIPC::PublishHudIntent(intent,false,now,false,2,true,false,true);
    Check(!VRIPC::ReadCoverIntent(intent,now),"team B order cannot enter native cover");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,true,true);
    Check(!VRIPC::ReadCoverIntent(intent,now),"inventory consumes cover B while held");
    VRIPC::PublishHudIntent(intent,false,now);
    Check(!VRIPC::ReadCoverIntent(intent,now),"B release clears entry permission");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,false,false,false,true);
    Check(VRIPC::ReadMountedStickIntent(intent,now)&&!VRIPC::ReadMountedStickIntent(intent,now+101),"mounted stick priority is explicit and expires with its host snapshot");
    intent.inputContext=VRIPC::kInputMenu;
    Check(!VRIPC::ReadMountedStickIntent(intent,now),"mounted stick cannot override controller aim from a native menu");
    intent.inputContext=VRIPC::kInputGame;
    VRIPC::PublishHudIntent(intent,false,now,false,2,true,false,false,false,true);
    Check(!VRIPC::ReadMountedStickIntent(intent,now),"team navigation is not native MG steering");
    VRIPC::PublishHudIntent(intent,false,now);
    Check(!VRIPC::ReadMountedStickIntent(intent,now),"stick release clears native MG steering priority");
    VRIPC::PublishHudIntent(intent,false,now+700,false,2,false,false,false,false,false,now);
    Check(VRIPC::ReadSecondaryFireIntent(intent,now+700).reloadTick==now,"manual A intent survives a slow game update after the native R key pulse");
    VRIPC::PublishHudIntent(intent,false,now+2001,false,2,false,false,false,false,false,now);
    Check(!VRIPC::ReadSecondaryFireIntent(intent,now+2001).reloadTick,"manual reload intent has a bounded lifetime");
    VRIPC::PublishHudIntent(intent,false,now,false,2,true,false,false,false,false,now);
    Check(!VRIPC::ReadSecondaryFireIntent(intent,now).reloadTick,"team recall A cannot authorize a weapon reload");
    VRIPC::PublishHudIntent(intent,false,now,false,2,false,false,false,false,false,0,true);
    Check(VRIPC::ReadClimbIntent(intent,now)&&!VRIPC::ReadCoverIntent(intent,now),"B climbing does not request sticky cover");
    VRIPC::PublishHudIntent(intent,true,now,false,2,true,true,false,false,false,0,true,now);
    Check(!VRIPC::ReadClimbIntent(intent,now)&&VRIPC::ReadTeamNextIntent(intent,now)==now,"the open HUD owns Grip and cannot climb");
    Check(!VRIPC::ReadTeamNextIntent(intent,now+101),"stale host intent cannot replay Grip");
    VRIPC::PublishHudIntent(intent,true,now+301,false,2,true,true,false,false,false,0,false,now);
    Check(!VRIPC::ReadTeamNextIntent(intent,now+301),"a recently published panel cannot replay an old Grip");
    std::printf("6DOF hands: %u math, anatomy and IPC checks passed.\n",checks);return 0;
}
