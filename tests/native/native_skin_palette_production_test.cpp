// Actual native sparse palette consumer and production pointer lifetime over
// private CPU memory. Optional real D3D9 NULLREF state proves read-only shader,
// texture and constant fingerprints. No game, runtime, detours or input.
#define main UnusedSkinShadowFixtureMain
#include "native_shadow_head_production_test.cpp"
#undef main
namespace SkinLeaseFixture {
using namespace NativeCameraMath;using namespace NativeHandsKL;
IDirect3DDevice9* device=nullptr;unsigned uploads=0;std::vector<float> consumed;
void __thiscall Upload(void*,const float* rows,unsigned floats) {
    ++uploads;consumed.assign(rows,rows+floats);
    NativeHandsKL::ObserveShaderConstants(rows,floats/4,20);
    if(device)ShadowFixture::Check(SUCCEEDED(device->SetVertexShaderConstantF(20,rows,floats/4)),"actual D3D9 receives gathered native bone constants");
}
void* __cdecl Copy(void* output,const void* source,std::size_t count){return std::memcpy(output,source,count);}
void __thiscall AlternateBind(void* object,void* renderer,std::uintptr_t) {
    const auto entry=reinterpret_cast<std::uintptr_t>(object),state=reinterpret_cast<std::uintptr_t>(renderer);
    const auto list=*reinterpret_cast<const std::uint32_t*>(entry+0x90),rows=*reinterpret_cast<const std::uint32_t*>(list+4),index=*reinterpret_cast<const std::uint32_t*>(state+0x3C4);
    ShadowFixture::Put(state+0x8C,*reinterpret_cast<const CameraPose*>(rows+index*0x38));
    ShadowFixture::Put(state+0x3BC,*reinterpret_cast<const std::uint32_t*>(rows+index*0x38+0x30));
}
void RelocateData(std::vector<unsigned char>& code,std::uint32_t old,std::uint32_t next) {
    for(std::size_t i=0;i+4<=code.size();++i){std::uint32_t value=0;std::memcpy(&value,code.data()+i,4);
        if(value==old)std::memcpy(code.data()+i,&next,4);}
}
}
int main(int argc,char** argv) {
    using namespace ShadowFixture;using namespace SkinLeaseFixture;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2)return 2;FileImage exe;Check(exe.Load(argv[1]),"supported EXE opened read-only");
    wchar_t path[MAX_PATH]{};char digest[65]{};
    Check(MultiByteToWideChar(CP_ACP,0,argv[1],-1,path,MAX_PATH)>0&&HashFile(path,digest)&&IsSupportedExecutableHash(digest),"exact supported EXE digest authenticates copied native consumer before execution");
    if(failed)return 1;
    constexpr std::uintptr_t base=0x33000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private skin consumer image allocated");if(!memory)return 1;
    image=base;installed=true;
    constexpr auto data=base+0x10000,player=base+0x20000,geometry=base+0x30000,model=base+0x40000,cache=base+0x50000,
        entry=base+0x60000,lookup=base+0x70000,parentsAt=base+0x71000,rest=base+0x72000,state=base+0x90000;
    Put(base+kGameData,data);Put(data+0xA2C,player);Put(player,base+kPlayerVtable);Put(player+0x70,model);
    Put(entry,base+kBonesVtable);Put(entry+0xF4,geometry);Put(geometry+0x60,player);Put(entry+0x108,cache);Put(entry+0x114,4u);
    Put(model+0x4A0,4u);Put(model+0xC,lookup);Put(model+4,rest);Put(model+0x10,parentsAt);
    const std::uint8_t parents[]={0,0,1,2};std::memcpy(reinterpret_cast<void*>(parentsAt),parents,sizeof(parents));
    Put(lookup,std::uint8_t(1));Put(lookup+6,std::uint8_t(2));Put(lookup+64,std::uint8_t(3));
    NativeSkinMath::BindLocal local[4];local[1].position={0,90,0};local[2].position={0,65,0};local[3].position={0,4,-8};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));const std::uint32_t ids[]={0,0,1,2};CameraPose bind[4];
    Check(NativeSkinMath::BindPose(local,ids,4,bind),"native bind tree generated");
    for(unsigned i=0;i<4;++i)Check(NativeSkinMath::Palette(bind[i],{bind[i].basis,bind[i].position},reinterpret_cast<float*>(cache+0x90+i*48)),"source palette generated");
    std::array<unsigned char,4*48> original{};std::memcpy(original.data(),reinterpret_cast<void*>(cache+0x90),original.size());
    CameraPose root;root.position={300,1200,-900};Put(entry+0x10,root);Put(entry+0x40,root);Put(base+kGpuPoseIndex,0u);
    CameraFrame camera;camera.valid=camera.firstPerson=camera.nativeBodyAction=true;camera.units=100;camera.tick=Clock();camera.pairId=42;
    camera.generation=frameGeneration.fetch_add(1)+1;camera.centre=root;camera.centre.position=Add(root.position,{0,159,0});camera.rawHeadOrientation[3]=1;
    camera.hands.present=camera.hands.mounted=1;camera.hands.tick=Clock();camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;camera.hands.head.orientation[3]=1;
    Check(CaptureRigMetadata(model,player,camera.rig),"owned native body rig captured");publishedFrame=camera;frameThread=GetCurrentThreadId();
    action={};action.valid=true;action.player=player;action.movementKind=NativeActionKL::Movement::Vehicle;acceptedState=state;entryAt=entry;originalBind=NativeBind;
    // Native sparse consumer 490CF1..490DC2, native handle resolver48F5F0.
    // Only external memcpy/uploader are substituted; float range gathering,
    // pointer cache, stride/count and actual memcpy arguments execute native.
    constexpr auto object=base+0xA0000,parameter=base+0xA1000,descriptor=base+0xA2000,ranges=base+0xA3000,
        uploader=base+0xA4000,uploaderVT=base+0xA5000;
    Put(state+0x14,object);Put(object+4,parameter);Put(parameter+0x14,static_cast<std::uint32_t>(descriptor|0x80000000));
    Put(descriptor+2,std::uint16_t(8));Put(descriptor+0x38,2u);Put(descriptor+0x3C,static_cast<std::uint32_t>(ranges|0x80000000));
    const std::uint32_t rangePairs[]={12,0,12,24};std::memcpy(reinterpret_cast<void*>(ranges),rangePairs,sizeof(rangePairs));
    Put(uploader,uploaderVT);Put(uploaderVT+0x14,reinterpret_cast<std::uint32_t>(&Upload));
    std::vector<unsigned char> code(0xD4);Check(exe.Read(0x490CF1,code.data(),code.size()),"original native skin constant consumer bytes read");
    for(const auto at:{0xB893B0u,0xB8879Cu,0xB887B0u})RelocateData(code,at,static_cast<std::uint32_t>(base+at-0x400000));
    std::memcpy(reinterpret_cast<void*>(base+0x90CF1),code.data(),code.size());Check(exe.Read(0x48F5F0,reinterpret_cast<void*>(base+0x8F5F0),0x14),"native direct/indexed handle resolver copied");
    Check(exe.Read(0x491865,reinterpret_cast<void*>(base+0x91865),0xD),"native cached-pointer skip epilogue copied");
    const unsigned char jump[]={0x68,0,0,0,0,0xC3};std::memcpy(reinterpret_cast<void*>(base+0x4BA9CE),jump,sizeof(jump));Put(base+0x4BA9CF,reinterpret_cast<std::uint32_t>(&Copy));
    const unsigned char bridge[]={0x81,0xEC,0xF8,8,0,0,0x53,0x55,0x56,0x57,0x8B,0xAC,0x24,0x0C,9,0,0,0xBB,0,0,0,0,0xE9,0,0,0,0};
    constexpr auto bridgeAt=base+0x90000+0x2000;std::memcpy(reinterpret_cast<void*>(bridgeAt),bridge,sizeof(bridge));Put(bridgeAt+18,static_cast<std::uint32_t>(uploader));
    Put(bridgeAt+23,static_cast<std::int32_t>((base+0x90CF1)-(bridgeAt+sizeof(bridge))));
    using Consumer=void(__stdcall*)(void*);const auto consume=reinterpret_cast<Consumer>(bridgeAt);
    BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(paletteLease.active&&*reinterpret_cast<std::uint32_t*>(state+0x3BC)==reinterpret_cast<std::uintptr_t>(gpuPalette.data()),"production binder grants one exact body palette lease");
    Put(base+kLastUploadedPalette,0u);consume(reinterpret_cast<void*>(state));
    Check(uploads==1&&consumed.size()==24&&consumed[12]<.001f,"actual native sparse consumer sees masked first-person head rows");
    Put(base+kLastUploadedPalette,0u);consume(reinterpret_cast<void*>(state));
    Check(uploads==2&&paletteLease.active&&consumed[12]<.001f,"another material on the same WORLD retains the owned pose until the next object boundary");
    ObserveWorldBind(reinterpret_cast<void*>(state),nullptr);
    Check(!paletteLease.active&&*reinterpret_cast<std::uint32_t*>(state+0x3BC)==cache+0x90&&*reinterpret_cast<std::uint32_t*>(base+kLastUploadedPalette)==0,
          "rigid pre-bind restores native palette and retires only the VR pointer cache");
    consume(reinterpret_cast<void*>(state));
    Check(uploads==3&&consumed[12]>.99f,"same native constant consumer for following rigid/foreign geometry cannot inherit the body's VR palette");
    BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);Put(state+0x3BC,cache+0x90u);
    const auto replaced=paletteLeaseReplaced.load();ObserveWorldBind(reinterpret_cast<void*>(state),&root);
    Check(paletteLeaseReplaced==replaced+1&&*reinterpret_cast<std::uint32_t*>(state+0x3BC)==cache+0x90,"another native writer's palette is never overwritten during retirement");
    BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);Put(entry+0x108,cache+4u);
    const auto rejected=paletteLeaseRejected.load();ObserveWorldBind(reinterpret_cast<void*>(state),&root);
    Check(paletteLeaseRejected==rejected+1&&paletteLeaseFallback==1&&*reinterpret_cast<const std::uint32_t*>(state+0x3BC)==reinterpret_cast<std::uintptr_t>(nativeIdentityPalette.data()),"retired/replaced source cache cannot resurrect its pointer or leak the VR palette");
    consume(reinterpret_cast<void*>(state));Check(uploads==4&&consumed.size()==24&&consumed[12]==1.f&&consumed[17]==1.f&&consumed[22]==1.f,
          "actual native following-shader consumer receives complete identity fallback after source retirement");
    Put(entry+0x108,cache);Put(state+0x3BC,cache+0x90u);
    Check(!std::memcmp(original.data(),reinterpret_cast<void*>(cache+0x90),original.size()),"body/rigid/foreign transitions preserve source palette bytes");
    constexpr auto instanceList=base+0xB0000,instanceRows=base+0xB1000,instancePalette=base+0xB2000;
    Put(entry+0x90,instanceList);Put(instanceList,2u);Put(instanceList+4,instanceRows);Put(state+0x3C4,1u);
    Put(instanceRows+0x38,root);Put(instanceRows+0x38+0x30,instancePalette);std::memcpy(reinterpret_cast<void*>(instancePalette),original.data(),original.size());
    alternatePaletteProof=true;originalBind=AlternateBind;
    BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(paletteLease.active&&paletteLease.source.alternate&&paletteLease.source.index==1,"actual binder authorizes exact selected alternate instance palette");
    ObserveWorldBind(reinterpret_cast<void*>(state),nullptr);
    Check(*reinterpret_cast<const std::uint32_t*>(state+0x3BC)==instancePalette,"retirement returns the selected alternate row palette rather than primary cache+90");
    consume(reinterpret_cast<void*>(state));Check(consumed.size()==24&&consumed[12]>.99f,"native sparse consumer reads the restored original instance data");
    BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);Put(instanceRows+0x38+0x30,instancePalette+48u);
    ObserveWorldBind(reinterpret_cast<void*>(state),nullptr);
    Check(*reinterpret_cast<const std::uint32_t*>(state+0x3BC)==reinterpret_cast<std::uintptr_t>(nativeIdentityPalette.data()),"replaced alternate row is not resurrected by a saved lease");
    Put(entry+0x90,0u);Put(state+0x3C4,0u);originalBind=NativeBind;
    // Real NULLREF device state, shader resources and native gather -> D3D
    // constants. It creates no visible window and performs no game rendering.
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);Check(d3d!=nullptr,"private D3D9 API created");
    HWND window=CreateWindowExW(0,L"STATIC",L"KL1 skin state fixture",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.hDeviceWindow=window;
    const auto hr=d3d?d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device):E_FAIL;
    Check(SUCCEEDED(hr)&&device,"private NULLREF device created");
    if(device) {
        const DWORD vsCode[]={0xFFFE0101,0x1F,0x80000000,0x900F0000,1,0xC00F0000,0x90E40000,0x0000FFFF};
        const DWORD psCode[]={0xFFFF0101,1,0x800F0000,0xA0E40000,0x0000FFFF};
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DTexture9* texture=nullptr;
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        const auto vsResult=device->CreateVertexShader(vsCode,&vs),psResult=device->CreatePixelShader(psCode,&ps),
            declResult=device->CreateVertexDeclaration(elements,&decl),texResult=device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);
        std::printf("NULLREF resources VS=%08lx PS=%08lx declaration=%08lx texture=%08lx\n",static_cast<unsigned long>(vsResult),static_cast<unsigned long>(psResult),static_cast<unsigned long>(declResult),static_cast<unsigned long>(texResult));
        Check(SUCCEEDED(vsResult)&&SUCCEEDED(psResult)&&SUCCEEDED(declResult)&&SUCCEEDED(texResult),"real shader/declaration/material resources created");
        device->SetVertexShader(vs);device->SetPixelShader(ps);device->SetVertexDeclaration(decl);device->SetTexture(0,texture);
        BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);Put(base+kLastUploadedPalette,0u);consume(reinterpret_cast<void*>(state));
        ObserveNativeDraw(device);SkinMaterialSample sample;
        Check(SnapshotSkinMaterial(false,sample)&&sample.vsHash&&sample.psHash&&sample.declarationHash&&sample.textures[0]==reinterpret_cast<std::uintptr_t>(texture),"owned probe fingerprints actual VS/PS/declaration/texture");
        Check(sample.constantsCompared&&sample.constantsMatch&&sample.registerCount==6&&sample.startRegister==20,"actual native sparse upload and D3D shader constants match at draw");
        const float bad[4]={9,9,9,9};device->SetVertexShaderConstantF(20,bad,1);skinProbeBudget[0].tick=0;ObserveNativeDraw(device);
        Check(skinConstantMismatches==1,"a later constant writer is detected rather than silently attributed to a matrix-scale problem");
        for(unsigned i=0;i<10000;++i)ObserveNativeDraw(device);
        Check(skinDrawSamples==2,"ten thousand further draws in the second diagnostic window perform no more than its single material sample");
        publishedFrame.input=publishedFrame.hands;publishedFrame.input.inputValid[0]=HandsIPC::kGripInput;publishedFrame.input.grip[0]=.8f;
        BindPaletteHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);Put(base+kLastUploadedPalette,0u);consume(reinterpret_cast<void*>(state));
        ObserveNativeDraw(device);SkinMaterialSample heldSample;
        Check(SnapshotSkinMaterial(true,heldSample)&&heldSample.support&&!heldSample.gestureHeld&&!heldSample.coupled&&heldSample.rawGrip>.79f,
              "physical grip is attributed to held diagnostics even when pose/IK has not coupled the fore-end");
        Check(heldSample.vsHash==sample.vsHash&&heldSample.psHash==sample.psHash&&heldSample.declarationHash==sample.declarationHash&&heldSample.textures[0]==sample.textures[0]&&heldSample.constantsMatch,
              "grip classification itself leaves shader/material identity and actual bone constant consumption unchanged");
        IDirect3DVertexShader9* sameVS=nullptr;IDirect3DPixelShader9* samePS=nullptr;IDirect3DBaseTexture9* sameTexture=nullptr;
        device->GetVertexShader(&sameVS);device->GetPixelShader(&samePS);device->GetTexture(0,&sameTexture);
        Check(sameVS==vs&&samePS==ps&&sameTexture==texture,"material observation does not bind or replace any GPU resource");
        if(sameVS)sameVS->Release();if(samePS)samePS->Release();if(sameTexture)sameTexture->Release();
        ObserveWorldBind(reinterpret_cast<void*>(state),nullptr);const auto sampled=skinDrawSamples.load();ObserveNativeDraw(device);
        Check(skinDrawSamples==sampled,"following unowned WORLD cannot borrow a former body material sampling tag");
        InvalidateMaterialProbe();device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetVertexDeclaration(nullptr);device->SetTexture(0,nullptr);
        if(vs)vs->Release();if(ps)ps->Release();if(decl)decl->Release();if(texture)texture->Release();device->Release();device=nullptr;
    }
    if(d3d)d3d->Release();if(window)DestroyWindow(window);installed=false;VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 native skin palette/material production: %u checks, %u failures.\n",checks,failed);return failed?1:0;
}
