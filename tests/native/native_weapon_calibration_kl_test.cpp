#include "native_weapon_calibration_kl.hpp"
#include <cstdio>
#include <limits>

using namespace NativeCameraMath;
using namespace NativeWeaponCalibrationKL;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
CameraPose Pose(float yaw,Vec3 point={}){
    const Quat q{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};CameraPose out;
    out.basis={Rotate(q,out.basis.forward),Rotate(q,out.basis.up),Rotate(q,out.basis.right)};
    out.position=point;return out;
}
}
int main(){
    const Key key{7,0x10000,0x20000,0x30000,123,5,1};
    CameraPose item=Pose(.2f,{40,20,-80});
    const auto wrist=[&](const CameraPose& gun,float offset=0.f,float yaw=0.f){
        const auto local=Pose(yaw,{offset,.1f,.3f});
        return NativeRigMath::ToWorld(gun,{local.basis,local.position});
    };
    SettledGrip gate;
    Check(!gate.Select(true,key,100,wrist(item),item,100),"first pair cannot capture");
    for(unsigned eye=0;eye<5;++eye)Check(!gate.Select(true,key,100,wrist(item),item,100),"same pair / repeated eye cannot raise stability");
    Check(!gate.Select(true,key,101,wrist(item,.2f),item,100),"second stable pair cannot capture");
    Check(gate.Select(true,key,102,wrist(item,.4f),item,100),"third stable pair can capture");
    Check(gate.Select(true,key,102,wrist(item,.4f),item,100),"repeat of the proved third pair remains stable");
    auto moved=Pose(.9f,{-110,250,29});
    Check(gate.Select(true,key,103,wrist(moved,.4f),moved,100),"shared world movement does not falsify local grip stability");
    Check(!gate.Select(false,key,104,wrist(moved),moved,100),"context/action ineligibility retires candidate");
    Check(!gate.Select(true,key,105,wrist(item),item,100)&&!gate.Select(true,key,106,wrist(item),item,100)&&
          gate.Select(true,key,107,wrist(item),item,100),"three new pairs are required after ineligible context");
    auto other=key;++other.item;
    Check(!gate.Select(true,other,108,wrist(item),item,100),"different weapon clears candidate");
    Check(!gate.Select(true,other,109,wrist(item,2.f),item,100),"position jump above 1.5 cm restarts stability");
    Check(!gate.Select(true,other,110,wrist(item,2.f,.08f),item,100),"rotation jump above three degrees restarts stability");
    Check(!gate.Select(true,other,111,wrist(item,2.f,.08f),item,100)&&
          gate.Select(true,other,112,wrist(item,2.f,.08f),item,100),"candidate becomes eligible only after new stable pairs");
    auto recentered=other;++recentered.recenter;
    Check(!gate.Select(true,recentered,113,wrist(item),item,100),"recenter retires candidate");
    auto otherRoot=recentered;otherRoot.root=0x40000;
    Check(!Same(otherRoot,recentered),"changed geometry root cannot inherit a settled binding");
    ++recentered.epoch;Check(!gate.Select(true,recentered,114,wrist(item),item,100),"new transition epoch retires candidate");
    Check(!gate.Select(true,recentered,113,wrist(item),item,100),"out-of-order pair restarts rather than reusing a future sample");
    Check(!gate.Select(true,recentered,115,wrist(item),item,std::numeric_limits<float>::quiet_NaN()),"invalid scene scale clears candidate");
    auto invalid=wrist(item);invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    Check(!gate.Select(true,recentered,116,invalid,item,100),"invalid wrist clears candidate");
    Check(!gate.Select(true,recentered,117,wrist(item),item,100)&&!gate.Select(true,recentered,118,wrist(item),item,100)&&
          gate.Select(true,recentered,119,wrist(item),item,100),"recovery after invalid samples requires three new pairs");
    SettledGrip rounded;auto approximate=wrist(item);
    approximate.basis.forward=Scale(approximate.basis.forward,.995f);
    approximate.basis.up=Scale(approximate.basis.up,.995f);
    approximate.basis.right=Scale(approximate.basis.right,.995f);
    Check(!rounded.Select(true,key,200,approximate,item,100)&&!rounded.Select(true,key,201,approximate,item,100)&&
          rounded.Select(true,key,202,approximate,item,100),"valid native matrices with mild basis length error are compared by angle, not scale");
    std::printf("KL1 grip calibration settling: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
