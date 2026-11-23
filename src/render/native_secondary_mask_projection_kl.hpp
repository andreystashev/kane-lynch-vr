#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Pinned native4C1658..4C1725 emits a projection stack around the alternate-
// FOV geometry-flag16 mask9 layer. A source camera alone is not permission.
// Retain exact closed packet bytes, main camera origin and push/draw/pop.
namespace NativeSecondaryMaskProjectionKL {
constexpr unsigned MaximumPackages=256,MaximumScopes=8;
struct Certificate {
    bool valid=false;
    std::uintptr_t queue=0,queueBegin=0,begin=0,end=0,sourceEntry=0,sourceCamera=0;
    std::uintptr_t cameraPacket=0,pushPacket=0,projectionPacket=0,drawPacket=0,popPacket=0;
    std::array<unsigned char,0xB0> camera{};
    std::array<unsigned char,24> push{},pop{};
    std::array<unsigned char,88> projection{};
    std::array<unsigned char,24+MaximumPackages*4> draw{};
    unsigned drawBytes=0;
};
struct Batch {unsigned count=0,commands=0,maskCommands=0,rejected=0,lastReason=0;Certificate lastRejected;std::array<Certificate,MaximumScopes> scopes{};};
inline std::uint32_t Word(const void* data,unsigned offset){std::uint32_t value=0;std::memcpy(&value,static_cast<const unsigned char*>(data)+offset,4);return value;}
inline bool SameDepthLenses(const float* main,const float* alternate){
    if(!main||!alternate||main[0]<=0||main[5]<=0||alternate[0]<=0||alternate[5]<=0)return false;
    for(unsigned i=0;i<16;++i){
        if(!std::isfinite(main[i])||!std::isfinite(alternate[i]))return false;
        if(i!=0&&i!=5&&main[i]!=alternate[i])return false;
    }
    return main[8]==0&&main[9]==0&&main[10]==1&&main[11]==1&&main[14]<=-5&&main[15]==0;
}
template<class Reader>bool Capture(std::uintptr_t queue,std::uintptr_t queueBegin,std::uintptr_t begin,std::uintptr_t end,
    std::uintptr_t sourceEntry,std::uintptr_t sourceCamera,Reader read,Batch& output){
    Batch next;Certificate camera;bool mainProjection=false;
    if(!queue||!queueBegin||begin<queueBegin||end<=begin||end-begin>0x1000000||!sourceEntry||!sourceCamera)return false;
    const auto header=[&](std::uintptr_t at,std::uint32_t (&h)[6]){
        return at>=begin&&at<=end&&end-at>=24&&read(at,h,24)&&h[2]==queue&&std::uint64_t(h[5])*4<=end-at-24;
    };
    for(auto at=begin;at<end;){
        std::uint32_t h[6]{};if(++next.commands>65536||!header(at,h))return false;
        const auto size=24+std::uintptr_t(h[5])*4;
        if(h[0]==1){
            mainProjection=h[1]==sourceEntry&&h[3]&&h[4]==0&&h[5]==0x26;
            camera={};camera.cameraPacket=at;
            const bool saved=size>=camera.camera.size()&&read(at,camera.camera.data(),camera.camera.size());mainProjection=mainProjection&&saved;
        }else if(h[0]==0x12&&h[4]==9)++next.maskCommands;
        else if(h[0]==0x2C&&h[1]==0&&h[3]==0&&h[4]==0&&h[5]==0){
            std::uint32_t p[6]{},d[6]{},pop[6]{};const auto pAt=at+24;
            Certificate candidate=camera;candidate.pushPacket=at;candidate.projectionPacket=pAt;
            candidate.queue=queue;candidate.queueBegin=queueBegin;candidate.begin=begin;candidate.end=end;candidate.sourceEntry=sourceEntry;candidate.sourceCamera=sourceCamera;
            unsigned reason=1;
            if(header(pAt,p)&&p[0]==0x2B&&p[1]==0&&p[3]==0&&p[4]==0&&p[5]==16){
                const auto dAt=pAt+88;candidate.drawPacket=dAt;
                reason=2;
                if(header(dAt,d)&&d[0]==0x12&&d[1]==0&&d[3]==0&&d[4]==9&&d[5]>0&&d[5]<=MaximumPackages){
                    candidate.drawBytes=24+d[5]*4;const auto popAt=dAt+candidate.drawBytes;candidate.popPacket=popAt;
                    reason=3;
                    if(header(popAt,pop)&&pop[0]==0x2D&&pop[1]==0&&pop[3]==0&&pop[4]==0&&pop[5]==0&&
                       read(at,candidate.push.data(),24)&&read(pAt,candidate.projection.data(),88)&&
                       read(dAt,candidate.draw.data(),candidate.drawBytes)&&read(popAt,candidate.pop.data(),24)){
                        float base[16]{},alternate[16]{};std::memcpy(base,candidate.camera.data()+0x48,64);std::memcpy(alternate,candidate.projection.data()+24,64);
                        reason=!mainProjection?7:!SameDepthLenses(base,alternate)?5:6;
                        if(mainProjection&&SameDepthLenses(base,alternate)&&next.count<MaximumScopes){
                            candidate.valid=true;candidate.queue=queue;candidate.queueBegin=queueBegin;candidate.begin=begin;candidate.end=end;
                            candidate.sourceEntry=sourceEntry;candidate.sourceCamera=sourceCamera;next.scopes[next.count++]=candidate;
                            ++next.maskCommands;next.commands+=3;at=popAt+24;continue;
                        }
                    }
                }
            }
            if(d[0]==0x12&&d[4]==9){
                ++next.rejected;next.lastReason=d[5]>MaximumPackages?6:reason;
                read(at,candidate.push.data(),24);
                if(pAt<=end&&end-pAt>=88)read(pAt,candidate.projection.data(),88);
                if(candidate.drawPacket&&candidate.drawPacket<=end&&end-candidate.drawPacket>=24){
                    candidate.drawBytes=d[5]<=MaximumPackages?24+d[5]*4:24;
                    if(candidate.drawBytes<=end-candidate.drawPacket)read(candidate.drawPacket,candidate.draw.data(),candidate.drawBytes);
                }
                if(candidate.popPacket&&candidate.popPacket<=end&&end-candidate.popPacket>=24)read(candidate.popPacket,candidate.pop.data(),24);
                next.lastRejected=candidate;
            }
            mainProjection=false;
        }else if(h[0]==0x2B||h[0]==0x2C||h[0]==0x2D)mainProjection=false;
        at+=size;
    }
    output=next;return true;
}
template<class Reader>bool Match(const Certificate& certificate,std::uintptr_t packet,Reader read,bool pop=false){
    if(!certificate.valid||packet!=(pop?certificate.popPacket:certificate.projectionPacket))return false;
    std::uint32_t begin=0,cursor=0,capacity=0;
    if(!read(certificate.queue+0x14,&begin,4)||begin!=certificate.queueBegin||
       !read(certificate.queue+0xC,&cursor,4)||cursor<certificate.end||!read(certificate.queue+0x10,&capacity,4)||cursor>capacity)return false;
    std::array<unsigned char,24+MaximumPackages*4> bytes{};
    const auto same=[&](std::uintptr_t at,const void* expected,std::size_t count){return count<=bytes.size()&&read(at,bytes.data(),count)&&!std::memcmp(bytes.data(),expected,count);};
    return same(certificate.cameraPacket,certificate.camera.data(),certificate.camera.size())&&
        same(certificate.pushPacket,certificate.push.data(),24)&&same(certificate.projectionPacket,certificate.projection.data(),88)&&
        same(certificate.drawPacket,certificate.draw.data(),certificate.drawBytes)&&same(certificate.popPacket,certificate.pop.data(),24);
}
}
