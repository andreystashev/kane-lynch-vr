#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "hands_renderer.hpp"
#include "native_team_marker_math.hpp"
#include "hands_ipc.hpp"
#include "hands_math.hpp"
#include "native_hand_geometry.hpp"
#include "profile_timing.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace NativeCameraMath;
struct Vertex {float x,y,z;DWORD color;};
constexpr float pi=3.14159265358979323846f;
std::atomic<bool> enabled{true},connected{false};
std::atomic<std::uint32_t> validHands{0},ageMs{0};
std::atomic<std::uint64_t> frames{0},handCount{0},failures{0};
std::atomic<bool> hudReady{false};
std::atomic<std::uint64_t> hudFrames{0},hudCards{0},hudRejected{0},hudFailures{0},hudTextureBuilds{0};
std::atomic<std::uint64_t> commandMarkerFrames{0},commandMarkers{0},commandMarkerRejected{0};
HandsIPC::Consumer consumer;
HandsIPC::Frame paired{};
std::uint64_t pairedId=~std::uint64_t(0);

struct Builder {
    Vec3 origin,axisX,axisY,axisZ;
    std::vector<Vertex>& vertices;
    Vec3 CameraPoint(Vec3 local) const {
        return Add(origin,Add(Add(Scale(axisX,local.x),Scale(axisY,local.y)),Scale(axisZ,local.z)));
    }
    void Triangle(Vec3 a,Vec3 b,Vec3 c,DWORD color) {
        // Simple directional shading gives glove/finger volume without
        // touching the game's lights or materials.
        const Vec3 normal=Cross(Subtract(b,a),Subtract(c,a));
        const float length=std::sqrt(Dot(normal,normal));
        float shade=.72f;
        if(length>1e-9f)shade=.55f+.45f*std::fabs(Dot(Scale(normal,1.f/length),{.3f,.65f,.7f}));
        const auto channel=[&](int shift){return static_cast<DWORD>(((color>>shift)&255)*shade);};
        const DWORD shaded=0xff000000|(channel(16)<<16)|(channel(8)<<8)|channel(0);
        for(Vec3 local:{a,b,c}){const auto point=CameraPoint(local);vertices.push_back({point.x,point.y,point.z,shaded});}
    }
    void Ring(float x,float yRadius,float zRadius,float nextX,float nextY,float nextZ,DWORD color) {
        constexpr unsigned sides=12;
        for(unsigned i=0;i<sides;++i) {
            const float a=2*pi*i/sides,b=2*pi*(i+1)/sides;
            const Vec3 p0{x,std::cos(a)*yRadius,std::sin(a)*zRadius};
            const Vec3 p1{x,std::cos(b)*yRadius,std::sin(b)*zRadius};
            const Vec3 q0{nextX,std::cos(a)*nextY,std::sin(a)*nextZ};
            const Vec3 q1{nextX,std::cos(b)*nextY,std::sin(b)*nextZ};
            Triangle(p0,p1,q0,color);Triangle(p1,q1,q0,color);
        }
    }
    void Capsule(Vec3 from,Vec3 to,float radius,DWORD color) {
        Vec3 axis=Subtract(to,from);const float length=std::sqrt(Dot(axis,axis));
        if(length<.001f)return;
        axis=Scale(axis,1.f/length);
        Vec3 side=Cross(axis,std::fabs(axis.z)<.8f?Vec3{0,0,1}:Vec3{0,1,0});
        side=Scale(side,1.f/std::sqrt(Dot(side,side)));const Vec3 up=Cross(axis,side);
        constexpr unsigned sides=8,rings=6;
        Vec3 previous[sides]{};
        for(unsigned ring=0;ring<rings;++ring) {
            // Rounded caps and a straight centre, rather than screen discs.
            const float t=ring/static_cast<float>(rings-1);
            const float axial=t*length;
            const float cap=ring==0||ring==rings-1?.1f:(ring==1||ring==rings-2?.85f:1.f);
            Vec3 current[sides];
            for(unsigned i=0;i<sides;++i) {
                const float angle=2*pi*i/sides;
                current[i]=Add(Add(from,Scale(axis,axial)),Add(Scale(side,std::cos(angle)*radius*cap),Scale(up,std::sin(angle)*radius*cap)));
            }
            if(ring)for(unsigned i=0;i<sides;++i) {
                const unsigned j=(i+1)%sides;
                Triangle(previous[i],previous[j],current[i],color);
                Triangle(previous[j],current[j],current[i],color);
            }
            std::memcpy(previous,current,sizeof(previous));
        }
    }
    void Finger(const NativeHandGeometry::Finger& finger,DWORD color) {
        // A single skin passes through the native joint centres. Separate
        // capped phalanges/spheres leave bead-shaped joints when curled.
        constexpr unsigned steps=3,rings=3*steps+3,sides=12;
        const auto unit=[](Vec3 value,Vec3 fallback) {
            const float length=std::sqrt(Dot(value,value));
            return length>1e-6f?Scale(value,1.f/length):fallback;
        };
        Vec3 direction[3];float length[3];
        for(unsigned i=0;i<3;++i) {
            const auto segment=Subtract(finger.joint[i+1],finger.joint[i]);
            length[i]=std::sqrt(Dot(segment,segment));direction[i]=unit(segment,{1,0,0});
        }
        const Vec3 slope[4]={Scale(direction[0],length[0]),
            Scale(unit(Add(direction[0],direction[1]),direction[1]),std::min(length[0],length[1])*.8f),
            Scale(unit(Add(direction[1],direction[2]),direction[2]),std::min(length[1],length[2])*.8f),
            Scale(direction[2],length[2])};
        const float radius=finger.radius;
        Vec3 centre[rings],tangent[rings];float widths[rings];
        centre[0]=Subtract(finger.joint[0],Scale(direction[0],radius*.55f));tangent[0]=direction[0];widths[0]=.95f;
        constexpr float jointWidth[4]={1.f,.94f,.8f,.65f};
        unsigned ringIndex=1;
        for(unsigned segment=0;segment<3;++segment)for(unsigned sample=0;sample<steps;++sample) {
            const float t=sample/static_cast<float>(steps),t2=t*t,t3=t2*t;
            const auto a=finger.joint[segment],b=finger.joint[segment+1],m=slope[segment],n=slope[segment+1];
            // Cubic interpolation passes through every native joint while
            // rounding the skin between them, rather than using hard mitres.
            centre[ringIndex]=Add(Add(Scale(a,2*t3-3*t2+1),Scale(m,t3-2*t2+t)),
                                  Add(Scale(b,-2*t3+3*t2),Scale(n,t3-t2)));
            tangent[ringIndex]=unit(Add(Add(Scale(a,6*t2-6*t),Scale(m,3*t2-4*t+1)),
                                       Add(Scale(b,-6*t2+6*t),Scale(n,3*t2-2*t))),direction[segment]);
            widths[ringIndex++]=jointWidth[segment]*(1-t)+jointWidth[segment+1]*t;
        }
        centre[ringIndex]=finger.joint[3];tangent[ringIndex]=direction[2];widths[ringIndex++]=.65f;
        centre[ringIndex]=Add(finger.joint[3],Scale(direction[2],radius*.3f));
        tangent[ringIndex]=direction[2];widths[ringIndex]=.4f;
        Vec3 surface[rings][sides];
        Vec3 side=unit(Cross(tangent[0],std::fabs(tangent[0].z)<.8f?Vec3{0,0,1}:Vec3{0,1,0}),{0,1,0});
        for(unsigned ring=0;ring<rings;++ring) {
            // Project the previous side into the new cross-section. This
            // transports its orientation through bends without sudden rolls.
            const Vec3 projected=Subtract(side,Scale(tangent[ring],Dot(side,tangent[ring])));
            side=unit(projected,unit(Cross(tangent[ring],std::fabs(tangent[ring].z)<.8f?Vec3{0,0,1}:Vec3{0,1,0}),{0,1,0}));
            const Vec3 up=Cross(tangent[ring],side);
            for(unsigned i=0;i<sides;++i) {
                const float angle=2*pi*i/sides;
                surface[ring][i]=Add(centre[ring],Scale(Add(Scale(side,std::cos(angle)),Scale(up,std::sin(angle))),radius*widths[ring]));
            }
        }
        for(unsigned ring=0;ring+1<rings;++ring)for(unsigned i=0;i<sides;++i) {
            const unsigned j=(i+1)%sides;
            Triangle(surface[ring][i],surface[ring][j],surface[ring+1][i],color);
            Triangle(surface[ring][j],surface[ring+1][j],surface[ring+1][i],color);
        }
        const Vec3 tip=Add(finger.joint[3],Scale(direction[2],radius*.65f));
        for(unsigned i=0;i<sides;++i)Triangle(surface[rings-1][i],surface[rings-1][(i+1)%sides],tip,color);
    }
    bool Hand(unsigned side,float grip,float trigger) {
        NativeHandGeometry::Shape shape;
        if(!NativeHandGeometry::Fallback(side,grip,trigger,shape))return false;
        const DWORD glove=0xff96948d,cuff=0xff293139;
        Ring(-.04f,.027f,.015f,-.012f,.025f,.013f,cuff);
        for(unsigned ring=0;ring+1<NativeHandGeometry::PalmRings;++ring)
            for(unsigned i=0;i<NativeHandGeometry::RingSides;++i) {
                const unsigned j=(i+1)%NativeHandGeometry::RingSides;
                Triangle(shape.palm[ring][i],shape.palm[ring][j],shape.palm[ring+1][i],glove);
                Triangle(shape.palm[ring][j],shape.palm[ring+1][j],shape.palm[ring+1][i],glove);
            }
        for(unsigned i=0;i<NativeHandGeometry::RingSides;++i)
            Triangle(shape.palmFront,shape.palm[NativeHandGeometry::PalmRings-1][i],
                     shape.palm[NativeHandGeometry::PalmRings-1][(i+1)%NativeHandGeometry::RingSides],glove);
        for(const auto& finger:shape.finger)Finger(finger,glove);
        return true;
    }
};

