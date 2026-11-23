#include "frame_delivery_metrics.hpp"
#include <cstdio>
int main(){
    unsigned checks=0,failures=0;const auto check=[&](bool okay,const char* name){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",name);}};
    FrameDelivery::Metrics m;m.SceneSubmit(1);check(!m.sceneSubmits,"waiting is not repeated gameplay");
    m.SetActive(true);check(m.NewPair(2,1,100),"first valid pair accepted");m.SceneSubmit(100);m.SceneSubmit(110);
    check(m.sceneSubmits==2&&m.repeatedSceneSubmits==1&&m.maxRepeatAgeMs==10,"compositor refresh is distinct from fresh game image");
    m.SceneSubmit(150);m.SceneSubmit(160);m.SceneSubmit(200);m.SceneSubmit(400);
    check(m.holds50==1&&m.holds100==1&&m.holds250==1,"each stalled pair crosses each hold threshold once");
    check(!m.NewPair(2,1,410)&&m.LastPairAge(410)==310,"same pair redraw never renews freshness");
    check(m.NewPair(8,1,420)&&m.sourcePairIdGaps==2,"unseen source pair identifiers counted independently");
    check(m.freshPairIntervals.Describe().medianUs==320000,"fresh pair arrival interval survives display resubmits");
    m.SceneSubmit(420);check(m.repeatedSceneSubmits==5,"new pair is not a repeated scene submit");
    m.SetActive(false);m.SceneSubmit(10000);m.SetActive(true);m.NewPair(1000,1,11000);
    check(m.sourcePairIdGaps==2&&m.freshPairIntervals.Count()==1,"menu or loading intervals never become gameplay delivery gaps");
    m.NewPair(2,2,11100);check(m.sourcePairIdGaps==2&&m.freshPairIntervals.Count()==1,"new transport epoch retires old identifier domain");
    check(!m.NewPair(3,2,11101)&&!m.NewPair(4,0,11102),"odd or unqualified stereo identifier refused");
    FrameDelivery::Metrics wrap;wrap.SetActive(true);wrap.NewPair(2,1,0xfffffff0u);wrap.SceneSubmit(0xfffffff0u);wrap.SceneSubmit(50);
    check(wrap.maxRepeatAgeMs==66&&wrap.holds50==1,"tick wrap does not create a huge hold");
    std::printf("Host fresh-pair delivery metrics: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
