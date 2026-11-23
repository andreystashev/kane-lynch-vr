#include "native_hud_shadow.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
using namespace NativeHudShadow;
Allocation Block(std::uint32_t offset,std::uint32_t count,bool discard=false) {
    return {{0x1000,0x2000,0x3000,4096,Kind::Vertex},offset,count,discard};
}
bool Read(const Cache& cache,const Allocation& allocation,std::vector<unsigned char>& output) {
    const auto& id=allocation.identity;
    return cache.Read(id.resource,id.device,id.kind,id.capacity,allocation.offset,allocation.bytes,output);
}
void RingWrites() {
    Cache cache;std::vector<unsigned char> input(256),output{99};
    for(unsigned i=0;i<input.size();++i)input[i]=static_cast<unsigned char>(i);
    auto first=Block(24,96);const auto token=cache.Begin(first);
    Check(token&&!Read(cache,first,output)&&output==std::vector<unsigned char>{99},"in-flight writes are unavailable and preserve output");
    Check(cache.Commit(first,token,input.data(),96),"native pre-Unlock copy committed");
    Check(Read(cache,first,output)&&output==std::vector<unsigned char>(input.begin(),input.begin()+96),"exact mapped source reaches GPU range");
    auto adjacent=Block(120,48);const auto next=cache.Begin(adjacent);
    Check(cache.Commit(adjacent,next,input.data()+96,48),"adjacent ring allocation committed");
    auto together=Block(24,144);
    Check(Read(cache,together,output)&&output==std::vector<unsigned char>(input.begin(),input.begin()+144),"adjacent covered allocations can be read together");
    auto overwrite=Block(48,24);const auto pending=cache.Begin(overwrite);
    Check(!Read(cache,together,output),"new write invalidates only overlapping old bytes before Unlock");
    Check(Read(cache,Block(24,24),output)&&Read(cache,Block(72,96),output),"unmodified ranges survive a partial overwrite");
    auto later=Block(300,24);const auto newest=cache.Begin(later);
    Check(!cache.Commit(overwrite,pending,input.data(),24),"superseded allocation cannot restore stale coverage");
    Check(cache.Commit(later,newest,input.data(),24),"latest allocation remains usable");
    auto wrap=Block(0,48,true);const auto wrapped=cache.Begin(wrap);
    Check(!Read(cache,later,output)&&!Read(cache,first,output),"DISCARD invalidates every previous ring span");
    Check(cache.Commit(wrap,wrapped,input.data(),48)&&Read(cache,wrap,output),"wrapped allocation starts a new valid buffer epoch");
}
void IdentityAndBounds() {
    Cache cache;auto allocation=Block(0,96);std::vector<unsigned char> bytes(96,7),output{99};
    auto token=cache.Begin(allocation);Check(cache.Commit(allocation,token,bytes.data(),bytes.size()),"fixture captured");
    for(unsigned field=0;field<5;++field) {
        auto wrong=allocation;
        if(field==0)++wrong.identity.resource;
        if(field==1)++wrong.identity.device;
        if(field==2)wrong.identity.kind=Kind::Index16;
        if(field==3)++wrong.identity.capacity;
        if(field==4)wrong.offset=4090;
        Check(!Read(cache,wrong,output),"foreign resource/device/format/size and out-of-bounds ranges rejected");
    }
    auto recycled=allocation;recycled.identity.control=0x5000;
    const auto changed=cache.Begin(recycled);
    Check(changed&&!Read(cache,allocation,output)&&!cache.Commit(allocation,token,bytes.data(),96),"COM pointer reuse with another native control destroys previous coverage");
    Check(cache.Commit(recycled,changed,bytes.data(),96)&&Read(cache,recycled,output),"new identity can establish its own coverage");
    auto invalid=allocation;invalid.offset=std::numeric_limits<std::uint32_t>::max();
    Check(!cache.Begin(invalid),"offset addition overflow rejected");
    invalid=allocation;invalid.identity.capacity=Cache::MaximumResourceBytes+1;
    Check(!cache.Begin(invalid),"oversize resources rejected");
    invalid=allocation;invalid.bytes=0;Check(!cache.Begin(invalid),"empty native allocation rejected");
    Check(!cache.Commit(recycled,changed,bytes.data(),95),"partial CPU copy never marks the complete GPU range valid");
    cache.Clear();Check(!Read(cache,recycled,output)&&cache.TotalBytes()==0,"reset discards cached COM identities and bytes");
}
void BoundedStorage() {
    Cache cache;std::vector<unsigned char> bytes(24,42),output;
    for(unsigned i=0;i<20;++i) {
        auto allocation=Block(0,24);allocation.identity.resource+=i;allocation.identity.control+=i;
        allocation.identity.capacity=Cache::MaximumResourceBytes;
        const auto token=cache.Begin(allocation);
        Check(token&&cache.Commit(allocation,token,bytes.data(),bytes.size()),"bounded cache accepts current resource");
        Check(cache.TotalBytes()<=Cache::MaximumTotalBytes,"native resource churn cannot grow memory without bound");
        Check(Read(cache,allocation,output)&&output==bytes,"most recent resource survives budget eviction");
    }
    Check(!Read(cache,Block(0,24),output),"old resource evicted instead of reused by stale pointer");
    Cache fragmented;
    for(unsigned i=0;i<80;++i) {
        const auto allocation=Block(i*32,8);const auto token=fragmented.Begin(allocation);
        Check(fragmented.Commit(allocation,token,bytes.data(),8),"fragmented uploads stay bounded");
        Check(Read(fragmented,allocation,output),"range-budget eviction never drops the fresh allocation about to be drawn");
    }
    Check(!Read(fragmented,Block(8,8),output),"holes between native allocations never qualify as captured data");
}
void SourceProvenance() {
    Cache cache;std::vector<unsigned char> bytes(96,42),output;
    auto glyph=Block(24,96);glyph.provenance=0x99551;glyph.tick=1234;
    Check(cache.Commit(glyph,cache.Begin(glyph),bytes.data(),bytes.size()),"native glyph source captured before Unlock");
    std::uint32_t writer=0,tick=0;
    Check(cache.Read(glyph.identity.resource,glyph.identity.device,glyph.identity.kind,glyph.identity.capacity,
        24,96,output,&writer,&tick)&&writer==0x99551&&tick==1234,"draw receives provenance and age of the bytes it actually selects");
    auto particle=Block(48,24);particle.provenance=0x99360;particle.tick=1235;
    Check(cache.Commit(particle,cache.Begin(particle),bytes.data(),24),"world billboard overwrites invalidate former glyph provenance");
    Check(!Read(cache,glyph,output),"mixed source ranges cannot inherit the original glyph authorization");
    Check(cache.Read(particle.identity.resource,particle.identity.device,particle.identity.kind,particle.identity.capacity,
        48,24,output,&writer,&tick)&&writer==0x99360&&!InterfaceVertexWriter(writer),"world source stays world after sharing the dynamic vertex buffer");
    Check(InterfaceVertexWriter(0x99551)&&InterfaceVertexWriter(0x996BF)&&!InterfaceVertexWriter(0)&&
        !InterfaceVertexWriter(0x99360)&&!InterfaceVertexWriter(0x98C74),"only the exact glyph/image native writer sites qualify for generic UI");
    const auto inFlight=cache.Begin(Block(24,96));(void)inFlight;
    Check(!Read(cache,particle,output),"an unobserved or unsupported new vertex layout still destroys old UI coverage");
}
void ScratchReuseAndFreshness() {
    Cache cache;auto allocation=Block(24,96);allocation.provenance=0x99551;allocation.tick=1000;
    std::vector<unsigned char> bytes(96,17),output;
    output.reserve(512);const auto* storage=output.data();const auto capacity=output.capacity();
    Check(cache.Commit(allocation,cache.Begin(allocation),bytes.data(),bytes.size()),"scratch fixture captured");
    std::uint32_t writer=0,tick=0;
    const auto freshRead=[&](std::uint32_t now,std::uint32_t age,std::uint32_t count=96) {
        return cache.Read(allocation.identity.resource,allocation.identity.device,allocation.identity.kind,
            allocation.identity.capacity,allocation.offset,count,output,&writer,&tick,now,age);
    };
    Check(freshRead(1250,250)&&output==bytes&&writer==0x99551&&tick==1000,
          "fresh covered bytes preserve exact writer provenance and boundary age");
    Check(output.data()==storage&&output.capacity()==capacity,
          "repeated HUD reads reuse reserved scratch rather than allocating and swapping a new buffer");
    const auto before=output;writer=123;tick=456;
    Check(!freshRead(1251,250)&&output==before&&writer==123&&tick==456&&output.data()==storage,
          "stale coverage fails before touching scratch or provenance outputs");
    Check(freshRead(1100,250,24)&&output.size()==24&&output.data()==storage&&output.capacity()==capacity,
          "smaller indexed ranges retain their bounded scratch allocation");
    const auto pending=cache.Begin(allocation);const auto old=output;
    Check(!freshRead(1100,250)&&output==old&&output.data()==storage,
          "scratch reuse cannot make an in-flight overwritten range readable");
    bytes.assign(96,93);allocation.provenance=0x996BF;allocation.tick=1100;
    Check(cache.Commit(allocation,pending,bytes.data(),bytes.size())&&freshRead(1100,250)&&
          output==bytes&&writer==0x996BF&&tick==1100&&output.data()==storage,
          "reused scratch receives newly committed bytes and their new writer without stale data");
    allocation.tick=0xfffffff0u;
    Check(cache.Commit(allocation,cache.Begin(allocation),bytes.data(),bytes.size())&&freshRead(20,40),
          "freshness checks retain the existing native unsigned timer-wrap convention");
    Check(!freshRead(41,40),"wrapped timer does not extend stale HUD coverage");
}
void SelectiveCapture() {
    Check(CaptureWriter(Kind::Vertex,0x99551,24)&&CaptureWriter(Kind::Vertex,0x996BF,24),"exact glyph/image vertices are copied");
    Check(CaptureWriter(Kind::Index16,0x9851C,2),"native quad builder indices remain available");
    Check(!CaptureWriter(Kind::Vertex,0x99360,24)&&!CaptureWriter(Kind::Vertex,0x99551,32)&&
          !CaptureWriter(Kind::Index16,0x9851D,2)&&!CaptureWriter(Kind::Index32,0x9851C,4),"world writers and unknown index layouts never trigger copies");
    Cache cache;std::vector<unsigned char> bytes(256,31),output;
    auto allocation=Block(24,192);auto token=cache.Begin(allocation);
    Check(cache.Commit(allocation,token,bytes.data(),allocation.bytes),"seed tracked interface range");
    const auto total=cache.TotalBytes();
    Check(!cache.ObserveUncaptured(0x9000,0xa000,4096,0,96,true)&&cache.TotalBytes()==total,
          "unrelated world writes allocate no shadow storage");
    Check(cache.ObserveUncaptured(0x3000,0x1000,4096,72,24,false)&&cache.TotalBytes()==total,
          "overwriting world bytes invalidate existing storage without reallocating it");
    Check(!Read(cache,allocation,output),"partially overwritten HUD cannot survive as complete coverage");
    auto left=Block(24,48);auto right=Block(96,120);
    Check(Read(cache,left,output)&&Read(cache,right,output),"unaffected ring-buffer ranges remain available");
    token=cache.Begin(allocation);
    cache.ObserveUncaptured(0x3000,0x1000,4096,0,24,true);
    Check(!cache.Commit(allocation,token,bytes.data(),allocation.bytes)&&!Read(cache,left,output),
          "discard cancels old tokens and all prior coverage even without a captured replacement");
    token=cache.Begin(allocation);Check(cache.Commit(allocation,token,bytes.data(),allocation.bytes),"restore fixture for identity change");
    cache.ObserveUncaptured(0x3000,0xb000,4096,24,48,false);
    Check(cache.TotalBytes()==0&&!Read(cache,allocation,output),"resource replacement through the same native control retires the old identity");
    Check(cache.Begin(allocation)!=0,"cache remains reusable after a skipped writer");
    cache.ObserveUncaptured(0x3000,0x1000,4096,4095,24,false);
    Check(cache.TotalBytes()==0,"invalid observed range fails closed for tracked control");
}
void DirectNativeRead() {
    Cache cache;std::vector<unsigned char> oldBytes(240,42),newBytes(96,17),out{99};
    auto whole=Block(24,240);whole.provenance=11;whole.tick=20;
    Check(cache.Commit(whole,cache.Begin(whole),oldBytes.data(),oldBytes.size()),"initial coverage for a direct native update");
    auto overwrite=Block(72,96);overwrite.provenance=12;overwrite.tick=21;
    const auto token=cache.Begin(overwrite);unsigned reads=0;
    Check(!cache.Capture(overwrite,token,[&](unsigned char* destination,std::size_t size){
        ++reads;std::memcpy(destination,newBytes.data(),size/2);return false;
    })&&reads==1,"partial native read does not commit coverage");
    std::uint32_t writer=91,tick=92;
    Check(!cache.Read(overwrite.identity.resource,overwrite.identity.device,overwrite.identity.kind,
        overwrite.identity.capacity,overwrite.offset,overwrite.bytes,out,&writer,&tick)&&
        out==std::vector<unsigned char>{99}&&writer==91&&tick==92,"failed read leaves output/provenance unchanged even after partial destination writes");
    Check(Read(cache,Block(24,48),out)&&out==std::vector<unsigned char>(48,42)&&
        Read(cache,Block(168,96),out)&&out==std::vector<unsigned char>(96,42),"partial read cannot alter disjoint valid native ranges");
    Check(cache.Capture(overwrite,token,[&](unsigned char* destination,std::size_t size){
        ++reads;Check(size==newBytes.size(),"direct reader receives only the exact retired span");
        std::memcpy(destination,newBytes.data(),size);return true;
    }),"complete direct read validates the native span without a scratch copy");
    Check(cache.Read(overwrite.identity.resource,overwrite.identity.device,overwrite.identity.kind,
        overwrite.identity.capacity,overwrite.offset,overwrite.bytes,out,&writer,&tick)&&out==newBytes&&writer==12&&tick==21,
        "successful direct capture preserves bytes and native provenance");
    Check(!cache.Capture(overwrite,token,[&](unsigned char* destination,std::size_t size){
        ++reads;std::memset(destination,99,size/2);return false;
    })&&!Read(cache,overwrite,out),"partial retry of an already successful token cannot expose damaged bytes");
    Check(Read(cache,Block(24,48),out)&&out==std::vector<unsigned char>(48,42)&&
        Read(cache,Block(168,96),out)&&out==std::vector<unsigned char>(96,42),"failed repeated capture preserves disjoint coverage");
    // Even an altered allocation selection under the same current token must
    // invalidate its own destination before a partial reader can change it.
    auto changedSpan=overwrite;changedSpan.offset=24;changedSpan.bytes=24;
    Check(!cache.Capture(changedSpan,token,[&](unsigned char* destination,std::size_t size){
        ++reads;std::memset(destination,99,size/2);return false;
    })&&!Read(cache,changedSpan,out)&&Read(cache,Block(48,24),out),"partial capture with a changed span cannot retain old destination authorization");
    const auto newer=cache.Begin(Block(300,24));(void)newer;
    const auto mustNotRead=[&](unsigned char*,std::size_t){++reads;return true;};
    Check(!cache.Capture(overwrite,token,mustNotRead)&&reads==4,"superseded token rejected before native read");
    auto wrong=Block(300,24);++wrong.identity.device;
    Check(!cache.Capture(wrong,newer,mustNotRead)&&reads==4,"foreign device rejected before native read");
    auto overflow=Block(4090,96);
    Check(!cache.Capture(overflow,newer,mustNotRead)&&reads==4,"out-of-bounds destination rejected before native read");
    cache.Clear();
    Check(!cache.Capture(Block(300,24),newer,mustNotRead)&&reads==4,"Reset destroys pending direct-reader authorization");
}
}
int main(){RingWrites();IdentityAndBounds();BoundedStorage();SourceProvenance();ScratchReuseAndFreshness();SelectiveCapture();DirectNativeRead();std::printf("native HUD shadow: %u checks PASS\n",checks);}
