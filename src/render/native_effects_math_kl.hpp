#pragma once
#include "native_camera_math.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace NativeEffectsKLMath {
using namespace NativeCameraMath;
struct Vertex {float x=0,y=0,z=0;std::uint32_t colour=0;float u=0,v=0;};
static_assert(sizeof(Vertex)==24,"pinned world sprite vertex stride");
struct ManualVertex {float x=0,y=0,z=0;std::uint32_t colour=0,index=0;float u=0,v=0;};
static_assert(sizeof(ManualVertex)==28,"pinned ManualScatter vertex stride");
namespace Detail {
inline constexpr unsigned MappedQuadLimit=2048;
// Native dynamic buffers can use PAGE_WRITECOMBINE. Repeated scalar reads
// from that memory are expensive even while its original Lock is valid.
// One bounded cache per thread is shared by all three vertex writers. Only
// the proven live span is read; no heap allocation or large stack frame is
// introduced per draw. Nothing retains or exposes a pointer to this cache.
alignas(16) inline thread_local unsigned char mappedScratch[MappedQuadLimit*4*sizeof(ManualVertex)];
template<class V,class Transform> bool MappedBatch(void* mapped,std::size_t bytes,Transform transform) {
    static_assert(MappedQuadLimit*4*sizeof(V)<=sizeof(mappedScratch),"bounded native vertex layout");
    if(!mapped||!bytes||bytes>MappedQuadLimit*4*sizeof(V)||bytes%(4*sizeof(V)))return false;
    std::memcpy(mappedScratch,mapped,bytes);
    for(std::size_t offset=0;offset<bytes;offset+=4*sizeof(V)){
        V quad[4];std::memcpy(quad,mappedScratch+offset,sizeof(quad));
        if(!transform(quad))return false;
        std::memcpy(mappedScratch+offset,quad,sizeof(quad));
    }
    // All quads passed validation before any native bytes are overwritten.
    std::memcpy(mapped,mappedScratch,bytes);return true;
}
}
inline Vec3 ToView(const Basis& world,Vec3 value){return {Dot(world.right,value),Dot(world.up,value),Dot(world.forward,value)};}
inline bool ReferenceNormal(Vec3 tracked,const CameraPose& eye,const CameraPose& reference,Vec3& output){
    if(!Finite(tracked)||!ValidatePose(eye)||!ValidatePose(reference))return false;
    const auto world=Add(Add(Scale(eye.basis.right,tracked.x),Scale(eye.basis.up,tracked.y)),Scale(eye.basis.forward,tracked.z));
    const auto next=ToView(reference.basis,world);if(!Finite(next))return false;
    output=next;return true;
}
inline bool WorldViewShader(const float world[12],const CameraPose& eye,float out[16]) {
    if(!world||!out||!ValidatePose(eye))return false;
    for(unsigned i=0;i<12;++i)if(!std::isfinite(world[i]))return false;
    const Vec3 axes[]{{world[6],world[7],world[8]},{world[3],world[4],world[5]},{world[0],world[1],world[2]},
        Subtract({world[9],world[10],world[11]},eye.position)};
    float next[16]{};
    for(unsigned i=0;i<4;++i){const auto v=ToView(eye.basis,axes[i]);if(!Finite(v))return false;next[i*4]=v.x;next[i*4+1]=v.y;next[i*4+2]=v.z;}
    next[15]=1;std::memcpy(out,next,sizeof(next));return true;
}
inline bool Columns(const CameraPose& eye,Vec3 centre,float out[8]) {
    if(!ValidatePose(eye)||!Finite(centre)||!out)return false;
    const auto up=ToView(eye.basis,{0,1,0}),delta=ToView(eye.basis,Subtract(eye.position,centre));
    const float next[8]={up.x,up.y,up.z,1e-6f,delta.x,delta.y,delta.z,0};
    for(float value:next)if(!std::isfinite(value))return false;
    std::memcpy(out,next,sizeof(next));return true;
}
inline bool RollColumns(const CameraPose& eye,const float projection[16],float out[8]) {
    if(!ValidatePose(eye)||!projection||!out||!std::isfinite(projection[0])||!std::isfinite(projection[5])||
       projection[0]<=0||projection[5]<=0)return false;
    const auto up=ToView(eye.basis,{0,1,0});
    const float length=std::sqrt(up.x*up.x+up.y*up.y);
    if(!std::isfinite(length)||length<.01f)return false;
    const float x=up.x/length,y=up.y/length,ratio=projection[0]/projection[5];
    const float next[8]={y,-x/ratio,0,0,x*ratio,y,0,0};
    for(float value:next)if(!std::isfinite(value))return false;
    std::memcpy(out,next,sizeof(next));return true;
}
inline bool ProjectionMatches(const float expected[16],const float actual[16]) {
    if(!expected||!actual||expected[0]<=0||expected[5]<=0||expected[14]>=0||
       std::fabs(expected[11]-1.f)>1e-4f||std::fabs(expected[15])>1e-4f)return false;
    bool rows=true,columns=true;
    for(unsigned i=0;i<16;++i){
        if(!std::isfinite(expected[i])||!std::isfinite(actual[i]))return false;
        rows=rows&&std::fabs(expected[i]-actual[i])<=1e-5f;
        columns=columns&&std::fabs(expected[(i%4)*4+i/4]-actual[i])<=1e-5f;
    }
    return rows||columns;
}
inline bool Quad(const Vertex* input,const float columns[8],Vertex* output) {
    if(!input||!columns||!output)return false;
    const Vec3 up{columns[0],columns[1],columns[2]},delta{columns[4],columns[5],columns[6]};
    if(!Finite(up)||!Finite(delta)||std::fabs(Dot(up,up)-1.f)>.01f)return false;
    Vec3 centre{};
    for(unsigned i=0;i<4;++i){const Vec3 p{input[i].x,input[i].y,input[i].z};if(!Finite(p))return false;centre=Add(centre,Scale(p,.25f));}
    const Vec3 facing=Add(centre,delta);Vec3 right=Cross(up,facing);
    const float length2=Dot(right,right);
    if(!std::isfinite(length2)||length2<1e-6f)return false;
    right=Scale(right,1.f/std::sqrt(length2));
    Vertex next[4];std::memcpy(next,input,sizeof(next));
    for(unsigned i=0;i<4;++i){
        if(std::fabs(input[i].z-centre.z)>.01f)return false;
        const auto p=Add(centre,Add(Scale(right,input[i].x-centre.x),Scale(up,input[i].y-centre.y)));
        if(!Finite(p))return false;next[i].x=p.x;next[i].y=p.y;next[i].z=p.z;
    }
    std::memcpy(output,next,sizeof(next));return true;
}
// No per-draw allocations, retained pointers or partially patched batches. The mapped
// span belongs to the exact native world-quad writer until original Unlock.
inline bool MappedQuads(void* mapped,std::size_t bytes,const float columns[8]) {
    return Detail::MappedBatch<Vertex>(mapped,bytes,[&](Vertex (&quad)[4]){return Quad(quad,columns,quad);});
}
// ManualScatter expands its corners on the CPU, including native authored
// rotation/growth/animation, in camera axes. Transport those already-expanded
// offsets from the same pair's untracked camera into the actual eye. Rotating
// all three components preserves its non-flat/aligned geometry; the native
// centre, colour, private index and UV are untouched. This is not a mesh rule:
// callers must prove the exact A37D3/stride28 borrowed native allocation.
template<class V> bool ReferenceQuad(const V* input,const CameraPose& eye,const CameraPose& reference,V* output) {
    if(!input||!output||!ValidatePose(eye)||!ValidatePose(reference))return false;
    Vec3 centre{},points[4];
    for(unsigned i=0;i<4;++i){points[i]={input[i].x,input[i].y,input[i].z};if(!Finite(points[i]))return false;centre=Add(centre,Scale(points[i],.25f));}
    const auto diagonal=Subtract(Add(points[0],points[2]),Add(points[1],points[3]));
    // The pinned writer emits four corners of one parallelogram, not arbitrary
    // triangles/meshes. Tolerance accommodates x87 stores at large world depth.
    if(Dot(diagonal,diagonal)>.01f)return false;
    V next[4];std::memcpy(next,input,sizeof(next));
    for(unsigned i=0;i<4;++i){const auto d=Subtract(points[i],centre);
        const auto world=Add(Add(Scale(reference.basis.right,d.x),Scale(reference.basis.up,d.y)),Scale(reference.basis.forward,d.z));
        const auto p=Add(centre,ToView(eye.basis,world));if(!Finite(p))return false;
        next[i].x=p.x;next[i].y=p.y;next[i].z=p.z;}
    std::memcpy(output,next,sizeof(next));return true;
}
inline bool ManualQuad(const ManualVertex* input,const CameraPose& eye,const CameraPose& reference,ManualVertex* output) {
    return ReferenceQuad(input,eye,reference,output);
}
inline bool MappedManualQuads(void* mapped,std::size_t bytes,const CameraPose& eye,const CameraPose& reference) {
    return Detail::MappedBatch<ManualVertex>(mapped,bytes,[&](ManualVertex (&quad)[4]){return ManualQuad(quad,eye,reference,quad);});
}
// The caller must supply the proven native LIVE prefix, not the allocation
// size. Dead type7 particles do not emit vertices; the remaining tail is never
// inspected here. Velocity-aligned emitters must be excluded by provenance.
inline bool MappedParticleQuads(void* mapped,unsigned liveQuads,const CameraPose& eye,const CameraPose& reference) {
    if(!mapped||liveQuads>Detail::MappedQuadLimit)return false;
    if(!liveQuads)return true;
    return Detail::MappedBatch<Vertex>(mapped,liveQuads*4*sizeof(Vertex),[&](Vertex (&quad)[4]){return ReferenceQuad(quad,eye,reference,quad);});
}
// Ordinary Type7 smoke is an expanded camera-facing particle, not a fixed
// world sheet. A reference-camera plane becomes edge-on when the player looks
// sideways. Face the particle toward the stereo CENTRE POSITION instead:
// head rotation cannot spin it, while looking at smoke from another position
// still presents its authored area. The reference up preserves native in-plane
// spin and has no tracked head roll. Both eyes use the same world-facing frame.
inline bool FacingParticleQuad(const Vertex* input,const CameraPose& eye,const CameraPose& reference,
                               Vec3 viewer,Vertex* output) {
    if(!input||!output||!ValidatePose(eye)||!ValidatePose(reference)||!Finite(viewer))return false;
    Vec3 centre{},points[4];
    for(unsigned i=0;i<4;++i){points[i]={input[i].x,input[i].y,input[i].z};if(!Finite(points[i]))return false;
        centre=Add(centre,Scale(points[i],.25f));}
    const auto diagonal=Subtract(Add(points[0],points[2]),Add(points[1],points[3]));
    if(Dot(diagonal,diagonal)>.01f)return false;
    const auto worldCentre=Add(eye.position,Add(Add(Scale(eye.basis.right,centre.x),Scale(eye.basis.up,centre.y)),Scale(eye.basis.forward,centre.z)));
    const auto normalize=[](Vec3 v,Vec3& result){const float length=Dot(v,v);
        if(!Finite(v)||!std::isfinite(length)||length<1e-10f)return false;result=Scale(v,1.f/std::sqrt(length));return Finite(result);};
    Vec3 forward,right,up;
    if(!normalize(Subtract(worldCentre,viewer),forward))forward=reference.basis.forward;
    if(!normalize(Cross(reference.basis.up,forward),right)&&
       !normalize(Subtract(reference.basis.right,Scale(forward,Dot(reference.basis.right,forward))),right))return false;
    if(!normalize(Cross(forward,right),up))return false;
    Vertex next[4];std::memcpy(next,input,sizeof(next));
    for(unsigned i=0;i<4;++i){const auto d=Subtract(points[i],centre);
        const auto world=Add(Add(Scale(right,d.x),Scale(up,d.y)),Scale(forward,d.z));
        const auto p=Add(centre,ToView(eye.basis,world));if(!Finite(p))return false;
        next[i].x=p.x;next[i].y=p.y;next[i].z=p.z;}
    std::memcpy(output,next,sizeof(next));return true;
}
inline bool MappedFacingParticleQuads(void* mapped,unsigned liveQuads,const CameraPose& eye,
                                      const CameraPose& reference,Vec3 viewer) {
    if(!mapped||liveQuads>Detail::MappedQuadLimit)return false;
    if(!liveQuads)return true;
    return Detail::MappedBatch<Vertex>(mapped,liveQuads*4*sizeof(Vertex),[&](Vertex (&quad)[4]){
        return FacingParticleQuad(quad,eye,reference,viewer,quad);});
}
// Type5/6 centres are native WORLD*VIEW transforms of source record XYZ.
// Four authored offsets may be asymmetric/non-flat, so rotate about that
// explicit anchor rather than guessing a parallelogram or its centroid.
inline bool MappedWorldSpriteQuads(void* mapped,std::size_t bytes,const void* records,unsigned type,
                                  const float world[12],const CameraPose& eye,const CameraPose& reference) {
    if(!records||(type!=5&&type!=6)||!ValidatePose(reference))return false;
    float wv[16];if(!WorldViewShader(world,eye,wv))return false;
    unsigned index=0;const auto* source=static_cast<const unsigned char*>(records);
    return Detail::MappedBatch<Vertex>(mapped,bytes,[&](Vertex (&quad)[4]) {
        Vec3 local;std::memcpy(&local,source+std::size_t(index++)*(type==5?40u:0x74u),sizeof(local));
        if(!Finite(local))return false;
        const Vec3 anchor{local.x*wv[0]+local.y*wv[4]+local.z*wv[8]+wv[12],
                          local.x*wv[1]+local.y*wv[5]+local.z*wv[9]+wv[13],
                          local.x*wv[2]+local.y*wv[6]+local.z*wv[10]+wv[14]};
        if(!Finite(anchor))return false;
        for(auto& vertex:quad){
            const Vec3 d=Subtract({vertex.x,vertex.y,vertex.z},anchor);if(!Finite(d))return false;
            const auto offset=Add(Add(Scale(reference.basis.right,d.x),Scale(reference.basis.up,d.y)),Scale(reference.basis.forward,d.z));
            const auto next=Add(anchor,ToView(eye.basis,offset));if(!Finite(next))return false;
            vertex.x=next.x;vertex.y=next.y;vertex.z=next.z;
        }
        return true;
    });
}
}
