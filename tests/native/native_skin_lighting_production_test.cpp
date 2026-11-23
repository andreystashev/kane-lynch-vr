// Exact embedded native/patched VS bytecode. Default mode performs CPU ABI
// checks only. Device execution requires --allow-device AND a closed game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d9.h>
#include "native_skin_lighting_shader.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace FixtureSkinOwnership {bool coupled=false;}
namespace NativeHandsKL {bool CoupledSkinLightingActive(){return FixtureSkinOwnership::coupled;}}
// The production selector/cache/restore scope is exercised on the same device,
// not replaced by a fixture copy. Only its owned-coupled draw authority is stubbed.
#include "native_skin_lighting_kl.cpp"
namespace {
unsigned checks=0;
void Check(bool value,const char* reason){++checks;if(!value){std::printf("FAIL: %s\n",reason);std::exit(1);}}
void Hr(HRESULT value,const char* reason){if(FAILED(value))std::printf("HRESULT %08lx: %s\n",static_cast<unsigned long>(value),reason);Check(SUCCEEDED(value),reason);}
template<class T>struct Com {T* value=nullptr;~Com(){if(value)value->Release();}T** Put(){return &value;}T* operator->()const{return value;}};
struct Constant {unsigned index=0,count=0;};
struct Contract {unsigned major=0;std::map<std::string,Constant> constants;std::vector<std::uint32_t> comments;};
Contract Parse(const std::uint32_t* code,std::size_t bytes){
    Contract out;const auto words=bytes/4;Check(bytes%4==0&&words>2&&(code[0]==0xFFFE0200u||code[0]==0xFFFE0300u),"exact compiled native VS2 or VS3 program");out.major=(code[0]>>8)&0xFF;
    for(std::size_t i=1;i<words;){const auto token=code[i];if((token&0xFFFF)==0xFFFF)break;
        if((token&0xFFFF)==0xFFFE){const auto count=(token>>16)&0x7FFFu;Check(count&&i+1+count<=words,"bounded shader comments");
            out.comments.insert(out.comments.end(),code+i,code+i+1+count);
            if(code[i+1]==0x42415443u){const auto* data=reinterpret_cast<const unsigned char*>(code+i+2);const auto size=(count-1)*4;
                const auto u32=[&](unsigned at){std::uint32_t n=0;Check(at<=size&&4<=size-at,"CTAB u32 bounds");std::memcpy(&n,data+at,4);return n;};
                const auto u16=[&](unsigned at){std::uint16_t n=0;Check(at<=size&&2<=size-at,"CTAB u16 bounds");std::memcpy(&n,data+at,2);return n;};
                const auto constantCount=u32(12),info=u32(16);Check(constantCount<64,"native constant table bounded");
                for(unsigned k=0;k<constantCount;++k){const auto at=info+k*20,name=u32(at);Check(name<size&&std::memchr(data+name,0,size-name),"bounded CTAB constant name");
                    if(u16(at+4)==2)out.constants[reinterpret_cast<const char*>(data+name)]={u16(at+6),u16(at+8)};}
            }
            i+=1+count;
        }else {const auto count=(token>>24)&15u;Check(i+1+count<=words,"bounded native instruction");i+=1+count;}
    }
    Check(out.constants.count("v4Bones")&&out.constants.count("m44ModelViewProj"),"real native palette and position constants found");return out;
}
bool GameRunning(){const auto handle=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(handle==INVALID_HANDLE_VALUE)return true;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool active=false;
    if(Process32FirstW(handle,&entry))do{if(!_wcsicmp(entry.szExeFile,L"KANEANDLYNCH.EXE")){active=true;break;}}while(Process32NextW(handle,&entry));
    CloseHandle(handle);return active;
}
struct Vertex {
    float position[4],weights[4];DWORD indices;float normal[3];DWORD diffuse;float uv[2],tangent[3],binormal[3];
};
static_assert(sizeof(Vertex)==84,"native weighted test vertex stride");
// The embedded programs are VS2.0. ProcessVertices output declarations require
// VS3.0; preserve their original bytecode and use the documented FVF destination.
// oFog shares the legacy specular alpha channel, which is not a TBN output.
// https://learn.microsoft.com/en-us/windows/win32/direct3d9/mapping-fvf-codes-to-a-directx-9-declaration
struct Output {float position[4];DWORD diffuse,specular;float tex[8][4];};
static_assert(sizeof(Output)==152,"native VS2 FVF output stride");
struct Vec {float x=0,y=0,z=0;};
Vec Read3(const float* f){return {f[0],f[1],f[2]};}float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Near(float a,float b,float e=.001f){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<e;}
Vec Unit(Vec v){const auto n=std::sqrt(Dot(v,v));return {v.x/n,v.y/n,v.z/n};}
void Constants(IDirect3DDevice9* device,const Contract& contract,float angle){
    std::array<std::array<float,4>,256> values{};
    for(const auto& item:contract.constants){const auto c=item.second;Check(c.index+c.count<=256,"native register allocation within device ABI");
        if(item.first.rfind("m44",0)==0&&c.count==4)for(unsigned i=0;i<4;++i)values[c.index+i][i]=1;
        else for(unsigned i=0;i<c.count;++i)values[c.index+i]={1,1,1,1};
    }
    const auto bones=contract.constants.at("v4Bones");Check(bones.count==192,"native bone palette remains192float4 rows");
    for(unsigned bone=0;bone<64;++bone)for(unsigned r=0;r<3;++r){values[bones.index+bone*3+r]={};values[bones.index+bone*3+r][r]=1;}
    const float co=std::cos(angle),si=std::sin(angle);values[bones.index+3]={co,0,si,0};values[bones.index+4]={0,1,0,0};values[bones.index+5]={-si,0,co,0};
    const auto set=[&](const char* name,std::array<float,4> value){const auto found=contract.constants.find(name);if(found!=contract.constants.end())values[found->second.index]=value;};
    set("v4EyePosObject",{2,3,4,1});set("v4LightPosObject",{2,3,4,1});set("vFogSettings",{0,1,0,1});set("v4Opacity",{1,1,1,1});
    const auto lights=contract.constants.find("v4BonesLights");if(lights!=contract.constants.end()){
        Check(lights->second.count>=5,"HQ native light direction rows remain available");const auto i=lights->second.index;
        values[i]={1,0,0,0};values[i+2]={0,1,0,0};values[i+4]={0,0,1,0};}
    Hr(device->SetVertexShaderConstantF(0,values[0].data(),256),"set actual native CTAB register layout");
}
void GenericConstants(IDirect3DDevice9* device,const Contract& contract,unsigned pose){
    Constants(device,contract,0);
    for(unsigned bone=0;bone<4;++bone){const float a=.173f*(pose+1)*(bone+1),b=-.217f*(pose+2)+.137f*bone,c=.119f*(bone+2)-.091f*pose;
        const float ca=std::cos(a),sa=std::sin(a),cb=std::cos(b),sb=std::sin(b),cc=std::cos(c),sc=std::sin(c);
        const float rows[]={cc*cb,cc*sb*sa-sc*ca,cc*sb*ca+sc*sa,.071f*(bone+1)-.019f*pose,
            sc*cb,sc*sb*sa+cc*ca,sc*sb*ca-cc*sa,-.047f*(bone+2)+.013f*pose,
            -sb,cb*sa,cb*ca,.023f*(pose+1)-.031f*bone};
        Hr(device->SetVertexShaderConstantF(contract.constants.at("v4Bones").index+bone*3,rows,3),"generic four-bone full rotation and translation");}
    const float mvp[]={1.37f,.021f,-.17f,.13f,-.09f,.83f,.19f,-.07f,.055f,-.11f,.72f,.21f,.12f,-.24f,.31f,1.13f};
    Hr(device->SetVertexShaderConstantF(contract.constants.at("m44ModelViewProj").index,mvp,4),"nonidentity native MVP for exact depth comparison");
}
void CurrentVertexShader(IDirect3DDevice9* device,IDirect3DVertexShader9* expected,const char* reason){
    Com<IDirect3DVertexShader9> current;Hr(device->GetVertexShader(current.Put()),"read actual current VS");Check(current.value==expected,reason);
}
void ModuleProof(IDirect3DDevice9* device,const Contract& contract){
    NativeSkinLightingKL::InvalidateDevice();
    const auto& known=NativeSkinLightingShader::Entries[0];
    Com<IDirect3DVertexShader9> source,foreign,unknown;
    Hr(device->CreateVertexShader(reinterpret_cast<const DWORD*>(known.original),source.Put()),"module exact native source creation");
    Hr(device->CreateVertexShader(reinterpret_cast<const DWORD*>(NativeSkinLightingShader::Entries[1].original),foreign.Put()),"module second real native source creation");
    std::vector<std::uint32_t> foreignCode(known.original,known.original+known.originalBytes/4);
    Check(foreignCode.size()>3&&(foreignCode[1]&0xFFFF)==0xFFFE,"native shader starts with bounded CTAB comment");
    foreignCode[2]^=1; // Unknown comment FourCC; native instructions are byte-identical.
    Hr(device->CreateVertexShader(reinterpret_cast<const DWORD*>(foreignCode.data()),unknown.Put()),"valid unknown shader creation");
    constexpr DWORD ps[]={0xFFFF0200,0x02000001,0x800F0800,0xA0E40000,0x0000FFFF};
    Com<IDirect3DPixelShader9> pixel;Hr(device->CreatePixelShader(ps,pixel.Put()),"independent pixel shader creation");
    Hr(device->SetPixelShader(pixel.value),"preserved pixel shader bind");
    Constants(device,contract,.6f);std::array<float,1024> constants{};
    Hr(device->GetVertexShaderConstantF(0,constants.data(),256),"capture untouched native constants");
    const auto preserved=[&](){std::array<float,1024> actual{};Hr(device->GetVertexShaderConstantF(0,actual.data(),256),"read constants after actual module scope");
        Check(actual==constants,"actual module preserves all256 native VS constant registers");
        Com<IDirect3DPixelShader9> current;Hr(device->GetPixelShader(current.Put()),"read PS after actual module scope");Check(current.value==pixel.value,"actual module preserves independent PS binding");};
    Hr(device->SetVertexShader(source.value),"source bind before owned gate check");
    FixtureSkinOwnership::coupled=false;const auto inactive=NativeSkinLightingKL::Snapshot();
    Check(!NativeSkinLightingKL::BeginDraw(device),"ownership false cannot open replacement scope");
    Check(NativeSkinLightingKL::Snapshot().candidates==inactive.candidates,"ownership false bypasses native shader inspection");
    CurrentVertexShader(device,source.value,"ownership false preserves native shader");
    FixtureSkinOwnership::coupled=true;const auto first=NativeSkinLightingKL::Snapshot();
    auto* token=NativeSkinLightingKL::BeginDraw(device);Check(token!=nullptr,"actual module recognizes exact embedded native source");
    Com<IDirect3DVertexShader9> replacement;Hr(device->GetVertexShader(replacement.Put()),"actual replacement shader capture");
    Check(replacement.value!=source.value,"actual module binds separate normalized shader only for coupled owned draw");
    UINT bytes=0;Hr(replacement->GetFunction(nullptr,&bytes),"actual selected shader byte count");Check(bytes==known.patchedBytes,"actual selected shader expected byte count");
    std::vector<std::uint32_t> selected(bytes/4);Hr(replacement->GetFunction(selected.data(),&bytes),"actual selected shader bytecode");
    Check(!std::memcmp(selected.data(),known.patched,bytes),"actual module selected byte-identical authenticated replacement");
    Check(!NativeSkinLightingKL::BeginDraw(device),"nested actual BeginDraw cannot overwrite active source lease");
    Check(NativeSkinLightingKL::FinishDraw(token,D3DERR_INVALIDCALL)==D3DERR_INVALIDCALL,"actual FinishDraw preserves draw HRESULT");
    CurrentVertexShader(device,source.value,"actual module restores source after draw");preserved();
    const auto afterFirst=NativeSkinLightingKL::Snapshot();Check(afterFirst.cacheMisses==first.cacheMisses+1&&afterFirst.matched==first.matched+1&&afterFirst.restored==first.restored+1,"actual source match/cache miss/restore counters");
    token=NativeSkinLightingKL::BeginDraw(device);Check(token!=nullptr,"same actual source reopens replacement scope");
    NativeSkinLightingKL::FinishDraw(token,S_OK);Check(NativeSkinLightingKL::Snapshot().cacheHits==afterFirst.cacheHits+1,"second actual source draw reuses shader cache");
    CurrentVertexShader(device,source.value,"cached scope restores original shader");
    Hr(device->SetVertexShader(unknown.value),"bind valid unknown program");const auto unrecognized=NativeSkinLightingKL::Snapshot();
    Check(!NativeSkinLightingKL::BeginDraw(device),"unknown bytecode cannot select lighting replacement");
    CurrentVertexShader(device,unknown.value,"unknown bytecode remains unchanged");Check(NativeSkinLightingKL::Snapshot().matched==unrecognized.matched,"unknown bytecode is never reported matched");
    Hr(device->SetVertexShader(source.value),"bind source before foreign later write");token=NativeSkinLightingKL::BeginDraw(device);Check(token!=nullptr,"open exact scope before later foreign VS write");
    Hr(device->SetVertexShader(foreign.value),"simulate later independent shader owner");NativeSkinLightingKL::FinishDraw(token,S_OK);
    CurrentVertexShader(device,foreign.value,"FinishDraw never overwrites a later foreign VS write");preserved();
    Hr(device->SetVertexShader(source.value),"bind source before device invalidation");Check(NativeSkinLightingKL::BeginDraw(device)!=nullptr,"open scope before actual device invalidation");
    NativeSkinLightingKL::InvalidateDevice();CurrentVertexShader(device,source.value,"device invalidation restores active source before cache release");
    FixtureSkinOwnership::coupled=false;preserved();Hr(device->SetPixelShader(nullptr),"release fixture pixel binding");
}
void DeviceProof(const std::vector<Contract>& contracts){
    Check(!GameRunning(),"game closed before any device/window creation");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KL1SkinLightingCPUVertexDeviceFixture";
    RegisterClassW(&wc);const auto window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);Check(window!=nullptr,"hidden fixture window");
    Com<IDirect3D9> api;api.value=Direct3DCreate9(D3D_SDK_VERSION);Check(api.value!=nullptr,"real system D3D9 API");
    D3DPRESENT_PARAMETERS present{};present.Windowed=TRUE;present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.hDeviceWindow=window;present.BackBufferWidth=present.BackBufferHeight=64;present.BackBufferFormat=D3DFMT_X8R8G8B8;
    Com<IDirect3DDevice9> device;Hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&present,device.Put()),"HAL device with actual programmable software vertex processor");
    D3DCAPS9 caps{};Hr(device->GetDeviceCaps(&caps),"native device caps");Check(caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&caps.MaxVertexShaderConst>=256,"actual shader model and constant capability");
    const D3DVERTEXELEMENT9 inputElements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDWEIGHT,0},{0,32,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDINDICES,0},
        {0,36,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,48,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,52,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,60,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},
        {0,72,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},D3DDECL_END()};
    // XYZW retains native clip coordinates, avoiding viewport rounding hiding
    // one-ULP clip-depth differences between the native and cloned VS.
    DWORD outputFvf=D3DFVF_XYZW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX8;
    for(unsigned i=0;i<8;++i)outputFvf|=D3DFVF_TEXCOORDSIZE4(i);
    Com<IDirect3DVertexDeclaration9> inputDecl;Hr(device->CreateVertexDeclaration(inputElements,inputDecl.Put()),"native weighted vertex declaration");
    std::vector<D3DVERTEXELEMENT9> outputElements{{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,20,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1}};
    for(unsigned i=0;i<8;++i)outputElements.push_back({0,static_cast<WORD>(24+i*16),D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,static_cast<BYTE>(i)});
    outputElements.push_back(D3DDECL_END());Com<IDirect3DVertexDeclaration9> outputDecl;
    Hr(device->CreateVertexDeclaration(outputElements.data(),outputDecl.Put()),"VS3 output declaration matches legacy FVF clip layout");
    Vertex vertices[3]{};for(unsigned i=0;i<3;++i){auto& v=vertices[i];v.position[0]=-.2f+.2f*i;v.position[1]=-.1f+.2f*i;v.position[2]=.3f-.2f*i;v.position[3]=1;v.weights[0]=v.weights[1]=.5f;
        v.indices=0x09060300;v.diffuse=0xFFFFFFFF;v.uv[0]=.13f+.2f*i;v.uv[1]=.23f+.1f*i;
        v.normal[0]=v.normal[1]=.5f;v.normal[2]=1;v.tangent[0]=1;v.tangent[1]=v.tangent[2]=.5f;v.binormal[0]=v.binormal[2]=.5f;v.binormal[1]=1;}
    Com<IDirect3DVertexBuffer9> input,output;Hr(device->CreateVertexBuffer(sizeof(vertices),D3DUSAGE_SOFTWAREPROCESSING,0,D3DPOOL_SYSTEMMEM,input.Put(),nullptr),"native source vertex buffer");
    Hr(device->CreateVertexBuffer(sizeof(Output)*3,D3DUSAGE_SOFTWAREPROCESSING,outputFvf,D3DPOOL_SYSTEMMEM,output.Put(),nullptr),"actual VS2 ProcessVertices FVF output buffer");
    void* mapped=nullptr;Hr(input->Lock(0,0,&mapped,0),"source vertex lock");std::memcpy(mapped,vertices,sizeof(vertices));Hr(input->Unlock(),"source vertex unlock");
    Hr(device->SetVertexDeclaration(inputDecl.value),"input declaration bind");Hr(device->SetStreamSource(0,input.value,0,sizeof(Vertex)),"native weighted stream");
    Hr(device->SetRenderState(D3DRS_CLIPPING,FALSE),"capture unclipped native outputs");
    unsigned ordinal=0;bool observedShrink=false;
    for(const auto& shader:NativeSkinLightingShader::Entries){Com<IDirect3DVertexShader9> original,patched;
        Hr(device->CreateVertexShader(reinterpret_cast<const DWORD*>(shader.original),original.Put()),"create exact embedded native shader");Hr(device->CreateVertexShader(reinterpret_cast<const DWORD*>(shader.patched),patched.Put()),"create exact normalized-TBN variant");
        for(float degrees:{0.f,60.f,120.f,170.f,179.99f,180.f}){Constants(device.value,contracts[ordinal],degrees*.0174532925199433f);std::array<Output,3> before{},after{};
            for(unsigned variant=0;variant<2;++variant){Hr(device->SetVertexShader(variant?patched.value:original.value),"bind exact shader bytecode variant");
                Hr(device->ProcessVertices(0,0,3,output.value,contracts[ordinal].major>=3?outputDecl.value:nullptr,0),"execute exact unmodified VS bytecode through native ProcessVertices");
                Hr(output->Lock(0,0,&mapped,D3DLOCK_READONLY),"native output readback");std::memcpy((variant?after:before).data(),mapped,sizeof(after));Hr(output->Unlock(),"native output unlock");}
            for(unsigned i=0;i<3;++i){Check(!std::memcmp(before[i].position,after[i].position,sizeof(before[i].position)),"original and patched actual clip/viewport positions bit-identical");
                Check(!std::memcmp(before[i].tex[0],after[i].tex[0],sizeof(float)*2),"actual native UV coordinates bit-identical");
                Check(before[i].diffuse==after[i].diffuse,"actual native vertex color unchanged");
                for(float v:after[i].position)Check(std::isfinite(v),"finite actual patched position including complete rank collapse");
                if(ordinal<=4){const auto a=Read3(after[i].tex[1]),b=Read3(after[i].tex[2]),n=Read3(after[i].tex[3]);
                    Check(Near(Dot(a,a),1)&&Near(Dot(b,b),1)&&Near(Dot(n,n),1)&&Near(Dot(a,b),0)&&Near(Dot(a,n),0)&&Near(Dot(b,n),0),
                        "actual compiled weighted TBN outputs are unit/orthogonal even at rank collapse");
                    const auto old=Read3(before[i].tex[1]);if(Dot(old,old)<.1f&&degrees>=170)observedShrink=true;
                }else {const auto light=Read3(after[i].tex[1]);Check(Near(Dot(light,light),1),"actual lighting pass keeps finite normalized tangent-space light");
                    if(degrees==180.f){const auto bones=contracts[ordinal].constants.at("v4Bones");(void)bones;
                        const auto expected=Unit({2,3-vertices[i].position[1],4});Check(Near(light.x,expected.x)&&Near(light.y,expected.y)&&Near(light.z,expected.z),"degenerate TBN uses deterministic dominant-bone lighting direction");}}
            }
        }
        for(unsigned pose=0;pose<5;++pose){
            for(unsigned i=0;i<3;++i){auto& v=vertices[i];v.position[0]=.231f+.173f*i-.037f*pose;v.position[1]=-.173f+.127f*i+.041f*pose;v.position[2]=.414f-.083f*i-.029f*pose;
                v.weights[0]=.13f+.017f*i;v.weights[1]=.27f-.013f*pose;v.weights[2]=.19f+.011f*pose;v.weights[3]=1-v.weights[0]-v.weights[1]-v.weights[2];}
            Hr(input->Lock(0,0,&mapped,0),"generic nonaxis weighted vertex source lock");std::memcpy(mapped,vertices,sizeof(vertices));Hr(input->Unlock(),"generic source unlock");
            GenericConstants(device.value,contracts[ordinal],pose);std::array<Output,3> before{},after{};
            for(unsigned variant=0;variant<2;++variant){Hr(device->SetVertexShader(variant?patched.value:original.value),"generic exact shader variant bind");
                Hr(device->ProcessVertices(0,0,3,output.value,contracts[ordinal].major>=3?outputDecl.value:nullptr,0),"generic actual native programmable shader execution");
                Hr(output->Lock(0,0,&mapped,D3DLOCK_READONLY),"generic exact output readback");std::memcpy((variant?after:before).data(),mapped,sizeof(after));Hr(output->Unlock(),"generic output unlock");}
            for(unsigned i=0;i<3;++i){const bool exact=!std::memcmp(before[i].position,after[i].position,sizeof(before[i].position));
                if(!exact)std::printf("Position mismatch family%u pose%u vertex%u: native(%a,%a,%a,%a) patched(%a,%a,%a,%a)\n",ordinal,pose,i,
                    before[i].position[0],before[i].position[1],before[i].position[2],before[i].position[3],after[i].position[0],after[i].position[1],after[i].position[2],after[i].position[3]);
                Check(exact,"generic four-weight full-transform actual clip oPos BITEXACT in native and patched shader");
                Check(!std::memcmp(before[i].tex[0],after[i].tex[0],sizeof(float)*2),"generic native UV remains BITEXACT");
                for(float value:after[i].position)Check(std::isfinite(value),"generic actual clip position finite");}
        }
        // Restore the controlled two-weight palette/source for the next family.
        for(unsigned i=0;i<3;++i){auto& v=vertices[i];v.position[0]=-.2f+.2f*i;v.position[1]=-.1f+.2f*i;v.position[2]=.3f-.2f*i;v.weights[0]=v.weights[1]=.5f;v.weights[2]=v.weights[3]=0;}
        Hr(input->Lock(0,0,&mapped,0),"restore rank-collapse source lock");std::memcpy(mapped,vertices,sizeof(vertices));Hr(input->Unlock(),"restore rank-collapse source unlock");
        std::printf("Actual shader family%u: controlled TBN + generic five-pose oPos/UV checked.\n",ordinal);++ordinal;
    }
    Check(observedShrink,"original real weighted shader exposes unnormalized light/TBN shrink under the same controlled palette");
    ModuleProof(device.value,contracts[0]);DestroyWindow(window);
}
}
int main(int argc,char** argv){
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);bool allowDevice=false;
    for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--allow-device"))allowDevice=true;else Check(false,"usage: native_skin_lighting_production_test [--allow-device]");}
    std::vector<Contract> contracts;
    for(const auto& shader:NativeSkinLightingShader::Entries){const auto original=Parse(shader.original,shader.originalBytes),patched=Parse(shader.patched,shader.patchedBytes);
        Check(original.comments==patched.comments,"native CTAB/comment/register ABI byte-identical after lighting-only patch");contracts.push_back(original);}
    Check(contracts.size()==9,"all nine embedded actual shader families covered");
    if(allowDevice){if(GameRunning()){std::puts("BLOCKED: game process active; no device or window opened.");return 2;}DeviceProof(contracts);}
    else std::puts("CPU ABI checks only; no device/window/FX compiler opened. Use --allow-device only after explicit closed-game authorization.");
    std::printf("Native skin lighting actual shader fixture: %u checks PASS%s.\n",checks,allowDevice?" (HAL/ProcessVertices)":" (CPU only)");return 0;
}
