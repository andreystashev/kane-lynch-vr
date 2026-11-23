// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#include "reload_body_pocket_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
using namespace ReloadBodyPocketKL;
unsigned checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool Near(Vec3 a,Vec3 b,float tolerance=.0002f){const auto d=Subtract(a,b);return Dot(d,d)<=tolerance*tolerance;}
void Advance(Input& input){input.tick+=20;input.sample=input.tick;++input.pairId;}
CameraPose Head(float yaw,float pitch=0,float roll=0){
    CameraPose pose;const Quat y{0,std::sin(-yaw*.5f),0,std::cos(yaw*.5f)};
    auto forward=Rotate(y,{1,0,0}),right=Rotate(y,{0,0,-1}),up=Vec3{0,1,0};
    const Quat p{right.x*std::sin(pitch*.5f),right.y*std::sin(pitch*.5f),right.z*std::sin(pitch*.5f),std::cos(pitch*.5f)};
    forward=Rotate(p,forward);up=Rotate(p,up);const Quat r{forward.x*std::sin(roll*.5f),forward.y*std::sin(roll*.5f),forward.z*std::sin(roll*.5f),std::cos(roll*.5f)};
    pose.basis={forward,Rotate(r,up),Rotate(r,right)};return pose;
}
Input Frame(float units){Input input;input.player=1;input.scene=2;input.reference=3;input.tick=input.sample=1000;input.pairId=4;input.units=units;
    input.head.position={10*units,1.7f*units,-4*units};input.rootAnchor={10*units,0,-4*units};input.rootAnchorValid=true;return input;}
