#include "native_passenger_roomscale_math.hpp"
#include "native_camera_kl_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks=0;
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}}
bool Close(NativeCameraMath::Vec3 a,NativeCameraMath::Vec3 b){const auto delta=NativeCameraMath::Subtract(a,b);return NativeCameraMath::Dot(delta,delta)<.000001f;}
}
int main() {
    using namespace NativePassengerRoomscale;
    State physical;Sample sample;sample.key={1,2,3,4};sample.pair=1;sample.tick=1000;sample.tracked=true;sample.headPosition={3,1.7f,5};
    const Quat yaw{0,std::sin(.785398163f),0,std::cos(.785398163f)},pitch{std::sin(.35f),0,0,std::cos(.35f)},roll{0,0,std::sin(.25f),std::cos(.25f)};
    sample.trackingOrigin=Multiply(yaw,Multiply(pitch,roll));Vec3 result;
    Check(physical.Select(sample,result)&&Close(result,{}),"entry origin retains accepted zero-offset passenger seat");
    sample.headPosition.y-=.5f;sample.tick+=16;++sample.pair;
    Check(physical.Select(sample,result)&&Close(result,{0,-.5f,0}),"entry yaw/pitch/roll cannot turn physical vertical crouch into horizontal motion");
    sample.headPosition={4,1.7f,5};sample.tick+=16;++sample.pair;
    Check(physical.Select(sample,result)&&Close(result,{0,0,1}),"physical translation is yaw-relative and can move the whole cargo actor beyond the old55cm seat radius");
    sample.headPosition={3,4,5};sample.tick+=16;++sample.pair;
    Check(physical.Select(sample,result)&&Close(result,{0,.20f,0}),"physical rise is bounded20cm above accepted seat");
    sample.headPosition={3,-1,5};sample.tick+=16;++sample.pair;
    Check(physical.Select(sample,result)&&Close(result,{0,-.65f,0}),"physical crouch is bounded65cm below accepted seat");
    const auto paired=result;sample.headPosition={100,100,100};sample.tick+=10;
    Check(physical.Select(sample,result)&&Close(result,paired),"both eyes share one local offset despite a newer physical publication");
    State navigation;Sample nav;nav.key={11,12,13,14};nav.tracked=true;nav.tick=1000;nav.pair=1;
    nav.intent={true,false,nav.tick,0,1};Check(navigation.Select(nav,result)&&Close(result,{}),"virtual entry does not advance from an unbounded elapsed time");
    nav.tick+=100;++nav.pair;nav.intent.tick=nav.tick;
    Check(navigation.Select(nav,result)&&Close(result,{0,0,-.0375f})&&navigation.lastDt==.05f,
          "forward stick moves within carrier at75cm/s and caps integration at50ms");
    const auto oneStep=result;nav.tick+=30;
    Check(navigation.Select(nav,result)&&Close(result,oneStep),"virtual navigation integrates once per stereo pair");
    nav.tick+=16;++nav.pair;nav.intent.tick=nav.tick-200;
    Check(navigation.Select(nav,result)&&Close(result,oneStep),"stale navigation intent stops movement immediately");
    nav.intent.leftY=0;nav.intent.crouched=true;
    for(unsigned i=0;i<20;++i){nav.tick+=20;++nav.pair;nav.intent.tick=nav.tick;Check(navigation.Select(nav,result),"fresh B crouch transition remains valid");}
    Check(std::fabs(result.y+.45f)<.0001f,"B crouch reaches45cm as pose intent rather than a second actor-root floor displacement");
    nav.headPosition.y=-.5f;nav.tick+=16;++nav.pair;nav.intent.tick=nav.tick;
    Check(navigation.Select(nav,result)&&std::fabs(result.y+.65f)<.0001f,"combined physical and virtual crouch obeys one65cm bound");
    nav.intent.leftX=1;nav.intent.leftY=1;
    for(unsigned i=0;i<80;++i){nav.tick+=50;++nav.pair;nav.intent.tick=nav.tick;Check(navigation.Select(nav,result),"bounded diagonal navigation remains valid");}
    Check(std::fabs(result.x-1.1f)<.0001f&&std::fabs(result.z+2.f)<.0001f,"diagonal travel uses bounded cargo envelope instead of the rejected tiny seat radius");
    const auto beforePause=result;nav.tracked=false;Vec3 sentinel{7,8,9};
    Check(!navigation.Select(nav,sentinel)&&Close(sentinel,{7,8,9}),"lost tracking suspends without fabricating another eye offset");
    nav.tracked=true;nav.headPosition={100,7,-20};nav.tick+=4000;++nav.pair;nav.intent.tick=nav.tick;
    Check(navigation.Select(nav,result)&&Close(result,beforePause)&&navigation.lastDt==0,
          "resume rebases changed tracking origin without teleport or accumulated stick movement");
    ++nav.key.recenter;nav.tick+=16;++nav.pair;nav.intent.tick=nav.tick;
    Check(navigation.Select(nav,result)&&Close(result,beforePause)&&Close(navigation.virtualOffset,beforePause),"recenter rebases tracking without teleporting the whole cargo actor back to its original seat");
    ++nav.key.player;nav.tick+=16;++nav.pair;
    Check(navigation.Select(nav,result)&&Close(result,{}),"replacement player cannot inherit another passenger's displacement");
    nav.intent.leftX=std::numeric_limits<float>::quiet_NaN();nav.tick+=16;++nav.pair;nav.intent.tick=nav.tick;
    Check(navigation.Select(nav,result)&&Close(result,{}),"invalid navigation axes do not move the camera");
    nav.key.player=0;Check(!navigation.Select(nav,result)&&!navigation.valid,"leaving exact passenger ownership retires its local state");
    State looking;nav={};nav.key={21,22,23,24};nav.tracked=true;nav.tick=1000;nav.pair=1;nav.relativeHead=yaw;nav.intent={true,false,1000,0,1};
    Check(looking.Select(nav,result),"head-relative navigation reference initializes");
    nav.tick+=50;++nav.pair;nav.intent.tick=nav.tick;
    Check(looking.Select(nav,result)&&Close(result,{-.0375f,0,0}),"forward navigation follows visible head yaw inside current carrier");
    State vertical;nav.key.player=31;nav.trackingOrigin={std::sin(.785398163f),0,0,std::cos(.785398163f)};nav.relativeHead={};nav.headPosition={};
    Check(vertical.Select(nav,result),"exactly vertical entry gaze does not block the passenger stereo camera");
    nav.headPosition.y=-.4f;nav.tick+=16;++nav.pair;nav.intent={};
    Check(vertical.Select(nav,result)&&Close(result,{0,-.4f,0}),"undefined entry yaw still preserves gravity-coherent physical crouch");
    // The same local displacement follows a current translated/turned carrier
    // through the actual camera pose math; no world-space seat is cached.
    StereoRenderMath::Mat4 delta;Check(StereoRenderMath::CameraViewDelta({},result,{},0,100,delta),"bounded passenger offset builds actual stereo view delta");
    CameraPose carrier;carrier.basis={{1,0,0},{0,1,0},{0,0,-1}};carrier.position={500,1200,-300};CameraPose eye;
    Check(NativeCameraKLMath::ApplyPose(carrier,&delta[0][0],eye)&&Close(eye.position,{500,1160,-300}),"physical crouch follows current carrier world position and gravity");
    State feedback;Sample queued;queued.key={71,72,73,74};queued.tracked=true;queued.tick=1000;queued.pair=1;
    queued.intent={true,false,1000,0,1};Check(feedback.Select(queued,result),"collision-feedback context enters with zero authored baseline");
    queued.tick+=50;++queued.pair;queued.intent.tick=queued.tick;Check(feedback.Select(queued,result),"first unresolved move is predicted");
    const auto delayedPair=queued.pair;
    queued.tick+=50;++queued.pair;queued.intent.tick=queued.tick;Check(feedback.Select(queued,result),"render may predict another pair while simulation catches up");
    Check(feedback.Accept(queued.key,delayedPair,{0,0,-.01f})&&Close(feedback.lastOffset,{0,0,-.0475f}),"late collision acceptance rebases newer pending input without discarding it");
    Check(!feedback.Accept(queued.key,delayedPair,{})&&!feedback.Accept(queued.key,queued.pair+1,{}),"duplicate and future collision certificates cannot alter current navigation");
    auto foreign=queued.key;++foreign.player;Check(!feedback.Accept(foreign,queued.pair,{}),"foreign actor cannot apply collision feedback");
    Check(feedback.Accept(queued.key,queued.pair,{0,-.45f,-.01f})&&Close(feedback.lastOffset,{0,0,-.01f}),"wall stop removes blocked accumulated navigation and leaves pose crouch to its independent channel");
    queued.relativeHead=yaw;queued.intent.leftY=0;queued.tick+=50;++queued.pair;queued.intent.tick=queued.tick;
    Check(feedback.Select(queued,result)&&Close(result,{0,0,-.01f}),"turning after a wall stop does not spring the avatar through the wall");
    Check(feedback.Accept(queued.key,queued.pair,{.02f,0,-.01f},100)&&Close(feedback.lastOffset,{.02f,0,-.01f}),"fresh native revision accepts first checked part of current pair");
    Check(feedback.Accept(queued.key,queued.pair,{.04f,0,-.01f},101)&&Close(feedback.lastOffset,{.04f,0,-.01f}),"second native simulation acceptance in the same pair immediately refines actual position");
    Check(!feedback.Accept(queued.key,queued.pair,{.02f,0,-.01f},100)&&Close(feedback.lastOffset,{.04f,0,-.01f}),"older samepair geometry revision cannot rewind current body navigation");
    std::printf("KL1 passenger roomscale: %u checks PASS.\n",checks);return 0;
}
