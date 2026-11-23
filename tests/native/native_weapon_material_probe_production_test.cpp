// Actual owned material observer with private native memory and COM ABI
// read stubs. No D3D/device/runtime creation and no GPU execution.
#define main UnusedMaterialScopeFixtureMain
#include "native_scope_fire_test.cpp"
#undef main
#include "native_skin_lighting_shader.hpp"
namespace MaterialProbeFixture {
struct Shader {void** table=nullptr;ULONG refs=1;const void* code=nullptr;UINT bytes=0;unsigned reads=0;};
struct Device {void** table=nullptr;Shader* vs=nullptr,*ps=nullptr;float constants[256*4]{};unsigned shaderReads=0,constantReads=0,renderReads=0;};
HRESULT STDMETHODCALLTYPE Texture(Device*,DWORD,IDirect3DBaseTexture9** out){*out=nullptr;return S_OK;}
HRESULT STDMETHODCALLTYPE Target(Device*,DWORD,IDirect3DSurface9** out){*out=nullptr;return S_OK;}
HRESULT STDMETHODCALLTYPE Depth(Device*,IDirect3DSurface9** out){*out=nullptr;return S_OK;}
ULONG STDMETHODCALLTYPE AddRef(Shader* self){return ++self->refs;}
ULONG STDMETHODCALLTYPE Release(Shader* self){return --self->refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* self,void* output,UINT* count){++self->reads;if(!count)return E_POINTER;
    if(!output){*count=self->bytes;return S_OK;}if(*count<self->bytes)return D3DERR_INVALIDCALL;std::memcpy(output,self->code,self->bytes);*count=self->bytes;return S_OK;}
HRESULT STDMETHODCALLTYPE VS(Device* self,Shader** out){++self->shaderReads;*out=self->vs;AddRef(*out);return S_OK;}
HRESULT STDMETHODCALLTYPE PS(Device* self,Shader** out){++self->shaderReads;*out=self->ps;AddRef(*out);return S_OK;}
HRESULT STDMETHODCALLTYPE Constants(Device* self,UINT start,float* out,UINT count){++self->constantReads;
    if(start+count>256)return D3DERR_INVALIDCALL;std::memcpy(out,self->constants+start*4,count*16);return S_OK;}
HRESULT STDMETHODCALLTYPE Render(Device* self,D3DRENDERSTATETYPE which,DWORD* out){++self->renderReads;*out=unsigned(which);return S_OK;}
[[noreturn]]void Forbidden(){std::puts("FAIL material observer attempted a GPU mutation/unlisted COM call");std::abort();}
}
int main(){
    using namespace Fixture;using namespace NativeWeaponKL;using namespace MaterialProbeFixture;
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private read-only material proof objects allocated");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);const auto at=[&](std::uintptr_t n){return std::uint32_t(base+n);};
    const auto data=at(0x10000),actor=at(0x20000),weapon=at(0x30000),root=at(0x40000),model=at(0x50000),entry=at(0x60000),state=at(0x70000),lights=at(0x80000);
    image=base;installed=true;fixtureTick=1000;renderedFixture=vrDraw=ready=true;player=actor;
    Put(base+kGameData,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));Put(actor+0x358,at(0x5508B4));
    NativeWeaponOwnership::HandState right;right.item=weapon;right.enabled=1;right.attacher=31;Put(actor+0x358+0x60,right);
    Put(weapon,at(0x577B74));Put(base+0x577B74+0x2D4,at(kSetTarget));Put(weapon+4,root);Put(root+0x60,weapon);
    Put(weapon+0x58,std::uint32_t(37));Put(weapon+0xC0,model);Put(model,at(0x577864));Put(model+0xA4,std::uint32_t(4));
    renderedHands={};renderedHands.player=actor;renderedHands.pairId=21;renderedHands.tick=fixtureTick;
    nativeProvider=entry;nativeProviderState=state;Put(entry+0xF4,root);
    CameraPose raw,tracked;tracked.position={10,20,30};Put(state+0x8C,tracked);Put(state+0xBC,raw);Put(state+0x3C0,lights);
    for(unsigned i=0;i<28;++i)Put(lights+i*4,float(i));
    materialBound={true,false,state,entry,actor,weapon,model,root,37,1,21,drawGeneration,raw,tracked};
    Check(!PostfilterForegroundActive(),"ordinary color pass cannot authorize a foreground mask override");
    nativeProviderPass=9;
    Check(PostfilterForegroundActive(),"exact tracked held firearm authorizes mask without a support grip");
    nativeProviderPass=10;Check(!PostfilterForegroundActive(),"alpha mask remains native");nativeProviderPass=9;
    materialBound.generation++;Check(!PostfilterForegroundActive(),"old draw generation cannot authorize a mask");materialBound.generation--;
    materialBound.pair++;Check(!PostfilterForegroundActive(),"other stereo pair cannot authorize a mask");materialBound.pair--;
    nativeProviderState+=4;Check(!PostfilterForegroundActive(),"other dispatcher state cannot borrow firearm authorization");nativeProviderState-=4;
    Put(weapon+0x58,std::uint32_t(38));Check(!PostfilterForegroundActive(),"recycled weapon reference rejects foreground override");Put(weapon+0x58,std::uint32_t(37));
    auto moved=tracked;moved.position.x+=1;Put(state+0x8C,moved);Check(!PostfilterForegroundActive(),"later WORLD replacement cannot inherit foreground authorization");Put(state+0x8C,tracked);
    Put(root+0x60,actor);Check(!PostfilterForegroundActive(),"foreign root owner rejects foreground override");Put(root+0x60,weapon);
    Put(data+0xA2C,actor+4);Check(!PostfilterForegroundActive(),"old actor cannot keep authorization after local-player replacement");Put(data+0xA2C,actor);
    Check(PostfilterForegroundActive(),"valid held draw still authorizes after all rejected cases");nativeProviderPass=0;
    void* shaderTable[5];for(auto& f:shaderTable)f=reinterpret_cast<void*>(&Forbidden);
    shaderTable[1]=reinterpret_cast<void*>(&AddRef);shaderTable[2]=reinterpret_cast<void*>(&Release);shaderTable[4]=reinterpret_cast<void*>(&Function);
    const std::uint32_t simplePS[]={0xffff0200,0x0000ffff};const auto& native=NativeSkinLightingShader::Entries[0];
    Shader vs{shaderTable,1,native.original,UINT(native.originalBytes),0},ps{shaderTable,1,simplePS,sizeof(simplePS),0};
    void* table[119];for(auto& f:table)f=reinterpret_cast<void*>(&Forbidden);
    table[38]=reinterpret_cast<void*>(&MaterialProbeFixture::Target);table[40]=reinterpret_cast<void*>(&Depth);table[64]=reinterpret_cast<void*>(&Texture);
    table[58]=reinterpret_cast<void*>(&Render);table[93]=reinterpret_cast<void*>(&VS);table[95]=reinterpret_cast<void*>(&Constants);
    table[108]=reinterpret_cast<void*>(&PS);table[110]=reinterpret_cast<void*>(&Constants);
    Device device;device.table=table;device.vs=&vs;device.ps=&ps;for(unsigned i=0;i<256*4;++i)device.constants[i]=float(i)/1024.f;
    auto* d3d=reinterpret_cast<IDirect3DDevice9*>(&device);ObserveNativeDraw(d3d);MaterialSample sample;
    Check(SnapshotMaterial(false,sample)&&sample.valid&&sample.worldMatch&&sample.vsHash==native.originalHash,"actual observer records the current owned gun/world/native shader");
    Check(sample.mvpCount==4&&sample.mvpRegister==197&&sample.eyeCount==1&&sample.eyeRegister==201&&sample.vsLightCount==5&&sample.vsLightRegister==192,
          "actual native CTAB maps complete MVP/object-eye/HQ-light registers without guessing rigid offsets");
    Check(!std::memcmp(sample.mvp,device.constants+197*4,sizeof(sample.mvp))&&sample.nativeLights[27]==27,
          "read-only native light source and actual GPU ABI rows stay separately identifiable");
    const auto gets=device.shaderReads+device.constantReads+device.renderReads;for(unsigned i=0;i<10000;++i)ObserveNativeDraw(d3d);
    Check(materialSampleCount==1&&gets==device.shaderReads+device.constantReads+device.renderReads,"ten thousand later draws incur no extra COM query inside diagnostic window");
    fixtureTick+=500;ObserveNativeDraw(d3d);
    Check(materialSampleCount==2&&vs.reads==2&&ps.reads==2&&materialShaderCacheHits==2,"later window reuses immutable COM bytecode/CTAB cache instead of GetFunction again");
    tracked.position.x+=1;Put(state+0x8C,tracked);fixtureTick+=500;ObserveNativeDraw(d3d);
    Check(SnapshotMaterial(false,sample)&&!sample.worldMatch&&materialWorldMismatches==1,"a later native WORLD writer is recorded as mismatch, not hidden by the probe");
    nativeProviderState=state+4;fixtureTick+=500;ObserveNativeDraw(d3d);Check(materialSampleCount==3,"another material dispatch cannot borrow the former gun observation");
    nativeProviderState=state;Put(weapon+0x58,std::uint32_t(38));ObserveNativeDraw(d3d);Check(materialSampleCount==3,"recycled item generation retires material evidence");
    Put(weapon+0x58,std::uint32_t(37));fixtureTick+=2000;
    for(unsigned i=0;i<10000;++i)ObserveMaterialSequence(d3d);
    MaterialSequence sequence;Check(SnapshotMaterialSequence(sequence)&&sequence.valid&&sequence.count==MaterialSequenceLimit&&
        sequence.pair==21&&sequence.draws[0].geometry==root&&sequence.draws[0].vsHash==native.originalHash,
        "sequence captures bounded ordered draws only for the currently certified weapon");
    const auto sequenceGets=device.shaderReads+device.renderReads;ObserveMaterialSequence(d3d);
    Check(sequenceGets==device.shaderReads+device.renderReads,"full sequence never queries more COM state in this pair");
    renderedHands.pairId=22;materialBound.pair=22;fixtureTick+=1;ObserveMaterialSequence(d3d);
    Check(sequenceGets==device.shaderReads+device.renderReads,"another pair within the two-second window incurs no COM queries");
    fixtureTick+=2000;nativeProviderState=state+4;ObserveMaterialSequence(d3d);
    Check(sequenceGets==device.shaderReads+device.renderReads,"foreign dispatch cannot start a sequence");
    nativeProviderState=state;ObserveMaterialSequence(d3d);Check(SnapshotMaterialSequence(sequence)&&sequence.pair==22&&sequence.count==1,
        "new window replaces the old bounded sequence only after ownership proof");
    InvalidateMaterialProbe();Check(vs.refs==1&&ps.refs==1&&materialShaders.empty()&&!materialBound.valid,"reset releases cached COM identities and all draw authorization");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Native owned gun material probe: %u checks, %u failures; private CPU/COM read stubs only.\n",checks,failures);return failures?1:0;
}
