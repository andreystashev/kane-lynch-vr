#include "native_grenade_kl_math.hpp"
#include "native_grenade_kl_image.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

namespace {
using namespace NativeCameraMath;
namespace M=NativeGrenadeKLMath;
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
bool Near(Vec3 a,Vec3 b,float epsilon=.002f){const auto d=Subtract(a,b);return Dot(d,d)<epsilon*epsilon;}
float Length(Vec3 value){return std::sqrt(Dot(value,value));}
NativeWeaponKL::HeadGaze Gaze() {
    NativeWeaponKL::HeadGaze g;g.player=0x11000;g.sourceCamera=0x22000;g.pairId=25;g.tick=1000;
    g.origin={100,170,500};g.direction={1,0,0};g.mounted=true;return g;
}
struct FileImage {
    std::vector<unsigned char> data;
    struct Section {std::uint32_t virtualSize,rva,rawSize,raw;};std::vector<Section> sections;
    template<class T> bool Get(std::size_t at,T& output)const {
        if(at>data.size()||sizeof(T)>data.size()-at)return false;std::memcpy(&output,data.data()+at,sizeof(T));return true;
    }
    bool Load(const char* path) {
        std::ifstream in(path,std::ios::binary);if(!in)return false;
        in.seekg(0,std::ios::end);const auto size=in.tellg();if(size<=0||size>64*1024*1024)return false;
        data.resize(static_cast<std::size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(data.data()),size);if(!in)return false;
        std::uint32_t pe=0,signature=0;std::uint16_t count=0,optional=0,magic=0;
        if(!Get(0x3C,pe)||!Get(pe,signature)||signature!=0x4550||!Get(pe+6,count)||count>96||
           !Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10B)return false;
        for(unsigned i=0;i<count;++i){Section s{};if(!Get(pe+24+optional+40*i+8,s))return false;sections.push_back(s);}return true;
    }
    bool Read(std::uintptr_t address,void* output,std::size_t size)const {
        if(address<0x400000)return false;const auto rva=address-0x400000;
        for(const auto& s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize) {
            const auto offset=rva-s.rva;
            if(size>s.rawSize-offset||s.raw+offset>data.size()||size>data.size()-s.raw-offset)return false;
            std::memcpy(output,data.data()+s.raw+offset,size);return true;
        }
        return false;
    }
};
void Certificates() {
    auto g=Gaze();Check(M::ValidGaze(g,1250),"actual rendered gaze is eligible through the bounded freshness window");
    Check(!M::ValidGaze(g,1251),"stale HMD direction is not a grenade aim certificate");
    for(unsigned field=0;field<9;++field) {
        auto bad=g;
        switch(field){case 0:bad.player=0;break;case 1:bad.sourceCamera=0;break;case 2:bad.mounted=false;break;
        case 3:bad.tick=0;break;case 4:bad.unitsPerMetre=0;break;case 5:bad.unitsPerMetre=1001;break;
        case 6:bad.origin.x=std::numeric_limits<float>::quiet_NaN();break;case 7:bad.direction={};break;
        default:bad.direction.z=std::numeric_limits<float>::infinity();break;}
        Check(!M::ValidGaze(bad,1000),"missing owner/camera/runtime, malformed coordinates or scale rejects gaze");
    }
    g.tick=0xfffffff0u;Check(M::ValidGaze(g,234)&&!M::ValidGaze(g,235),"freshness expiry remains correct across GetTickCount wrap");
}
void DirectionAndCharge() {
    const auto gaze=Gaze();const Vec3 origin{110,160,505},nativeAim{0,0,1};
    Vec3 oldDirection;Check(M::Unit({.1f,.3f,1},oldDirection),"native throw direction includes existing lateral/ballistic adjustment");
    for(float distance:{3.f,20.f,100.f,4000.f}) {
        const auto target=Add(origin,Scale(oldDirection,distance));
        for(int yaw=-180;yaw<=180;yaw+=30)for(int pitch=-80;pitch<=80;pitch+=20) {
            const float y=yaw*.01745329252f,p=pitch*.01745329252f;
            auto head=gaze;head.direction={std::cos(p)*std::cos(y),std::sin(p),std::cos(p)*std::sin(y)};
            Vec3 mapped;Check(M::RemapTarget(origin,target,nativeAim,head,head.direction,mapped),"owned native throw remaps through full HMD yaw and pitch sweep");
            const auto delta=Subtract(mapped,origin);Basis oldBasis,newBasis;
            Check(M::AimBasis(nativeAim,nativeAim,oldBasis)&&M::AimBasis(head.direction,head.direction,newBasis),"both aim frames use the same native F/U/R convention");
            Check(std::fabs(Length(delta)-distance)<std::max(.001f,distance*.00001f),"charge-controlled native throw length is preserved");
            Check(std::fabs(Dot(delta,newBasis.right))<std::max(.001f,distance*.00001f)&&
                  std::fabs(Dot(delta,newBasis.up)-Dot(Subtract(target,origin),oldBasis.up))<std::max(.001f,distance*.00001f),
                  "native ballistic loft remains while the release stays on the new pointing plane");
            auto straight=Add(origin,Scale(nativeAim,distance));
            Check(M::RemapTarget(origin,straight,nativeAim,head,head.direction,mapped)&&
                  Near(Subtract(mapped,origin),Scale(head.direction,distance),std::max(.002f,distance*.00001f)),
                  "without native loft the throw points exactly where the HMD looks, independent of actor/controller yaw");
        }
    }
    auto head=gaze;head.direction={0,1,0};Vec3 output;
    Check(M::RemapTarget(origin,Add(origin,Scale(oldDirection,400)),nativeAim,head,{1,0,0},output),
          "vertical HMD gaze retains the last useful horizontal reference instead of inventing sideways rotation");
    const auto target=Add(origin,Scale(oldDirection,400));const Vec3 shift{-10000,1200,4000};
    Vec3 a,b;Check(M::RemapTarget(origin,target,nativeAim,gaze,gaze.direction,a),"translated baseline remaps");
    head=gaze;head.origin=Add(head.origin,shift);
    Check(M::RemapTarget(Add(origin,shift),Add(target,shift),nativeAim,head,head.direction,b)&&Near(Add(a,shift),b,.003f),
          "world translation never changes the grenade origin or resulting aim");
    const Vec3 sentinel{7,8,9};
    for(unsigned field=0;field<7;++field) {
        auto o=origin,t=target,n=nativeAim;head=gaze;output=sentinel;
        switch(field){case 0:t=o;break;case 1:n={};break;case 2:o.x+=400;break;case 3:head.unitsPerMetre=0;break;
        case 4:t.z=std::numeric_limits<float>::quiet_NaN();break;case 5:head.direction={};break;default:t.z+=100000;break;}
        Check(!M::RemapTarget(o,t,n,head,head.direction,output)&&Near(output,sentinel),"invalid native origin/target/gaze rejects atomically and preserves native path");
    }
}
void ActualImage(const char* path) {
    FileImage image;Check(image.Load(path),"supported PE32 loads read-only without starting the game");
    const auto read=[&](auto at,void* out,auto size){return image.Read(at,out,size);};
    Check(NativeGrenadeKLImage::Verify(0x400000,read),"actual PE proves owned Throw update, exact SetTarget return, native origin, Fire, aim source and grenade types");
    for(std::uintptr_t address:{0x56665Cu+0x10,0x577B74u+0x2D4,0x577B74u+0x344,0x1FE810u,0x1FEC3Eu,
        0x1FEE05u,0x1FEE16u,0x1E7910u,0x1FEC87u,0x65E3C4u,0x65E3C8u,0x584D9Cu,0x65E3D0u,0x65E3D4u,0x584D90u,
        0x1E75F4u,0x1E7602u,0x22FE80u,0x2338B0u,0x2338B2u,0x233495u,0x233497u}) {
        const auto corrupt=[&](auto at,void* out,auto size){if(!read(at,out,size))return false;
            if(at==0x400000+address)static_cast<unsigned char*>(out)[0]^=1;return true;};
        Check(!NativeGrenadeKLImage::Verify(0x400000,corrupt),"changed actual native call/layout/type evidence disables only the grenade hook");
    }
}
}
int main(int argc,char** argv) {
    Certificates();DirectionAndCharge();if(argc>1)ActualImage(argv[1]);
    std::printf("KL1 grenade HMD aim: %u checks, %u failures; no game/headset or input opened.\n",checks,failures);
    return failures?1:0;
}
