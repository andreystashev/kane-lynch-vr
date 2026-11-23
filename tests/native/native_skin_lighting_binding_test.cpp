// Actual production selector/cache/binding/restore with private COM ABI stubs.
// No D3D API creation, window, runtime, shader compiler or GPU operation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
namespace LightingBindingFixture {
enum class Draw {Foreign,Unheld,Shadow,Coupled};
Draw draw=Draw::Foreign;
}
namespace NativeHandsKL {bool CoupledSkinLightingActive(){return LightingBindingFixture::draw==LightingBindingFixture::Draw::Coupled;}}
#include "native_skin_lighting_kl.cpp"
namespace LightingBindingFixture {
unsigned checks=0;
void Check(bool value,const char* reason){++checks;if(!value){std::printf("FAIL: %s\n",reason);std::exit(1);}}
[[noreturn]]void Forbidden(){std::puts("FAIL: unlisted COM call (GPU/compiler/PS/constants path)");std::abort();}
struct Shader {
    void** table=nullptr;
    ULONG refs=1;
    std::vector<std::uint32_t> code;
    unsigned reads=0,adds=0,releases=0;
    bool failRead=false;
};
struct Device {
    void** table=nullptr;
    Shader* current=nullptr;
    std::vector<std::unique_ptr<Shader>> objects;
    unsigned gets=0,sets=0,creates=0;
    HRESULT getResult=S_OK,setResult=S_OK,createResult=S_OK;
};
std::array<void*,5> shaderMethods;
std::array<void*,119> deviceMethods;
ULONG STDMETHODCALLTYPE Add(Shader* self){Check(self&&self->refs>0,"COM AddRef only on live shader");++self->adds;return ++self->refs;}
ULONG STDMETHODCALLTYPE Release(Shader* self){Check(self&&self->refs>0,"COM Release only on live shader");++self->releases;return --self->refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* self,void* output,UINT* bytes){
    Check(self&&self->refs>0,"GetFunction on live shader");++self->reads;
    if(!bytes)return E_POINTER;if(self->failRead)return E_FAIL;
    const auto size=UINT(self->code.size()*4);
    if(!output){*bytes=size;return S_OK;}
    if(*bytes<size)return D3DERR_MOREDATA;
    std::memcpy(output,self->code.data(),size);*bytes=size;return S_OK;
}
Shader* Make(Device& device,const std::uint32_t* code,std::size_t bytes){
    auto shader=std::make_unique<Shader>();shader->table=shaderMethods.data();shader->code.assign(code,code+bytes/4);
    auto* out=shader.get();device.objects.push_back(std::move(shader));return out;
}
HRESULT STDMETHODCALLTYPE Get(Device* self,Shader** output){
    ++self->gets;if(FAILED(self->getResult))return self->getResult;
    if(!output)return E_POINTER;*output=self->current;if(*output)Add(*output);return self->getResult;
}
HRESULT STDMETHODCALLTYPE Set(Device* self,Shader* shader){
    ++self->sets;if(FAILED(self->setResult))return self->setResult;
    if(shader)Add(shader);if(self->current)Release(self->current);self->current=shader;return self->setResult;
}
HRESULT STDMETHODCALLTYPE Create(Device* self,const DWORD* input,Shader** output){
    ++self->creates;if(FAILED(self->createResult))return self->createResult;
    if(!output)return E_POINTER;
    // A COM creation stub receives only the exact production replacement.
    // No fixture shader rewriting or runtime compiler substitutes for it.
    for(const auto& entry:NativeSkinLightingShader::Entries)
        if(input==reinterpret_cast<const DWORD*>(entry.patched)){
            Check(!std::memcmp(input,entry.patched,entry.patchedBytes),"replacement input contains exact authenticated bytes");
            *output=Make(*self,entry.patched,entry.patchedBytes);return S_OK;
        }
    Check(false,"CreateVertexShader receives an exact authenticated replacement");return D3DERR_INVALIDCALL;
}
IDirect3DDevice9* Native(Device& device){return reinterpret_cast<IDirect3DDevice9*>(&device);}
std::uint32_t Fingerprint(const std::vector<std::uint32_t>& code){std::uint32_t h=2166136261u;
    const auto* data=reinterpret_cast<const unsigned char*>(code.data());
    for(std::size_t i=0;i<code.size()*4;++i){h=(h^data[i])*16777619u;}return h;}
std::vector<std::uint32_t> HashCollision(const std::uint32_t* code,std::size_t bytes){
    std::vector<std::uint32_t> out(code,code+bytes/4);const auto wanted=Fingerprint(out);
    auto* data=reinterpret_cast<unsigned char*>(out.data());constexpr std::uint32_t prime=16777619u,inverse=0x359c449bu;
    static_assert(std::uint32_t(prime*inverse)==1,"FNV32 inverse");
    constexpr std::size_t at=16;Check(bytes>at+4,"native shader has room for a private unknown-code collision");
    for(unsigned trial=1;trial<256;++trial){std::memcpy(out.data(),code,bytes);data[8]^=static_cast<unsigned char>(trial);
        std::uint32_t prefix=2166136261u;for(std::size_t i=0;i<at;++i)prefix=(prefix^data[i])*prime;
        auto suffix=wanted;for(std::size_t i=bytes;i-->at+4;)suffix=(suffix*inverse)^data[i];
        std::unordered_map<std::uint32_t,std::uint16_t> first;first.reserve(65536);
        for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)first.emplace(((prefix^a)*prime^b)*prime,std::uint16_t(a|(b<<8)));
        for(unsigned d=0;d<256;++d)for(unsigned c=0;c<256;++c){const auto mid=(((suffix*inverse)^d)*inverse)^c;const auto found=first.find(mid);
            if(found!=first.end()){data[at]=static_cast<unsigned char>(found->second);data[at+1]=static_cast<unsigned char>(found->second>>8);
                data[at+2]=static_cast<unsigned char>(c);data[at+3]=static_cast<unsigned char>(d);
                Check(Fingerprint(out)==wanted&&std::memcmp(out.data(),code,bytes),"fixture produces different bytes with the exact known length and FNV hash");return out;}}
    }
    Check(false,"bounded FNV collision construction succeeded");return {};
}
void Bind(Device& device,Shader* shader){Check(SUCCEEDED(Set(&device,shader)),"fixture native shader binding succeeds");}
void Empty(Device& device){NativeSkinLightingKL::InvalidateDevice();Bind(device,nullptr);
    for(auto& shader:device.objects){Check(shader->refs<=1,"no remaining cache/scope references after invalidate");if(shader->refs)Release(shader.get());
        Check(shader->refs==0,"all COM shader references balanced after native binding release");}}
}
int main(){
    using namespace LightingBindingFixture;
    shaderMethods.fill(reinterpret_cast<void*>(&Forbidden));deviceMethods.fill(reinterpret_cast<void*>(&Forbidden));
    shaderMethods[1]=reinterpret_cast<void*>(&Add);shaderMethods[2]=reinterpret_cast<void*>(&Release);shaderMethods[4]=reinterpret_cast<void*>(&Function);
    deviceMethods[91]=reinterpret_cast<void*>(&Create);deviceMethods[92]=reinterpret_cast<void*>(&Set);deviceMethods[93]=reinterpret_cast<void*>(&Get);
    Device device;device.table=deviceMethods.data();auto* native=Native(device);
    NativeSkinLightingKL::InvalidateDevice();const auto& known=NativeSkinLightingShader::Entries[0];
    auto* source=Make(device,known.original,known.originalBytes);Bind(device,source);
    for(const auto mode:{Draw::Foreign,Draw::Unheld,Draw::Shadow}){
        draw=mode;const auto reads=device.gets,creates=device.creates,sets=device.sets;
        Check(!NativeSkinLightingKL::BeginDraw(native),"foreign/unheld/shadow authority cannot open shader scope");
        Check(device.gets==reads&&device.creates==creates&&device.sets==sets&&device.current==source,"ineligible scope performs no COM inspection or mutation");
    }
    draw=Draw::Coupled;Check(!NativeSkinLightingKL::BeginDraw(nullptr),"null device rejected without COM calls");
    const auto initial=NativeSkinLightingKL::Snapshot();auto* token=NativeSkinLightingKL::BeginDraw(native);
    Check(token&&device.current!=source,"eligible actual production BeginDraw binds a distinct shader clone");
    auto* replacement=device.current;Check(replacement->code.size()*4==known.patchedBytes&&
        !std::memcmp(replacement->code.data(),known.patched,known.patchedBytes),"bound replacement bytes exactly equal the authenticated program");
    Check(source->reads==2&&device.creates==1,"first native source read and single replacement creation");
    const auto nestedGets=device.gets,nestedSets=device.sets;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.gets==nestedGets&&device.sets==nestedSets,"nested Begin cannot replace the active source lease");
    Check(NativeSkinLightingKL::FinishDraw(&device,E_HANDLE)==E_HANDLE&&device.current==replacement,"foreign token cannot close an active production scope");
    Check(NativeSkinLightingKL::FinishDraw(token,D3DERR_DEVICELOST)==D3DERR_DEVICELOST&&device.current==source,"Finish restores native source while propagating the native draw failure");
    Check(NativeSkinLightingKL::Snapshot().restored==initial.restored+1,"successful restoration is counted");
    const auto gets=source->reads,creates=device.creates;const auto hit=NativeSkinLightingKL::Snapshot().cacheHits;
    for(unsigned i=0;i<1000;++i){token=NativeSkinLightingKL::BeginDraw(native);Check(token!=nullptr,"cached owned scope opens");
        Check(NativeSkinLightingKL::FinishDraw(token,S_FALSE)==S_FALSE&&device.current==source,"cached scope restores source and preserves success HRESULT");}
    Check(source->reads==gets&&device.creates==creates&&NativeSkinLightingKL::Snapshot().cacheHits==hit+1000,"one thousand draws reuse exact COM cache without shader reads or compilation");
    for(const auto& entry:NativeSkinLightingShader::Entries){
        auto* shader=Make(device,entry.original,entry.originalBytes);Bind(device,shader);token=NativeSkinLightingKL::BeginDraw(native);
        Check(token&&device.current->code.size()*4==entry.patchedBytes&&
            !std::memcmp(device.current->code.data(),entry.patched,entry.patchedBytes),"each of nine exact native families selects its own exact replacement");
        NativeSkinLightingKL::FinishDraw(token,S_OK);Check(device.current==shader,"each native family restores its actual COM source");
    }
    auto* unknown=Make(device,known.original,known.originalBytes);unknown->code[2]^=1;Bind(device,unknown);
    const auto unknownCreates=device.creates;Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==unknown,"one-byte unknown native shader stays untouched");
    const auto unknownReads=unknown->reads;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&unknown->reads==unknownReads&&device.creates==unknownCreates,"unknown bytecode negative cache prevents repeated reads/creation");
    const auto collidingCode=HashCollision(known.original,known.originalBytes);auto* collision=Make(device,collidingCode.data(),collidingCode.size()*4);Bind(device,collision);
    const auto collisionMatched=NativeSkinLightingKL::Snapshot().matched;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==collision&&NativeSkinLightingKL::Snapshot().matched==collisionMatched,
        "same native hash and length cannot authorize nonidentical shader bytes");
    auto* foreign=Make(device,known.original,known.originalBytes);Bind(device,source);token=NativeSkinLightingKL::BeginDraw(native);Bind(device,foreign);
    const auto foreignSets=device.sets;Check(NativeSkinLightingKL::FinishDraw(token,E_FAIL)==E_FAIL&&device.current==foreign&&device.sets==foreignSets,"later foreign VS binding is not clobbered on Finish");
    Bind(device,source);token=NativeSkinLightingKL::BeginDraw(native);Check(token!=nullptr,"owned scope before device invalidate");
    NativeSkinLightingKL::InvalidateDevice();Check(device.current==source,"Invalidate restores active exact source before releasing cache");
    const auto afterInvalidate=device.creates;token=NativeSkinLightingKL::BeginDraw(native);NativeSkinLightingKL::FinishDraw(token,S_OK);
    Check(device.creates==afterInvalidate+1,"Invalidate clears source cache and recreates the next owned clone");
    NativeSkinLightingKL::InvalidateDevice();device.getResult=E_FAIL;const auto failedGetCreates=device.creates;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==source&&device.creates==failedGetCreates,"GetVertexShader failure cannot mutate or create");device.getResult=S_OK;
    auto* unreadable=Make(device,known.original,known.originalBytes);unreadable->failRead=true;Bind(device,unreadable);
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==unreadable,"GetFunction failure retains native shader");
    NativeSkinLightingKL::InvalidateDevice();Bind(device,source);device.createResult=E_OUTOFMEMORY;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==source,"CreateVertexShader failure keeps original binding");
    const auto failedCreates=device.creates;Check(!NativeSkinLightingKL::BeginDraw(native)&&device.creates==failedCreates,"failed immutable source is cached until explicit invalidate");
    device.createResult=S_OK;NativeSkinLightingKL::InvalidateDevice();device.setResult=D3DERR_DEVICELOST;
    Check(!NativeSkinLightingKL::BeginDraw(native)&&device.current==source,"failed replacement bind leaves source active and balances temporary source reference");
    device.setResult=S_OK;token=NativeSkinLightingKL::BeginDraw(native);replacement=device.current;device.setResult=D3DERR_DEVICELOST;
    const auto failedRestore=NativeSkinLightingKL::Snapshot().failed;
    Check(NativeSkinLightingKL::FinishDraw(token,D3DERR_INVALIDCALL)==D3DERR_INVALIDCALL&&device.current==replacement&&
        NativeSkinLightingKL::Snapshot().failed==failedRestore+1,"failed restore is counted and does not replace original native HRESULT");
    device.setResult=S_OK;Bind(device,source);token=NativeSkinLightingKL::BeginDraw(native);device.getResult=E_FAIL;
    const auto failedQuery=NativeSkinLightingKL::Snapshot().failed;
    Check(NativeSkinLightingKL::FinishDraw(token,E_ABORT)==E_ABORT&&NativeSkinLightingKL::Snapshot().failed==failedQuery+1,"failed restore query remains observable and propagates native HRESULT");
    device.getResult=S_OK;Bind(device,source);NativeSkinLightingKL::InvalidateDevice();
    Bind(device,nullptr);Check(!NativeSkinLightingKL::BeginDraw(native)&&!device.current,"null current VS does not create replacement");Bind(device,source);
    Device other;other.table=deviceMethods.data();auto* second=Make(other,known.original,known.originalBytes);Bind(other,second);
    token=NativeSkinLightingKL::BeginDraw(native);NativeSkinLightingKL::FinishDraw(token,S_OK);
    const auto sourceRefs=source->refs;token=NativeSkinLightingKL::BeginDraw(Native(other));
    Check(token&&source->refs+1==sourceRefs,"device change releases old source cache before opening new device lease");
    NativeSkinLightingKL::FinishDraw(token,S_OK);Check(other.current==second,"new device scope restores its own source");
    Empty(other);Empty(device);
    std::printf("Native skin lighting production binding: %u checks PASS (private CPU/COM only; PS/constants/GPU calls forbidden).\n",checks);return 0;
}