template<class T> void Release(T*& object){if(object){object->Release();object=nullptr;}}
constexpr D3DRENDERSTATETYPE renderTypes[]={D3DRS_LIGHTING,D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE,D3DRS_CULLMODE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,
    D3DRS_SPECULARENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FILLMODE,D3DRS_CLIPPING,D3DRS_COLORWRITEENABLE,
    D3DRS_SRGBWRITEENABLE,D3DRS_VERTEXBLEND,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_CLIPPLANEENABLE,
    D3DRS_SHADEMODE,D3DRS_MULTISAMPLEMASK,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,
    D3DRS_SEPARATEALPHABLENDENABLE};
constexpr D3DTRANSFORMSTATETYPE transformTypes[]={D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION};
constexpr D3DTEXTURESTAGESTATETYPE stageTypes[]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_ALPHAOP,
    D3DTSS_ALPHAARG1,D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS};
constexpr D3DSAMPLERSTATETYPE samplerTypes[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,
    D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE};
struct Scope {
    IDirect3DDevice9* device;
    IDirect3DStateBlock9* state=nullptr;
    IDirect3DSurface9* target[4]{},*depth=nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD renders[std::size(renderTypes)]{},stages[std::size(stageTypes)]{},stage1=0;
    DWORD samplers[std::size(samplerTypes)]{};
    D3DMATRIX transforms[3]{};
    IDirect3DVertexShader9* vertexShader=nullptr;
    IDirect3DPixelShader9* pixelShader=nullptr;
    IDirect3DVertexDeclaration9* declaration=nullptr;
    IDirect3DVertexBuffer9* stream=nullptr;
    UINT streamOffset=0,streamStride=0;
    UINT streamFrequency=1;
    IDirect3DBaseTexture9* textures[8]{};
    unsigned targets=0;
    DWORD fvf=0;
    bool saved=false,ownScene=false,full=true,targetsChanged=false;
    bool Save(bool fullState=true) {
        full=fullState;
        D3DCAPS9 caps{};
        if(FAILED(device->GetDeviceCaps(&caps))||FAILED(device->GetViewport(&viewport))||
           FAILED(device->GetScissorRect(&scissor)))return false;
        targets=std::min(4u,static_cast<unsigned>(caps.NumSimultaneousRTs));
        if(!targets)return false;
        for(unsigned i=0;i<targets;++i) {
            const HRESULT result=device->GetRenderTarget(i,&target[i]);
            if(FAILED(result)&&(i==0||result!=D3DERR_NOTFOUND))return false;
        }
        const HRESULT depthResult=device->GetDepthStencilSurface(&depth);
        if(FAILED(depthResult)&&depthResult!=D3DERR_NOTFOUND)return false;
        for(unsigned i=0;i<std::size(renderTypes);++i)if(FAILED(device->GetRenderState(renderTypes[i],&renders[i])))return false;
        for(unsigned i=0;i<(full?std::size(stageTypes):4u);++i)if(FAILED(device->GetTextureStageState(0,stageTypes[i],&stages[i])))return false;
        if(FAILED(device->GetTextureStageState(1,D3DTSS_COLOROP,&stage1)))return false;
        if(FAILED(device->GetVertexShader(&vertexShader))||FAILED(device->GetPixelShader(&pixelShader))||
           FAILED(device->GetVertexDeclaration(&declaration))||
           FAILED(device->GetStreamSource(0,&stream,&streamOffset,&streamStride))||
           FAILED(device->GetStreamSourceFreq(0,&streamFrequency)))return false;
        if(full){
            for(unsigned i=0;i<3;++i)if(FAILED(device->GetTransform(transformTypes[i],&transforms[i])))return false;
            for(unsigned i=0;i<std::size(samplerTypes);++i)if(FAILED(device->GetSamplerState(0,samplerTypes[i],&samplers[i])))return false;
            for(unsigned i=0;i<8;++i)if(FAILED(device->GetTexture(i,&textures[i])))return false;
            if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state))||FAILED(state->Capture()))return false;
        }else if(FAILED(device->GetFVF(&fvf)))return false;
        saved=true;return true;
    }
    bool BindAimTarget(IDirect3DSurface9* backbuffer){
        targetsChanged=target[0]!=backbuffer;
        for(unsigned i=1;i<targets;++i)targetsChanged|=target[i]!=nullptr;
        if(!targetsChanged)return true;
        bool okay=true;
        for(unsigned i=1;i<targets;++i)if(target[i]&&FAILED(device->SetRenderTarget(i,nullptr)))okay=false;
        if(depth&&FAILED(device->SetDepthStencilSurface(nullptr)))okay=false;
        if(target[0]!=backbuffer&&FAILED(device->SetRenderTarget(0,backbuffer)))okay=false;
        return okay;
    }
    bool Restore() {
        if(!saved)return true;
        bool okay=true;
        // State blocks do not restore RT/depth; bind them explicitly before
        // restoring viewport/scissor, which SetRenderTarget may reset.
        if(full||targetsChanged){
            for(unsigned i=1;i<targets;++i)if(FAILED(device->SetRenderTarget(i,nullptr)))okay=false;
            if(FAILED(device->SetDepthStencilSurface(nullptr)))okay=false;
            if(FAILED(device->SetRenderTarget(0,target[0])))okay=false;
            for(unsigned i=1;i<targets;++i)if(FAILED(device->SetRenderTarget(i,target[i])))okay=false;
            if(FAILED(device->SetDepthStencilSurface(depth)))okay=false;
        }
        if(full&&FAILED(state->Apply()))okay=false;
        // Some D3D9 implementations report successful state-block Apply while
        // omitting individual states. Explicitly restore every state touched
        // by this pass as well as DrawPrimitiveUP's stream-0 side effect.
        for(unsigned i=0;i<std::size(renderTypes);++i)if(FAILED(device->SetRenderState(renderTypes[i],renders[i])))okay=false;
        for(unsigned i=0;i<(full?std::size(stageTypes):4u);++i)if(FAILED(device->SetTextureStageState(0,stageTypes[i],stages[i])))okay=false;
        if(FAILED(device->SetTextureStageState(1,D3DTSS_COLOROP,stage1)))okay=false;
        if(FAILED(fvf?device->SetFVF(fvf):device->SetVertexDeclaration(declaration))||FAILED(device->SetVertexShader(vertexShader))||
           FAILED(device->SetPixelShader(pixelShader))||
           FAILED(device->SetStreamSource(0,stream,streamOffset,streamStride))||
           FAILED(device->SetStreamSourceFreq(0,streamFrequency)))okay=false;
        if(full){
            for(unsigned i=0;i<std::size(samplerTypes);++i)if(FAILED(device->SetSamplerState(0,samplerTypes[i],samplers[i])))okay=false;
            for(unsigned i=0;i<3;++i)if(FAILED(device->SetTransform(transformTypes[i],&transforms[i])))okay=false;
            for(unsigned i=0;i<8;++i)if(FAILED(device->SetTexture(i,textures[i])))okay=false;
        }
        if(FAILED(device->SetViewport(&viewport))||FAILED(device->SetScissorRect(&scissor)))okay=false;
        if(ownScene&&FAILED(device->EndScene()))okay=false;
        saved=false;return okay;
    }
    ~Scope(){Restore();for(auto& value:target)Release(value);Release(depth);Release(state);
        Release(vertexShader);Release(pixelShader);Release(declaration);Release(stream);
        for(auto& texture:textures)Release(texture);}
};

