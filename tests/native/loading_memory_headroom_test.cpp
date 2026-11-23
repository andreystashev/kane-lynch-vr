#include "loading_memory_headroom.hpp"
#include <cstdio>
#include <array>
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int main(){
    using namespace LoadingMemoryHeadroom;
    const std::array<Region,5> regions={{{0,20,MEM_FREE},{20,10,MEM_COMMIT},{30,15,MEM_RESERVE},{45,40,MEM_FREE},{85,15,MEM_COMMIT}}};
    const auto query=[&](std::uint64_t at,Region& out,std::uint32_t& error){
        for(const auto& r:regions)if(at>=r.base&&at<r.base+r.bytes){out=r;return true;}error=123;return false;
    };
    const auto sample=Scan({10,95},query);
    Check(sample.valid&&sample.queryCount==5,"five regions span exact application bounds");
    Check(sample.freeBytes==50&&sample.largestFreeRegion==40,"free holes and largest clipped free span");
    Check(sample.committedBytes==20&&sample.reservedBytes==15,"committed/reserved bytes clipped at end");
    Check(sample.freeRegions==2&&sample.committedRegions==2&&sample.reservedRegions==1,"region counts by state");
    Check(sample.minimumAddress==10&&sample.limitAddress==95,"bounds retained");
    Check(sample.freeBytes+sample.committedBytes+sample.reservedBytes==85,"no holes or double counted bytes");
    Check(Scan({3,3},query).failure==Failure::InvalidBounds,"empty bounds rejected");
    Check(Scan({4,3},query).failure==Failure::InvalidBounds,"reversed bounds rejected");
    Check(Scan({0,100},query,2).failure==Failure::RegionLimit,"bad scan cannot run without a bound");
    auto fail=Scan({0,100},[](auto,Region&,std::uint32_t& error){error=87;return false;});
    Check(!fail.valid&&fail.lastError==87&&fail.failure==Failure::QueryFailed,"query failure retains exact error");
    Check(Scan({0,10},[](auto,Region& r,auto&){r={0,0,MEM_FREE};return true;}).failure==Failure::ZeroSize,"zero region rejected");
    Check(Scan({5,10},[](auto,Region& r,auto&){r={0,5,MEM_FREE};return true;}).failure==Failure::NoProgress,"no-progress query rejected");
    Check(Scan({5,10},[](auto,Region& r,auto&){r={6,3,MEM_FREE};return true;}).failure==Failure::InvalidBase,"skipped-address query rejected");
    Check(Scan({5,10},[](auto,Region& r,auto&){r={5,~std::uint64_t(0),MEM_FREE};return true;}).failure==Failure::Overflow,"region end overflow rejected");
    Check(Scan({0,10},[](auto,Region& r,auto&){r={0,10,7};return true;}).failure==Failure::UnknownState,"unknown state cannot yield a valid sample");
    const auto high=Scan({0xfffff000ULL,0x100000000ULL},[](auto,Region& r,auto&){r={0xffff0000ULL,0x10000,MEM_FREE};return true;});
    Check(high.valid&&high.freeBytes==4096,"x86 final address does not wrap at 4GiB");
    Observer observer;unsigned calls=0;
    const auto read=[&]{++calls;Sample s;s.valid=true;return s;};
    Check(!observer.Observe(0,false,0,read)&&calls==0,"no scan outside loading");
    Check(observer.Observe(1,true,100,read)&&calls==1,"new entry sampled once");
    Check(!observer.Observe(1,true,2099,read)&&calls==1,"retry rate gated below 2000ms");
    Check(observer.Observe(1,true,2100,read)&&calls==2,"retry at exactly 2000ms");
    Check(observer.Observe(2,true,2101,read)&&calls==3,"new loading entry sampled independently");
    Check(!observer.Observe(2,false,9000,read)&&calls==3,"no retry after loading ends");
    Check(observer.Snapshot().entryCount==2&&observer.Snapshot().tick==2101&&observer.Snapshot().scanCount==3,"sample identity and attempt count retained");
    observer.Reset();Check(!observer.Snapshot().observed&&observer.Snapshot().scanCount==0,"reset clears telemetry identity");
    Check(observer.Observe(7,true,0xffffff00U,read),"first near-wrap sample");
    Check(!observer.Observe(7,true,0x20U,read),"clock wrap respects retry interval");
    Check(observer.Observe(7,true,0x700U,read),"clock wrap allows elapsed retry");
    const auto actual=ReadCurrentProcess();
    Check(actual.valid&&actual.queryCount>0,"own-process VirtualQuery smoke without game/GPU");
    Check(actual.freeBytes+actual.committedBytes+actual.reservedBytes==actual.limitAddress-actual.minimumAddress,"actual address space fully accounted");
    std::printf("Loading memory telemetry: %u checks, %u failures; actual free=%llu largest=%llu regions=%llu\n",checks,failures,
        static_cast<unsigned long long>(actual.freeBytes),static_cast<unsigned long long>(actual.largestFreeRegion),static_cast<unsigned long long>(actual.queryCount));
    return failures?1:0;
}
