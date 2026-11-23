#include "native_effects_math_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using namespace NativeEffectsKLMath;
namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
CameraPose Eye(float yaw,float pitch,float roll,Vec3 p){
    const Vec3 f{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vec3 r{std::cos(yaw),0,-std::sin(yaw)},u=Cross(f,r);
    return {{f,Subtract(Scale(u,std::cos(roll)),Scale(r,std::sin(roll))),Add(Scale(r,std::cos(roll)),Scale(u,std::sin(roll)))},p};
}
Vec3 World(const CameraPose& eye,Vertex p){return Add(eye.position,Add(Add(Scale(eye.basis.right,p.x),Scale(eye.basis.up,p.y)),Scale(eye.basis.forward,p.z)));}
bool Near(Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<.00001f;}
}
int main(){
    const auto reference=Eye(.3f,.1f,-.2f,{10,20,30});const Vec3 viewer{20,30,40};
    for(const Vec3 centre: {Vec3{700,160,900},Vec3{-400,30,100},Vec3{20,700,40},viewer}){
        Vertex expected[4]{};bool have=false;
        for(float separation:{-3.2f,0.f,3.2f})for(float yaw:{-1.4f,.3f,2.f}){
            auto eye=Eye(yaw,.4f,-.3f,Add(viewer,{separation,0,0}));
            const auto c=ToView(eye.basis,Subtract(centre,eye.position));
            Vertex input[4]{{c.x-12,c.y+7,c.z,0x87654321u,.1f,.2f},{c.x+12,c.y+7,c.z,0x87654322u,.8f,.2f},
                {c.x+12,c.y-7,c.z,0x87654323u,.8f,.9f},{c.x-12,c.y-7,c.z,0x87654324u,.1f,.9f}},output[4];
            Check(FacingParticleQuad(input,eye,reference,viewer,output),"ordinary and vertical smoke billboards have a finite facing frame");
            Vec3 worldCentre{};for(unsigned i=0;i<4;++i){
                Check(output[i].colour==input[i].colour&&!std::memcmp(&output[i].u,&input[i].u,8),"authored RGBA and animated texture coordinates remain byte-exact");
                const auto world=World(eye,output[i]);worldCentre=Add(worldCentre,Scale(world,.25f));
                if(have)Check(Near(world,World(reference,expected[i])),"both eyes and arbitrary head rotations share identical world smoke corners");
                else {const auto p=ToView(reference.basis,Subtract(world,reference.position));expected[i]=output[i];expected[i].x=p.x;expected[i].y=p.y;expected[i].z=p.z;}
            }
            have=true;Check(Near(worldCentre,centre),"spherical billboarding leaves native source centre stationary");
            const auto edge=Subtract(World(eye,output[1]),World(eye,output[0]));Check(std::fabs(Dot(edge,edge)-576.f)<.02f,"authored particle width is preserved");
        }
    }
    Vertex batch[8]{{-1,1,10,0x12345678u,0,0},{1,1,10,0x12345678u,1,0},{1,-1,10,0x12345678u,1,1},{-1,-1,10,0x12345678u,0,1}};
    std::memcpy(batch+4,batch,4*sizeof(Vertex));batch[7].x=std::numeric_limits<float>::quiet_NaN();
    Vertex original[8];std::memcpy(original,batch,sizeof(batch));
    Check(!MappedFacingParticleQuads(batch,2,reference,reference,viewer)&&!std::memcmp(batch,original,sizeof(batch)),"invalid later particle rejects entire borrowed batch without partial native writes");
    Check(!MappedFacingParticleQuads(batch,Detail::MappedQuadLimit+1,reference,reference,viewer),"unproven particle count cannot read beyond native live span");
    Check(!FacingParticleQuad(batch,reference,reference,{std::numeric_limits<float>::infinity(),0,0},batch),"nonfinite viewer rejects without writing a particle");
    std::printf("Position-facing smoke: %u checks PASS; stereo/head-turn/vertical/centre/attributes/live bounds.\n",checks);
}