Vec3 Radial(Vec3 centre,float radius,float angle,float units,float height=0){
    return Add(centre,{std::cos(angle)*radius*units,height*units,std::sin(angle)*radius*units});
}
void SameVolume(const Volume& actual,const Volume& expected,const char* message,float tolerance=.0002f){
    Check(Near(actual.pose.position,expected.pose.position,tolerance)&&Near(actual.shoulder,expected.shoulder,tolerance)&&
        Near(actual.pose.basis.forward,expected.pose.basis.forward)&&Near(actual.pose.basis.up,expected.pose.basis.up)&&
        Near(actual.pose.basis.right,expected.pose.basis.right)&&actual.units==expected.units&&actual.floorBounded==expected.floorBounded,message);
}
}
int main(){
    for(float units:{1.f,10.f,100.f,250.f,1000.f}){
        auto input=Frame(units);State state;Volume pocket;
        Check(state.Step(input,pocket)&&pocket.floorBounded,"fresh owned context establishes inventory reach volumes");
        Check(Near(pocket.pose.position,{input.head.position.x,.98f*units,input.head.position.z},.0002f*units),"standing belt centre is directly below the current optical centre");
        Check(Near(pocket.shoulder,{input.head.position.x,1.56f*units,input.head.position.z},.0002f*units),"shoulder source follows comfortable head-relative height");
        const auto original=pocket;
        for(float yaw:{-3.1f,-1.5f,0.f,1.5f,3.1f})for(float pitch:{-1.55f,0.f,1.55f})for(float roll:{-.7f,.7f}){
            Advance(input);const auto position=input.head.position;input.head=Head(yaw,pitch,roll);input.head.position=position;
            Check(state.Step(input,pocket),"all head orientations retain a valid inventory volume");
            SameVolume(pocket,original,"head yaw pitch and roll cannot rotate or translate either reach volume",.0002f*units);
        }
        for(float angle:{0.f,.7f,1.6f,2.8f,4.1f,5.7f}){
            Check(Contains(pocket,Radial(pocket.pose.position,.29f,angle,units)),"belt accepts natural hip reach on every side without guessing torso yaw");
            Check(Contains(pocket,Radial(pocket.pose.position,.11f,angle,units)),"belt inner valid radial edge is reachable");
            Check(Contains(pocket,Radial(pocket.pose.position,.47f,angle,units)),"belt outer valid radial edge is generous");
            Check(!Contains(pocket,Radial(pocket.pose.position,.08f,angle,units)),"hand through torso centre cannot buy ammunition");
            Check(!Contains(pocket,Radial(pocket.pose.position,.50f,angle,units)),"remote hand outside belt cannot buy ammunition");
            Check(Contains(pocket,Radial(pocket.pose.position,.29f,angle,units,.23f)),"belt accepts natural vertical reach");
            Check(!Contains(pocket,Radial(pocket.pose.position,.29f,angle,units,.25f)),"hand above belt height limit is rejected");
            Check(!Contains(pocket,Radial(pocket.pose.position,.47f,angle,units,.23f)),"ellipse rejects diagonal corner outside combined radial and height limits");
            Check(Contains(pocket,Radial(pocket.pose.position,.50f,angle,units),.04f),"bounded extra reach can widen radial tolerance");
            const auto shoulder=Radial(pocket.shoulder,.32f,angle,units);
            Check(ShoulderContains(pocket,shoulder)&&!Contains(pocket,shoulder),"shoulder source is distinct from belt return volume");
            Check(CanSupply(pocket,shoulder,Add(shoulder,{.4f*units,0,0})),"free shoulder reach supplies with weapon safely away");
            Check(!CanSupply(pocket,shoulder,Add(shoulder,{.27f*units,0,0})),"hand close to face-held weapon cannot become a shoulder supply");
            Check(!ShoulderContains(pocket,Radial(pocket.shoulder,.09f,angle,units)),"shoulder source rejects hand passing through the head centre");
            Check(ShoulderContains(pocket,Radial(pocket.shoulder,.55f,angle,units)),"expanded source accepts a hand comfortably outside either ear");
            Check(ShoulderContains(pocket,Radial(pocket.shoulder,.35f,angle,units,.28f)),"expanded source accepts a hand lifted above the former shoulder height");
            Check(!ShoulderContains(pocket,Radial(pocket.shoulder,.61f,angle,units)),"expanded shoulder source remains bounded in radius");
            Check(!ShoulderContains(pocket,Radial(pocket.shoulder,.35f,angle,units,.33f)),"expanded shoulder source remains bounded in height");
        }
        const auto before=pocket;Advance(input);input.rootAnchor.x+=10000*units;input.rootAnchor.z-=20000*units;
        Check(state.Step(input,pocket)&&pocket.floorBounded,"far hidden actor preserves only its plausible floor Y");
        SameVolume(pocket,before,"native root XZ cannot drag inventory away from tracked hands",.0002f*units);
        for(const Vec3 travel: {Vec3{.05f,0,-.07f},Vec3{1.5f,0,.8f},Vec3{-20.f,0,15.f}}){
            const auto previous=pocket;Advance(input);input.head.position=Add(input.head.position,Scale(travel,units));
            Check(state.Step(input,pocket),"physical or virtual locomotion keeps inventory reachable");
            Check(Near(pocket.pose.position,Add(previous.pose.position,Scale(travel,units)),.0002f*units)&&
                Near(pocket.shoulder,Add(previous.shoulder,Scale(travel,units)),.0002f*units),"small and large locomotion follow optical position without lag or historical root blend");
        }
        Advance(input);input.head.position.y=.55f*units;Check(state.Step(input,pocket)&&pocket.floorBounded,"crouched head retains plausible floor bound");
        Check(std::fabs(pocket.pose.position.y-.35f*units)<.0002f*units,"deep crouch keeps belt above validated floor");
        Advance(input);input.head.position.y=2.3f*units;Check(state.Step(input,pocket)&&pocket.floorBounded,"high head position retains plausible floor bound");
        Check(std::fabs(pocket.pose.position.y-1.04f*units)<.0002f*units,"standing height clamp prevents belt rising into chest");
        Advance(input);input.head.position.y=.49f*units;Check(state.Step(input,pocket)&&!pocket.floorBounded,"implausibly close floor cannot constrain inventory");
        Check(std::fabs(pocket.pose.position.y-(.49f-.72f)*units)<.0002f*units,"unqualified floor leaves fresh head-relative fallback");
    }
    {
        auto input=Frame(100);State state;Volume pocket;Check(state.Step(input,pocket),"same-pair checks initialize volume");const auto stable=pocket;
        input.head.position.x+=100;input.rootAnchor.y=20;input.tick+=10;input.sample=input.tick;
        Check(state.Step(input,pocket),"repeated stereo pair remains available");SameVolume(pocket,stable,"same stereo pair cannot remap one hand against two inventory centres");
        Advance(input);Check(state.Step(input,pocket)&&pocket.pose.position.x==input.head.position.x,"new stereo pair consumes translated optical centre");
        for(unsigned reset=0;reset<6;++reset){
            input.head.position.x+=50;
            if(reset==0)++input.reference;
            if(reset==1)++input.player;
            if(reset==2)++input.scene;
            if(reset==3){input.tick+=301;input.sample=input.tick;}
            if(reset==4){input.pairId=1;}
            if(reset==5){input.units=200;}
            Check(state.Step(input,pocket)&&pocket.pose.position.x==input.head.position.x&&pocket.units==input.units,"recenter owner scene gap pair rollback and scale reset discard old same-pair volume");
        }
        state.Reset();Check(!state.valid&&!state.player&&!state.pairId,"explicit reset clears inventory ownership");
        Check(state.Step(input,pocket),"fresh data reconstructs volume after reset");
    }
    {
        const float nan=std::numeric_limits<float>::quiet_NaN();
        for(unsigned kind=0;kind<14;++kind){
            auto input=Frame(100);State state;Volume pocket;Check(state.Step(input,pocket),"invalid-input scenario starts valid");const auto stable=pocket;const auto pair=state.pairId;
            Advance(input);
            switch(kind){
                case 0:input.player=0;break;case 1:input.scene=0;break;case 2:input.reference=UINT32_MAX;break;
                case 3:input.pairId=0;break;case 4:input.sample=0;break;case 5:input.sample=input.tick-151;break;
                case 6:input.sample=input.tick+1;break;case 7:input.units=nan;break;case 8:input.units=.9f;break;
                case 9:input.units=1001;break;case 10:input.head.position.x=nan;break;case 11:input.head.basis.up={};break;
                case 12:input.head.basis.right=input.head.basis.forward;break;case 13:input.head.basis.forward.z=nan;break;
            }
            Check(!state.Step(input,pocket)&&state.pairId==pair,"missing stale future or corrupt tracking cannot authorize or advance inventory");SameVolume(pocket,stable,"failed input leaves caller output untouched");
        }
        auto input=Frame(100);State state;Volume pocket;input.sample=input.tick-150;
        Check(state.Step(input,pocket),"tracking at exact freshness bound remains valid");
        Advance(input);input.rootAnchor={nan,nan,nan};Check(state.Step(input,pocket)&&!pocket.floorBounded,"corrupt optional native root is ignored rather than poisoning optical inventory");
        const auto belt=Radial(pocket.pose.position,.29f,0,100),shoulder=Radial(pocket.shoulder,.32f,0,100);
        Check(CanSupply(pocket,belt,{nan,0,0}),"valid belt reach does not depend on optional gun distance");
        Check(!CanSupply(pocket,shoulder,{nan,0,0}),"shoulder supply requires a valid gun separation");
        Check(!Contains(pocket,{nan,0,0})&&!ShoulderContains(pocket,{0,nan,0}),"invalid hand position cannot enter either source");
        Check(!Contains(pocket,belt,-.01f)&&!Contains(pocket,belt,.081f)&&!Contains(pocket,belt,nan),"reach expansion is finite positive and bounded");
        auto broken=pocket;broken.units=0;Check(!Contains(broken,belt),"corrupt volume scale cannot authorize supply");
        broken=pocket;broken.pose.basis.forward={};Check(!Contains(broken,belt),"corrupt volume frame cannot authorize supply");
    }
    std::printf("VR inventory reach volumes: %u checks passed.\n",checks);return 0;
}
