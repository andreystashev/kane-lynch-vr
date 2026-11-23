#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

// Flat native UI is authored in monitor clip coordinates. Identical clip X/Y
// in asymmetric eye frusta are NOT the same viewing ray. Reproject that UI
// onto one head-local plane before the real shader upload, preserving native
// atlas/font geometry, alpha, clip W and native depth.
namespace NativeHudMath {
enum class IndexedPath { None,SpriteManager,QuadArray };
inline IndexedPath IndexedSource(std::uint32_t callerRva,bool managerActive) {
    // Other native indexed callers draw world meshes/particles and cannot
    // reach the proven HUD writers. Reject them before querying D3D state.
    if(callerRva==0x99003&&managerActive)return IndexedPath::SpriteManager;
    return callerRva==0x985C5?IndexedPath::QuadArray:IndexedPath::None;
}
struct Plane {
    float scaleX=1.f,scaleY=1.f,offsetX=0.f,offsetY=0.f;
    float tangentX=0.f,tangentY=0.f;
};
struct Rect {std::int32_t left=0,top=0,right=0,bottom=0;};
inline bool PatchScissor(const Rect& source,unsigned width,unsigned height,const Plane& plane,Rect& output) {
    if(!width||!height||width>65536||height>65536||source.left<0||source.top<0||
       source.right<=source.left||source.bottom<=source.top||
       static_cast<unsigned>(source.right)>width||static_cast<unsigned>(source.bottom)>height||
       !std::isfinite(plane.scaleX)||!std::isfinite(plane.scaleY)||!std::isfinite(plane.offsetX)||
       !std::isfinite(plane.offsetY)||plane.scaleX<=0.f||plane.scaleY<=0.f)return false;
    const auto x=[&](std::int32_t pixel){return ((2.0*pixel/width-1.0)*plane.scaleX+plane.offsetX+1.0)*width*.5;};
    const auto y=[&](std::int32_t pixel){return (1.0-((1.0-2.0*pixel/height)*plane.scaleY+plane.offsetY))*height*.5;};
    Rect next;
    next.left=static_cast<std::int32_t>(std::clamp(std::floor(x(source.left)),0.0,double(width)));
    next.right=static_cast<std::int32_t>(std::clamp(std::ceil(x(source.right)),0.0,double(width)));
    next.top=static_cast<std::int32_t>(std::clamp(std::floor(y(source.top)),0.0,double(height)));
    next.bottom=static_cast<std::int32_t>(std::clamp(std::ceil(y(source.bottom)),0.0,double(height)));
    if(next.left>=next.right||next.top>=next.bottom)return false;
    output=next;return true;
}
inline bool Build(const float fov[2][4],const float eyeOffset[3],unsigned eye,
                  float aspect,float distanceMetres,float coverage,Plane& output) {
    if(eye>1||!fov||!eyeOffset||!NativeCameraMath::ValidFov(fov[0])||
       !NativeCameraMath::ValidFov(fov[1])||!std::isfinite(aspect)||aspect<.5f||aspect>4.f||
       !std::isfinite(distanceMetres)||distanceMetres<.5f||distanceMetres>10.f||
       !std::isfinite(coverage)||coverage<.25f||coverage>.95f)return false;
    for(unsigned i=0;i<3;++i)
        if(!std::isfinite(eyeOffset[i])||std::fabs(eyeOffset[i])>.2f)return false;
    const float eyeDistance=distanceMetres+eyeOffset[2]; // tracking forward is -Z
    if(eyeDistance<.25f)return false;
    const float horizontal=std::min({fov[0][0],fov[0][1],fov[1][0],fov[1][1]})*coverage;
    const float vertical=std::min({fov[0][2],fov[0][3],fov[1][2],fov[1][3]})*coverage;
    Plane result;
    result.tangentY=std::min(vertical,horizontal/aspect);
    result.tangentX=result.tangentY*aspect;
    const float width=fov[eye][0]+fov[eye][1],height=fov[eye][2]+fov[eye][3];
    result.scaleX=2.f*result.tangentX*distanceMetres/(width*eyeDistance);
    result.scaleY=2.f*result.tangentY*distanceMetres/(height*eyeDistance);
    result.offsetX=(fov[eye][0]-fov[eye][1])/width-2.f*eyeOffset[0]/(width*eyeDistance);
    result.offsetY=(fov[eye][3]-fov[eye][2])/height-2.f*eyeOffset[1]/(height*eyeDistance);
    for(float value:{result.scaleX,result.scaleY,result.offsetX,result.offsetY,
                     result.tangentX,result.tangentY})if(!std::isfinite(value))return false;
    output=result;return true;
}

// Native shader matrices are uploaded transposed: four dot-product rows.
// Composition in clip space also works when the matrix includes a local UI
// transform. No assumptions about native vertex coordinates or UI z are made.
inline bool PatchRows(float* rows,const Plane& plane) {
    if(!rows)return false;
    float result[16];std::memcpy(result,rows,sizeof(result));
    for(unsigned i=0;i<16;++i)if(!std::isfinite(result[i]))return false;
    for(unsigned i=0;i<4;++i) {
        result[i]=rows[i]*plane.scaleX+rows[12+i]*plane.offsetX;
        result[4+i]=rows[4+i]*plane.scaleY+rows[12+i]*plane.offsetY;
    }
    for(float value:result)if(!std::isfinite(value))return false;
    std::memcpy(rows,result,sizeof(result));return true;
}

constexpr float ReadableTextScale=1.60f;
inline bool CinemaTextScale(float hudTangentY,float panelHeight,float panelDistance,float& scale){
    if(!std::isfinite(hudTangentY)||hudTangentY<=0.f||!std::isfinite(panelHeight)||panelHeight<=0.f||
       !std::isfinite(panelDistance)||panelDistance<=0.f)return false;
    const float next=ReadableTextScale*hudTangentY/(panelHeight/(2.f*panelDistance));
    if(!std::isfinite(next)||next<=0.f)return false;
    scale=next;return true;
}
inline void EnlargeText(Plane& plane,float factor,float baseline){
    // Compose the enlargement around the original text group's baseline.
    // Both eyes keep their original plane offsets and the same font scale.
    plane.offsetY+=plane.scaleY*baseline*(1.f-factor);
    plane.scaleX*=factor;
    plane.scaleY*=factor;
}

inline bool PlaceAt(const float* rows,NativeCameraMath::Vec3 source,float x,float y,float scale,Plane& output,float eyeAspect=1.f) {
    if(!rows||!NativeCameraMath::Finite(source)||!std::isfinite(x)||!std::isfinite(y)||
       !std::isfinite(scale)||scale<.125f||scale>2.f||std::fabs(x)>1.5f||std::fabs(y)>1.5f||
       !std::isfinite(eyeAspect)||eyeAspect<.25f||eyeAspect>4.f)return false;
    const auto row=[&](unsigned i){return rows[i]*source.x+rows[i+1]*source.y+rows[i+2]*source.z+rows[i+3];};
    const auto w=row(12);
    if(!std::isfinite(w)||w<.01f)return false;
    Plane plane;
    plane.scaleX=scale*eyeAspect;plane.scaleY=scale;
    plane.offsetX=x-plane.scaleX*row(0)/w;
    plane.offsetY=y-scale*row(4)/w;
    if(!std::isfinite(plane.offsetX)||!std::isfinite(plane.offsetY))return false;
    output=plane;return true;
}

// SpriteManagerD3D's screen-space batches use an identity/orthographic UI
// projection or a short native perspective frustum. Main world cameras use
// 40000 units/infinite far and must never qualify merely because they draw
// alpha-blended sprites.
inline bool ScreenProjection(const float* p) {
    if(!p)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(p[i]))return false;
    if(std::fabs(p[3])>1e-5f||std::fabs(p[7])>1e-5f||
       std::fabs(p[0])<1e-5f||std::fabs(p[5])<1e-5f)return false;
    if(std::fabs(p[11])<1e-5f&&std::fabs(p[15]-1.f)<1e-5f)return true;
    if(std::fabs(p[11]-1.f)>1e-5f||std::fabs(p[15])>1e-5f||
       p[10]<=1.000001f||p[14]>=0.f)return false;
    const float farUnits=-p[14]/(p[10]-1.f),nearUnits=-p[14]/p[10];
    return nearUnits>0.f&&farUnits>nearUnits&&farUnits<=4000.f;
}
inline bool InfiniteScreenCandidate(const float* p) {
    if(!p)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(p[i]))return false;
    return std::fabs(p[3])<1e-5f&&std::fabs(p[7])<1e-5f&&
        p[0]>.01f&&p[5]>.01f&&std::fabs(p[10]-1.f)<1e-6f&&
        std::fabs(p[11]-1.f)<1e-5f&&std::fabs(p[15])<1e-5f&&p[14]<0.f&&p[14]>=-20.f;
}
// Native monitor projection: transposed symmetric perspective, near=5.
// This is only a matrix predicate. c4 is vFogSettings (observed fog ranges
// 3000 in menus and 11000 in gameplay), and cannot authorize a HUD draw.
// The production draw also requires exact glyph/image source provenance,
// native UI depth state, the actual UI-plane vertices and main viewport.
inline bool NativeMonitorProjection(const float* rows) {
    if(!rows)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(rows[i]))return false;
    for(unsigned i:{1u,2u,3u,4u,6u,7u,8u,9u,12u,13u,15u})
        if(std::fabs(rows[i])>1e-5f)return false;
    return rows[0]>.05f&&rows[0]<20.f&&rows[5]>.05f&&rows[5]<20.f&&
        std::fabs(rows[10]-1.f)<1e-5f&&std::fabs(rows[11]+5.f)<1e-4f&&
        std::fabs(rows[14]-1.f)<1e-5f;
}
struct Vertex {float x=0.f,y=0.f,z=0.f;std::uint32_t colour=0;float u=0.f,v=0.f;};
static_assert(sizeof(Vertex)==24,"native SpriteManager GPU vertex stride");
inline bool InterfacePlane(const Vertex* vertices,std::size_t count) {
    if(!vertices||!count||count%4)return false;
    // Native glyph/type5 and image/type6 UI roots place source z=0 at z=6,
    // one unit beyond their near=5 plane. This is an additional geometry
    // gate; the writer provenance and native depth state are also required.
    for(std::size_t i=0;i<count;++i) {
        const auto& vertex=vertices[i];
        if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||!std::isfinite(vertex.z)||
           !std::isfinite(vertex.u)||!std::isfinite(vertex.v)||std::fabs(vertex.z-6.f)>.002f)return false;
    }
    bool visible=false;
    for(std::size_t i=0;i<count;i+=4) {
        float minX=vertices[i].x,maxX=minX,minY=vertices[i].y,maxY=minY;
        for(unsigned j=1;j<4;++j){minX=std::min(minX,vertices[i+j].x);maxX=std::max(maxX,vertices[i+j].x);
            minY=std::min(minY,vertices[i+j].y);maxY=std::max(maxY,vertices[i+j].y);}
        visible=visible||(maxX-minX>1e-7f&&maxY-minY>1e-7f);
    }
    return visible;
}
inline bool FullScreenQuad(const Vertex* quad,const float* rows) {
    if(!quad||!rows)return false;
    float minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f;
    for(unsigned vertex=0;vertex<4;++vertex) {
        const auto& p=quad[vertex];
        const float w=p.x*rows[12]+p.y*rows[13]+p.z*rows[14]+rows[15];
        if(!std::isfinite(w)||w<=1e-6f)return false;
        const float x=(p.x*rows[0]+p.y*rows[1]+p.z*rows[2]+rows[3])/w;
        const float y=(p.x*rows[4]+p.y*rows[5]+p.z*rows[6]+rows[7])/w;
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        minX=std::min(minX,x);maxX=std::max(maxX,x);
        minY=std::min(minY,y);maxY=std::max(maxY,y);
    }
    return minX<=-.95f&&maxX>=.95f&&minY<=-.95f&&maxY>=.95f;
}
}
