#include "native_secondary_mask_projection_kl.hpp"
#include <cstdio>
#include <vector>
#include <limits>
namespace {
using namespace NativeSecondaryMaskProjectionKL;
unsigned checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000,queue=0x20000000,entry=0x30000000,camera=0x40000000;
    std::vector<unsigned char> data;std::uint32_t cursor=0,capacity=0,start=base;
    std::uintptr_t main=0,push=0,projection=0,draw=0,pop=0;
    void Set(std::uintptr_t at,unsigned offset,std::uint32_t value){std::memcpy(data.data()+at-base+offset,&value,4);}
    std::uintptr_t Add(unsigned kind,unsigned words=0,unsigned source=0,unsigned pass=0){auto at=base+data.size();data.resize(data.size()+24+words*4);
        Set(at,0,kind);Set(at,4,source);Set(at,8,queue);Set(at,16,pass);Set(at,20,words);return at;}
    Fixture(){
        main=Add(1,0x26,entry);Set(main,12,0x55550000);
        float p[16]{};p[0]=1.2f;p[5]=1.6f;p[10]=p[11]=1;p[14]=-5;
        std::memcpy(data.data()+main-base+0x48,p,64);
        push=Add(0x2C);projection=Add(0x2B,16);p[0]=2.1f;p[5]=2.8f;std::memcpy(data.data()+projection-base+24,p,64);
        draw=Add(0x12,2,0,9);Set(draw,24,0x66660000);Set(draw,28,0x77770000);pop=Add(0x2D);
        cursor=base+static_cast<std::uint32_t>(data.size());capacity=cursor+0x1000;
    }
    bool Read(std::uintptr_t at,void* out,std::size_t n)const{
        if(at==queue+0x14&&n==4){std::memcpy(out,&start,4);return true;}
        if(at==queue+0xC&&n==4){std::memcpy(out,&cursor,4);return true;}
        if(at==queue+0x10&&n==4){std::memcpy(out,&capacity,4);return true;}
        if(at<base||at-base>data.size()||n>data.size()-(at-base))return false;
        std::memcpy(out,data.data()+at-base,n);return true;
    }
    bool Capture(Batch& b)const{return NativeSecondaryMaskProjectionKL::Capture(queue,base,base,base+data.size(),entry,camera,
        [&](std::uintptr_t at,void* out,std::size_t n){return Read(at,out,n);},b);}
    bool Match(const Certificate& c,bool end=false)const{return NativeSecondaryMaskProjectionKL::Match(c,end?pop:projection,
        [&](std::uintptr_t at,void* out,std::size_t n){return Read(at,out,n);},end);}
};
}
int main(){
    Fixture f;Batch b;Check(f.Capture(b)&&b.count==1,"complete source-main push/alternate-P/mask9/pop certificate");const auto c=b.scopes[0];
    Check(c.sourceEntry==Fixture::entry&&c.sourceCamera==Fixture::camera&&c.projectionPacket==f.projection&&c.popPacket==f.pop,"source and exact packet identities retained");
    Check(f.Match(c)&&f.Match(c,true),"both consumer copy and pop require unchanged full closed scope");
    Check(c.drawBytes==32&&Word(c.draw.data(),16)==9&&Word(c.draw.data(),24)==0x66660000,"all actual selected material package pointers retained");
    for(auto at:{f.main,f.push,f.projection,f.draw,f.pop}){
        auto bad=f;bad.Set(at,8,0x12345678);Check(!bad.Capture(b),"foreign queue owner fails closed");
        Check(!bad.Match(c)&&!bad.Match(c,true),"consumer cannot borrow changed packet bytes on copy or pop");
    }
    for(auto field:{4u,12u,16u,20u}){
        auto bad=f;bad.Set(f.projection,field,1);Check(!bad.Capture(b)||b.count==0,"unknown projection packet identity/flags/length never qualifies");
    }
    for(auto pass:{1u,7u,10u,11u}){auto bad=f;bad.Set(f.draw,16,pass);Check(bad.Capture(b)&&!b.count,"ordinary, reflection, alpha and legacy Bloom passes remain native");}
    auto bad=f;bad.Set(f.main,4,0x77770000);Check(bad.Capture(b)&&!b.count,"other camera source cannot grant secondary projection");
    bad=f;bad.Set(f.pop,0,0x2C);Check(bad.Capture(b)&&!b.count,"missing exact native projection pop rejected");
    bad=f;bad.data.pop_back();Check(!bad.Capture(b),"truncated native pop cannot cross producer end");
    bad=f;bad.Set(f.projection,24+14*4,0);Check(bad.Capture(b)&&!b.count,"different depth intent rejected instead of global projection allowance");
    Check(b.rejected==1&&b.lastReason==5&&Word(b.lastRejected.projection.data(),24+14*4)==0&&
        Word(b.lastRejected.camera.data(),0x48+14*4)!=0,"rejected native lens still records actual main/alternate P before certification");
    bad=f;bad.Set(f.projection,24,std::numeric_limits<std::uint32_t>::max());Check(bad.Capture(b)&&!b.count,"nonfinite lens rejected");
    bad=f;bad.Set(f.draw,24,0x99990000);Check(!bad.Match(c),"later package replacement invalidates exact scope");
    bad=f;bad.start+=4;Check(!bad.Match(c),"queue reallocation cannot borrow producer certificate");
    bad=f;bad.cursor=static_cast<std::uint32_t>(f.pop);Check(!bad.Match(c),"queue rewind/reuse retires even old matching addresses");
    bad=f;bad.capacity=bad.cursor-1;Check(!bad.Match(c),"corrupt queue capacity rejected");
    bad=f;bad.data.resize(f.draw-Fixture::base);bad.draw=bad.Add(0x12,MaximumPackages+1,0,9);bad.pop=bad.Add(0x2D);bad.cursor=Fixture::base+bad.data.size();bad.capacity=bad.cursor+0x1000;
    Check(bad.Capture(b)&&!b.count&&b.rejected==1&&b.lastReason==6&&Word(b.lastRejected.draw.data(),20)==MaximumPackages+1,
        "bounded package limit still records actual overflowing native header");
    bad=f;bad.Set(f.main,4,0x77770000);Check(bad.Capture(b)&&b.lastReason==7&&Word(b.lastRejected.camera.data(),4)==0x77770000,
        "wrong actual main camera identity is retained as rejected source evidence");
    Check(f.Capture(b)&&b.count==1,"unrelated failed captures do not corrupt later valid source");
    std::printf("Secondary native mask projection provenance: %u/%u PASS; CPU only.\n",checks-failed,checks);return failed?1:0;
}
