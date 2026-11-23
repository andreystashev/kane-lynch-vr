#include "native_aim_target_policy.hpp"
#include <cstdio>
#include <limits>
int main(){
    using namespace NativeCameraMath;using namespace NativeAimTargetPolicy;
    unsigned checks=0,failures=0;
    const auto check=[&](bool okay,const char* text){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",text);}};
    const auto near=[](Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<1.e-5f;};
    NativeWeaponKL::AimFrame pose;pose.player=1;pose.weapon=2;pose.root=3;pose.model=4;
    pose.hand=1;pose.pairId=10;pose.tick=1000;pose.recenterAck=7;pose.unitsPerMetre=100;
    pose.origin={10,150,20};pose.direction={0,0,1};
    NativeAimKL::Endpoint endpoint;endpoint.valid=true;endpoint.pose=pose;endpoint.tick=1000;endpoint.point={10,150,220};
    PairTarget policy;Selection selected;
    check(policy.Select(pose,&endpoint,1000,selected)&&selected.hit&&near(selected.target,endpoint.point),"first eye selects its real strict collision contact");
    auto other=endpoint;other.point.z=900;
    check(policy.Select(pose,&other,1016,selected)&&selected.hit&&near(selected.target,endpoint.point),"second eye/native overlay pass cannot change an already selected contact");
    check(policy.Select(pose,nullptr,1051,selected)&&selected.hit&&near(selected.target,endpoint.point),"same immutable pair retains its selected surface contact for the second eye");
    check(policy.Select(pose,&other,1052,selected)&&near(selected.target,endpoint.point),"late native query cannot replace half a stereo pair");
    pose.pairId++;pose.tick=1060;other.pose=pose;other.tick=1060;
    check(policy.Select(pose,&other,1060,selected)&&selected.hit&&near(selected.target,other.point),"next pair can select a new proven contact");
    policy.Reset();
    check(policy.Select(pose,nullptr,1060,selected)&&!selected.hit&&near(selected.target,{10,150,2020}),"no-contact pair retains the visible directional20m beam");
    check(policy.Select(pose,&other,1076,selected)&&!selected.hit&&near(selected.target,{10,150,2020}),"query arriving between eyes cannot create a second/far synthetic reticle");
    for(unsigned field=0;field<10;++field){
        PairTarget owned;owned.Select(pose,&other,1060,selected);auto changed=pose;
        switch(field){case 0:++changed.player;break;case 1:++changed.weapon;break;case 2:++changed.root;break;
            case 3:++changed.model;break;case 4:changed.hand=0;break;case 5:++changed.recenterAck;break;
            case 6:changed.origin.x+=.1f;break;case 7:changed.direction.x=.001f;break;
            case 8:++changed.tick;break;default:++changed.unitsPerMetre;break;}
        const bool motion=field>=6&&field<=8;
        check(owned.Select(changed,&other,1076,selected)==motion,"same-pair carrier motion retains surface contact while ownership/origin epoch changes retire it");
    }
    auto invalid=pose;invalid.origin.x=std::numeric_limits<float>::quiet_NaN();
    check(!policy.Select(invalid,nullptr,1076,selected),"nonfinite muzzle cannot create even a fallback beam");
    check(!policy.Select(pose,nullptr,1211,selected),"stale canonical source cannot keep a beam indefinitely");
    policy.Reset();auto expired=other;expired.tick=800;expired.pose.tick=800;
    check(policy.Select(pose,&expired,1060,selected)&&!selected.hit,"expired old endpoint never supplies guessed distance or contact dot");
    check(policy.Snapshot().resolved>=4&&policy.Snapshot().reused>=3,"pair decision reuse and retirement are diagnostic");
    auto moved=pose;moved.origin.x+=2.f;moved.direction.x=.003f;
    check(NativeAimKLMath::MatchVisual(other.pose,other.tick,other.point,moved,1076)&&
        !NativeAimKLMath::Match(other.pose,other.tick,other.point,moved,1076),"recent true surface contact tolerates bounded render latency without relaxing Fire/command ray proof");
    moved.direction.x=.1f;check(NativeAimKLMath::MatchVisual(other.pose,other.tick,other.point,moved,1076),"ordinary moving aim retains the recent actual surface point instead of blinking");
    moved.direction.x=.3f;check(!NativeAimKLMath::MatchVisual(other.pose,other.tick,other.point,moved,1076),"a large aim jump retires the previous visual surface");
    std::printf("KL1 pair-consistent aim target: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
