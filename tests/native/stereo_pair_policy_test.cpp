#include "stereo_pair_policy.hpp"
#include <cstdio>
#include <limits>
int main(){
    unsigned checks=0,failures=0;
    const auto check=[&](bool okay,const char* name){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",name);}};
    VRIPC::EyeMetadata l{},r{};l.width=r.width=2560;l.height=r.height=1600;l.stride=r.stride=10240;
    l.frameId=2;r.frameId=3;l.poseValid=r.poseValid=VRIPC::kSceneFrame;l.headOrientation[3]=r.headOrientation[3]=1;
    l.sensorTime=r.sensorTime=12.3;l.tick=1000;r.tick=1200;
    check(StereoPairPolicy::Compatible(l,r),"a coherent 200ms native render gap remains a complete pair");
    r.tick=2000;check(StereoPairPolicy::Compatible(l,r),"pair lifetime bound is inclusive");
    r.tick=2001;check(!StereoPairPolicy::Compatible(l,r),"unbounded old source pair is refused");r.tick=1200;
    r.frameId=5;check(!StereoPairPolicy::Compatible(l,r),"different source pair never combines");r.frameId=3;
    r.sensorTime=12.4;check(!StereoPairPolicy::Compatible(l,r),"different tracking sample never combines");r.sensorTime=l.sensorTime;
    r.headOrientation[1]=.01f;check(!StereoPairPolicy::Compatible(l,r),"different orientation never combines");r.headOrientation[1]=0;
    r.headPosition[0]=.01f;check(!StereoPairPolicy::Compatible(l,r),"different position never combines");r.headPosition[0]=0;
    r.poseValid=VRIPC::kUiFrame;check(!StereoPairPolicy::Compatible(l,r),"mono transition cannot be a stereo pair");r.poseValid=l.poseValid;
    r.width=1280;check(!StereoPairPolicy::Compatible(l,r),"resize cannot be a stereo pair");r.width=l.width;
    r.sensorTime=l.sensorTime=std::numeric_limits<double>::quiet_NaN();check(!StereoPairPolicy::Compatible(l,r),"NaN sample cannot establish identity");
    check(StereoPairPolicy::Fresh(2000,1000),"completed source image freshness bound");
    check(!StereoPairPolicy::Fresh(2001,1000),"expired completed source rejected");
    check(!StereoPairPolicy::Fresh(999,1000),"future tick rejected");
    check(StereoPairPolicy::Fresh(50,0xfffffff0u),"tick wrap retains elapsed freshness");
    check(!StereoPairPolicy::Fresh(50,0),"unset tick rejected");
    // Reproduce a live 480 ms pre-camera stall without a sleep. These are
    // production pair sequencing/latching calls, with a deterministic host.
    struct Tracking {bool valid=true,mounted=true;DWORD tick=1000;double sensorTime=1;float yaw=0;} sample,host;
    StereoRenderMath::PairSequence pair;
    long ack=7,hostAck=7;unsigned reads=0;
    const auto refresh=[&]{sample=host;ack=hostAck;++reads;};
    const auto camera=[&](DWORD now,bool started=false){return StereoPairPolicy::LatchAtCamera(pair,started,sample,now,ack,hostAck,refresh);};
    host.tick=1480;host.sensorTime=1.48;host.yaw=.3f;
    check(camera(1480)&&sample.tick==1480&&sample.sensorTime==1.48&&reads==1,
        "pre-camera stall latches current host pose instead of discarding fresh rendering");
    const auto leftId=pair.FrameId();const auto leftSample=sample;
    pair.CompleteEye();host.tick=1490;host.sensorTime=1.49;host.yaw=.4f;
    check(!camera(1490)&&pair.FrameId()==leftId+1&&reads==1&&sample.sensorTime==leftSample.sensorTime&&sample.yaw==leftSample.yaw,
        "ordinary right eye keeps exact left tracking sample despite newer host pose");
    check(!camera(1730)&&reads==1,"right eye retains the sample at the inclusive 250 ms bound");
    host.tick=1960;host.sensorTime=1.96;host.yaw=.8f;
    check(camera(1960)&&pair.Eye()==0&&pair.FrameId()>leftId+1&&sample.sensorTime==1.96&&reads==2,
        "right-eye pre-camera stall cancels half-pair and starts a new left before rendering");
    const auto replacementId=pair.FrameId();const auto replacement=sample;
    host.tick=2500;host.sensorTime=2.5;++hostAck;
    check(!camera(2500,true)&&pair.FrameId()==replacementId&&sample.sensorTime==replacement.sensorTime&&ack==7&&reads==2,
        "applied camera cannot resample or change reference even after stall and recenter");
    pair.CompleteEye();
    check(camera(2500)&&pair.Eye()==0&&pair.FrameId()>replacementId+1&&ack==hostAck&&reads==3,
        "unstarted right eye changes reference only by restarting its pair");
    pair.CompleteEye();host.tick=2510;host.sensorTime=2.51;sample.mounted=false;
    check(camera(2510)&&pair.Eye()==0&&sample.mounted&&reads==4,
        "remounted headset starts a new pair rather than retaining an unmounted sample");
    pair.CompleteEye();sample.valid=false;
    check(camera(2510)&&pair.Eye()==0&&sample.valid&&reads==5,"invalid half-pair restarts before a fresh sample");
    host.valid=false;host.tick=0;
    check(camera(2520)&&!sample.valid&&!sample.tick,"late latch cannot manufacture tracking when host has no valid pose");
    host.valid=true;host.tick=2600;host.sensorTime=2.6;
    check(camera(2600)&&sample.valid&&sample.tick==2600,"a provider retry before any applied eye can recover tracking");
    pair.CompleteEye();const auto beforeRecenter=pair.FrameId();++hostAck;host.tick=2610;
    check(camera(2610)&&pair.Eye()==0&&pair.FrameId()>beforeRecenter&&ack==hostAck,
        "fresh right eye still starts a new pair when the recenter reference changes");
    pair.CompleteEye();const auto startedRight=pair.FrameId();const auto retainedSample=sample;const auto beforeStarted=reads;
    ++hostAck;host.tick=3000;host.sensorTime=3;
    check(!camera(3000,true)&&pair.FrameId()==startedRight&&sample.sensorTime==retainedSample.sensorTime&&reads==beforeStarted,
        "already applied right eye never swaps to a new pair mid-image");
    hostAck=ack;
    sample.tick=0xfffffff0u;host.tick=50;
    const auto wrappedId=pair.FrameId();const auto beforeWrap=reads;
    check(!camera(50)&&pair.FrameId()==wrappedId&&reads==beforeWrap,"tracking latch keeps coherent right eye across tick wrap");
    sample.tick=60;
    check(camera(50)&&pair.Eye()==0&&reads==beforeWrap+1,"future tracking timestamp cannot retain a right half-pair");
    // A new host sample under a new left ID cannot match the abandoned left.
    l.sensorTime=leftSample.sensorTime;r.sensorTime=sample.sensorTime;l.tick=1480;r.tick=50;
    l.frameId=leftId;r.frameId=pair.FrameId()+1;
    check(!StereoPairPolicy::Compatible(l,r),"host pairing rejects the abandoned left after camera-side restart");
    // Rejected hands admission must recover from either eye. In particular,
    // a failed first eye never reaches the existing right-eye age timeout.
    StereoRenderMath::PairSequence admission;
    auto admissionId=admission.FrameId();
    StereoPairPolicy::FinishImage(admission,false,false,true);
    check(admission.Eye()==0&&admission.FrameId()>admissionId,"rejected first eye retires the sticky admission instead of retrying it forever");
    admissionId=admission.FrameId();
    StereoPairPolicy::FinishImage(admission,true,false,false);
    check(admission.Eye()==1&&admission.FrameId()==admissionId+1,"accepted first eye advances normally after a fresh pair admission");
    StereoPairPolicy::FinishImage(admission,false,false,true);
    check(admission.Eye()==0&&admission.FrameId()>admissionId+1,"rejected second eye discards its partial pair at the boundary");
    admissionId=admission.FrameId();
    StereoPairPolicy::FinishImage(admission,false,false,false);
    check(admission.FrameId()==admissionId,"ordinary publication backpressure retains the same eye for retry");
    StereoPairPolicy::FinishImage(admission,true,false,false);
    StereoPairPolicy::FinishImage(admission,true,false,false);
    check(admission.Eye()==0&&admission.FrameId()>admissionId+1,"a subsequent complete pair progresses after rejected admission");
    admissionId=admission.FrameId();
    StereoPairPolicy::FinishImage(admission,true,true,false);
    check(admission.Eye()==0&&admission.FrameId()>admissionId,"monoscopic scope completes its own pair without a second eye");
    std::printf("Tracked stereo pair stall policy: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
