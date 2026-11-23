#include "native_hud_math.hpp"
#include "native_letterbox_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks=0;
void Check(bool valid,const char* what) {++checks;if(!valid){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
bool Near(float a,float b,float epsilon=2e-5f){return std::fabs(a-b)<epsilon;}
void PlaneGeometry() {
    // Deliberately unequal frusta, vertical eye offsets and eye relief. The
    // same centre/edge/diagonal HUD pixel must reconstruct the SAME 3D point.
    const float fov[2][4]={{1.3f,.9f,1.1f,1.0f},{.87f,1.25f,1.07f,1.03f}};
    const float eyeOffset[2][3]={{-.032f,.001f,.012f},{.032f,-.001f,.009f}};
    for(float aspect:{4.f/3.f,16.f/9.f,21.f/9.f})for(float distance:{.5f,2.f,5.f}) {
        NativeHudMath::Plane plane[2];
        for(unsigned eye=0;eye<2;++eye)
            Check(NativeHudMath::Build(fov,eyeOffset[eye],eye,aspect,distance,.8f,plane[eye]),"asymmetric HUD plane builds");
        for(float x:{-1.f,-.5f,0.f,.5f,1.f})for(float y:{-1.f,0.f,1.f})for(unsigned eye=0;eye<2;++eye) {
            const auto& p=plane[eye];
            const float ndcX=x*p.scaleX+p.offsetX,ndcY=y*p.scaleY+p.offsetY;
            const float tangentX=(ndcX*(fov[eye][0]+fov[eye][1])-(fov[eye][0]-fov[eye][1]))*.5f;
            const float tangentY=(ndcY*(fov[eye][2]+fov[eye][3])-(fov[eye][3]-fov[eye][2]))*.5f;
            Check(Near(eyeOffset[eye][0]+tangentX*(distance+eyeOffset[eye][2]),x*p.tangentX*distance),
                  "both eye rays intersect identical HUD X at fixed plane");
            Check(Near(eyeOffset[eye][1]+tangentY*(distance+eyeOffset[eye][2]),y*p.tangentY*distance),
                  "both eye rays intersect identical HUD Y at fixed plane");
            Check(std::fabs(ndcX)<1.f&&std::fabs(ndcY)<1.f,"HUD corners remain inside both eye viewports");
        }
        Check(Near(plane[0].tangentX/plane[0].tangentY,aspect),"native text aspect preserved");
    }
}
void ShaderRows() {
    const NativeHudMath::Plane p{.7f,.8f,.13f,-.06f,.8f,.45f};
    for(float w:{.5f,1.f,10.f}) {
        float rows[16]={2.f,.1f,0.f,.2f, .03f,3.f,.2f,-.3f, 0.f,0.f,1.f,-5.f, 0.f,0.f,1.f,w};
        float original[16];std::memcpy(original,rows,sizeof(rows));
        Check(NativeHudMath::PatchRows(rows,p),"native transposed clip matrix patches");
        for(unsigned i=0;i<4;++i) {
            Check(Near(rows[i],original[i]*p.scaleX+original[12+i]*p.offsetX),"X row is clip-space composition");
            Check(Near(rows[4+i],original[4+i]*p.scaleY+original[12+i]*p.offsetY),"Y row is clip-space composition");
        }
        Check(!std::memcmp(rows+8,original+8,8*sizeof(float)),"native depth and clip W byte-exact");
    }
    float invalid[16]{};invalid[3]=std::numeric_limits<float>::quiet_NaN();float copy[16];std::memcpy(copy,invalid,sizeof(copy));
    Check(!NativeHudMath::PatchRows(invalid,p)&&!std::memcmp(copy,invalid,sizeof(copy)),"NaN upload rejected without partial writes");
    float teamRows[16]={2,0,0,0,0,3,0,0,0,0,1,-5,0,0,1,0};
    const NativeCameraMath::Vec3 anchor{1.37f,2.15f,6.f};
    NativeHudMath::Plane placement;
    Check(NativeHudMath::PlaceAt(teamRows,anchor,-.25f,-.35f,.8f,placement)&&NativeHudMath::PatchRows(teamRows,placement),
          "native team art relocates from the source anchor to the projected wrist");
    Check(Near((teamRows[0]*anchor.x+teamRows[2]*anchor.z)/anchor.z,-.25f)&&
          Near((teamRows[5]*anchor.y+teamRows[6]*anchor.z)/anchor.z,-.35f)&&
          Near(teamRows[0],1.6f)&&Near(teamRows[5],2.4f),
          "the entire horizontal row, including text, keeps the same proportions");
    Check(!NativeHudMath::PlaceAt(teamRows,{1,1,-1},0,0,1,placement),"native UI behind the source plane is rejected");
    float eyeRows[16]={1.125f,0,0,0,0,2,0,0,0,0,1,-5,0,0,1,0};
    Check(NativeHudMath::PlaceAt(eyeRows,anchor,.15f,-.2f,.75f,placement,16.f/9.f)&&
          NativeHudMath::PatchRows(eyeRows,placement),"monitor art projects on a square angular eye plane");
    Check(Near(eyeRows[0],eyeRows[5])&&Near(eyeRows[0]*anchor.x/anchor.z+eyeRows[2],.15f)&&
          Near(eyeRows[5]*anchor.y/anchor.z+eyeRows[6],-.2f),"circle stays round in eye angles and keeps its anchor");
}
void Gates() {
    float projection[16]{};projection[0]=2.f;projection[5]=3.f;projection[10]=projection[15]=1.f;
    Check(NativeHudMath::ScreenProjection(projection),"native orthographic UI qualifies");
    projection[10]=3000.f/2995.f;projection[14]=-5.f*projection[10];projection[11]=1.f;projection[15]=0.f;
    Check(NativeHudMath::ScreenProjection(projection),"finite 3000-unit UI camera qualifies");
    projection[10]=40000.f/39995.f;projection[14]=-5.f*projection[10];
    Check(!NativeHudMath::ScreenProjection(projection),"perspective main world never qualifies as HUD");
    projection[10]=1.f;projection[14]=-5.f;
    Check(!NativeHudMath::ScreenProjection(projection)&&NativeHudMath::InfiniteScreenCandidate(projection),
          "infinite projection requires separate source/depth/geometry authorization");
    const float fov[2][4]={{1,1,1,1},{1,1,1,1}};const float eye[3]={0,0,0};NativeHudMath::Plane plane;
    Check(!NativeHudMath::Build(fov,eye,2,16.f/9.f,2.f,.8f,plane),"invalid eye rejected");
    Check(!NativeHudMath::Build(fov,eye,0,16.f/9.f,0.f,.8f,plane),"invalid plane distance rejected");
    Check(!NativeHudMath::Build(fov,eye,0,16.f/9.f,2.f,1.f,plane),"unsafe viewport coverage rejected");
    float identity[16]{};identity[0]=identity[5]=identity[10]=identity[15]=1.f;
    NativeHudMath::Vertex full[4]={{-1,-1,0},{1,-1,0},{-1,1,0},{1,1,0}};
    Check(NativeHudMath::FullScreenQuad(full,identity),"native full-screen fade must remain full-screen");
    for(auto& vertex:full){vertex.x*=.6f;vertex.y*=.15f;}
    Check(!NativeHudMath::FullScreenQuad(full,identity),"text/icon or subtitle rectangle is not a full-screen fade");
    float native[20]={2.41421366f,0,0,0, 0,4.29193544f,0,0, 0,0,1,-5, 0,0,1,0, 2,1.f/3000.f,0,1};
    for(float fog:{3000.f,11000.f,40000.f}) {
        native[17]=1.f/fog;
        Check(NativeHudMath::NativeMonitorProjection(native),"fog settings do not classify a projection as screen/world geometry");
    }
    native[17]=1.f/3000.f;native[3]=100;
    Check(!NativeHudMath::NativeMonitorProjection(native),"translated world-view projection cannot authorize generic HUD");
    native[3]=0;native[2]=.03f;
    Check(!NativeHudMath::NativeMonitorProjection(native),"asymmetric rendered VR scene projection cannot authorize generic HUD");
    native[2]=0;native[11]=-50;
    Check(!NativeHudMath::NativeMonitorProjection(native),"world near plane cannot authorize generic HUD");
    native[11]=-5;native[6]=std::numeric_limits<float>::quiet_NaN();
    Check(!NativeHudMath::NativeMonitorProjection(native),"invalid source constants fail generic HUD authorization");
    Check(!NativeHudMath::NativeMonitorProjection(nullptr),"missing generic HUD constants fail closed");
    NativeHudMath::Vertex glyph[4]={{-.25f,-.1f,6.f,0,0,0},{.25f,-.1f,6.f,0,1,0},
        {-.25f,.1f,6.f,0,0,1},{.25f,.1f,6.f,0,1,1}};
    Check(NativeHudMath::InterfacePlane(glyph,4),"measured native glyph/image UI plane qualifies geometrically");
    glyph[3].z=20.f;Check(!NativeHudMath::InterfacePlane(glyph,4),"out-of-plane world quad rejected");
    for(auto& vertex:glyph)vertex.z=100.f;
    Check(!NativeHudMath::InterfacePlane(glyph,4),"coplanar world billboards do not qualify at another depth");
    for(auto& vertex:glyph)vertex.z=6.f;
    glyph[1].u=std::numeric_limits<float>::quiet_NaN();
    Check(!NativeHudMath::InterfacePlane(glyph,4),"invalid native atlas coordinates rejected");
    glyph[1].u=1.f;for(auto& vertex:glyph)vertex.x=0;
    Check(!NativeHudMath::InterfacePlane(glyph,4),"degenerate quads cannot authorize geometry");
    Check(!NativeHudMath::InterfacePlane(glyph,3)&&!NativeHudMath::InterfacePlane(nullptr,4),"missing/incomplete quad geometry fails closed");
}
void IndexedSources() {
    using namespace NativeHudMath;
    Check(IndexedSource(0x985C5,false)==IndexedPath::QuadArray&&
          IndexedSource(0x985C5,true)==IndexedPath::QuadArray,
          "native quad-array caller remains eligible inside and outside SpriteManager");
    Check(IndexedSource(0x99003,true)==IndexedPath::SpriteManager&&
          IndexedSource(0x99003,false)==IndexedPath::None,
          "native SpriteManager caller needs its exact active manager scope");
    for(auto rva:{0x8EF4Fu,0x8EFA1u,0x9A6EAu,0x9A937u,0x9AE7Au,0x9B0C4u,0x9DF15u,0xA3173u,0xADF08u})
        Check(IndexedSource(rva,false)==IndexedPath::None&&IndexedSource(rva,true)==IndexedPath::None,
              "world/particle indexed caller is rejected before camera or D3D state queries");
    for(auto rva:{0u,0x985C4u,0x985C6u,0x99002u,0x99004u,0xffffffffu})
        Check(IndexedSource(rva,false)==IndexedPath::None&&IndexedSource(rva,true)==IndexedPath::None,
              "unknown or neighbouring caller cannot acquire a HUD source path");
}
void ScissorGeometry() {
    using namespace NativeHudMath;
    const Rect source{300,200,900,500};
    for(unsigned width:{1280u,1920u})for(unsigned height:{720u,1080u})for(float ipd:{-.04f,.04f}) {
        const Plane plane{.6f,.7f,ipd,-.03f,.8f,.45f};Rect result;
        Check(PatchScissor(source,width,height,plane,result),"native scissor transforms with the HUD eye plane");
        for(int x:{source.left,source.right})for(int y:{source.top,source.bottom}) {
            const double projectedX=((2.0*x/width-1.0)*plane.scaleX+plane.offsetX+1.0)*width*.5;
            const double projectedY=(1.0-((1.0-2.0*y/height)*plane.scaleY+plane.offsetY))*height*.5;
            Check(projectedX>=result.left&&projectedX<=result.right&&projectedY>=result.top&&projectedY<=result.bottom,
                  "all transformed original clip corners remain inside the rounded eye scissor");
        }
        Check(result.left>=0&&result.top>=0&&result.right<=static_cast<int>(width)&&result.bottom<=static_cast<int>(height),
              "temporary scissor stays inside the actual viewport");
    }
    Rect sentinel{1,2,3,4};const auto before=sentinel;
    Check(!PatchScissor({-1,0,10,10},1280,720,Plane{},sentinel)&&!std::memcmp(&before,&sentinel,sizeof(before)),
          "invalid native scissor leaves the caller result unchanged");
    Plane shifted;shifted.offsetX=10;
    Check(!PatchScissor(source,1280,720,shifted,sentinel),"fully off-screen projected scissor fails closed");
    const Plane original{.62f,.65f,.07f,-.03f,.56f,.46f};
    Plane enlarged=original;
    constexpr float baseline=-.72f;
    EnlargeText(enlarged,ReadableTextScale,baseline);
    Check(std::fabs((enlarged.scaleY*baseline+enlarged.offsetY)-(original.scaleY*baseline+original.offsetY))<.0001f&&
          enlarged.offsetX==original.offsetX&&std::fabs(enlarged.scaleX/original.scaleX-enlarged.scaleY/original.scaleY)<.0001f,
          "subtitle enlargement preserves the projected baseline and binocular centre with uniform scaling");
}
void CinemaTextGeometry() {
    using namespace NativeHudMath;
    for(float hudTangentY:{.35f,.65f,.9f})for(float panelHeight:{1.8f,2.4f})for(float panelDistance:{1.5f,2.5f}) {
        float scale=0.f;
        Check(CinemaTextScale(hudTangentY,panelHeight,panelDistance,scale),
              "valid cinema panel and gameplay plane produce a shared apparent font size");
        Check(std::fabs(scale*panelHeight/(2.f*panelDistance)-ReadableTextScale*hudTangentY)<.0001f,
              "a native glyph spans the same angular tangent in gameplay and on the cinema panel");
    }
    float sentinel=1.f;
    Check(!CinemaTextScale(.6f,0.f,2.f,sentinel)&&sentinel==1.f,"invalid panel size leaves its scale unchanged");
    Check(!CinemaTextScale(.6f,1.8f,std::numeric_limits<float>::infinity(),sentinel)&&sentinel==1.f,
          "invalid panel distance leaves its scale unchanged");
}
void NativeLetterboxScope() {
    NativeLetterboxKL::State state;
    NativeCinematicKL::State mini{true,0,1},normal{true,0,0},full{true,1,1};
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,100),"exact native mini-cinema bands are hidden in fresh first-person VR");
    Check(state.Step(0x1000,0x2000,true,true,normal,.5f,600),"native fade-out remains hidden after MiniCutscene End");
    Check(!state.Step(0x1000,0x2000,true,true,normal,0.f,650),"fade completion retires the mini-cinema latch");
    Check(!state.Step(0x1000,0x2000,true,true,normal,1.f,700),"ordinary letterbox calls do not inherit a completed mini-cinema scope");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,800)&&
        !state.Step(0x1000,0x2000,true,true,full,1.f,810),"full cinematics preserve native bands and clear the first-person latch");
    Check(!state.Step(0x1000,0x2000,true,true,normal,.5f,820),"return from a full cinematic does not reuse the old mini latch");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,900)&&
        !state.Step(0x1000,0x2000,true,false,mini,1.f,910),"tracking/menu/panel authorization loss keeps the native draw");
    Check(!state.Step(0x1000,0x2000,false,true,mini,1.f,920),"black HUD panels and other image types never count as native letterbox source");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,1000)&&
        !state.Step(0x1000,0x3000,true,true,normal,.5f,1010),"native owner replacement invalidates a reused object address");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,1100)&&
        !state.Step(0x3000,0x2000,true,true,normal,.5f,1110),"a different display cannot inherit cinematic suppression");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,1200)&&
        !state.Step(0x1000,0x2000,true,true,normal,.5f,2201),"stale fade latch cannot affect a later native display lifetime");
    Check(state.Step(0x1000,0x2000,true,true,mini,1.f,0xfffffff0u)&&
        state.Step(0x1000,0x2000,true,true,normal,.5f,40),"native timer wrap preserves only the short current fade");
    Check(!state.Step(0x1000,0x2000,true,true,NativeCinematicKL::State{},1.f,50),"unknown cinematic counters fail closed to the native draw");
    Check(!state.Step(0x1000,0x2000,true,true,mini,std::numeric_limits<float>::quiet_NaN(),60),"invalid native animation amount cannot suppress a draw");
    Check(state.Step(0x1000,0x2000,true,true,full,1.f,70,true),"verified authored stereo cinema hides only its native letterbox display");
    Check(!state.Step(0x1000,0x2000,true,true,normal,.5f,80),"full stereo cinema does not seed a mini-scene fade latch");
    Check(!state.Step(0x1000,0x2000,true,true,full,1.f,90,false),"panel fallback retains native full-cinema bands");
    Check(!state.Step(0x1000,0x2000,true,false,full,1.f,100,true),"tracking loss still retains native cinematic bands");
}
}
int main(){PlaneGeometry();ShaderRows();Gates();IndexedSources();ScissorGeometry();CinemaTextGeometry();NativeLetterboxScope();std::printf("Native HUD binocular plane: %u checks passed.\n",checks);}
