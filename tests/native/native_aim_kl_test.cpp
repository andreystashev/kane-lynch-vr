#include "native_aim_kl_math.hpp"
#include "native_aim_kl_image.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL: %s\n",message);}}
namespace M=NativeAimKLMath;
using namespace NativeCameraMath;
bool Near(Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<.0001f;}
NativeWeaponKL::AimFrame Pose(float units=100.f) {
    NativeWeaponKL::AimFrame p;p.player=0x10000;p.weapon=0x20000;p.root=0x30000;p.model=0x40000;
    p.pairId=1;p.tick=1000;p.recenterAck=3;p.hand=1;p.unitsPerMetre=units;p.origin={10,20,30};p.direction={0,0,2};return p;
}
void Math() {
    for(float units:{1.f,100.f,1000.f}) {
        const auto p=Pose(units);M::Query before;
        Check(M::Prepare(p,1010,before)&&Near(before.start,p.origin)&&Near(before.delta,{0,0,100.f*units}),"current visible muzzle prepares normalized 100 metre native ray");
        Check(before.flags==1&&before.reference==0&&before.fraction==1,"query initializer matches native fresh ray defaults");
        for(float fraction:{0.f,.001f,.01f,.5f,1.f}) {
            auto after=before;after.fraction=fraction;Vec3 hit{99,99,99};
            const auto expected=Add(p.origin,Scale(before.delta,fraction));
            Check(M::Hit(before,after,1,hit)&&Near(hit,expected),"native collision fraction determines endpoint");
            Check(M::Match(p,1010,hit,p,1010),"same current ray accepts real collision endpoint");
        }
        auto after=before;after.fraction=.5f;Vec3 out{1,2,3};
        Check(!M::Hit(before,after,0,out)&&Near(out,{1,2,3}),"miss cannot generate endpoint or overwrite caller output");
        Check(!M::Hit(before,after,0x100,out),"only native low-byte success authorizes hit");
        after.start.x+=1;Check(!M::Hit(before,after,1,out),"native mutation of ray origin rejects query");after=before;
        after.delta.y+=1;Check(!M::Hit(before,after,1,out),"native mutation of ray direction rejects query");after=before;
        for(float invalid:{-.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            after.fraction=invalid;Check(!M::Hit(before,after,1,out),"invalid native collision fraction rejected");
        }
        const auto hit=Add(p.origin,Scale(before.delta,.5f));auto current=p;current.tick=1016;current.pairId=2;
        Check(M::Match(p,1010,hit,current,1020),"fresh certificate survives a new render pair only while source and ray still match");
        current.origin.x+=.02f*units;Check(!M::Match(p,1010,hit,current,1020),"hand movement over one centimetre retires endpoint");
        current=p;current.direction={.01f,0,1};Check(!M::Match(p,1010,hit,current,1020),"rotation moving current ray away from hit retires endpoint");
        Check(!M::Match(p,1010,hit,p,1051),"old muzzle is rejected despite a recent endpoint");
        current=p;current.tick=1060;Check(!M::Match(p,1010,hit,current,1061),"endpoint expires independently of new tracking");
        Check(!M::Match(p,1010,Subtract(p.origin,{0,0,units}),p,1010),"target behind muzzle rejected");
        Check(!M::Match(p,1010,Add(p.origin,{0,0,101.f*units}),p,1010),"target outside query range rejected");
        for(unsigned change=0;change<7;++change) {
            current=p;switch(change){case 0:++current.player;break;case 1:++current.weapon;break;case 2:++current.root;break;
                case 3:++current.model;break;case 4:++current.recenterAck;break;case 5:current.hand=0;break;default:current.unitsPerMetre*=2;break;}
            Check(!M::Match(p,1010,hit,current,1010),"owner/model/root/controller/recenter/scale change retires certificate");
        }
    }
    for(unsigned change=0;change<11;++change) {
        auto p=Pose();switch(change){case 0:p.player=0;break;case 1:p.weapon=0;break;case 2:p.root=0;break;case 3:p.model=0;break;
            case 4:p.pairId=0;break;case 5:p.tick=0;break;case 6:p.hand=-1;break;case 7:p.hand=2;break;
            case 8:p.direction={};break;case 9:p.origin.x=std::numeric_limits<float>::infinity();break;default:p.unitsPerMetre=0;break;}
        M::Query out;Check(!M::Prepare(p,1010,out),"invalid visible weapon pose cannot invoke native collision");
    }
    auto wrap=Pose();wrap.tick=UINT32_MAX-10;Check(M::Valid(wrap,5),"fresh pose tick handles GetTickCount wrap");
    Check(!NativeAimKLImage::Verify(0x400000,[](std::uintptr_t,void*,std::size_t){return false;}),"missing native image proof rejects activation");
}
void Image(const char* path) {
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
    const auto u16=[&](std::size_t at){std::uint16_t x=0;if(at+2<=raw.size())std::memcpy(&x,raw.data()+at,2);return x;};
    const auto u32=[&](std::size_t at){std::uint32_t x=0;if(at+4<=raw.size())std::memcpy(&x,raw.data()+at,4);return x;};
    const auto pe=u32(0x3c),opt=pe+24;
    if(raw.size()<0x100||u16(0)!=0x5a4d||u32(pe)!=0x4550||u16(opt)!=0x10b){Check(false,"read actual supported PE32 image");return;}
    const auto base=u32(opt+28),size=u32(opt+56),headers=u32(opt+60);
    if(!size||size>0x2000000||headers>raw.size()||headers>size){Check(false,"bounded PE image layout");return;}
    std::vector<unsigned char> mapped(size);std::memcpy(mapped.data(),raw.data(),headers);
    const auto section=opt+u16(pe+20);
    for(unsigned i=0;i<u16(pe+6);++i) {
        const auto at=section+i*40,rva=u32(at+12),bytes=u32(at+16),offset=u32(at+20);
        if(std::uint64_t(rva)+bytes>mapped.size()||std::uint64_t(offset)+bytes>raw.size()){Check(false,"bounded PE section");return;}
        std::memcpy(mapped.data()+rva,raw.data()+offset,bytes);
    }
    const auto read=[&](std::uintptr_t at,void* out,std::size_t bytes){if(at<base||at-base>mapped.size()||bytes>mapped.size()-(at-base))return false;
        std::memcpy(out,mapped.data()+at-base,bytes);return true;};
    Check(NativeAimKLImage::Verify(base,read),"actual KL1 ROOM/query/projectile/backend/vtable bytes match");
    for(auto rva:{0x110830u,0x11098eu,0x1fa20u,0x150dceu,0x1fb290u,0x1fc196u,0xc3b20u,0x54a998u,0x54aca0u,0x54b618u,0x575f60u,0x5411f0u,0x5412a0u}) {
        mapped[rva]^=1;Check(!NativeAimKLImage::Verify(base,read),"a changed native collision signature or vtable rejects activation");mapped[rva]^=1;
    }
}
}
int main(int argc,char** argv) {Math();if(argc==2)Image(argv[1]);std::printf("KL1 aim preview: %u checks, %u failures%s.\n",checks,failures,argc==2?", actual PE inspected":"; no game or runtime opened");return failures?1:0;}
