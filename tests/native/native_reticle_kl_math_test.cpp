#include "native_reticle_kl_math.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace NativeCameraMath;
unsigned checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<.00002f;}
std::array<float,16> NativeRows(){return {1.2f,0,0,0,0,1.7f,0,0,0,0,1,-5,0,0,1,0};}
Vec3 Clip(const NativeHudMath::Vertex& vertex,const float* rows){
    const float w=vertex.x*rows[12]+vertex.y*rows[13]+vertex.z*rows[14]+rows[15];
    return {(vertex.x*rows[0]+vertex.y*rows[1]+vertex.z*rows[2]+rows[3])/w,
        (vertex.x*rows[4]+vertex.y*rows[5]+vertex.z*rows[6]+rows[7])/w,
        (vertex.x*rows[8]+vertex.y*rows[9]+vertex.z*rows[10]+rows[11])/w};
}
std::array<NativeHudMath::Vertex,12> AuthoredReticle(){
    std::array<NativeHudMath::Vertex,12> vertices{};
    for(unsigned arm=0;arm<3;++arm){
        const float angle=float(arm)*6.2831853072f/3.f;
        for(unsigned corner=0;corner<4;++corner){
            const float x=(corner&1)?.04f:-.04f,y=.15f+((corner&2)?.04f:-.04f);
            vertices[arm*4+corner]={.2f+x*std::cos(angle)-y*std::sin(angle),-.1f+x*std::sin(angle)+y*std::cos(angle),
                6,0x40808080u+corner,float(corner&1),float((corner>>1)&1)};
        }
    }
    return vertices;
}
void AlignmentAndPreservation(){
    const auto authored=AuthoredReticle(),source=authored;const auto native=NativeRows(),original=native;
    for(float yaw:{-2.f,-.7f,0.f,.8f,2.7f})for(float distance:{20.f,100.f,1000.f}){
        CameraPose centre;centre.position={120,180,-70};
        centre.basis.forward={std::cos(yaw),0,std::sin(yaw)};
        centre.basis.right={std::sin(yaw),0,-std::cos(yaw)};
        const Vec3 target=Add(centre.position,Add(Scale(centre.basis.forward,distance),Add(Scale(centre.basis.right,10),Scale(centre.basis.up,5))));
        for(float eyeOffset:{-3.2f,3.2f}){
            CameraPose eye=centre;eye.position=Add(centre.position,Scale(centre.basis.right,eyeOffset));
            float projection[16]{};projection[0]=1.1f;projection[5]=1.4f;projection[8]=eyeOffset<0?.13f:-.11f;
            projection[9]=.02f;projection[10]=1;projection[11]=1;projection[14]=-.5f;
            float output[16]{};
            Check(NativeReticleKLMath::Translate(native.data(),authored.data(),authored.size(),eye,projection,target,output),"original native three-arm reticle relocates for each actual eye");
            Vec3 mean{};
            for(const auto& vertex:authored)mean=Add(mean,Scale(Clip(vertex,output),1.f/float(authored.size())));
            const auto d=Subtract(target,eye.position);
            // Reconstruct the world point from the resulting eye ray. This
            // checks binocular convergence against a common target directly.
            const float tangentX=(mean.x-projection[8])/projection[0],tangentY=(mean.y-projection[9])/projection[5];
            const float depth=Dot(d,eye.basis.forward);
            const auto reconstructed=Add(eye.position,Add(Scale(eye.basis.forward,depth),Add(Scale(eye.basis.right,tangentX*depth),Scale(eye.basis.up,tangentY*depth))));
            Check(std::fabs(reconstructed.x-target.x)<.001f&&std::fabs(reconstructed.y-target.y)<.001f&&std::fabs(reconstructed.z-target.z)<.001f,
                  "left and right reticle rays reconstruct the shared target within floating-point world precision");
            for(unsigned index=1;index<authored.size();++index){
                const auto before=Subtract(Clip(authored[index],native.data()),Clip(authored[0],native.data()));
                const auto after=Subtract(Clip(authored[index],output),Clip(authored[0],output));
                Check(Near(before.x*native[5]/native[0],after.x*projection[5]/projection[0])&&
                      Near(before.y,after.y)&&Near(before.z,after.z),
                      "native aspect and spread are preserved in eye angles");
            }
            Check(!std::memcmp(output+8,native.data()+8,8*sizeof(float)),"native depth and clip-W rows remain byte exact");
        }
    }
    Check(!std::memcmp(authored.data(),source.data(),sizeof(source)),"native vertex positions, UV and colour remain read only");
    Check(native==original,"original shader constants remain read only");
}
void Rejects(){
    auto vertices=AuthoredReticle();auto native=NativeRows();CameraPose eye;
    float projection[16]{};projection[0]=projection[5]=projection[10]=projection[11]=1;projection[14]=-.5f;
    const auto target=Scale(eye.basis.forward,100.f);
    const auto reject=[&](const float* rows,const NativeHudMath::Vertex* data,std::size_t count,const CameraPose& pose,const float* p,Vec3 aim){
        float output[16];for(float& value:output)value=1234.f;
        Check(!NativeReticleKLMath::Translate(rows,data,count,pose,p,aim,output),"invalid, foreign or unavailable reticle geometry is rejected");
        for(float value:output)Check(value==1234.f,"rejection leaves every output row unchanged");
    };
    reject(nullptr,vertices.data(),12,eye,projection,target);
    reject(native.data(),nullptr,12,eye,projection,target);
    reject(native.data(),vertices.data(),0,eye,projection,target);
    reject(native.data(),vertices.data(),11,eye,projection,target);
    reject(native.data(),vertices.data(),36,eye,projection,target);
    reject(native.data(),vertices.data(),12,eye,nullptr,target);
    reject(native.data(),vertices.data(),12,eye,projection,Scale(target,-1));
    reject(native.data(),vertices.data(),12,eye,projection,eye.position);
    auto invalidEye=eye;invalidEye.basis.right={};reject(native.data(),vertices.data(),12,invalidEye,projection,target);
    vertices[0].z=9;reject(native.data(),vertices.data(),12,eye,projection,target);vertices=AuthoredReticle();
    vertices[0].u=std::numeric_limits<float>::quiet_NaN();reject(native.data(),vertices.data(),12,eye,projection,target);vertices=AuthoredReticle();
    native[3]=1;reject(native.data(),vertices.data(),12,eye,projection,target);native=NativeRows();
    projection[11]=0;reject(native.data(),vertices.data(),12,eye,projection,target);
}
void ExactSources(){
    Check(NativeReticleKLMath::CopiedStride(5)==40&&NativeReticleKLMath::CopiedStride(6)==116,"KL1 copied material geometry strides match its own jump table");
    for(unsigned kind:{0u,1u,2u,3u,4u,7u,8u})Check(!NativeReticleKLMath::CopiedStride(kind),"borrowed/foreign particle layouts do not acquire crosshair authorization");
    Check(NativeReticleKLMath::Emitter(0x254660,10)&&NativeReticleKLMath::Emitter(0x2546C2,9),"only the exact primary and overlaid KL1 reticle sprites qualify");
    Check(!NativeReticleKLMath::Emitter(0x254883,10)&&!NativeReticleKLMath::Emitter(0x254660,9)&&!NativeReticleKLMath::Emitter(0x254661,10)&&!NativeReticleKLMath::Emitter(0,10),
          "five-bar meter, wrong producer return and wrong sprite type cannot become a reticle");
}
}
int main(){AlignmentAndPreservation();Rejects();ExactSources();std::printf("KL1 original reticle: %u checks passed.\n",checks);}
