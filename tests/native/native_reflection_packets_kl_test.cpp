// Exact production queue parser on private CPU fixtures. Native packet order
// and sizes come from the pinned reflector's four producers; matrix values
// are synthetic because the retained gameplay log has no raw camera packets.
#include "native_reflection_packets_kl.hpp"
#include <cstdio>
#include <vector>
#include <limits>
namespace {
using namespace NativeReflectionPacketsKL;
unsigned checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000,queue=0x20000000,source=0x30000000,reflector=0x40000000,view=0x50000000;
    std::vector<unsigned char> bytes;
    std::array<std::uintptr_t,4> cameras{};
    void Word(std::uintptr_t address,unsigned offset,std::uint32_t x){std::memcpy(bytes.data()+address-base+offset,&x,4);}
    std::uintptr_t Add(unsigned kind,unsigned words=0,std::uintptr_t entry=0){
        const auto address=base+bytes.size();bytes.resize(bytes.size()+24+words*4);
        Word(address,0,kind);Word(address,4,static_cast<std::uint32_t>(entry));
        Word(address,8,queue);Word(address,12,view);Word(address,20,words);return address;
    }
    Fixture(){
        Add(0x20);Add(0x13,4);
        for(unsigned n=0;n<4;++n){
            Add(n==1?0x17:0x16);cameras[n]=Add(1,0x26,reflector);
            // First/third native VIEW and P restore exactly; second/fourth
            // mirror VIEW matches while the oblique and authored P may differ.
            for(unsigned k=0;k<0x70;k+=4)Word(cameras[n],0x18+k,k+((n&1)?100:0));
            if(n==3)Word(cameras[n],0x48,999);
            Add(n==1?0x16:0x1e);Add(0x12,8);
        }
    }
    bool Read(std::uintptr_t address,void* data,std::size_t count)const{
        if(address<base||address-base>bytes.size()||count>bytes.size()-(address-base))return false;
        std::memcpy(data,bytes.data()+address-base,count);return true;
    }
    bool Capture(Certificate& c)const{return NativeReflectionPacketsKL::Capture(queue,base,base+bytes.size(),source,reflector,view,
        [&](std::uintptr_t address,void* data,std::size_t count){return Read(address,data,count);},c);}
};
}
int main(){
    Fixture f;Certificate certificate;
    Check(f.Capture(certificate),"verified producer four-camera sequence accepted");
    for(unsigned n=0;n<4;++n)Check(Match(certificate,f.cameras[n],f.bytes.data()+f.cameras[n]-Fixture::base,PacketBytes)==int(n),"exact packet identity and complete payload match phase");
    const auto saved=certificate;
    auto changed=f;changed.Word(changed.cameras[1],0x48,123456);
    Check(Match(certificate,changed.cameras[1],changed.bytes.data()+changed.cameras[1]-Fixture::base,PacketBytes)<0,"same packet pointer with changed native lens is not certified");
    Check(Match(certificate,changed.cameras[1]+4,changed.bytes.data()+changed.cameras[1]-Fixture::base,PacketBytes)<0,"interior pointer cannot borrow provenance");
    for(unsigned phase=0;phase<4;++phase){
        auto bad=f;bad.Word(bad.cameras[phase],4,0x66666666);
        Check(!bad.Capture(certificate),"different queued reflector entry rejected");
    }
    for(unsigned field: {8u,12u,16u,20u}){
        auto bad=f;bad.Word(bad.cameras[1],field,0x12345678);
        Check(!bad.Capture(certificate),"owner, view, flags or payload length substitution rejected");
    }
    auto bad=f;bad.Word(bad.cameras[2],0x18,789);
    Check(!bad.Capture(certificate),"main-camera restore with a different native VIEW rejected");
    bad=f;bad.Word(bad.cameras[3],0x18,789);
    Check(!bad.Capture(certificate),"final reflector camera with a different mirrored VIEW rejected");
    bad=f;bad.Add(1,0x26,Fixture::source);
    Check(!bad.Capture(certificate),"nested or changed fifth camera fails closed");
    bad=f;bad.bytes.pop_back();Check(!bad.Capture(certificate),"truncated command cannot cross queue end");
    bad=f;bad.Word(Fixture::base,20,std::numeric_limits<std::uint32_t>::max());
    Check(!bad.Capture(certificate),"overflowed word count rejected before address arithmetic");
    Check(!std::memcmp(&certificate,&saved,sizeof(saved)),"failed capture leaves established certificate unchanged");
    Check(!NativeReflectionPacketsKL::Capture(Fixture::queue,Fixture::base,Fixture::base+0x1000001,Fixture::source,Fixture::reflector,Fixture::view,
        [&](std::uintptr_t address,void* data,std::size_t count){return f.Read(address,data,count);},certificate),"unbounded producer span rejected");
    // An inner verified scope remains independently certifiable, while an
    // outer scope containing extra cameras cannot inherit the inner identity.
    Fixture inner;Certificate innerCertificate;Check(inner.Capture(innerCertificate),"nested inner producer owns a separate complete certificate");
    Trace trace;const auto captureTrace=[&](const Fixture& fixture){return NativeReflectionPacketsKL::Capture(Fixture::queue,Fixture::base,
        Fixture::base+fixture.bytes.size(),Fixture::source,Fixture::reflector,Fixture::view,
        [&](std::uintptr_t address,void* data,std::size_t count){return fixture.Read(address,data,count);},certificate,&trace);};
    Check(captureTrace(f)&&trace.reason==Failure::None&&trace.cameras==4&&trace.observed.packets[0].Word(4)==Fixture::reflector,
        "accepted producer trace records four actual reflector identities independently of source argument");
    auto oldShape=f;for(unsigned n=0;n<3;++n)oldShape.Word(oldShape.cameras[n],4,Fixture::source);
    Check(!captureTrace(oldShape)&&trace.reason==Failure::Entry&&trace.cameras==1&&trace.header[1]==Fixture::source&&
        trace.observed.packets[0].Word(4)==Fixture::source&&trace.observed.sourceEntry==Fixture::source&&trace.observed.reflector==Fixture::reflector,
        "old mistaken camera-entry shape is rejected with coherent offending raw packet evidence");
    std::printf("reflection packet provenance: %u/%u\n",checks-failed,checks);return failed?1:0;
}
