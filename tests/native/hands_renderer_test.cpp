#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "hands_renderer.hpp"
#include "hands_ipc.hpp"
#include "native_hud_test_fixture.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool value,const char* message) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void Hr(HRESULT result,const char* message){if(FAILED(result))std::fprintf(stderr,"HRESULT: 0x%08lx\n",static_cast<unsigned long>(result));Check(SUCCEEDED(result),message);}
template<class T> void Release(T*& value){if(value){value->Release();value=nullptr;}}
HandsIPC::Pose Pose(float x,float y,float z) {
    HandsIPC::Pose pose{};pose.orientation[3]=1;pose.position[0]=x;pose.position[1]=y;pose.position[2]=z;
    pose.flags=HandsIPC::kOrientation|HandsIPC::kPosition;return pose;
}
struct Coverage {unsigned pixels=0;double x=0;unsigned minX=UINT_MAX,maxX=0,minY=UINT_MAX,maxY=0;};
Coverage Pixels(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer) {
    D3DSURFACE_DESC desc{};Hr(backbuffer->GetDesc(&desc),"read backbuffer descriptor");
    IDirect3DSurface9* system=nullptr;
    Hr(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&system,nullptr),"create readback surface");
    Hr(device->GetRenderTargetData(backbuffer,system),"read rendered hand pixels");
    D3DLOCKED_RECT lock{};Hr(system->LockRect(&lock,nullptr,D3DLOCK_READONLY),"lock readback");
    Coverage result;
    for(unsigned y=0;y<desc.Height;++y) {
        const auto* row=reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch);
        for(unsigned x=0;x<desc.Width;++x)if(row[x]&0xffffff){++result.pixels;result.x+=x;
            result.minX=std::min(result.minX,x);result.maxX=std::max(result.maxX,x);result.minY=std::min(result.minY,y);result.maxY=std::max(result.maxY,y);}
    }
    if(result.pixels)result.x/=result.pixels;
    system->UnlockRect();Release(system);return result;
}
}
int main(int argc,char** argv) {
    NativeHudFixture::Image nativeImage;
    if(argc>1){Check(nativeImage.Load(argv[1]),"load supported PE read-only for native marker filter/atlas proof");
        Check(NativeTeamMarkerKL::VerifyImage(NativeHudFixture::Base,nativeImage.Reader()),"HAL fixture uses actual native marker table and player-index filter proof");}
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"KaneHandsFixture";
    RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"Kane hands fixture",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
    Check(window!=nullptr,"create hidden fixture window");
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"create D3D9 API");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9* device=nullptr;
    HRESULT result=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device);
    const bool hal=SUCCEEDED(result);
    if(!hal) {
        std::printf("SKIP: HAL unavailable (0x%08lx); trying NULLREF for render-state checks only.\n",static_cast<unsigned long>(result));
        result=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device);
    }
    if(FAILED(result)) {
        std::printf("SKIP: NULLREF also unavailable (0x%08lx); live D3D9 fixture cannot run.\n",static_cast<unsigned long>(result));
        Release(api);DestroyWindow(window);return 0;
    }
    IDirect3DSurface9* backbuffer=nullptr,*original=nullptr,*depth=nullptr;
    if(hal)Hr(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer),"get backbuffer");
    else {
        // A NULLREF device has no swapchain/backbuffer. A normal render target
        // still exercises the production pass's explicit target restoration.
        Hr(device->CreateRenderTarget(320,240,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&backbuffer,nullptr),"create NULLREF simulated backbuffer");
        Hr(device->SetRenderTarget(0,backbuffer),"bind NULLREF simulated backbuffer");
    }
    Hr(device->CreateRenderTarget(320,240,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&original,nullptr),"create native offscreen target");
    if(FAILED(device->GetDepthStencilSurface(&depth))) {
        Hr(device->CreateDepthStencilSurface(320,240,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr),"create fixture native depth");
        Hr(device->SetDepthStencilSurface(depth),"bind fixture native depth");
    }
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"clear black backbuffer");
    Hr(device->SetRenderTarget(0,original),"bind native offscreen target");
    const D3DVIEWPORT9 viewport{7,9,301,211,.2f,.8f};const RECT scissor{10,12,200,190};
    Hr(device->SetViewport(&viewport),"set native viewport");Hr(device->SetScissorRect(&scissor),"set native scissor");
    Hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"set native scissor enable");
    Hr(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"set native alpha blend");
    DWORD expectedScissor=0,expectedBlend=0;
    Hr(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&expectedScissor),"read fixture scissor baseline");
    Hr(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&expectedBlend),"read fixture blend baseline");
    if(!hal&&(expectedScissor!=TRUE||expectedBlend!=TRUE))std::puts("SKIP: NULLREF ignores some render states; state restoration is compared with reported baseline.");
    D3DMATRIX world{};world._11=world._22=world._33=world._44=1;world._41=37;
    Hr(device->SetTransform(D3DTS_WORLD,&world),"set native world matrix");
    IDirect3DVertexBuffer9* stream=nullptr;
    Hr(device->CreateVertexBuffer(64,0,D3DFVF_XYZ|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,&stream,nullptr),"create native source stream");
    Hr(device->SetStreamSource(0,stream,0,16),"bind native source stream");
    Hr(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"set native declaration");
    IDirect3DVertexDeclaration9* declaration=nullptr;Hr(device->GetVertexDeclaration(&declaration),"get native declaration");
    // Minimal legal shader-model 1.1 pass-through programs. The fixture
    // verifies that the fixed-function hand pass restores both shaders.
    const DWORD vsTokens[]={0xfffe0101,0x0000001f,0x80000000,0x900f0000,
        0x00000001,0xc00f0000,0x90e40000,0x0000ffff};
    const DWORD psTokens[]={0xffff0101,0x00000001,0x800f0000,0x90e40000,0x0000ffff};
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    const HRESULT vertexResult=device->CreateVertexShader(vsTokens,&vs),pixelResult=device->CreatePixelShader(psTokens,&ps);
    if(hal){Hr(vertexResult,"create native vertex shader");Hr(pixelResult,"create native pixel shader");}
    else if(FAILED(vertexResult)||FAILED(pixelResult))
        std::puts("SKIP: NULLREF does not provide shader objects; shader restoration checks use null bindings.");
    Hr(device->SetVertexShader(vs),"bind native vertex shader");Hr(device->SetPixelShader(ps),"bind native pixel shader");
    HandsIPC::Producer producer;Check(producer.Open(GetCurrentProcessId()),"create fixture tracking producer");
    HandsIPC::Frame tracking{};tracking.head=Pose(0,1.7f,0);tracking.hands[0]=Pose(-.15f,1.4f,-.5f);
    tracking.hands[1]=Pose(.15f,1.4f,-.5f);tracking.present=tracking.mounted=1;
    tracking.tick=GetTickCount();producer.Publish(tracking);
    HandsRenderer::Initialize(L"C:\\__KaneHandsFixtureNoConfig__\\");
    VRIPC::EyeMetadata metadata{};metadata.poseValid=VRIPC::kSceneFrame;metadata.frameId=2;
    metadata.headOrientation[3]=1;metadata.headPosition[1]=1.7f;
    for(auto& tangent:metadata.fov)tangent=1;
    const float leftOffset[3]={-.032f,0,0},rightOffset[3]={.032f,0,0};
    Hr(device->BeginScene(),"begin native scene");
    Check(HandsRenderer::Draw(device,backbuffer,metadata,leftOffset),"hand pass draws inside existing native BeginScene/EndScene");
    Hr(device->EndScene(),"hand pass preserves native open scene");
    IDirect3DSurface9* actualTarget=nullptr,*actualDepth=nullptr;
    Hr(device->GetRenderTarget(0,&actualTarget),"query restored native target");
    Hr(device->GetDepthStencilSurface(&actualDepth),"query restored native depth");
    Check(actualTarget==original&&actualDepth==depth,"native render target and depth restored");Release(actualTarget);Release(actualDepth);
    D3DVIEWPORT9 actualViewport{};RECT actualScissor{};DWORD state=0;D3DMATRIX actualWorld{};
    Hr(device->GetViewport(&actualViewport),"query viewport");Hr(device->GetScissorRect(&actualScissor),"query scissor");
    Check(actualViewport.X==viewport.X&&actualViewport.Y==viewport.Y&&actualViewport.Width==viewport.Width&&actualViewport.Height==viewport.Height&&
          actualViewport.MinZ==viewport.MinZ&&actualViewport.MaxZ==viewport.MaxZ,"native viewport including depth range restored");
    Check(EqualRect(&scissor,&actualScissor)!=FALSE,"native scissor rectangle restored");
    Hr(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&state),"query scissor state");Check(state==expectedScissor,"scissor enable restored");
    Hr(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&state),"query blending");Check(state==expectedBlend,"native alpha blending restored");
    Hr(device->GetTransform(D3DTS_WORLD,&actualWorld),"query world matrix");Check(actualWorld._41==37,"native world matrix restored");
    IDirect3DVertexShader9* actualVs=nullptr;IDirect3DPixelShader9* actualPs=nullptr;
    Hr(device->GetVertexShader(&actualVs),"query vertex shader");Hr(device->GetPixelShader(&actualPs),"query pixel shader");
    Check(actualVs==vs&&actualPs==ps,"native vertex and pixel shaders restored");Release(actualVs);Release(actualPs);
    IDirect3DVertexBuffer9* actualStream=nullptr;UINT offset=0,stride=0;
    Hr(device->GetStreamSource(0,&actualStream,&offset,&stride),"query source stream");
    Check(actualStream==stream&&offset==0&&stride==16,"DrawPrimitiveUP source stream side effect restored");Release(actualStream);
    IDirect3DVertexDeclaration9* actualDeclaration=nullptr;
    Hr(device->GetVertexDeclaration(&actualDeclaration),"query vertex declaration");
    Check(actualDeclaration==declaration,"native vertex declaration restored");Release(actualDeclaration);
    const Coverage left=hal?Pixels(device,backbuffer):Coverage{};
    Hr(device->SetRenderTarget(0,backbuffer),"bind right-eye clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear right-eye image");
    Hr(device->SetRenderTarget(0,original),"rebind native target");
    // A changed newest input must not change wrist placement halfway through
    // the rendered pair: only the eye offset changes stereo disparity.
    tracking.hands[0].position[0]+=.7f;tracking.hands[1].position[0]+=.7f;
    tracking.tick=GetTickCount();producer.Publish(tracking);metadata.frameId=3;
    Check(HandsRenderer::Draw(device,backbuffer,metadata,rightOffset),"right-eye pass draws outside native scene");
    if(hal) {
        const Coverage right=Pixels(device,backbuffer);
        Check(left.pixels>200&&right.pixels>200,"HAL readback contains actual volumetric hand pixels");
        Check(left.x-right.x>10&&left.x-right.x<30,"paired wrists retain position and receive correct physical IPD disparity");
        std::printf("HAL hand coverage: %u / %u pixels; stereo centroid disparity %.2f pixels.\n",left.pixels,right.pixels,left.x-right.x);
    } else std::puts("SKIP: NULLREF has no rasterizer; pixel coverage/stereo disparity checks skipped.");
    tracking.hands[0].flags=tracking.hands[1].flags=0;tracking.tick=GetTickCount();producer.Publish(tracking);metadata.frameId=4;
    Check(!HandsRenderer::Draw(device,backbuffer,metadata,leftOffset)&&HandsRenderer::Snapshot().validHands==0,"position tracking loss hides both hands");
    producer.Close();Check(!HandsRenderer::Draw(device,backbuffer,metadata,leftOffset),"closed tracking producer hides hands");
    Check(HandsRenderer::Snapshot().failures==0,"hand renderer reports no graphics failures");
    // Native world wrists drive information cards independently of the
    // optional glove IPC. Exercise the production textured pass and all
    // native state it changes, including sampler/alpha/stream restoration.
    NativeHandHudKL::Frame hud;hud.player=0x11000000;hud.pairId=5;hud.tick=GetTickCount();hud.recenterAck=0;hud.held=true;
    hud.tracked[0]=hud.tracked[1]=true;hud.wrist[0].position={50,-10,18};hud.wrist[1].position={50,-10,-18};
    hud.actualEye.position=NativeCameraMath::Scale(hud.centre.basis.right,-3.2f);
    const float hudFov[]={1,1,1,1};
    Check(NativeCameraMath::ApplyEyeProjection(hud.projection,hudFov,3,10000,false),"build native-world HUD projection");
    hud.telemetry.ammoKnown=true;hud.telemetry.magazine=27;hud.telemetry.reserve=90;
    hud.telemetry.contextKnown=true;hud.telemetry.action=NativeHandHudKL::Action::PickupWeapon;
    Hr(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT),"set native HUD sampler baseline");
    Hr(device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"set native HUD addressing baseline");
    Hr(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"set native HUD source blend baseline");
    Hr(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"set native HUD destination blend baseline");
    DWORD expectedMin=0,expectedAddress=0,expectedSource=0,expectedDestination=0;
    Hr(device->GetSamplerState(0,D3DSAMP_MINFILTER,&expectedMin),"read actual native min filter");
    Hr(device->GetSamplerState(0,D3DSAMP_ADDRESSU,&expectedAddress),"read actual native texture addressing");
    Hr(device->GetRenderState(D3DRS_SRCBLEND,&expectedSource),"read actual native source blend");
    Hr(device->GetRenderState(D3DRS_DESTBLEND,&expectedDestination),"read actual native destination blend");
    Hr(device->SetRenderTarget(0,backbuffer),"bind HUD clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear HUD image");
    Hr(device->SetRenderTarget(0,original),"restore native target before HUD");
    const auto buildsBefore=HandsRenderer::Snapshot().hudTextureBuilds;
    Hr(device->BeginScene(),"begin native scene around HUD");
    bool contextDrawn=false;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&contextDrawn,"HUD draws actual context card with native world wrists after glove producer closes");
    Hr(device->EndScene(),"HUD preserves caller's native scene");
    Check(HandsRenderer::Snapshot().hudTextureBuilds==buildsBefore+3,"two control cards and the action card each build one texture");
    Hr(device->GetRenderTarget(0,&actualTarget),"query target after textured HUD");
    Hr(device->GetDepthStencilSurface(&actualDepth),"query depth after textured HUD");
    Check(actualTarget==original&&actualDepth==depth,"HUD preserves native target and depth");Release(actualTarget);Release(actualDepth);
    Hr(device->GetSamplerState(0,D3DSAMP_MINFILTER,&state),"query filter after HUD");Check(state==expectedMin,"HUD restores native texture filtering");
    Hr(device->GetSamplerState(0,D3DSAMP_ADDRESSU,&state),"query addressing after HUD");Check(state==expectedAddress,"HUD restores native texture addressing");
    Hr(device->GetRenderState(D3DRS_SRCBLEND,&state),"query source blend after HUD");Check(state==expectedSource,"HUD restores native source alpha mode");
    Hr(device->GetRenderState(D3DRS_DESTBLEND,&state),"query destination blend after HUD");Check(state==expectedDestination,"HUD restores native destination alpha mode");
    Hr(device->GetVertexShader(&actualVs),"query vertex shader after HUD");Hr(device->GetPixelShader(&actualPs),"query pixel shader after HUD");
    Check(actualVs==vs&&actualPs==ps,"HUD restores native material shaders");Release(actualVs);Release(actualPs);
    Hr(device->GetStreamSource(0,&actualStream,&offset,&stride),"query stream after HUD");
    Check(actualStream==stream&&offset==0&&stride==16,"textured HUD restores native stream");Release(actualStream);
    const Coverage hudLeft=hal?Pixels(device,backbuffer):Coverage{};
    Hr(device->SetRenderTarget(0,backbuffer),"bind second HUD eye clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear second HUD eye");
    Hr(device->SetRenderTarget(0,original),"restore native target before second HUD eye");
    hud.actualEye.position=NativeCameraMath::Scale(hud.centre.basis.right,3.2f);hud.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud),"HUD renders second eye with same current pair data");
    Check(HandsRenderer::Snapshot().hudTextureBuilds==buildsBefore+3,"unchanged card text reuses textures between eyes");
    if(hal){const Coverage hudRight=Pixels(device,backbuffer);
        Check(hudLeft.pixels>30&&hudRight.pixels>30,"both eyes contain actual text pixels");
        Check(hudLeft.x-hudRight.x>10&&hudLeft.x-hudRight.x<30,"world-space HUD receives proper stereo disparity");}
    const auto cardsBefore=HandsRenderer::Snapshot().hudCards;
    hud.held=false;hud.telemetry.contextKnown=false;hud.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn&&HandsRenderer::Snapshot().hudCards==cardsBefore,"Y release removes held cards immediately and authorizes no context suppression");
    hud.held=true;hud.tick=GetTickCount();contextDrawn=true;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn,"held control cards alone never authorize native action suppression");
    hud.telemetry.contextKnown=true;hud.tracked[0]=false;hud.tick=GetTickCount();contextDrawn=true;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn,"missing X hand retains native action prompt while right-hand information draws");
    hud.tracked[0]=true;hud.tracked[1]=false;hud.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&contextDrawn,"valid X hand can display actual context when opposite hand is missing");
    hud.held=true;hud.tracked[0]=hud.tracked[1]=false;
    Check(!HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn,"HUD tracking loss creates no screen-centre fallback or suppression authorization");
    HandsRenderer::InvalidateDevice();Check(!HandsRenderer::Snapshot().hudReady,"Reset callback retires card resources and readiness");
    hud.tracked[0]=hud.tracked[1]=true;hud.telemetry.contextKnown=true;hud.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud)&&HandsRenderer::Snapshot().hudTextureBuilds==buildsBefore+6,"device invalidation safely rebuilds cards");
    hud.controlHints=false;hud.weaponMenuHeld=true;hud.tick=GetTickCount();contextDrawn=true;
    const auto hintsOff=HandsRenderer::Snapshot();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&contextDrawn&&HandsRenderer::Snapshot().hudCards==hintsOff.hudCards+1,
          "VR control hints off leaves only the default X action card");
    hud.gameHints=false;hud.tick=GetTickCount();contextDrawn=true;const auto gameOff=HandsRenderer::Snapshot().hudCards;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn&&HandsRenderer::Snapshot().hudCards==gameOff,
          "both hints switches off leaves native weapon and ammo drawing to the game HUD");
    hud.gameHints=true;
    hud.controlHints=true;hud.weaponMenuHeld=false;
    hud.held=false;hud.teamHeld=true;hud.tick=GetTickCount();
    const auto teamBefore=HandsRenderer::Snapshot();
    contextDrawn=true;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud,&contextDrawn)&&!contextDrawn&&
          HandsRenderer::Snapshot().hudCards==teamBefore.hudCards&&
          HandsRenderer::Snapshot().hudTextureBuilds==teamBefore.hudTextureBuilds,
          "native team UI suppresses the local action prompt and creates no duplicate wrist card");
    Check(HandsRenderer::Snapshot().hudFailures==0,"HUD reports no graphics or restore failures");
    hud.teamHeld=false;hud.weaponMenuHeld=true;hud.telemetry.contextKnown=false;hud.tick=GetTickCount();
    const auto weaponCards=HandsRenderer::Snapshot().hudCards;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud)&&HandsRenderer::Snapshot().hudCards==weaponCards+1,
          "weapon wheel help uses the right wrist and its own cached card");
    hud.weaponMenuHeld=false;hud.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud)&&HandsRenderer::Snapshot().hudCards==weaponCards+1,
          "weapon menu release removes its help immediately");
    hud.held=hud.teamHeld=hud.weaponMenuHeld=false;hud.telemetry.contextKnown=false;
    NativeHudFixture::Memory nativeMarkers;nativeMarkers.image=argc>1?&nativeImage:nullptr;nativeMarkers.Markers();
    hud.commandMarkers=NativeTeamMarkerKL::Capture(NativeHudFixture::Base,NativeHudFixture::Player,GetTickCount(),nativeMarkers.Reader());
    Check(hud.commandMarkers.valid&&hud.commandMarkers.count==3,"native index0 records produce the three actual command marker identities before GPU rendering");
    hud.commandMarkers.player=hud.player;
    const auto retainedBefore=HandsRenderer::Snapshot().commandMarkers;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud)&&HandsRenderer::Snapshot().commandMarkers==retainedBefore,
          "retained original native world markers do not acquire duplicate custom glyphs");
    hud.nativeCommandMarkers=false;
    hud.actualEye.position=NativeCameraMath::Scale(hud.centre.basis.right,-3.2f);hud.tick=hud.commandMarkers.tick=GetTickCount();
    Hr(device->SetRenderTarget(0,backbuffer),"bind command marker clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear command marker first eye");
    Hr(device->SetRenderTarget(0,original),"restore native target before command markers");
    const auto markerBefore=HandsRenderer::Snapshot();const auto markerCards=markerBefore.hudCards;
    Check(HandsRenderer::DrawHud(device,backbuffer,hud),"native world command icons render without an open wrist menu");
    Check(HandsRenderer::Snapshot().hudCards==markerCards,"world command icons need no extra cached card or texture");
    Check(HandsRenderer::Snapshot().commandMarkerFrames==markerBefore.commandMarkerFrames+1&&
          HandsRenderer::Snapshot().commandMarkers==markerBefore.commandMarkers+3,"world marker diagnostics count completed visible draw geometry");
    const auto markerLeft=hal?Pixels(device,backbuffer):Coverage{};
    Hr(device->GetRenderTarget(0,&actualTarget),"query target after command markers");
    Check(actualTarget==original,"world command marker pass preserves native render target");Release(actualTarget);
    Hr(device->SetRenderTarget(0,backbuffer),"bind command marker second eye clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear command marker second eye");
    Hr(device->SetRenderTarget(0,original),"restore native target before second marker eye");
    hud.actualEye.position=NativeCameraMath::Scale(hud.centre.basis.right,3.2f);hud.tick=hud.commandMarkers.tick=GetTickCount();
    Check(HandsRenderer::DrawHud(device,backbuffer,hud),"command markers render at the second actual eye");
    if(hal){const auto markerRight=Pixels(device,backbuffer);
        Check(markerLeft.pixels>100&&markerRight.pixels>100,"actual shield lightning and circle produce visible pixels");
        Check(markerLeft.x-markerRight.x>2&&markerLeft.x-markerRight.x<6,"world order markers have target-depth stereo disparity instead of duplicate screen coordinates");}
    // Main BeforeCapture has only the applied camera frame here: it never
    // copies tracked wrists from BuildHandHud. Native fixed type21 has the
    // hidden local player as lifetime owner, plus separate recipient records.
    nativeMarkers.Set(NativeHudFixture::Records+0x64,NativeHudFixture::Player);
    nativeMarkers.Set(NativeHudFixture::Records+0x64+0x1C,std::uint32_t(21));
    nativeMarkers.Set(NativeHudFixture::Records+0x64+0x4D,std::uint8_t(1));
    hud.commandMarkers=NativeTeamMarkerKL::Capture(NativeHudFixture::Base,NativeHudFixture::Player,GetTickCount(),nativeMarkers.Reader());
    Check(hud.commandMarkers.valid&&hud.commandMarkers.count==3&&hud.commandMarkers.markers[1].fixed,
          "native Move constructor layout yields a fixed destination independently of its hidden owner and two attached recipients");
    hud.commandMarkers.player=hud.player;hud.tracked[0]=hud.tracked[1]=false;
    hud.held=hud.teamHeld=hud.weaponMenuHeld=true;hud.telemetry.contextKnown=true;hud.nativeCommandMarkers=true;
    const auto worldBefore=HandsRenderer::Snapshot();Coverage worldEyes[2];
    for(unsigned eye=0;eye<2;++eye){
        hud.actualEye.position=NativeCameraMath::Scale(hud.centre.basis.right,eye?3.2f:-3.2f);
        hud.tick=hud.commandMarkers.tick=GetTickCount();
        Hr(device->SetRenderTarget(0,backbuffer),"bind camera-only destination clear target");
        Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear camera-only destination eye");
        Hr(device->SetRenderTarget(0,original),"restore native target before camera-only destination");
        Hr(device->SetViewport(&viewport),"restore authored viewport after fixture target switch");
        Hr(device->SetScissorRect(&scissor),"restore authored scissor before camera-only destination");
        Check(!NativeHandHudKL::Fresh(hud,GetTickCount()),"real camera-only frame has zero tracked wrists and cannot use ordinary DrawHud");
        Check(HandsRenderer::DrawWorldMarkers(device,backbuffer,hud),"dedicated WORLD shield draws from actual eye with zero tracked wrists");
        const auto worldAfter=HandsRenderer::Snapshot();
        Check(worldAfter.commandMarkers==worldBefore.commandMarkers+eye+1&&worldAfter.hudCards==worldBefore.hudCards&&
              worldAfter.hudTextureBuilds==worldBefore.hudTextureBuilds,
              "destination route draws exactly one fixed shield without recipient icons, action, ammo, team or weapon help cards");
        Hr(device->GetRenderTarget(0,&actualTarget),"query native target after camera-only destination");
        Check(actualTarget==original,"camera-only destination restores native target");Release(actualTarget);
        Hr(device->GetViewport(&actualViewport),"query native viewport after camera-only destination");
        Hr(device->GetScissorRect(&actualScissor),"query native scissor after camera-only destination");
        Check(!std::memcmp(&actualViewport,&viewport,sizeof(viewport))&&EqualRect(&actualScissor,&scissor),
              "camera-only destination preserves authored viewport and scissor");
        if(hal)worldEyes[eye]=Pixels(device,backbuffer);
    }
    if(hal){
        for(const auto& pixels:worldEyes)Check(pixels.pixels>20&&pixels.maxX-pixels.minX<40&&pixels.maxY-pixels.minY<40,
            "HAL fixed destination has visible bounded shield pixels and no unrelated HUD elsewhere");
        Check(worldEyes[0].x-worldEyes[1].x>2&&worldEyes[0].x-worldEyes[1].x<6,
              "camera-only WORLD shield keeps actual-eye target-depth stereo disparity");
    }
    const auto validWorld=hud;
    hud.commandMarkers.player+=4;Check(!HandsRenderer::DrawWorldMarkers(device,backbuffer,hud),"foreign native marker player rejects camera-only destination");hud=validWorld;
    hud.tick=GetTickCount()-201;Check(!HandsRenderer::DrawWorldMarkers(device,backbuffer,hud),"stale applied-eye frame rejects camera-only destination");hud=validWorld;
    hud.tick=GetTickCount();hud.commandMarkers.tick=hud.tick-201;
    Check(!HandsRenderer::DrawWorldMarkers(device,backbuffer,hud),"stale native marker lifetime cannot persist through fresh camera frame");hud=validWorld;
    hud.tick=hud.commandMarkers.tick=GetTickCount();for(unsigned i=0;i<hud.commandMarkers.count;++i)hud.commandMarkers.markers[i].fixed=false;
    const auto recipientsOnly=HandsRenderer::Snapshot();
    Hr(device->SetRenderTarget(0,backbuffer),"bind recipient-only clear target");Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear recipient-only image");
    Hr(device->SetRenderTarget(0,original),"restore native target before recipient-only route");
    Check(HandsRenderer::DrawWorldMarkers(device,backbuffer,hud)&&HandsRenderer::Snapshot().commandMarkers==recipientsOnly.commandMarkers&&
          HandsRenderer::Snapshot().hudCards==recipientsOnly.hudCards,"attached recipient records alone draw no custom destination or other HUD");
    if(hal)Check(Pixels(device,backbuffer).pixels==0,"HAL recipient-only route leaves image black and accepted native recipient layout untouched");
    hud.commandMarkers={};
    NativeAimRenderKL::Frame aim;aim.eye=hud.actualEye;aim.origin={50,-20,-15};aim.contact={1000,0,0};
    std::memcpy(aim.projection,hud.projection,sizeof(aim.projection));
    Hr(device->SetRenderTarget(0,backbuffer),"bind laser clear target");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear laser image");
    Hr(device->SetRenderTarget(0,original),"restore native target before laser");
    Hr(device->BeginScene(),"begin native scene around laser");
    Check(HandsRenderer::DrawAim(device,backbuffer,aim),"actual transparent beam and contact dot render");
    Hr(device->EndScene(),"laser preserves caller native scene");
    Hr(device->GetRenderTarget(0,&actualTarget),"query target after laser");
    Hr(device->GetDepthStencilSurface(&actualDepth),"query depth after laser");
    Check(actualTarget==original&&actualDepth==depth,"laser preserves native target and depth");Release(actualTarget);Release(actualDepth);
    Hr(device->GetRenderState(D3DRS_SRCBLEND,&state),"query source blend after laser");Check(state==expectedSource,"laser restores source blend");
    Hr(device->GetRenderState(D3DRS_DESTBLEND,&state),"query destination blend after laser");Check(state==expectedDestination,"laser restores destination blend");
    Hr(device->GetVertexShader(&actualVs),"query vertex shader after laser");Hr(device->GetPixelShader(&actualPs),"query pixel shader after laser");
    Check(actualVs==vs&&actualPs==ps,"laser restores native shaders");Release(actualVs);Release(actualPs);
    if(hal)Check(Pixels(device,backbuffer).pixels>20,"HAL renders visible laser and contact pixels");
    // A pending collision query must never make an otherwise valid eye lose
    // its entire beam. Render both physical eye poses without a contact dot.
    aim.contactValid=false;Coverage beamEyes[2];
    for(unsigned eye=0;eye<2;++eye) {
        aim.eye.position=NativeCameraMath::Scale(hud.centre.basis.right,eye?3.2f:-3.2f);
        Hr(device->SetRenderTarget(0,backbuffer),"bind stereo beam clear target");
        Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear stereo beam image");
        Hr(device->SetRenderTarget(0,original),"restore native target before stereo beam");
        Check(HandsRenderer::DrawAim(device,backbuffer,aim),"actual eye draws beam without waiting for a native collision dot");
        if(hal)beamEyes[eye]=Pixels(device,backbuffer);
    }
    if(hal)Check(beamEyes[0].pixels>10&&beamEyes[1].pixels>10,"HAL laser beam is visible in BOTH eyes without a contact certificate");
    Hr(device->SetRenderTarget(0,backbuffer),"bind active backbuffer for the direct laser path");
    Hr(device->SetViewport(&viewport),"set caller viewport on the active backbuffer");
    Hr(device->SetVertexShader(nullptr),"use fixed function vertex processing");
    const DWORD nativeFvf=D3DFVF_XYZ|D3DFVF_DIFFUSE;
    Hr(device->SetFVF(nativeFvf),"set caller FVF before direct laser");
    IDirect3DTexture9* nativeTexture=nullptr;
    Hr(device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&nativeTexture,nullptr),"create caller texture");
    Hr(device->SetTexture(0,nativeTexture),"bind caller texture before direct laser");
    Check(HandsRenderer::DrawAim(device,backbuffer,aim),"laser draws without switching an active backbuffer");
    Hr(device->GetRenderTarget(0,&actualTarget),"query directly bound backbuffer after laser");
    Hr(device->GetDepthStencilSurface(&actualDepth),"query depth after direct laser");
    Check(actualTarget==backbuffer&&actualDepth==depth,"direct laser preserves active backbuffer and depth");Release(actualTarget);Release(actualDepth);
    Hr(device->GetFVF(&state),"query FVF after direct laser");Check(state==nativeFvf,"direct laser restores caller FVF");
    Hr(device->GetViewport(&actualViewport),"query viewport after direct laser");
    Check(actualViewport.X==viewport.X&&actualViewport.Width==viewport.Width&&actualViewport.MinZ==viewport.MinZ,"direct laser restores caller viewport");
    Hr(device->GetStreamSource(0,&actualStream,&offset,&stride),"query stream after direct laser");
    Check(actualStream==stream&&offset==0&&stride==16,"direct laser restores caller stream");Release(actualStream);
    IDirect3DBaseTexture9* actualTexture=nullptr;
    Hr(device->GetTexture(0,&actualTexture),"query caller texture after direct laser");
    Check(actualTexture==nativeTexture,"direct laser leaves caller texture bound");Release(actualTexture);Release(nativeTexture);
    NativeAimRenderKL::Frame batch[4]={aim,aim,aim,aim};
    for(unsigned i=0;i<4;++i){
        batch[i].prop=i==3?NativeAimRenderKL::Prop::Round:NativeAimRenderKL::Prop::ShotgunShell;
        batch[i].eye.position={};
        batch[i].propPose.position={40.f,-3.f,(float(i)-1.5f)*9.f};
        batch[i].propHalf={1.f,1.f,3.5f};
    }
    Hr(device->SetDepthStencilSurface(nullptr),"remove scene depth");
    Check(HandsRenderer::DrawReloadProps(device,batch,4)==0,"no screen overlay fallback without native scene depth");
    Hr(device->SetDepthStencilSurface(depth),"restore scene depth for cartridges");
    Hr(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_GREATER),"set distinctive native Z comparison");
    Hr(device->SetViewport(&viewport),"restore test scene viewport");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1.f,0),"clear cartridge colour and depth");
    Hr(device->BeginScene(),"begin native draw scene for cartridges");
    Check(HandsRenderer::DrawReloadProps(device,batch,4)==4,"four cartridges use one native depth-aware batch");
    Hr(device->EndScene(),"end native draw scene for cartridges");
    const auto unoccluded=hal?Pixels(device,backbuffer):Coverage{};
    if(hal)Check(unoccluded.pixels>60&&unoccluded.minX<160&&unoccluded.maxX>160,"four shell and rifle props visibly cover both halves");
    const D3DRECT foreground{0,0,160,240};
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1.f,0),"reset colour and depth before occlusion proof");
    Hr(device->Clear(1,&foreground,D3DCLEAR_ZBUFFER,0,.4f,0),"put closer geometry in left half of the native depth");
    Hr(device->BeginScene(),"begin occlusion proof scene");
    Check(HandsRenderer::DrawReloadProps(device,batch,4)==4,"cartridges still submit while depth masks covered pixels");
    Hr(device->EndScene(),"end occlusion proof");
    Hr(device->GetDepthStencilSurface(&actualDepth),"query scene depth after cartridges");
    Check(actualDepth==depth,"preserve exact native depth surface");Release(actualDepth);
    Hr(device->GetRenderState(D3DRS_ZFUNC,&state),"query native comparison after cartridges");
    if(hal)Check(state==D3DCMP_GREATER,"cartridges restore native depth comparison");
    Hr(device->GetViewport(&actualViewport),"query native viewport after cartridges");
    if(hal)Check(!std::memcmp(&actualViewport,&viewport,sizeof(viewport)),"cartridges preserve offset viewport and native depth range");
    if(hal){
        const auto occluded=Pixels(device,backbuffer);
        Check(occluded.pixels>12&&occluded.pixels<unoccluded.pixels&&occluded.minX>=160,
              "actual D3D depth hides all cartridge pixels behind the foreground");
    }
    HandsRenderer::InvalidateDevice();
    producer.Close();Release(declaration);Release(vs);Release(ps);Release(stream);Release(original);Release(depth);Release(backbuffer);
    Release(device);Release(api);DestroyWindow(window);
    std::printf("D3D9 hands integration: %u checks passed (%s).\n",checks,hal?"HAL":"NULLREF state-only");return 0;
}