struct CardTexture {
    IDirect3DTexture9* texture=nullptr;
    std::wstring text;
    unsigned width=0,height=0;
    int font=0;
};
std::array<CardTexture,5> cardTextures;
IDirect3DDevice9* cardDevice=nullptr; // borrowed; retired by render-thread Reset callback
void ForgetCards() {
    for(auto& card:cardTextures){Release(card.texture);card.text.clear();}
    cardDevice=nullptr;
}
bool PrepareCard(IDirect3DDevice9* device,unsigned slot,const std::wstring& text,unsigned width,unsigned height,int size) {
    if(slot>=cardTextures.size()||text.empty()||text.size()>512||!width||width>1024||!height||height>512)return false;
    if(cardDevice!=device){ForgetCards();cardDevice=device;}
    auto& card=cardTextures[slot];
    if(card.texture&&card.text==text&&card.width==width&&card.height==height&&card.font==size)return true;
    Release(card.texture);card.text.clear();
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-static_cast<int>(height);info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    bool okay=bitmap&&pixels&&font;HGDIOBJ oldBitmap=nullptr,oldFont=nullptr;
    if(okay) {
        oldBitmap=SelectObject(dc,bitmap);oldFont=SelectObject(dc,font);
        okay=oldBitmap&&oldBitmap!=HGDI_ERROR&&oldFont&&oldFont!=HGDI_ERROR;
        if(okay) {
            std::memset(pixels,0,std::size_t(width)*height*4);
            SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
            RECT area{8,4,static_cast<int>(width)-8,static_cast<int>(height)-4};
            okay=DrawTextW(dc,text.c_str(),-1,&area,DT_LEFT|DT_NOPREFIX|DT_WORDBREAK)>0&&GdiFlush()!=FALSE;
        }
        if(okay)okay=SUCCEEDED(device->CreateTexture(width,height,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&card.texture,nullptr));
        if(okay) {
            D3DLOCKED_RECT locked{};okay=SUCCEEDED(card.texture->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
            if(okay) {
                if(!locked.pBits||locked.Pitch<static_cast<int>(width*4))okay=false;
                if(okay)for(unsigned y=0;y<height;++y) {
                    auto* output=reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(locked.pBits)+std::size_t(y)*locked.Pitch);
                    const auto* source=static_cast<const std::uint32_t*>(pixels)+std::size_t(y)*width;
                    for(unsigned x=0;x<width;++x) {
                        const auto rgb=source[x],alpha=std::max({rgb&255u,(rgb>>8)&255u,(rgb>>16)&255u});
                        output[x]=(alpha<<24)|0xFFFFFFu;
                    }
                }
                if(FAILED(card.texture->UnlockRect(0)))okay=false;
            }
        }
    }
    if(oldFont&&oldFont!=HGDI_ERROR)SelectObject(dc,oldFont);
    if(oldBitmap&&oldBitmap!=HGDI_ERROR)SelectObject(dc,oldBitmap);
    if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);DeleteDC(dc);
    if(!okay){Release(card.texture);return false;}
    card.text=text;card.width=width;card.height=height;card.font=size;++hudTextureBuilds;return true;
}
} // namespace

