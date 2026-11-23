#include "profile_timing.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {unsigned checks=0;void Check(bool result,const char* message){++checks;if(!result){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}}
int main(){
    using namespace ProfileTiming;
    {Scope scope(WeaponMesh);}
    Check(counters[WeaponMesh].samples==0,"disabled profiling never samples");
    enabled=true;
    for(unsigned i=0;i<61;++i){Scope scope(WeaponMesh);scope.Stop();scope.Stop();}
    Check(counters[WeaponMesh].samples==1,"one in sixty-one calls is sampled; Stop/destructor cannot count twice");
    for(unsigned i=0;i<3;++i){Scope scope(GPUBridge);}
    Check(counters[GPUBridge].samples==3,"frame-level GPU bridge scope measures every call");
    Check(EstimatedMs(WeaponMesh)>=0&&EstimatedMs(GPUBridge)>=0,"timings are nonnegative wall-clock estimates");
    for(auto region:{NativeWorldBind,NativePaletteBind,NativeShaderUpload}){
        for(unsigned i=0;i<61;++i){Scope scope(region);}
        Check(counters[region].samples==1,"native driver regions keep the same bounded sampling rate");
        Check(std::strstr(Names[region],"Inclusive")!=nullptr,"native driver timings expose their inclusive scope");
    }
    for(auto region:{NativeVertexLock,NativeIndexLock,NativeBufferUnlock}){
        for(unsigned i=0;i<3;++i){Scope scope(region);}
        Check(counters[region].samples==3&&Stride(region)==1,"original dynamic resource calls are measured without sparse stall sampling");
        Check(std::strstr(Names[region],"Inclusive")!=nullptr,"resource diagnostics distinguish original native calls from shadow copying");
    }
    {Scope scope(On12QueuePacing);}
    Check(counters[On12QueuePacing].samples==1&&Stride(On12QueuePacing)==1,"bounded own GPU pacing is measured on every present");
    Check(std::strstr(Names[On12QueuePacing],"Own")!=nullptr,"own pacing cannot be confused with native driver lock time");
    for(unsigned i=0;i<3;++i){Scope scope(SkinMaterialEvidenceOwn);}
    Check(counters[SkinMaterialEvidenceOwn].samples==3&&Stride(SkinMaterialEvidenceOwn)==1,
        "already-budgeted material captures record actual cost without multiplying by draw sampling stride");
    Check(!std::strcmp(Names[SkinMaterialEvidenceOwn],"skinMaterialEvidenceOwn"),
        "material evidence cost has its own stable diagnostic name");
    enabled=false;{Scope scope(GPUBridge);}
    Check(counters[GPUBridge].samples==3,"live disable immediately stops new timing samples");
    std::printf("Sampled nested mod wall timings: %u checks passed.\n",checks);return 0;
}
