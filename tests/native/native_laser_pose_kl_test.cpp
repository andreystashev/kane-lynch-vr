#include "native_laser_pose_kl.hpp"
#include <cstdio>
#include <limits>
using namespace NativeCameraMath;
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL: %s\n",message);}}
int main(){
    for(float units:{1.f,100.f,1000.f}) {
        NativeWeaponKL::AimFrame pose;pose.player=1;pose.weapon=2;pose.root=3;pose.model=4;pose.pairId=10;
        pose.tick=1000;pose.hand=1;pose.recenterAck=7;pose.unitsPerMetre=units;pose.origin={10*units,1.5f*units,-50*units};pose.direction={0,0,1};
        NativeAimKL::Endpoint endpoint;endpoint.valid=true;endpoint.pose=pose;endpoint.tick=1000;
        endpoint.point=Add(pose.origin,{0,0,10*units});
        NativeLaserPoseKL::Beam beam;
        Check(NativeLaserPoseKL::Build(pose,&endpoint,1000,beam)&&beam.surfaceContact&&std::fabs(beam.distance-10*units)<.001f*units,
            "fresh collinear real collision supplies the full beam and actual surface dot");
        for(float degrees:{1.f,3.f,5.f,9.f,15.f,45.f,100.f}) {
            auto current=pose;const float radians=degrees*.0174532925199433f;current.direction={std::sin(radians),0,std::cos(radians)};
            current.tick=1016;
            Check(NativeLaserPoseKL::Build(current,&endpoint,1016,beam),"rapid current barrel rotation keeps a visible emitter segment");
            Vec3 actual;NativeAimKLMath::Unit(Subtract(beam.end,current.origin),actual);
            Check(Dot(actual,current.direction)>.999999f,"beam remains exactly collinear with the current barrel instead of steering to the old contact");
            Check(!beam.surfaceContact&&beam.distance<=NativeLaserPoseKL::PendingLengthMetres*units+.0001f*units,
                "off-ray collision bounds a rigid directional guide without an invented surface dot");
            Check(beam.distance>7.f*units,"rapid motion keeps a practically visible guide instead of the old25cm stub");
        }
        auto near=endpoint;near.point=Add(pose.origin,{0,0,.1f*units});auto moved=pose;moved.origin.x+=.02f*units;
        Check(NativeLaserPoseKL::Build(moved,&near,1010,beam)&&!beam.surfaceContact&&beam.distance<=.1f*units+.0001f*units,
            "pending segment is also bounded by the projected distance of a nearby real hit");
        auto expired=endpoint;expired.tick=800;
        Check(NativeLaserPoseKL::Build(pose,&expired,1010,beam)&&!beam.surfaceContact&&beam.distance==8.f*units,
            "expired collision leaves the rigid eight-metre directional guide without a surface dot");
        auto foreign=endpoint;++foreign.pose.weapon;
        Check(NativeLaserPoseKL::Build(pose,&foreign,1010,beam)&&!beam.surfaceContact,
            "another weapon cannot supply this gun's surface point");
        Check(NativeLaserPoseKL::Build(pose,nullptr,1010,beam)&&!beam.surfaceContact&&beam.distance==8.f*units,
            "missing or missed query retains a visible directional guide but never a contact dot");
        auto almost=endpoint;almost.point.x+=.005f*units;
        Check(NativeLaserPoseKL::Build(pose,&almost,1010,beam)&&beam.surfaceContact&&std::fabs(beam.end.x-pose.origin.x)<.001f*units,
            "one-centimetre collision tolerance does not bend the displayed beam");
        auto invalid=pose;invalid.direction={};Check(!NativeLaserPoseKL::Build(invalid,&endpoint,1010,beam),"invalid render direction rejects laser geometry");
        invalid=pose;invalid.origin.x=std::numeric_limits<float>::quiet_NaN();Check(!NativeLaserPoseKL::Build(invalid,&endpoint,1010,beam),"nonfinite render origin rejects laser geometry");
        Check(!NativeLaserPoseKL::Build(pose,&endpoint,1151,beam),"an expired renderer image cannot keep an emitter segment forever");
        NativeLaserPoseKL::PairBeam pair;
        Check(pair.Select(pose,nullptr,1000,beam)&&!beam.surfaceContact,"first eye admits the pending emitter choice without a native hit");
        auto latePose=pose;latePose.tick=1016;auto lateContact=endpoint;lateContact.pose=latePose;lateContact.tick=1016;
        Check(pair.Select(latePose,&lateContact,1016,beam)&&!beam.surfaceContact&&beam.distance==8.f*units,
            "a native hit arriving between eyes cannot replace the shared directional guide with a one-eye dot");
        pair.Reset();
        Check(pair.Select(pose,&endpoint,1000,beam)&&beam.surfaceContact,"fresh collision is admitted once for the whole physical pair");
        latePose.tick=1160;
        Check(pair.Select(latePose,nullptr,1160,beam)&&beam.surfaceContact&&std::fabs(beam.distance-10*units)<.001f*units,
            "collision TTL crossing between eyes cannot remove an already admitted contact while current render pose is valid");
        auto different=endpoint;different.point.z+=units;different.pose=latePose;different.tick=1160;
        Check(pair.Select(latePose,&different,1160,beam)&&std::fabs(beam.distance-10*units)<.001f*units,
            "a later current-pair native query cannot replace the admitted surface point");
        ++latePose.pairId;
        Check(pair.Select(latePose,nullptr,1160,beam)&&!beam.surfaceContact,"the next physical pair independently admits its current pending choice");
        for(unsigned identity=0;identity<8;++identity){
            pair.Reset();pair.Select(pose,&endpoint,1000,beam);auto changed=pose;
            switch(identity){case 0:++changed.player;break;case 1:++changed.weapon;break;case 2:++changed.root;break;case 3:++changed.model;break;
                case 4:changed.hand=0;break;case 5:++changed.recenterAck;break;case 6:changed.unitsPerMetre+=1;break;default:changed.unitsPerMetre=0;break;}
            Check(!pair.Select(changed,&endpoint,1010,beam)&&!pair.Select(pose,&endpoint,1010,beam),
                "changed same-pair owner/reference/hand/recenter/scale retires the visual choice without resurrection");
        }
        pair.Reset();pair.Select(pose,&endpoint,1000,beam);auto carrier=pose;carrier.origin.x+=.02f*units;
        Check(pair.Select(carrier,nullptr,1010,beam)&&!beam.surfaceContact&&beam.distance==8.f*units,
            "genuine within-pair carrier motion cannot retain a now-off-ray surface dot");
        Check(pair.Select(pose,&endpoint,1020,beam)&&!beam.surfaceContact,
            "unsupported within-pair surface choice does not resurrect when the old world point later realigns");
        pair.Reset();Check(pair.Select(pose,&near,1000,beam)&&beam.surfaceContact,"near collision is initially on the real ray");
        auto past=pose;past.origin.z+=units;
        Check(pair.Select(past,nullptr,1010,beam)&&!beam.surfaceContact&&beam.distance==8.f*units,
            "moving the current barrel beyond an admitted old hit preserves directional visibility without reversing the beam");
    }
    std::printf("Rigid current-render laser: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
