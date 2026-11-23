#include "capture_frame_policy.hpp"
#include <cstdio>
#include <initializer_list>
int main(){
    using namespace CaptureFramePolicy;unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL: %s\n",why);}};
    check(Select(true,true,true,false)==Action::Scene,"complete scene is published");
    check(Select(false,true,true,false)==Action::Hold,"invalid established stereo frame is held");
    check(HeldGPUResult()==0,"hold drops capture without requesting a CPU readback");
    check(Select(false,true,false,false)==Action::Panel,"initial uncalibrated session may show a panel");
    check(Select(false,true,false,false,true)==Action::Hold,"an incomplete first tracked scene cannot become a panel");
    check(Select(false,true,false,true,true)==Action::Panel,"explicit cinema loading and menus override first-scene intent");
    check(Select(false,false,false,false,true)==Action::Panel,"explicit flat mode does not inherit tracked-scene intent");
    SceneIntent intent;
    check(!intent.Observe(false,true,false,true),"unknown startup without a native camera retains panel fallback");
    check(intent.Observe(true,true,false,true),"a real tracked camera establishes first-scene intent");
    check(intent.Observe(false,true,false,true),"Present clearing the frame snapshot cannot erase pending scene intent");
    check(!intent.Observe(false,true,true,true),"an explicit movie menu or loading screen retires scene intent");
    intent.Observe(true,true,false,true);
    check(!intent.Observe(false,true,false,false),"host loss retires pending scene intent");
    intent.Observe(true,true,false,true);
    check(!intent.Observe(false,false,false,true),"explicit flat mode retires pending scene intent");
    intent.Observe(true,true,false,true);intent.Reset();
    check(!intent.observed,"device reset explicitly clears native scene intent");
    check(Select(false,true,true,true)==Action::Panel,"actual menu or movie overrides previous stereo");
    check(Select(false,false,true,false)==Action::Panel,"explicit flat mode retains mono output");
    for(unsigned elapsed:{100u,500u,1000u,10000u}){(void)elapsed;
        check(Select(false,true,true,false)==Action::Hold,"elapsed stall time cannot manufacture a menu or movie");}
    // Replay the observed first-scene sequence: a valid left camera starts a
    // full GPU generation; its publication is Busy until the host opens it.
    // The next left retry fails its render-pair admission. With no established
    // scene yet, the old policy called that retry UI and replaced the pending
    // generation before the host's delayed acknowledgement could be used.
    enum class Pool {Panel,Scene};
    Pool pool=Pool::Panel;unsigned generation=1,ack=1,waiting=0,changes=0;
    unsigned firstSceneGeneration=0,eye=0,published=0;bool established=false;
    SceneIntent pending;
    for(unsigned present=0;present<16;++present){
        const bool prepared=(present%2)==0;
        // Only the first camera callback needs to be observed. Later retries
        // also cover a completely empty frameSnapshot after Present cleanup.
        const auto action=Select(prepared,true,established,false,pending.Observe(present==0,true,false,true));
        if(action!=Action::Hold){
            const auto wanted=action==Action::Scene?Pool::Scene:Pool::Panel;
            if(wanted!=pool){pool=wanted;++generation;++changes;waiting=0;
                if(wanted==Pool::Scene&&!firstSceneGeneration)firstSceneGeneration=generation;}
            // The GPU consumer takes two capture opportunities to open and
            // acknowledge a new source. Holds still let that consumer run.
            if(action==Action::Scene&&ack==generation){++published;eye^=1;established=true;}
        }
        if(++waiting==2)ack=generation;
        if(published==2)break;
    }
    check(pool==Pool::Scene&&changes==1&&generation==firstSceneGeneration,
          "incomplete first-scene retries retain one full pool until its acknowledgement");
    check(published==2&&eye==0&&established,
          "delayed GPU acknowledgement eventually publishes the first complete stereo pair");

    constexpr std::uint64_t frequency=7200,period=frequency/PanelCopyCadence::RateHz;
    const PanelCopyCadence::Key menu{3840,2160,1,2};
    PanelCopyCadence cadence;
    check(!cadence.Skip(menu,100),"first panel is available immediately");
    check(!cadence.Skip(menu,101),"an attempt without a queued copy does not start the deadline");
    cadence.Copied(101,period);
    check(cadence.Skip(menu,150)&&!cadence.Skip(menu,151),"QPC panel deadline admits exactly at the target time");
    for(const PanelCopyCadence::Key change:{PanelCopyCadence::Key{1920,2160,1,2},
            PanelCopyCadence::Key{3840,1080,1,2},PanelCopyCadence::Key{3840,2160,2,2},
            PanelCopyCadence::Key{3840,2160,1,4}}){
        cadence.Reset();cadence.Skip(menu,100);cadence.Copied(100,period);
        check(!cadence.Skip(change,101)&&!cadence.Skip(change,102),
            "a new dimension, transport epoch or panel class admits until its first successful copy");
    }
    cadence.Reset();cadence.Skip(menu,100);cadence.Copied(100,period);cadence.Reset();
    check(!cadence.Skip(menu,101),"a projection or new texture generation retires the prior panel deadline");
    cadence.Copied(101,period);
    check(!cadence.Skip(menu,500),"a panel after a long gap is admitted immediately");
    cadence.Copied(500,period);
    check(cadence.Skip(menu,501)&&!cadence.Skip(menu,550),"a long gap discards accumulated copy debt");
    cadence.Copied(550,0);
    check(!cadence.Skip(menu,551),"an unavailable clock leaves capture uncapped");
    for(const unsigned hz:{120u,150u,240u,300u}){
        cadence.Reset();unsigned copies=0;
        for(unsigned i=0;i<hz;++i){
            const auto now=1+i*frequency/hz;
            if(!cadence.Skip(menu,now)){cadence.Copied(now,period);++copies;}
        }
        check(copies==(hz<PanelCopyCadence::RateHz?hz:PanelCopyCadence::RateHz),
            "steady sources keep the lower of the game and panel copy cadence");
    }

    const ReflectionWorld tokyo{1,2,3,4};
    ReflectionContinuity history;
    check(!history.Skip(tokyo,100),"an untracked startup cannot create reflection history");
    check(history.Qualified(tokyo,100)&&history.Skip(tokyo,118)&&history.Qualified(tokyo,136),
          "one rejected eye inside the same world keeps the previous reflected target until the next eye");
    check(history.Skip(tokyo,154)&&!history.Skip(tokyo,172),
          "an incomplete image does not renew the reflection continuity lease");
    check(history.Qualified(tokyo,100)&&!history.Skip(tokyo,221),
          "a stalled image exceeds the short reflection gap budget");
    for(const ReflectionWorld other:{ReflectionWorld{5,2,3,4},ReflectionWorld{1,5,3,4},
                                    ReflectionWorld{1,2,5,4},ReflectionWorld{1,2,3,5},ReflectionWorld{}}){
        check(history.Qualified(tokyo,100)&&!history.Skip(other,118),
              "a gap cannot inherit history from a different scene player camera or recenter");
    }
    check(history.Qualified(tokyo,100)&&!history.Qualified(ReflectionWorld{1,2,5,4},118),
          "even a qualified image clears previous-world history at an identity change");
    history.Reset();
    check(!history.Skip(tokyo,120),"an explicit lifecycle reset clears continuity immediately");
    check(history.Qualified(tokyo,0xfffffff0u)&&history.Skip(tokyo,8),
          "a short reflection gap survives the tick counter entering its next epoch");

    std::printf("Capture frame disposition: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