namespace HandsRenderer {
void Initialize(const wchar_t* folder) {
    const std::wstring path=std::wstring(folder)+L"vr.ini";
    enabled.store(GetPrivateProfileIntW(L"Hands",L"Enabled",1,path.c_str())!=0);
}
bool Draw(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,
          const VRIPC::EyeMetadata& metadata,const float eyeOffset[3]) {
    validHands.store(0);
    if(!enabled.load()||!device||!backbuffer||!eyeOffset||metadata.poseValid!=VRIPC::kSceneFrame)return false;
    const DWORD now=GetTickCount();
    HandsIPC::Frame latest{};
    const bool fresh=consumer.Open(GetCurrentProcessId())&&consumer.Read(latest,now);
    connected.store(fresh);
    if(!fresh){pairedId=~std::uint64_t(0);return false;}
    if(!latest.present||!latest.mounted||!HandsMath::ValidHeadReference(latest.head))return false;
    const auto pair=metadata.frameId>>1;
    if(pair!=pairedId||paired.recenterAck!=latest.recenterAck){paired=latest;pairedId=pair;}
    ageMs.store(now-paired.tick);
    if(!HandsIPC::Fresh(now,paired.tick))return false;
    // A recenter creates a different tracking coordinate system. Never draw
    // wrists from a new origin over a substantially older image pose.
    if(metadata.sensorTime>0&&paired.sensorTime>0&&std::fabs(metadata.sensorTime-paired.sensorTime)>.25)return false;
    float projection[16];
    if(!ApplyEyeProjection(projection,metadata.fov,.03f,10.f,false))return false;
    std::vector<Vertex> vertices;vertices.reserve(13000);
    unsigned count=0;
    for(unsigned side=0;side<2;++side) {
        const auto& wrist=paired.hands[side];
        if(!HandsMath::ValidPose(wrist))continue;
        const Vec3 reach=Subtract(Vec3{wrist.position[0],wrist.position[1],wrist.position[2]},
            Vec3{paired.head.position[0],paired.head.position[1],paired.head.position[2]});
        if(Dot(reach,reach)>2.25f)continue;
        Vec3 origin,x,y,z;
        if(!HandsMath::EyePoint(wrist,side,{},metadata.headOrientation,metadata.headPosition,eyeOffset,origin)||
           !HandsMath::EyePoint(wrist,side,{1,0,0},metadata.headOrientation,metadata.headPosition,eyeOffset,x)||
           !HandsMath::EyePoint(wrist,side,{0,1,0},metadata.headOrientation,metadata.headPosition,eyeOffset,y)||
           !HandsMath::EyePoint(wrist,side,{0,0,1},metadata.headOrientation,metadata.headPosition,eyeOffset,z))continue;
        Builder builder{origin,Subtract(x,origin),Subtract(y,origin),Subtract(z,origin),vertices};
        if(builder.Hand(side,paired.grip[side],paired.trigger[side]))++count;
    }
    validHands.store(count);
    if(!count||vertices.empty())return false;
    // The game's final depth may belong to a different postprocess target.
    // Paint the glove triangles from back to front for self-occlusion while
    // leaving that native depth untouched.
    std::vector<std::array<Vertex,3>> triangles(vertices.size()/3);
    for(std::size_t i=0;i<triangles.size();++i)
        for(unsigned j=0;j<3;++j)triangles[i][j]=vertices[i*3+j];
    std::sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b){
        return a[0].z+a[1].z+a[2].z>b[0].z+b[1].z+b[2].z;
    });
    for(std::size_t i=0;i<triangles.size();++i)
        for(unsigned j=0;j<3;++j)vertices[i*3+j]=triangles[i][j];
    Scope scope{device};
    if(!scope.Save()){++failures;return false;}
    const HRESULT begin=device->BeginScene();
    if(FAILED(begin)&&begin!=D3DERR_INVALIDCALL){++failures;return false;}
    scope.ownScene=SUCCEEDED(begin);
    bool okay=true;
    const auto check=[&](HRESULT result){if(FAILED(result))okay=false;};
    for(unsigned i=1;i<scope.targets;++i)check(device->SetRenderTarget(i,nullptr));
    check(device->SetDepthStencilSurface(nullptr));check(device->SetRenderTarget(0,backbuffer));
    D3DSURFACE_DESC description{};
    check(backbuffer->GetDesc(&description));
    if(!description.Width||!description.Height){++failures;return false;}
    const D3DVIEWPORT9 viewport{0,0,description.Width,description.Height,0.f,1.f};
    check(device->SetViewport(&viewport));
    D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1.f;
    D3DMATRIX proj;std::memcpy(&proj,projection,sizeof(proj));
    check(device->SetTransform(D3DTS_WORLD,&identity));check(device->SetTransform(D3DTS_VIEW,&identity));
    check(device->SetTransform(D3DTS_PROJECTION,&proj));
    check(device->SetVertexShader(nullptr));check(device->SetPixelShader(nullptr));
    check(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));
    for(unsigned i=0;i<8;++i)check(device->SetTexture(i,nullptr));
    check(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    check(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE));
    check(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    check(device->SetRenderState(D3DRS_LIGHTING,FALSE));check(device->SetRenderState(D3DRS_FOGENABLE,FALSE));
    check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(device->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE));
    check(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));check(device->SetRenderState(D3DRS_STENCILENABLE,FALSE));
    check(device->SetRenderState(D3DRS_SPECULARENABLE,FALSE));check(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));check(device->SetRenderState(D3DRS_CLIPPING,TRUE));
    check(device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf));check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    check(device->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE));check(device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE));
    check(device->SetRenderState(D3DRS_CLIPPLANEENABLE,0));check(device->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD));
    check(device->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    if(okay)check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(vertices.size()/3),vertices.data(),sizeof(Vertex)));
    const bool restored=scope.Restore();
    if(!okay||!restored){++failures;return false;}
    ++frames;handCount+=count;return true;
}
void InvalidateDevice(){ForgetCards();hudReady=false;}
static bool DrawAimGeometry(IDirect3DDevice9* device,const NativeAimRenderKL::Vertex* vertices,unsigned count,bool depthWrite){
    bool okay=true;const auto check=[&](HRESULT result){if(FAILED(result))okay=false;};
    check(device->SetVertexShader(nullptr));check(device->SetPixelShader(nullptr));
    check(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1));check(device->SetStreamSourceFreq(0,1));
    check(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    check(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
    check(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    check(device->SetRenderState(D3DRS_LIGHTING,FALSE));check(device->SetRenderState(D3DRS_FOGENABLE,FALSE));
    check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,!depthWrite));check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));check(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    check(device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
    check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(device->SetRenderState(D3DRS_ZENABLE,depthWrite?D3DZB_TRUE:D3DZB_FALSE));
    check(device->SetRenderState(D3DRS_ZWRITEENABLE,depthWrite));check(device->SetRenderState(D3DRS_STENCILENABLE,FALSE));
    check(device->SetRenderState(D3DRS_SPECULARENABLE,FALSE));check(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));check(device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf));
    check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));check(device->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE));
    check(device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE));check(device->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
    check(device->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD));check(device->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    if(okay)check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices,sizeof(NativeAimRenderKL::Vertex)));
    return okay;
}
bool DrawAim(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeAimRenderKL::Frame& frame){
    ProfileTiming::Scope timing(ProfileTiming::AimOverlay);
    if(!device||!backbuffer)return false;
    D3DSURFACE_DESC description{};NativeAimRenderKL::Geometry geometry;
    if(FAILED(backbuffer->GetDesc(&description))||!NativeAimRenderKL::Build(frame,description.Width,description.Height,geometry))return false;
    Scope scope{device};if(!scope.Save(false))return false;
    const auto scene=device->BeginScene();if(FAILED(scene)&&scene!=D3DERR_INVALIDCALL)return false;scope.ownScene=SUCCEEDED(scene);
    if(!scope.BindAimTarget(backbuffer))return false;
    const D3DVIEWPORT9 viewport{0,0,description.Width,description.Height,0,1};
    const bool drawn=SUCCEEDED(device->SetViewport(&viewport))&&DrawAimGeometry(device,geometry.vertices.data(),geometry.count,false);
    const bool restored=scope.Restore();
    return drawn&&restored;
}
unsigned DrawReloadProps(IDirect3DDevice9* device,const NativeAimRenderKL::Frame* frames,unsigned count){
    ProfileTiming::Scope timing(ProfileTiming::ReloadOverlay);
    constexpr unsigned maxItems=4;
    if(!device||!frames||!count||count>maxItems)return 0;
    Scope scope{device};
    if(!scope.Save(false)||!scope.depth)return 0;
    D3DSURFACE_DESC colour{},depth{};
    if(FAILED(scope.target[0]->GetDesc(&colour))||FAILED(scope.depth->GetDesc(&depth))||
       colour.MultiSampleType!=depth.MultiSampleType||colour.MultiSampleQuality!=depth.MultiSampleQuality||
       depth.Width<colour.Width||depth.Height<colour.Height)return 0;
    const auto& viewport=scope.viewport;
    NativeAimRenderKL::PropGeometry geometry;
    std::array<NativeAimRenderKL::Vertex,maxItems*std::tuple_size<decltype(geometry.vertices)>::value> vertices;
    unsigned used=0,rendered=0;
    for(unsigned i=0;i<count;++i){
        if(!NativeAimRenderKL::BuildProp(frames[i],viewport.Width,viewport.Height,geometry))continue;
        for(unsigned n=0;n<geometry.count;++n){
            auto vertex=geometry.vertices[n];
            vertex.x+=float(viewport.X);vertex.y+=float(viewport.Y);
            vertex.z=viewport.MinZ+vertex.z*(viewport.MaxZ-viewport.MinZ);
            vertices[used++]=vertex;
        }
        ++rendered;
    }
    if(!used)return 0;

    // Keep the game's actual scene surfaces and viewport bound. Both the
    // already submitted world and the following arm draw can occlude this batch.
    DWORD previousZFunc=0;
    if(FAILED(device->GetRenderState(D3DRS_ZFUNC,&previousZFunc))||
       FAILED(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL)))return 0;
    const bool drawn=DrawAimGeometry(device,vertices.data(),used,true);
    const bool depthRestored=SUCCEEDED(device->SetRenderState(D3DRS_ZFUNC,previousZFunc));
    const bool restored=scope.Restore();
    return drawn&&depthRestored&&restored?rendered:0;
}
static bool FreshWorldMarkers(const NativeHandHudKL::Frame& frame,std::uint32_t now) {
    if(!frame.player||!frame.pairId||!frame.tick||frame.recenterAck==UINT32_MAX||
       std::int32_t(now-frame.tick)<0||now-frame.tick>150||
       !std::isfinite(frame.unitsPerMetre)||frame.unitsPerMetre<1||frame.unitsPerMetre>1000||
       !NativeCameraMath::ValidatePose(frame.actualEye,.05f)||
       !NativeTeamMarkerKL::Fresh(frame.commandMarkers,frame.player,now))return false;
    for(float value:frame.projection)if(!std::isfinite(value))return false;
    return frame.projection[0]>0&&frame.projection[5]>0&&
        std::fabs(frame.projection[11]-1.f)<.0001f&&std::fabs(frame.projection[15])<.0001f&&frame.projection[14]<0;
}
static bool DrawHudPass(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeHandHudKL::Frame& frame,
                        bool markerOnly,bool* contextDrawn) {
    if(contextDrawn)*contextDrawn=false;
    hudReady=false;
    const auto now=GetTickCount();
    if(!device||!backbuffer||!(markerOnly?FreshWorldMarkers(frame,now):NativeHandHudKL::Fresh(frame,now))){++hudRejected;return false;}
    const auto* action=frame.teamHeld||!frame.gameHints?nullptr:NativeHandHudKL::ActionText(frame.telemetry);
    const bool markers=NativeTeamMarkerKL::Fresh(frame.commandMarkers,frame.player,GetTickCount())&&frame.commandMarkers.count;
    if(!frame.held&&!action&&!frame.weaponMenuHeld&&!markers){hudReady=true;return true;}
    D3DSURFACE_DESC description{};
    if(FAILED(backbuffer->GetDesc(&description))||!description.Width||!description.Height||
       description.Width>16384||description.Height>16384){++hudFailures;return false;}
    struct Card {unsigned slot=0;std::array<NativeHandHudKL::Vertex,6> vertices{};};
    std::array<Card,5> cards{};unsigned count=0;
    const auto add=[&](unsigned slot,unsigned side,const std::wstring& text,float width,float height,float across,float above,
                       unsigned pixelsWidth,unsigned pixelsHeight,int font) {
        NativeHandHudKL::Panel panel;Card next;next.slot=slot;
        if(!NativeHandHudKL::NearHand(frame,side,width,height,across,above,panel)||
           !NativeHandHudKL::Quad(panel,frame.actualEye,frame.projection,description.Width,description.Height,next.vertices))return;
        if(!PrepareCard(device,slot,text,pixelsWidth,pixelsHeight,font)){++hudFailures;return;}
        cards[count++]=next;
    };
    if(frame.held) {
        if(frame.controlHints)for(unsigned side=0;side<2;++side)add(side,side,NativeHandHudKL::ControlsText(side,frame.supportGripEnabled),.30f,.25f,side?.04f:-.04f,-.15f,512,512,32);
    }
    if(action)add(3,0,action,.25f,.05f,0,.12f,512,128,36);
    if(frame.weaponMenuHeld&&frame.controlHints)add(4,1,L"WEAPONS\nHold Y; right stick: select\nRelease Y: equip\nNear teammate: repeat direction\nto select their ring / exchange",.40f,.19f,0,.19f,640,384,32);
    NativeTeamMarkerMath::Geometry markerGeometry;
    if(markers&&!NativeTeamMarkerMath::Build(frame,description.Width,description.Height,GetTickCount(),markerGeometry)){++hudRejected;++commandMarkerRejected;return false;}
    if(!count&&!markerGeometry.count){hudReady=true;return true;}
    Scope scope{device};if(!scope.Save()){++hudFailures;return false;}
    const auto scene=device->BeginScene();
    if(FAILED(scene)&&scene!=D3DERR_INVALIDCALL){++hudFailures;return false;}
    scope.ownScene=SUCCEEDED(scene);
    bool okay=true;const auto check=[&](HRESULT result){if(FAILED(result))okay=false;};
    for(unsigned i=1;i<scope.targets;++i)check(device->SetRenderTarget(i,nullptr));
    check(device->SetDepthStencilSurface(nullptr));check(device->SetRenderTarget(0,backbuffer));
    const D3DVIEWPORT9 viewport{0,0,description.Width,description.Height,0,1};check(device->SetViewport(&viewport));
    check(device->SetVertexShader(nullptr));check(device->SetPixelShader(nullptr));
    check(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1));check(device->SetStreamSourceFreq(0,1));
    check(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
    check(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
    check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
    check(device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));
    check(device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
    check(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    check(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
    check(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
    check(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    check(device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
    check(device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    check(device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
    check(device->SetRenderState(D3DRS_LIGHTING,FALSE));check(device->SetRenderState(D3DRS_FOGENABLE,FALSE));
    check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));check(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    check(device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
    check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(device->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE));
    check(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));check(device->SetRenderState(D3DRS_STENCILENABLE,FALSE));
    check(device->SetRenderState(D3DRS_SPECULARENABLE,FALSE));check(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    check(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));check(device->SetRenderState(D3DRS_COLORWRITEENABLE,0xF));
    check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));check(device->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE));
    check(device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE));check(device->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
    check(device->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD));check(device->SetRenderState(D3DRS_MULTISAMPLEMASK,0xFFFFFFFFu));
    bool contextRendered=false;
    if(okay&&markerGeometry.count) {
        check(device->SetTexture(0,nullptr));
        check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
        check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
        if(okay)check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,markerGeometry.count/3,markerGeometry.vertices.data(),sizeof(NativeHandHudKL::Vertex)));
        check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
        check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
    }
    if(okay)for(unsigned i=0;i<count;++i) {
        check(device->SetTexture(0,cardTextures[cards[i].slot].texture));
        if(okay)check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,cards[i].vertices.data(),sizeof(NativeHandHudKL::Vertex)));
        if(okay&&cards[i].slot==3)contextRendered=true;
    }
    const bool restored=scope.Restore();
    if(!okay||!restored){++hudFailures;return false;}
    if(contextDrawn)*contextDrawn=contextRendered;
    if(markerGeometry.markers){++commandMarkerFrames;commandMarkers+=markerGeometry.markers;}
    hudReady=true;++hudFrames;hudCards+=count;return true;
}
bool DrawHud(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeHandHudKL::Frame& frame,bool* contextDrawn) {
    return DrawHudPass(device,backbuffer,frame,false,contextDrawn);
}
bool DrawWorldMarkers(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,const NativeHandHudKL::Frame& source) {
    // Validate before the bounded filter; use the game's current snapshot,
    // never a remembered order or a fabricated controller tracking pose.
    if(!FreshWorldMarkers(source,GetTickCount())){++hudRejected;++commandMarkerRejected;return false;}
    NativeHandHudKL::Frame frame;
    frame.player=source.player;frame.pairId=source.pairId;frame.tick=source.tick;frame.recenterAck=source.recenterAck;
    frame.unitsPerMetre=source.unitsPerMetre;frame.actualEye=source.actualEye;
    std::memcpy(frame.projection,source.projection,sizeof(frame.projection));
    frame.nativeCommandMarkers=false;frame.commandMarkers=source.commandMarkers;frame.commandMarkers.count=0;
    for(unsigned i=0;i<source.commandMarkers.count;++i){
        const auto& marker=source.commandMarkers.markers[i];
        if(marker.fixed&&marker.icon==NativeTeamMarkerKL::Icon::Shield)
            frame.commandMarkers.markers[frame.commandMarkers.count++]=marker;
    }
    return DrawHudPass(device,backbuffer,frame,true,nullptr);
}
Stats Snapshot(){return {enabled.load(),connected.load(),validHands.load(),ageMs.load(),frames.load(),handCount.load(),failures.load(),
    hudReady.load(),hudFrames.load(),hudCards.load(),hudRejected.load(),hudFailures.load(),hudTextureBuilds.load(),commandMarkerFrames.load(),commandMarkers.load(),commandMarkerRejected.load()};}
} // namespace HandsRenderer
