// Production observer and D3D9 getter ABI exercised against private COM
// stubs. There is no game, real device, GPU submission or window.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
namespace {DWORD testTick=1000;DWORD WINAPI TestTick(){return testTick;}}
#define GetTickCount TestTick
#include "native_reflection_probe_kl.cpp"
#undef GetTickCount
namespace {
unsigned checks=0,failed=0;bool resetDuringPixel=false;
unsigned sceneChecks=0;bool sceneAllowed=false;
bool Scene(){++sceneChecks;return sceneAllowed;}
unsigned contextChecks=0,constantChecks=0;
bool contextAllowed=true,resetDuringContext=false;UINT failConstantRegister=~0u,nanConstantRegister=~0u;
NativeReflectionProbeKL::DrawContext contextValue{};
bool Context(NativeReflectionProbeKL::DrawContext& out){++contextChecks;out=contextValue;
    if(resetDuringContext){resetDuringContext=false;NativeReflectionProbeKL::Reset();}return contextAllowed;}
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
[[noreturn]] void Forbidden(){std::puts("FAIL probe attempted an unlisted COM call");std::abort();}
struct Shader {void** table=nullptr;ULONG refs=1;const void* code=nullptr;UINT bytes=0;unsigned reads=0;};
struct Resource {void** table=nullptr;ULONG refs=1;D3DRESOURCETYPE kind=D3DRTYPE_TEXTURE;};
struct Device {void** table=nullptr;Shader* shader=nullptr;Resource* texture=nullptr;Resource* target=nullptr;unsigned gets=0;Shader* vertex=nullptr;};
ULONG STDMETHODCALLTYPE ShaderRef(Shader* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE ShaderRelease(Shader* s){return --s->refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* s,void* data,UINT* bytes){++s->reads;if(!bytes)return E_POINTER;
    if(!data){*bytes=s->bytes;return S_OK;}if(*bytes<s->bytes)return D3DERR_INVALIDCALL;
    std::memcpy(data,s->code,s->bytes);*bytes=s->bytes;return S_OK;}
ULONG STDMETHODCALLTYPE ResourceRelease(Resource* r){return --r->refs;}
D3DRESOURCETYPE STDMETHODCALLTYPE Type(Resource* r){return r->kind;}
HRESULT STDMETHODCALLTYPE TextureDesc(Resource*,UINT level,D3DSURFACE_DESC* d){if(level)return D3DERR_INVALIDCALL;*d={};d->Width=512;d->Height=256;d->Format=D3DFMT_A8R8G8B8;return S_OK;}
HRESULT STDMETHODCALLTYPE SurfaceDesc(Resource*,D3DSURFACE_DESC* d){*d={};d->Width=3840;d->Height=2160;d->Format=D3DFMT_X8R8G8B8;return S_OK;}
HRESULT STDMETHODCALLTYPE Pixel(Device* d,Shader** out){++d->gets;if(resetDuringPixel){resetDuringPixel=false;NativeReflectionProbeKL::Reset();}*out=d->shader;if(*out)ShaderRef(*out);return S_OK;}
HRESULT STDMETHODCALLTYPE Vertex(Device* d,Shader** out){++d->gets;*out=d->vertex;if(*out)ShaderRef(*out);return S_OK;}
HRESULT STDMETHODCALLTYPE VertexConstants(Device* d,UINT first,float* out,UINT count){++d->gets;++constantChecks;if(first+count>256)return D3DERR_INVALIDCALL;for(UINT i=0;i<count*4;++i)out[i]=float(first)+float(i)/10.f;
    if(first==failConstantRegister){out[0]=12345.f;return E_FAIL;}if(first==nanConstantRegister)out[0]=std::numeric_limits<float>::quiet_NaN();return S_OK;}
HRESULT STDMETHODCALLTYPE GetTextureStub(Device* d,DWORD,Resource** out){++d->gets;*out=d->texture;if(*out)++(*out)->refs;return S_OK;}
HRESULT STDMETHODCALLTYPE Target(Device* d,DWORD,Resource** out){++d->gets;*out=d->target;if(*out)++(*out)->refs;return S_OK;}
HRESULT STDMETHODCALLTYPE Viewport(Device* d,D3DVIEWPORT9* v){++d->gets;*v={10,20,1280,720,0.f,1.f};return S_OK;}
HRESULT STDMETHODCALLTYPE State(Device* d,D3DRENDERSTATETYPE which,DWORD* out){++d->gets;*out=DWORD(which);return S_OK;}
std::vector<std::uint32_t> Bytecode(){
    constexpr unsigned block=144;std::vector<std::uint32_t> words(3+block/4+1);
    words[0]=0xffff0300;words[1]=((1+block/4)<<16)|0xfffe;words[2]=0x42415443;
    auto* c=reinterpret_cast<unsigned char*>(words.data()+3);
    const auto u32=[&](unsigned at,std::uint32_t x){std::memcpy(c+at,&x,4);};
    const auto u16=[&](unsigned at,std::uint16_t x){std::memcpy(c+at,&x,2);};
    u32(12,3);u32(16,28);u32(28,88);u16(32,3);u16(34,2);u16(36,1);
    u32(48,104);u16(52,3);u16(54,5);u16(56,1);
    u32(68,120);u16(72,3);u16(74,7);u16(76,1);
    std::memcpy(c+88,"mapReflection2D",16);std::memcpy(c+104,"mapRefraction2D",16);
    std::memcpy(c+120,"mapEnvironment",15);
    words.back()=0x0000ffff;return words;
}
std::vector<std::uint32_t> VertexBytecode(){
    constexpr unsigned block=120;std::vector<std::uint32_t> words(3+block/4+1);
    words[0]=0xfffe0300;words[1]=((1+block/4)<<16)|0xfffe;words[2]=0x42415443;
    auto* c=reinterpret_cast<unsigned char*>(words.data()+3);
    const auto u32=[&](unsigned at,std::uint32_t x){std::memcpy(c+at,&x,4);};
    const auto u16=[&](unsigned at,std::uint16_t x){std::memcpy(c+at,&x,2);};
    u32(12,2);u32(16,28);u32(28,68);u16(32,2);u16(34,9);u16(36,1);
    u32(48,84);u16(52,2);u16(54,40);u16(56,3);
    std::memcpy(c+68,"v4EyePosObject",15);std::memcpy(c+84,"m34ObjToCubeSpace",18);
    words.back()=0x0000ffff;return words;
}
struct Symbol{const char* name;std::uint16_t first,count;};
std::vector<std::uint32_t> NamedVertex(const std::vector<Symbol>& symbols){
    const unsigned names=28+unsigned(symbols.size())*20;
    unsigned block=names;for(const auto& s:symbols)block+=unsigned(std::strlen(s.name))+1;
    block=(block+3)&~3u;std::vector<std::uint32_t> words(3+block/4+1);
    words[0]=0xfffe0300;words[1]=((1+block/4)<<16)|0xfffe;words[2]=0x42415443;words.back()=0xffff;
    auto* c=reinterpret_cast<unsigned char*>(words.data()+3);
    const auto u32=[&](unsigned at,std::uint32_t x){std::memcpy(c+at,&x,4);};
    const auto u16=[&](unsigned at,std::uint16_t x){std::memcpy(c+at,&x,2);};
    u32(12,unsigned(symbols.size()));u32(16,28);unsigned name=names;
    for(unsigned i=0;i<symbols.size();++i){const auto& s=symbols[i];u32(28+i*20,name);u16(32+i*20,2);u16(34+i*20,s.first);u16(36+i*20,s.count);
        std::memcpy(c+name,s.name,std::strlen(s.name)+1);name+=unsigned(std::strlen(s.name))+1;}
    return words;
}
}
int main(){using namespace NativeReflectionProbeKL;
    StratifiedSampler schedule;unsigned initial=0;
    for(unsigned i=0;i<1024;++i)if(schedule.Next(1))++initial;
    unsigned firstFrame[4]{},firstCount=0;
    for(unsigned i=0;i<1024;++i)if(schedule.Next(2)){if(firstCount<4)firstFrame[firstCount]=i;++firstCount;}
    Check(initial==4&&firstCount==4&&firstFrame[1]-firstFrame[0]==256&&firstFrame[2]-firstFrame[1]==256&&firstFrame[3]-firstFrame[2]==256,
        "draw candidates span the complete preceding frame");
    unsigned nextFirst=0,nextCount=0;
    for(unsigned i=0;i<1024;++i)if(schedule.Next(3)){if(!nextCount)nextFirst=i;++nextCount;}
    Check(nextCount==4&&nextFirst!=firstFrame[0],"candidate positions move across frames");
    schedule.Reset();Check(schedule.Next(999),"reset restarts the sampling schedule");
    bool phases[256]{};unsigned distinctPhases=0;schedule.Reset();
    // First frame establishes a 1024-draw estimate; then sample at the same
    // one-in-four cadence a fixed time limiter may impose.
    for(unsigned i=0;i<1024;++i)schedule.Next(0);
    for(unsigned frame=4;frame<=1024;frame+=4){bool first=true;for(unsigned draw=0;draw<1024;++draw)
        if(schedule.Next(frame)&&first){first=false;if(!phases[draw]){phases[draw]=true;++distinctPhases;}}}
    Check(distinctPhases>120,"fixed one-in-four cadence covers diverse draw phases");
    auto code=Bytecode();Samplers map;
    Check(ParseSamplers(code.data(),code.size()*4,map)&&map.reflection==(1u<<2)&&map.refraction==(1u<<5)&&map.environment==(1u<<7),"exact planar and environment sampler registers from CTAB");
    auto bad=code;bad[1]=0x7ffffffe;Check(!ParseSamplers(bad.data(),bad.size()*4,map),"truncated comment rejected");
    bad=code;reinterpret_cast<unsigned char*>(bad.data()+3)[88]='x';Check(ParseSamplers(bad.data(),bad.size()*4,map)&&!map.reflection&&map.refraction&&map.environment,"different names do not match reflection");
    bad=code;reinterpret_cast<unsigned char*>(bad.data()+3)[120]='x';Check(ParseSamplers(bad.data(),bad.size()*4,map)&&!map.environment&&map.reflection,"only exact environment name accepted");
    bad=code;auto* renamed=reinterpret_cast<unsigned char*>(bad.data()+3);std::memcpy(renamed+120,"samplerEnvironment",19);
    Check(ParseSamplers(bad.data(),bad.size()*4,map)&&map.environment==(1u<<7),"compiled shader samplerEnvironment name accepted");
    auto samplerNames=code;samplerNames.resize(3+216/4+1);samplerNames[1]=((1+216/4)<<16)|0xfffe;samplerNames.back()=0x0000ffff;
    auto* symbols=reinterpret_cast<unsigned char*>(samplerNames.data()+3);
    const std::uint32_t name1=144,name2=168,name3=192;std::memcpy(symbols+28,&name1,4);std::memcpy(symbols+48,&name2,4);std::memcpy(symbols+68,&name3,4);
    std::memcpy(symbols+144,"samplerReflection2D",20);std::memcpy(symbols+168,"samplerRefraction2D",20);std::memcpy(symbols+192,"samplerEnvironment",19);
    Check(ParseSamplers(samplerNames.data(),samplerNames.size()*4,map)&&map.reflection==(1u<<2)&&map.refraction==(1u<<5)&&map.environment==(1u<<7),"compiled sampler variable names accepted for all reflection sources");
    std::vector<std::uint32_t> more(3+288/4+1);more[0]=0xffff0300;more[1]=((1+288/4)<<16)|0xfffe;more[2]=0x42415443;more.back()=0x0000ffff;
    auto* data=reinterpret_cast<unsigned char*>(more.data()+3);const auto u32=[&](unsigned off,std::uint32_t v){std::memcpy(data+off,&v,4);};const auto u16=[&](unsigned off,std::uint16_t v){std::memcpy(data+off,&v,2);};
    u32(12,3);u32(16,28);const char* names[]={"samplerEnvironment","samplerReflectionMask","samplerReflectionFallOff"};
    for(unsigned i=0;i<3;++i){u32(28+i*20,112+i*32);u16(32+i*20,3);u16(34+i*20,std::uint16_t(7+i));u16(36+i*20,1);std::memcpy(data+112+i*32,names[i],std::strlen(names[i])+1);}
    Check(ParseSamplers(more.data(),more.size()*4,map)&&map.environment==(1u<<7)&&map.mask==(1u<<8)&&map.falloff==(1u<<9),"environment reflection mask and falloff registers are separately identified");
    auto boundary=more;auto* bd=reinterpret_cast<unsigned char*>(boundary.data()+3);std::uint16_t reg=0,nreg=16;std::memcpy(bd+34,&reg,2);std::memcpy(bd+36,&nreg,2);
    Check(ParseSamplers(boundary.data(),boundary.size()*4,map)&&map.environment==0xffff,"maximum legal sampler range retained");
    reg=15;nreg=2;std::memcpy(bd+34,&reg,2);std::memcpy(bd+36,&nreg,2);
    Check(ParseSamplers(boundary.data(),boundary.size()*4,map)&&!map.environment,"sampler range beyond register 15 rejected");
    const std::uint32_t stripped[]={0xffff0300,0x0000ffff};Check(!ParseSamplers(stripped,sizeof(stripped),map),"stripped shader makes no invented CTAB");
    Check(!ParseSamplers(nullptr,0,map),"null bytecode rejected");
    auto vertexCode=VertexBytecode();VertexMap vertexMap;
    Check(ParseVertexConstants(vertexCode.data(),vertexCode.size()*4,vertexMap)&&vertexMap.eye.start==9&&vertexMap.eye.count==1&&vertexMap.cube.start==40&&vertexMap.cube.count==3,"exact eye and cube-space CTAB ranges");
    auto invalidVertex=vertexCode;auto* vc=reinterpret_cast<unsigned char*>(invalidVertex.data()+3);const std::uint16_t invalidStart=255;std::memcpy(vc+54,&invalidStart,2);
    Check(ParseVertexConstants(invalidVertex.data(),invalidVertex.size()*4,vertexMap)&&!vertexMap.cube.count&&vertexMap.eye.count==1,"out-of-bounds cube registers rejected");
    invalidVertex=vertexCode;invalidVertex[1]=0x7ffffffe;Check(!ParseVertexConstants(invalidVertex.data(),invalidVertex.size()*4,vertexMap),"malformed vertex CTAB rejected");
    Check(!ParseVertexConstants(stripped,sizeof(stripped),vertexMap),"stripped vertex shader leaves no invented registers");
    void* shaderTable[5];for(auto& f:shaderTable)f=reinterpret_cast<void*>(&Forbidden);
    shaderTable[1]=reinterpret_cast<void*>(&ShaderRef);shaderTable[2]=reinterpret_cast<void*>(&ShaderRelease);shaderTable[4]=reinterpret_cast<void*>(&Function);
    Shader shader{shaderTable,1,code.data(),UINT(code.size()*4),0};
    Shader vertex{shaderTable,1,vertexCode.data(),UINT(vertexCode.size()*4),0};
    void* textureTable[18];for(auto& f:textureTable)f=reinterpret_cast<void*>(&Forbidden);
    textureTable[2]=reinterpret_cast<void*>(&ResourceRelease);textureTable[10]=reinterpret_cast<void*>(&Type);textureTable[17]=reinterpret_cast<void*>(&TextureDesc);
    void* surfaceTable[13];for(auto& f:surfaceTable)f=reinterpret_cast<void*>(&Forbidden);
    surfaceTable[2]=reinterpret_cast<void*>(&ResourceRelease);surfaceTable[12]=reinterpret_cast<void*>(&SurfaceDesc);
    Resource texture{textureTable,1},target{surfaceTable,1};
    void* table[119];for(auto& f:table)f=reinterpret_cast<void*>(&Forbidden);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&State);
    table[64]=reinterpret_cast<void*>(&GetTextureStub);table[93]=reinterpret_cast<void*>(&Vertex);table[95]=reinterpret_cast<void*>(&VertexConstants);table[108]=reinterpret_cast<void*>(&Pixel);
    Device fixture{table,&shader,&texture,&target,0,&vertex};auto* device=reinterpret_cast<IDirect3DDevice9*>(&fixture);
    Configure(false);Reset();Observe(device,8);Check(fixture.gets==0&&!Snapshot().enabled&&!Snapshot().historyCount,"disabled diagnostic makes no COM calls and leaves history empty");
    Configure(true);Observe(device,8);auto out=Snapshot();
    Check(out.enabled&&out.samples==1&&out.ctab==1&&out.named==1&&out.lastNamed.valid&&out.lastNamed.frameId==8,"live observer retains bounded named shader sample");
    Check(out.lastNamed.textures[2].width==512&&out.lastNamed.textures[5].height==256&&out.lastNamed.targetWidth==3840&&out.lastNamed.viewportWidth==1280,"named textures and render target descriptors obtained");
    Check(out.lastNamed.vertexCtab&&out.lastNamed.eyeRead&&out.lastNamed.cubeRead&&out.lastNamed.eye[0]==9.f&&out.lastNamed.cube[0]==40.f,"matching draw captures current vertex eye and cube registers");
    Check(shader.refs==1&&vertex.refs==1&&texture.refs==1&&target.refs==1,"all COM getters release their references");
    const auto gets=fixture.gets;for(unsigned i=0;i<10000;++i)Observe(device,9);
    Check(fixture.gets==gets&&Snapshot().samples==1,"ten thousand draws inside the rate limit do not query device");
    const auto scan=[&](std::uint64_t frame){for(unsigned i=0;i<10000;++i)Observe(device,frame);};
    testTick+=100;texture.kind=D3DRTYPE_CUBETEXTURE;scan(10);out=Snapshot();
    Check(out.lastNamed.samplers.environment==(1u<<7)&&out.lastNamed.textures[7].kind==D3DRTYPE_CUBETEXTURE&&out.lastNamed.textures[7].width==512,"cube-map descriptor and sampler retained");
    testTick+=100;shader.code=stripped;shader.bytes=sizeof(stripped);scan(11);out=Snapshot();
    Check(out.samples==3&&out.shaderReads==3&&out.ctab==2&&out.named==2&&out.lastNamed.frameId==10,"stripped CTAB counted but previous named sample stays identifiable");
    Check(out.recorded==3&&out.historyCount==3&&!out.history[2].valid&&!out.history[2].ctabKnown&&
        out.history[2].shaderHash&&out.history[2].textures[0].width==512&&out.history[2].targetWidth==3840&&out.history[2].vertexHash,
        "stripped CTAB retains bounded fallback shader, textures, target and vertex evidence");
    shader.code=code.data();shader.bytes=UINT(code.size()*4);
    for(unsigned i=0;i<HistoryLimit+4;++i){testTick+=100;scan(100+i);}
    out=Snapshot();Check(out.historyCount==HistoryLimit&&out.historyOverwritten==7&&out.named==HistoryLimit+6&&out.recorded==HistoryLimit+7,"bounded history counts overwrites after a prolonged writer stall");
    bool chronological=true;for(unsigned i=0;i<out.historyCount;++i)
        chronological=chronological&&out.history[i].sequence==8+i&&out.history[i].frameId==104+i&&out.history[i].eyeRead&&out.history[i].cubeRead;
    Check(chronological&&out.lastNamed.sequence==HistoryLimit+7,"history preserves sample order and same-draw data after wrap");
    const auto beforeRate=out.samples;testTick+=39;scan(10000);Check(Snapshot().samples==beforeRate,"global 40ms limiter rejects early candidates");
    testTick+=1;scan(10001);Check(Snapshot().samples==beforeRate+1,"global 40ms limiter accepts a subsequent candidate");
    Configure(false);testTick+=100;scan(10002);Check(Snapshot().samples==beforeRate+1,"disable stops sampling immediately");
    const auto lastSequence=Snapshot().latestSequence;
    Reset();out=Snapshot();Check(!out.samples&&!out.named&&!out.lastNamed.valid&&!out.historyCount&&out.latestSequence==lastSequence,
        "reset clears counters and history while preserving global sequence");
    Configure(true);testTick+=100;scan(20000);out=Snapshot();
    Check(out.named==1&&out.historyCount==1&&out.latestSequence==lastSequence+1&&out.history[0].sequence==lastSequence+1,
        "new device samples remain distinguishable from previous device samples");
    const auto beforeConcurrent=out.latestSequence;resetDuringPixel=true;testTick+=100;
    for(unsigned i=0;i<10000&&resetDuringPixel;++i)Observe(device,20001);
    out=Snapshot();
    Check(!resetDuringPixel&&!out.samples&&!out.named&&!out.historyCount&&out.latestSequence==beforeConcurrent,
        "in-flight old-device observation is discarded when Reset races the draw");
    testTick+=100;scan(20002);out=Snapshot();
    Check(out.named==1&&out.history[0].sequence==beforeConcurrent+1,"new observation succeeds after racing Reset");
    Reset();testTick+=100;sceneChecks=0;sceneAllowed=false;const auto beforeGatedGets=fixture.gets;
    for(unsigned i=0;i<40000;++i)Observe(device,60000,Scene);
    Check(sceneChecks==1&&fixture.gets==beforeGatedGets&&!Snapshot().samples,
        "40000 real observer calls defer expensive scene predicate until one bounded candidate; denied scene never reads COM");
    sceneAllowed=true;testTick+=40;
    for(unsigned i=0;i<40000;++i)Observe(device,60001,Scene);
    Check(sceneChecks==2&&fixture.gets>beforeGatedGets&&Snapshot().samples==1,
        "next eligible bounded candidate retains the original same-draw shader/material evidence");
    const auto beforeLimitedGets=fixture.gets;sceneAllowed=false;testTick+=39;
    for(unsigned i=0;i<40000;++i)Observe(device,60002,Scene);
    Check(sceneChecks==2&&fixture.gets==beforeLimitedGets&&Snapshot().samples==1,
        "40ms limiter runs before the expensive scene predicate as well as before GPU getters");
    Configure(false);testTick+=100;
    for(unsigned i=0;i<40000;++i)Observe(device,60003,Scene);
    Check(sceneChecks==2&&fixture.gets==beforeLimitedGets,
        "disabled probe neither evaluates scene predicate nor touches COM across a full draw stream");
    auto shadowCode=NamedVertex({{"m44ModelViewProj",0,4},{"m44WorldViewProjScale",4,4},
        {"m44StaticShadowProjectionMatrix",70,4},{"m44WorldShadowProj",80,4},{"m44WorldShadowClip",90,4},
        {"m44WorldShadowProj1",100,4},{"m44WorldShadowClip1",110,4},{"m44WorldShadowProj2",120,4},
        {"m44WorldShadowClip2",130,4},{"m44WorldShadowProj3",140,4},{"m44WorldShadowClip3",150,4}});
    Check(ParseVertexConstants(shadowCode.data(),shadowCode.size()*4,vertexMap)&&vertexMap.modelViewProjection.start==0&&
        vertexMap.modelViewProjection.count==4&&vertexMap.worldViewProjectionScale.start==4&&
        vertexMap.shadow[0].start==70&&vertexMap.shadow[8].start==150,"exact MVP, scale and all nine bounded shadow CTAB names parsed");
    auto unknownShadow=NamedVertex({{"m44WorldShadowProjector",20,4},{"m44StaticShadowProjectionMatrix",252,5},{"m44WorldShadowClip",254,4}});
    Check(ParseVertexConstants(unknownShadow.data(),unknownShadow.size()*4,vertexMap)&&!vertexMap.shadow[0].count&&
        !vertexMap.shadow[1].count&&!vertexMap.shadow[2].count,"unknown, oversized and out-of-bounds shadow declarations produce no invented ranges");
    vertex.code=shadowCode.data();vertex.bytes=UINT(shadowCode.size()*4);fixture.vertex=&vertex;fixture.shader=&shader;
    contextValue.state=0x123400;contextValue.generation=77;contextValue.eyeIndex=1;
    for(unsigned i=0;i<12;++i){contextValue.world[i]=float(i);contextValue.eye[i]=float(20+i);}
    for(unsigned i=0;i<16;++i)contextValue.projection[i]=float(40+i);
    ConfigureContext(Context);Configure(true);Reset();testTick+=100;contextChecks=constantChecks=0;scan(70000);out=Snapshot();
    const auto sample=out.history[0];
    Check(out.recorded==1&&sample.pixelAvailable&&sample.vertexAvailable&&sample.contextResult==ContextResult::Available&&contextChecks==1,
        "available same-draw context is captured once on an eligible shader sample");
    Check(sample.context.state==contextValue.state&&sample.context.generation==77&&sample.context.eyeIndex==1&&
        !std::memcmp(sample.context.world,contextValue.world,sizeof(contextValue.world))&&
        !std::memcmp(sample.context.eye,contextValue.eye,sizeof(contextValue.eye))&&
        !std::memcmp(sample.context.projection,contextValue.projection,sizeof(contextValue.projection)),"context arrays and exact generation remain coupled to the same draw");
    Check(sample.planarMatricesAttempted&&sample.planarMatricesRead&&sample.planarMatrices[31]==3.1f,
        "named planar sample retains raw c0 through c7 without claiming other shader layouts");
    Check(sample.shadowAttemptMask==0x1ff&&sample.shadowReadMask==0x1ff&&sample.shadowValues[8][15]==151.5f&&constantChecks==11,
        "only nine declared bounded shadow ranges, shadow MVP and one planar range are read");
    Check(sample.shadowModelViewProjectionAttempted&&sample.shadowModelViewProjectionRead&&sample.shadowModelViewProjection[15]==1.5f,
        "named shadow draw retains its actual exact CTAB MVP instead of assuming c0 layout");
    const auto contextCount=contextChecks,constantCount=constantChecks;const auto getterCount=fixture.gets;
    testTick+=39;for(unsigned i=0;i<40000;++i)Observe(device,70001,Scene);
    Check(contextChecks==contextCount&&constantChecks==constantCount&&fixture.gets==getterCount,
        "all new context and constant getters remain behind the 40ms gate across 40000 draws");
    failConstantRegister=70;nanConstantRegister=80;testTick+=100;scan(70002);out=Snapshot();
    Check(out.history[out.historyCount-1].shadowAttemptMask==0x1ff&&out.history[out.historyCount-1].shadowReadMask==0x1fc&&
        out.history[out.historyCount-1].shadowValues[0][0]==0&&out.history[out.historyCount-1].shadowValues[1][0]==0,
        "failed or nonfinite shadow reads are explicitly missing and zeroed instead of leaking partial constants");
    failConstantRegister=0;nanConstantRegister=~0u;contextAllowed=false;testTick+=100;scan(70003);out=Snapshot();
    Check(out.history[out.historyCount-1].planarMatricesAttempted&&!out.history[out.historyCount-1].planarMatricesRead&&
        out.history[out.historyCount-1].planarMatrices[0]==0&&out.history[out.historyCount-1].contextResult==ContextResult::Unavailable&&
        !out.history[out.historyCount-1].context.state,"planar read failure and denied context remain separate explicit absences");
    failConstantRegister=~0u;contextAllowed=true;contextValue.projection[2]=std::numeric_limits<float>::quiet_NaN();
    testTick+=100;scan(70004);out=Snapshot();
    Check(out.history[out.historyCount-1].contextResult==ContextResult::Invalid&&out.history[out.historyCount-1].context.projection[2]==0,
        "nonfinite context cannot produce invalid JSON or a false valid matrix");
    contextValue.projection[2]=42;ConfigureContext(nullptr);const auto priorContext=contextChecks;testTick+=100;scan(70005);out=Snapshot();
    Check(out.history[out.historyCount-1].contextResult==ContextResult::NotConfigured&&contextChecks==priorContext,
        "unconfigured callback is distinguishable from an unavailable same-draw context");
    ConfigureContext(Context);fixture.shader=nullptr;Reset();testTick+=100;scan(70006);out=Snapshot();
    Check(out.samples==1&&out.shaderUnavailable==1&&!out.shaderReads&&out.recorded==1&&!out.named&&out.historyCount==1&&
        !out.history[0].pixelAvailable&&out.history[0].vertexAvailable&&!out.history[0].valid&&!out.history[0].shaderHash&&
        out.history[0].shadowReadMask==0x1ff&&!out.history[0].planarMatricesAttempted&&out.history[0].contextResult==ContextResult::Available,
        "PS NULL shadow path still captures VS constants and context without inventing reflection or pixel identity");
    Check(out.history[0].shadowModelViewProjectionRead&&out.history[0].shadowModelViewProjection[15]==1.5f,
        "PS NULL shadow path also retains its real geometry MVP");
    vertex.code=stripped;vertex.bytes=sizeof(stripped);testTick+=100;scan(70007);out=Snapshot();
    Check(out.history[out.historyCount-1].vertexAvailable&&!out.history[out.historyCount-1].vertexCtab&&
        !out.history[out.historyCount-1].shadowAttemptMask&&!out.history[out.historyCount-1].shadowReadMask,
        "stripped vertex CTAB records no invented shadow bindings");
    fixture.vertex=nullptr;Reset();const auto noShaderContexts=contextChecks;testTick+=100;scan(70008);out=Snapshot();
    Check(out.samples==1&&!out.recorded&&!out.historyCount&&contextChecks==noShaderContexts,
        "missing both shaders performs no context read and publishes no false material observation");
    fixture.shader=&shader;fixture.vertex=&vertex;vertex.code=shadowCode.data();vertex.bytes=UINT(shadowCode.size()*4);
    contextValue.state=0;testTick+=100;scan(70009);out=Snapshot();
    Check(out.history[0].contextResult==ContextResult::Invalid,"zero context state cannot masquerade as a valid same-draw owner");
    contextValue.state=0x123400;contextValue.generation=0;testTick+=100;scan(70010);out=Snapshot();
    Check(out.history[out.historyCount-1].contextResult==ContextResult::Invalid,"zero context generation remains explicit invalid evidence");
    contextValue.generation=77;contextValue.eyeIndex=2;testTick+=100;scan(70011);out=Snapshot();
    Check(out.history[out.historyCount-1].contextResult==ContextResult::Invalid,"out-of-range eye index cannot borrow another eye identity");
    contextValue.eyeIndex=1;resetDuringContext=true;testTick+=100;
    for(unsigned i=0;i<10000&&resetDuringContext;++i)Observe(device,70012);out=Snapshot();
    Check(!resetDuringContext&&!out.samples&&!out.recorded&&!out.historyCount,"Reset racing a context callback discards the entire old-device observation");
    Configure(false);ConfigureContext(nullptr);
    std::printf("Native reflection material probe: %u checks, %u failures; private CPU/COM only; sample=%zu bytes history=%zu bytes.\n",checks,failed,sizeof(Sample),sizeof(Stats));return failed?1:0;
}
