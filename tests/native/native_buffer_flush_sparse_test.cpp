#include "native_buffer_flush_policy.hpp"

#include <array>
#include <cstdio>
#include <vector>
using namespace NativeBufferFlushKL;
unsigned checks=0,failures=0;
void Check(bool b,const char* text){++checks;if(!b){++failures;std::printf("FAIL %s\n",text);}}
int main(){
    std::array<unsigned char,256> gpu{},shadow{},expected{};Range ranges[]={{100,20},{4,16},{110,24},{240,16}};
    for(unsigned i=0;i<gpu.size();++i){gpu[i]=expected[i]=static_cast<unsigned char>(i);shadow[i]=static_cast<unsigned char>(i^0xA5);}
    for(auto r:ranges)std::memcpy(expected.data()+r.offset,shadow.data()+r.offset,r.bytes);
    unsigned locks=0,unlocks=0;bool held=false;void* pending=shadow.data();
    auto lock=[&](unsigned offset,unsigned bytes,void** data,DWORD flags){++locks;Check(!held&&offset==4&&bytes==252&&!flags,"single positive exactbounding Lock0");held=true;*data=gpu.data()+offset;return S_OK;};
    auto unlock=[&](){++unlocks;Check(held&&pending==gpu.data()+4,"native pending mapping retained through Unlock");held=false;return S_OK;};
    auto out=SparseUpload(ranges,4,256,shadow.data(),pending,lock,unlock);
    Check(out.completed&&locks==1&&unlocks==1&&!pending&&!held&&gpu==expected,"sparse dirty order and holes exact after balanced single map");
    std::uint32_t random=42;bool exact=true;
    for(unsigned trial=0;trial<10000;++trial){for(unsigned i=0;i<256;++i){gpu[i]=expected[i]=static_cast<unsigned char>(i+trial);shadow[i]=static_cast<unsigned char>(i^trial);}
        Range queue[32];for(auto& r:queue){random=random*1664525+1013904223;r.offset=random%240;random=random*1664525+1013904223;r.bytes=1+random%16;
            std::memcpy(expected.data()+r.offset,shadow.data()+r.offset,r.bytes);}
        pending=shadow.data();out=SparseUpload(queue,32,256,shadow.data(),pending,[&](unsigned at,unsigned,void** map,DWORD f){exact=exact&&!f;*map=gpu.data()+at;return S_OK;},[]{return S_OK;});
        exact=exact&&out.completed&&!pending&&gpu==expected;
    }Check(exact,"10000 sparse unsorted/overlapping queues preserve full bytes including holes");
    for(unsigned i=0;i<256;++i){gpu[i]=expected[i]=static_cast<unsigned char>(i);shadow[i]=static_cast<unsigned char>(i^0xA5);}
    const Range tail[]={{200,12},{248,8}};for(auto r:tail)std::memcpy(expected.data()+r.offset,shadow.data()+r.offset,r.bytes);
    pending=shadow.data();out=SparseUpload(tail,2,256,shadow.data(),pending,[&](unsigned at,unsigned bytes,void** data,DWORD){
        Check(at==200&&bytes==56,"nonzero span starts near capacity and includes last byte");*data=gpu.data()+at;return S_OK;},[]{return S_OK;});
    Check(out.completed&&gpu==expected,"offset subtraction precedes mapped pointer arithmetic at resource end");
    for(unsigned scenario=0;scenario<4;++scenario){pending=shadow.data();locks=unlocks=0;
        out=SparseUpload(ranges,4,256,shadow.data(),pending,[&](unsigned,unsigned,void** data,DWORD){++locks;*data=scenario?nullptr:reinterpret_cast<void*>(1);return scenario?S_OK:E_FAIL;},[&](){++unlocks;return scenario>=2?E_FAIL:S_OK;});
        if(scenario==0)Check(out.lockFailed&&!out.completed&&pending==shadow.data()&&locks==1&&!unlocks,"failed initial Lock restores touched output for checked retry without Unlock");
        if(scenario==1)Check(out.invalidMapping&&!out.completed&&pending==shadow.data()&&unlocks==1,"successful null map balances successful Unlock before deferred checked retry");
        if(scenario>=2)Check(out.invalidMapping&&out.unlockFailed&&out.completed&&!pending&&unlocks==1,"failed null-map Unlock forbids nested fallback and mirrors native cleanup");
    }
    pending=shadow.data();out=SparseUpload(ranges,4,256,shadow.data(),pending,[&](unsigned at,unsigned,void** data,DWORD){*data=gpu.data()+at;return S_OK;},[]{return E_FAIL;});
    Check(out.completed&&out.unlockFailed&&!pending,"failed Unlock after copies consumes native queue without unsafe retry");
    Range bad[]={{0,8},{8,0}};pending=shadow.data();locks=0;
    auto neverLock=[&](unsigned,unsigned,void**,DWORD){++locks;return E_FAIL;};
    for(unsigned n:{0u,1u,2u,2048u}){out=SparseUpload(bad,n,256,shadow.data(),pending,neverLock,[]{return S_OK;});Check(!out.attempted&&!locks&&pending==shadow.data(),"zero/single/zero-size/saturated queue untouched before COM");}
    bad[1]={255,2};out=SparseUpload(bad,2,256,shadow.data(),pending,neverLock,[]{return S_OK;});Check(!out.attempted&&!locks,"out of capacity untouched");
    bad[1]={12,3};out=SparseUpload(bad,2,256,shadow.data(),pending,neverLock,[]{return S_OK;},2);Check(!out.attempted&&!locks,"index byte alignment checked");
    const Range single[]={{16,24}};
    for(unsigned count:{1u,2048u,3000u}){gpu.fill(0x55);pending=shadow.data();locks=unlocks=0;
        out=CheckedWorkerUpload(single,count,256,shadow.data(),pending,[&](unsigned offset,unsigned bytes,void** data,DWORD flags){
            ++locks;Check(!flags&&offset==(count==1?16u:0u)&&bytes==(count==1?24u:256u),"single/full mode retains exact native lock extent");*data=gpu.data()+offset;return S_OK;},[&](){++unlocks;return S_OK;});
        Check(out.completed&&!pending&&locks==1&&unlocks==1,"single and saturated queue use one checked lock/unlock");
        bool correct=true;for(unsigned i=0;i<256;++i)correct=correct&&gpu[i]==(count==1&&(i<16||i>=40)?0x55:shadow[i]);Check(correct,"single/full upload exact complete byte image");
        pending=shadow.data();gpu.fill(0x99);out=CheckedWorkerUpload(single,count,256,shadow.data(),pending,[&](unsigned,unsigned,void** data,DWORD){*data=nullptr;return E_OUTOFMEMORY;},[]{return E_FAIL;});
        Check(out.lockFailed&&!out.completed&&pending==shadow.data()&&gpu[0]==0x99,"single/full failed map keeps shadow sentinel and never memcpy's null");
    }
    // The disabled-optimization/DXVK route must preserve original per-range
    // behavior, while failure after a copied prefix remains safe to retry.
    gpu.fill(0x55);pending=shadow.data();locks=unlocks=0;
    out=CheckedNativeUpload(ranges,4,256,shadow.data(),pending,[&](unsigned offset,unsigned bytes,void** data,DWORD flags){
        Check(!flags&&offset==ranges[locks].offset&&bytes==ranges[locks].bytes,"checked native route retains map order and exact original extents");
        ++locks;*data=gpu.data()+offset;return locks==3?E_OUTOFMEMORY:S_OK;},[&](){++unlocks;return S_OK;});
    Check(out.lockFailed&&!out.completed&&locks==3&&unlocks==2&&pending==shadow.data(),"mid-queue failure leaves original queue eligible for idempotent whole retry");
    locks=unlocks=0;out=CheckedNativeUpload(ranges,4,256,shadow.data(),pending,[&](unsigned offset,unsigned,void** data,DWORD){++locks;*data=gpu.data()+offset;return S_OK;},[&](){++unlocks;return S_OK;});
    Check(out.completed&&!pending&&locks==4&&unlocks==4,"retry completes every original dirty range after partial first attempt");
    bool correct=true;for(unsigned i=0;i<256;++i){bool dirty=false;for(auto r:ranges)dirty=dirty||(i>=r.offset&&i<r.offset+r.bytes);correct=correct&&gpu[i]==(dirty?shadow[i]:0x55);}
    Check(correct,"native retry preserves every gap and exact final dirty bytes");
    pending=shadow.data();locks=unlocks=0;out=CheckedNativeUpload(ranges,4,256,shadow.data(),pending,[&](unsigned offset,unsigned,void** data,DWORD){++locks;*data=gpu.data()+offset;return S_OK;},[&](){++unlocks;return E_FAIL;});
    Check(out.completed&&out.unlockFailed&&!pending&&locks==1&&unlocks==1,"native-route failed Unlock consumes queue and forbids later nested maps");
    std::printf("native_buffer_flush_sparse_test: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
