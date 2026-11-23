#include "meta_submit_pacing.hpp"
#include <cstdio>
#include <limits>

namespace {
unsigned failures=0;
void Check(bool okay,const char* message){if(!okay){std::printf("FAIL: %s\n",message);++failures;}}
}
int main(){
    MetaSubmitPacing::Cadence cadence;
    Check(cadence.Due(false,0,13),"first held submission is immediately due");
    cadence.Begin(0);
    Check(!cadence.Due(false,7,13),"7 ms compositor wait counts toward 13 ms display interval");
    Check(!cadence.Due(false,12,13),"unchanged image waits only the remaining interval");
    Check(cadence.Due(false,13,13),"held image is due 13 ms from start, not 20 ms");
    cadence.Begin(13);
    Check(!cadence.Due(false,20,13)&&cadence.Due(false,26,13),"repeated 7 ms waits preserve start-to-start cadence");
    Check(cadence.Due(true,21,13),"fresh pair bypasses held-image deadline");
    cadence.Begin(21);
    Check(!cadence.Due(false,33,13)&&cadence.Due(false,34,13),"fresh image resets next hold deadline to its actual start");
    cadence.Begin(34);
    Check(cadence.Due(false,54,13),"slow compositor call creates no extra post-submit delay");
    cadence.Begin(54);
    Check(!cadence.Due(false,54,13),"fast compositor return does not busy-submit held images");
    const auto wrap=std::numeric_limits<std::uint32_t>::max()-5;
    cadence.Begin(wrap);
    Check(!cadence.Due(false,6,13)&&cadence.Due(false,7,13),"millisecond wrap keeps cadence valid");
    std::printf("Meta start-to-start submit pacing: %s.\n",failures?"FAILED":"passed");return failures?1:0;
}
