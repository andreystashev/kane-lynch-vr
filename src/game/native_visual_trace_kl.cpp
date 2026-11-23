#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "native_visual_trace_kl.hpp"
#include "native_visual_trace_policy.hpp"
#include "recording_ipc.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <deque>
#include <set>
#include <unordered_map>
#include <vector>
#include <string>

namespace {
namespace Policy=NativeVisualTracePolicy;
constexpr unsigned TileW=96,TileH=54,Columns=16,Rows=16,Pages=1;
constexpr unsigned MaxDraws=8192,MaxDetails=6,IntervalMs=Policy::IntervalMs,MaxFrames=Policy::MaxFrames;
constexpr std::uint64_t DiskBudget=Policy::DiskBudget,EventBudget=Policy::EventBudget;
template<class T> void Release(T*& p){if(p){p->Release();p=nullptr;}}
struct EventHeader {std::uint32_t tag=0,size=0,draw=0,ordinal=0;std::uint64_t sequence=0;};
struct Texture {std::uint64_t pointer=0;unsigned type=0,width=0,height=0,depth=0,format=0,levels=0;HRESULT result=E_FAIL;};
struct Stream {std::uint64_t pointer=0;unsigned offset=0,stride=0,size=0,usage=0,pool=0;HRESULT result=E_FAIL;};
constexpr D3DRENDERSTATETYPE RenderStates[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,
    D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,
    D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_COLORWRITEENABLE,D3DRS_CULLMODE,D3DRS_DEPTHBIAS,
    D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,
    D3DRS_LIGHTING,D3DRS_TEXTUREFACTOR,D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
struct DrawState {
    unsigned version=1,index=0,tick=0,thread=0;
    D3D9Capture::DrawCall call{};
    std::uint64_t rt=0,depth=0,vs=0,ps=0,declaration=0,indexBuffer=0;
    D3DSURFACE_DESC rtDesc{},depthDesc{};D3DVIEWPORT9 viewport{};RECT scissor{};
    DWORD fvf=0,renderStates[24]{};HRESULT renderStateResults[24]{};
    DWORD samplerStates[8][6]{};
    HRESULT samplerResults[8][6]{},transformResults[3]{};
    Texture textures[16]{};Stream streams[4]{};
    D3DINDEXBUFFER_DESC indexDesc{};
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT elementCount=MAXD3DDECLLENGTH+1;
    std::uint64_t vsHash=0,psHash=0;
    D3DMATRIX world{},view{},projection{};
    float vsConstants[256*4]{},psConstants[224*4]{};
    HRESULT results[16]{};
};
struct DrawResult {HRESULT result=E_FAIL,preview=E_PENDING,detail=E_PENDING;unsigned page=0,tile=0,detailIndex=~0u;};
struct ShaderHeader {std::uint64_t pointer=0,hash=0;unsigned kind=0,length=0;HRESULT result=E_FAIL;};
struct Image {unsigned width=0,height=0;std::wstring name;std::vector<unsigned char> pixels;HRESULT result=E_FAIL;};
struct Surface {IDirect3DSurface9* p=nullptr;unsigned width=0,height=0;std::wstring name;HRESULT copyResult=E_PENDING;};
// The game is32-bit. Evidence must not require an80MiB contiguous allocation
// or a vector growth that temporarily doubles that address-space burden.
struct EventBuffer {
    static constexpr std::size_t Chunk=1024*1024;
    std::vector<std::vector<unsigned char>> blocks;std::size_t bytes=0;
    std::size_t size()const{return bytes;}
    void Append(const void* data,std::size_t count){auto* source=static_cast<const unsigned char*>(data);
        while(count){if(blocks.empty()||blocks.back().size()==Chunk){blocks.emplace_back();blocks.back().reserve(Chunk);}
            auto& block=blocks.back();const auto take=std::min(count,Chunk-block.size());block.insert(block.end(),source,source+take);source+=take;count-=take;bytes+=take;}}
};
struct Frame {
    std::atomic<unsigned>* inflight=nullptr;
    DWORD drawThread=0;
    unsigned number=0,draws=0,omitted=0,ordinal=0;std::uint64_t droppedEvents=0;
    NativeVisualTraceKL::Scene scene{};DWORD start=0;bool beforeCapture=false;
    EventBuffer events;std::vector<Image> images;
    std::vector<Surface> atlas,details;Surface final;
    IDirect3DSurface9* resolve=nullptr;D3DSURFACE_DESC resolveDesc{};
    std::unordered_map<std::uint64_t,std::uint64_t> shaders;std::set<std::uint64_t> detailKeys;
    std::vector<IUnknown*> shaderRefs; // prevent shader pointer reuse within a recorded frame
    std::uint64_t detailKey=0;DrawState current{};bool drawOpen=false;
    ~Frame(){for(auto& s:atlas)Release(s.p);for(auto& s:details)Release(s.p);Release(final.p);Release(resolve);for(auto*& s:shaderRefs)Release(s);if(inflight)--*inflight;}
};
struct Session {
    std::atomic<bool> enabled{false},active{false},budget{false};
    std::atomic<std::uint64_t> frames{0},written{0},draws{0},dropped{0},images{0},copyFail{0},readFail{0},writeFail{0},queueDrops{0},bytes{0};
    std::atomic<std::uint64_t> recordingSuspended{0},memorySuspended{0},previewSkipped{0};
    std::atomic<NativeVisualTraceKL::ContextCallback> context{nullptr};
    std::atomic<unsigned> inflight{0};bool writerReady=false;
    std::wstring directory;HANDLE event=nullptr;std::mutex lock;
    std::recursive_mutex dataLock; // producer and GPU consumer are different threads
    std::deque<std::unique_ptr<Frame>> queue;
    std::unique_ptr<Frame> frame;unsigned remaining=0;DWORD lastArm=0;bool support=false;
};
Session* session=nullptr; // Deliberately process-lifetime; no loader-lock join.
thread_local bool insideTrace=false;
struct RecorderDemand {
    HANDLE mapping=nullptr;const RecordingIPC::Shared* shared=nullptr;DWORD nextPoll=0;
    void Close(){if(shared)UnmapViewOfFile(shared);shared=nullptr;if(mapping)CloseHandle(mapping);mapping=nullptr;}
    bool Active(DWORD now){
        if(!shared&&(!nextPoll||static_cast<LONG>(now-nextPoll)>=0)){
            nextPoll=now+500;wchar_t name[100];RecordingIPC::MappingName(name,100,GetCurrentProcessId());
            mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
            // Only metadata is mapped. Never map/copy the recorder's12MiB pixels.
            if(mapping)shared=static_cast<const RecordingIPC::Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,128));
            if(!shared){Close();return false;}
        }
        if(!shared)return false;
        RecordingIPC::Request request{};
        if(RecordingIPC::Compatible(shared,GetCurrentProcessId())&&RecordingIPC::ReadRequest(shared,request)&&RecordingIPC::Valid(request,now))return true;
        // A completed/restarted recorder must not retain an obsolete mapping.
        Close();return false;
    }
} recorder;
bool Protected(DWORD now){
    if(recorder.Active(now)){++session->recordingSuspended;return true;}
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(!Policy::MemoryAvailable(GlobalMemoryStatusEx(&memory)!=FALSE,memory.ullAvailVirtual,memory.ullAvailPhys)){
        ++session->memorySuspended;return true;
    }
    return false;
}
void AllocationFailure(){insideTrace=false;if(session){session->active=false;session->budget=true;++session->writeFail;}}
std::uint64_t Hash(const void* bytes,std::size_t count){auto p=static_cast<const unsigned char*>(bytes);std::uint64_t h=14695981039346656037ull;while(count--){h^=*p++;h*=1099511628211ull;}return h;}
void Append(Frame& f,unsigned tag,const void* data,std::size_t size,std::uint64_t sequence=0){
    if((!data&&size)||!Policy::Fits(f.events.size(),sizeof(EventHeader),size)){++f.droppedEvents;return;}
    EventHeader header{tag,static_cast<unsigned>(size),f.draws,f.ordinal++,sequence};
    f.events.Append(&header,sizeof(header));if(size)f.events.Append(data,size);
}
template<class Shader> std::uint64_t ShaderBytes(Frame& f,Shader* shader,unsigned kind){
    if(!shader)return 0;const auto pointer=reinterpret_cast<std::uintptr_t>(shader);
    const auto key=std::uint64_t(pointer)|(std::uint64_t(kind)<<32);
    const auto old=f.shaders.find(key);if(old!=f.shaders.end())return old->second;
    f.shaderRefs.push_back(shader);shader->AddRef();
    ShaderHeader h;h.pointer=pointer;h.kind=kind;UINT size=0;h.result=shader->GetFunction(nullptr,&size);
    std::vector<unsigned char> bytes;
    if(SUCCEEDED(h.result)&&size&&size<=256*1024){bytes.resize(size);h.result=shader->GetFunction(bytes.data(),&size);if(SUCCEEDED(h.result)){h.length=size;h.hash=Hash(bytes.data(),size);}else bytes.clear();}
    else if(SUCCEEDED(h.result))h.result=E_INVALIDARG;
    std::vector<unsigned char> packet(sizeof(h)+h.length);std::memcpy(packet.data(),&h,sizeof(h));if(h.length)std::memcpy(packet.data()+sizeof(h),bytes.data(),h.length);
    Append(f,3,packet.data(),packet.size());f.shaders[key]=h.hash;return h.hash;
}
void State(IDirect3DDevice9* d,Frame& f,const D3D9Capture::DrawCall& call){
    DrawState s;s.index=f.draws;s.call=call;s.tick=GetTickCount();s.thread=GetCurrentThreadId();
    IDirect3DSurface9* surface=nullptr;s.results[0]=d->GetRenderTarget(0,&surface);s.rt=reinterpret_cast<std::uintptr_t>(surface);
    if(surface){s.results[1]=surface->GetDesc(&s.rtDesc);surface->Release();surface=nullptr;}
    s.results[2]=d->GetDepthStencilSurface(&surface);s.depth=reinterpret_cast<std::uintptr_t>(surface);
    if(surface){s.results[3]=surface->GetDesc(&s.depthDesc);surface->Release();}
    s.results[4]=d->GetViewport(&s.viewport);s.results[5]=d->GetScissorRect(&s.scissor);s.results[6]=d->GetFVF(&s.fvf);
    for(unsigned i=0;i<24;++i)s.renderStateResults[i]=d->GetRenderState(RenderStates[i],&s.renderStates[i]);
    constexpr D3DSAMPLERSTATETYPE sampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<6;++j)s.samplerResults[i][j]=d->GetSamplerState(i,sampler[j],&s.samplerStates[i][j]);
    for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9* t=nullptr;auto& out=s.textures[i];out.result=d->GetTexture(i,&t);out.pointer=reinterpret_cast<std::uintptr_t>(t);
        if(t){out.type=t->GetType();out.levels=t->GetLevelCount();D3DSURFACE_DESC desc{};
            if(out.type==D3DRTYPE_TEXTURE)out.result=static_cast<IDirect3DTexture9*>(t)->GetLevelDesc(0,&desc);
            else if(out.type==D3DRTYPE_CUBETEXTURE)out.result=static_cast<IDirect3DCubeTexture9*>(t)->GetLevelDesc(0,&desc);
            else if(out.type==D3DRTYPE_VOLUMETEXTURE){D3DVOLUME_DESC v{};out.result=static_cast<IDirect3DVolumeTexture9*>(t)->GetLevelDesc(0,&v);desc.Width=v.Width;desc.Height=v.Height;desc.Format=v.Format;out.depth=v.Depth;}
            out.width=desc.Width;out.height=desc.Height;out.format=desc.Format;t->Release();}}
    for(unsigned i=0;i<4;++i){IDirect3DVertexBuffer9* b=nullptr;auto& out=s.streams[i];out.result=d->GetStreamSource(i,&b,&out.offset,&out.stride);out.pointer=reinterpret_cast<std::uintptr_t>(b);
        if(b){D3DVERTEXBUFFER_DESC desc{};out.result=b->GetDesc(&desc);out.size=desc.Size;out.usage=desc.Usage;out.pool=desc.Pool;b->Release();}}
    IDirect3DIndexBuffer9* ib=nullptr;s.results[7]=d->GetIndices(&ib);s.indexBuffer=reinterpret_cast<std::uintptr_t>(ib);if(ib){s.results[8]=ib->GetDesc(&s.indexDesc);ib->Release();}
    IDirect3DVertexShader9* vs=nullptr;s.results[9]=d->GetVertexShader(&vs);s.vs=reinterpret_cast<std::uintptr_t>(vs);s.vsHash=ShaderBytes(f,vs,0);Release(vs);
    IDirect3DPixelShader9* ps=nullptr;s.results[10]=d->GetPixelShader(&ps);s.ps=reinterpret_cast<std::uintptr_t>(ps);s.psHash=ShaderBytes(f,ps,1);Release(ps);
    IDirect3DVertexDeclaration9* decl=nullptr;s.results[11]=d->GetVertexDeclaration(&decl);s.declaration=reinterpret_cast<std::uintptr_t>(decl);if(decl){s.results[11]=decl->GetDeclaration(s.elements,&s.elementCount);decl->Release();}
    s.results[12]=d->GetVertexShaderConstantF(0,s.vsConstants,256);s.results[13]=d->GetPixelShaderConstantF(0,s.psConstants,224);
    s.transformResults[0]=d->GetTransform(D3DTS_WORLD,&s.world);s.transformResults[1]=d->GetTransform(D3DTS_VIEW,&s.view);s.transformResults[2]=d->GetTransform(D3DTS_PROJECTION,&s.projection);
    f.current=s;Append(f,1,&s,sizeof(s));
}
HRESULT MakeSurface(IDirect3DDevice9* device,Surface& s,unsigned width,unsigned height,const wchar_t* name){
    s.width=width;s.height=height;s.name=name;
    return device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&s.p,nullptr);
}
HRESULT Copy(IDirect3DDevice9* d,Frame& f,IDirect3DSurface9* source,IDirect3DSurface9* dest,const RECT* rect){
    D3DSURFACE_DESC desc{};auto hr=source->GetDesc(&desc);if(FAILED(hr))return hr;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
        if(!f.resolve||f.resolveDesc.Width!=desc.Width||f.resolveDesc.Height!=desc.Height||f.resolveDesc.Format!=desc.Format){Release(f.resolve);f.resolveDesc=desc;
            hr=d->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&f.resolve,nullptr);if(FAILED(hr))return hr;}
        hr=d->StretchRect(source,nullptr,f.resolve,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;source=f.resolve;
    }
    return d->StretchRect(source,nullptr,dest,rect,D3DTEXF_POINT);
}
void ReadImage(IDirect3DDevice9* d,Frame& f,const Surface& source){
    Image out;out.width=source.width;out.height=source.height;out.name=source.name;
    if(!source.p||FAILED(source.copyResult)){out.result=source.copyResult;f.images.push_back(std::move(out));return;}
    IDirect3DSurface9* system=nullptr;out.result=d->CreateOffscreenPlainSurface(out.width,out.height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&system,nullptr);
    if(SUCCEEDED(out.result))out.result=d->GetRenderTargetData(source.p,system);
    if(SUCCEEDED(out.result)){D3DLOCKED_RECT lock{};out.result=system->LockRect(&lock,nullptr,D3DLOCK_READONLY);
        if(SUCCEEDED(out.result)){
            if(!lock.pBits||lock.Pitch<0||unsigned(lock.Pitch)<out.width*4)out.result=E_INVALIDARG;
            else {out.pixels.resize(std::size_t(out.width)*out.height*4);for(unsigned y=0;y<out.height;++y)
                std::memcpy(out.pixels.data()+std::size_t(y)*out.width*4,static_cast<const unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,out.width*4);}
            const auto unlocked=system->UnlockRect();if(FAILED(unlocked))out.result=unlocked;
            if(FAILED(out.result))out.pixels.clear();}}
    if(FAILED(out.result))++session->readFail;Release(system);f.images.push_back(std::move(out));
}
std::uint64_t FileSize(const std::wstring& name){WIN32_FILE_ATTRIBUTE_DATA data{};return GetFileAttributesExW(name.c_str(),GetFileExInfoStandard,&data)?(std::uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow:0;}
void Schema(const std::wstring& directory){
    FILE* file=_wfopen((directory+L"\\schema.json").c_str(),L"wb");if(!file)return;
    std::fprintf(file,"{\"version\":1,\"endianness\":\"little\",\"pointerBytes\":%zu,\"eventHeader\":[\"tag:u32\",\"size:u32\",\"draw:u32\",\"ordinal:u32\",\"sequence:u64\"],\"drawBytes\":%zu,\"resultBytes\":%zu,\"shaderHeaderBytes\":%zu,\"fields\":{",sizeof(void*),sizeof(DrawState),sizeof(DrawResult),sizeof(ShaderHeader));
#define FIELD(name) std::fprintf(file,"\"" #name "\":[%zu,%zu],",offsetof(DrawState,name),sizeof(DrawState::name))
    FIELD(version);FIELD(index);FIELD(tick);FIELD(thread);FIELD(call);FIELD(rt);FIELD(depth);FIELD(rtDesc);FIELD(depthDesc);FIELD(viewport);FIELD(scissor);FIELD(fvf);FIELD(renderStates);FIELD(renderStateResults);FIELD(samplerStates);FIELD(samplerResults);FIELD(transformResults);FIELD(textures);FIELD(streams);FIELD(indexBuffer);FIELD(indexDesc);FIELD(elements);FIELD(elementCount);FIELD(vs);FIELD(ps);FIELD(vsHash);FIELD(psHash);FIELD(world);FIELD(view);FIELD(projection);FIELD(vsConstants);FIELD(psConstants);
#undef FIELD
    std::fprintf(file,"\"results\":[%zu,%zu]},\"renderStateEnums\":[",offsetof(DrawState,results),sizeof(DrawState::results));for(unsigned i=0;i<24;++i)std::fprintf(file,"%s%u",i?",":"",unsigned(RenderStates[i]));
    std::fprintf(file,"],\"tile\":[%u,%u],\"grid\":[%u,%u],\"atlasPages\":%u,\"previewStride\":%u,\"images\":\"PNG straight RGBA from actual BGRA readback; alpha preserved; sampled tiles plus exact selected details; unsampled result S_FALSE/page FFFFFFFF\",\"events\":{\"1\":\"actual draw state\",\"2\":\"draw HRESULT and image location\",\"3\":\"shader header then exact bytecode\",\"10\":\"native owner context\",\"11\":\"native camera context\",\"12\":\"native effect source context\",\"13\":\"closed producer queue context\",\"14\":\"full closed CPU batch bytes, joined by sequence\"},\"types\":{",TileW,TileH,Columns,Rows,Pages,Policy::PreviewStride);
    bool first=true;
#define TYPE(T,NAME) std::fprintf(file,"\"" NAME "\":{\"size\":%zu,\"fields\":{",sizeof(T));first=true
#define TF(T,N) std::fprintf(file,"%s\"" #N "\":[%zu,%zu]",first?"":",",offsetof(T,N),sizeof(T::N));first=false
#define END_TYPE() std::fputs("}}",file)
    using Call=D3D9Capture::DrawCall;TYPE(Call,"DrawCall");TF(Call,kind);TF(Call,type);TF(Call,baseVertex);TF(Call,minVertex);TF(Call,vertices);TF(Call,first);TF(Call,count);TF(Call,stride);TF(Call,indexFormat);TF(Call,vertexData);TF(Call,indexData);END_TYPE();std::fputc(',',file);
    TYPE(DrawResult,"DrawResult");TF(DrawResult,result);TF(DrawResult,preview);TF(DrawResult,detail);TF(DrawResult,page);TF(DrawResult,tile);TF(DrawResult,detailIndex);END_TYPE();std::fputc(',',file);
    TYPE(ShaderHeader,"ShaderHeader");TF(ShaderHeader,pointer);TF(ShaderHeader,hash);TF(ShaderHeader,kind);TF(ShaderHeader,length);TF(ShaderHeader,result);END_TYPE();std::fputc(',',file);
    TYPE(Texture,"Texture");TF(Texture,pointer);TF(Texture,type);TF(Texture,width);TF(Texture,height);TF(Texture,depth);TF(Texture,format);TF(Texture,levels);TF(Texture,result);END_TYPE();std::fputc(',',file);
    TYPE(Stream,"Stream");TF(Stream,pointer);TF(Stream,offset);TF(Stream,stride);TF(Stream,size);TF(Stream,usage);TF(Stream,pool);TF(Stream,result);END_TYPE();std::fputc(',',file);
    TYPE(D3DSURFACE_DESC,"SurfaceDesc");TF(D3DSURFACE_DESC,Format);TF(D3DSURFACE_DESC,Type);TF(D3DSURFACE_DESC,Usage);TF(D3DSURFACE_DESC,Pool);TF(D3DSURFACE_DESC,MultiSampleType);TF(D3DSURFACE_DESC,MultiSampleQuality);TF(D3DSURFACE_DESC,Width);TF(D3DSURFACE_DESC,Height);END_TYPE();
#undef TYPE
#undef TF
#undef END_TYPE
    std::fputs("}}",file);std::fclose(file);
}
DWORD WINAPI Writer(void* pointer){
    auto& s=*static_cast<Session*>(pointer);Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;const auto gdip=Gdiplus::GdiplusStartup(&token,&input,nullptr);
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    for(;;){WaitForSingleObject(s.event,1000);for(;;){std::unique_ptr<Frame> f;{std::lock_guard<std::mutex> lock(s.lock);if(s.queue.empty())break;f=std::move(s.queue.front());s.queue.pop_front();}
        wchar_t number[32];swprintf(number,32,L"frame-%04u",f->number);const auto folder=s.directory+L"\\"+number;
        if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){++s.writeFail;continue;}
        auto path=folder+L"\\events.bin";FILE* binary=_wfopen(path.c_str(),L"wb");bool success=false;
        if(binary){success=true;for(const auto& block:f->events.blocks)if(std::fwrite(block.data(),1,block.size(),binary)!=block.size()){success=false;break;}if(std::fclose(binary))success=false;}if(!success)++s.writeFail;
        s.bytes+=FileSize(path);
        for(auto& image:f->images){if(FAILED(image.result)||image.pixels.empty())continue;
            Gdiplus::Status result=Gdiplus::GenericError;if(gdip==Gdiplus::Ok){Gdiplus::Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppARGB,image.pixels.data());result=bitmap.Save((folder+L"\\"+image.name+L".png").c_str(),&png,nullptr);}
            if(result==Gdiplus::Ok){++s.images;s.bytes+=FileSize(folder+L"\\"+image.name+L".png");}else ++s.writeFail;
            image.result=result==Gdiplus::Ok?S_OK:E_FAIL;
        }
        FILE* json=_wfopen((folder+L"\\frame.json").c_str(),L"wb");if(json){const auto& c=f->scene;
            std::fprintf(json,"{\"version\":1,\"number\":%u,\"startTick\":%lu,\"endTick\":%u,\"frame\":%llu,\"epoch\":%llu,\"eye\":%u,\"tracked\":%s,\"applied\":%s,\"cinematic\":%s,\"scope\":%s,\"gameplay\":%s,\"beforeCapture\":%s,\"draws\":%u,\"droppedDraws\":%u,\"droppedEvents\":%llu,\"eventBytes\":%zu,\"images\":[",f->number,f->start,c.tick,static_cast<unsigned long long>(c.frame),static_cast<unsigned long long>(c.epoch),c.eye,c.tracked?"true":"false",c.applied?"true":"false",c.cinematic?"true":"false",c.scope?"true":"false",c.gameplay?"true":"false",f->beforeCapture?"true":"false",f->draws,f->omitted,static_cast<unsigned long long>(f->droppedEvents),f->events.size());
            for(unsigned i=0;i<f->images.size();++i){const auto& v=f->images[i];std::fprintf(json,"%s{\"name\":\"%ls.png\",\"width\":%u,\"height\":%u,\"result\":%ld}",i?",":"",v.name.c_str(),v.width,v.height,long(v.result));}
            std::fprintf(json,"],\"drawThread\":%lu}",f->drawThread);std::fclose(json);}else ++s.writeFail;
        ++s.written;if(s.bytes>=DiskBudget)s.budget=true;
    }}
}
}

namespace NativeVisualTraceKL {
void Configure(const wchar_t* folder,bool enabled,ContextCallback callback){
    if(!session){if(!enabled)return;auto* s=new Session;SYSTEMTIME t{};GetSystemTime(&t);wchar_t suffix[100];
        swprintf(suffix,100,L"visual-%04u%02u%02uT%02u%02u%02u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
        std::wstring logs=folder;logs+=L"logs";CreateDirectoryW(logs.c_str(),nullptr);s->directory=logs+L"\\"+suffix;
        if(!CreateDirectoryW(s->directory.c_str(),nullptr)){delete s;return;}s->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!s->event){delete s;return;}
        Schema(s->directory);session=s;HANDLE thread=CreateThread(nullptr,0,Writer,s,0,nullptr);if(!thread){++s->writeFail;s->budget=true;return;}s->writerReady=true;CloseHandle(thread);
    }
    session->context=callback;session->enabled=enabled&&session->writerReady;
}
bool Active(){return session&&session->active.load(std::memory_order_relaxed);}
bool Enabled(){return session&&session->enabled.load(std::memory_order_relaxed);}
void Evidence(unsigned tag,const void* data,std::size_t size,std::uint64_t sequence) try {if(!session)return;std::lock_guard<std::recursive_mutex> lock(session->dataLock);if(Active()&&session->frame)Append(*session->frame,tag,data,size,sequence);}catch(...){AllocationFailure();}
void Detail(std::uint64_t key){if(!session)return;std::lock_guard<std::recursive_mutex> lock(session->dataLock);if(Active()&&session->frame)session->frame->detailKey=key;}
void TraceDraw(IDirect3DDevice9* d,const D3D9Capture::DrawCall& call,bool after,HRESULT result) try {
    if(!Active()||insideTrace)return;std::lock_guard<std::recursive_mutex> lock(session->dataLock);
    if(!Active()||!session->frame)return;insideTrace=true;auto& f=*session->frame;
    const auto thread=GetCurrentThreadId();if(!f.drawThread)f.drawThread=thread;
    if(thread!=f.drawThread){if(!after){++f.omitted;++session->dropped;}insideTrace=false;return;}
    if(!after){f.drawOpen=false;f.detailKey=0;if(f.draws>=MaxDraws){++f.omitted;++session->dropped;insideTrace=false;return;}
        ++f.draws;++session->draws;State(d,f,call);f.drawOpen=true;const auto cb=session->context.load();if(cb)cb();
    }else if(f.drawOpen){DrawResult out;out.result=result;
        const bool preview=Policy::Preview(f.draws);const unsigned index=(f.draws-1)/Policy::PreviewStride;
        out.page=preview?index/(Columns*Rows):~0u;out.tile=preview?index%(Columns*Rows):~0u;
        const bool detail=f.detailKey&&f.details.size()<MaxDetails&&f.detailKeys.insert(f.detailKey).second;
        IDirect3DSurface9* source=nullptr;const auto got=(preview||detail)?d->GetRenderTarget(0,&source):E_PENDING;out.preview=got;
        if(!preview){++session->previewSkipped;out.preview=S_FALSE;}
        if(SUCCEEDED(got)&&source){
            if(preview&&out.page<Pages){while(f.atlas.size()<=out.page){Surface surface;wchar_t name[32];swprintf(name,32,L"atlas-%02u",unsigned(f.atlas.size()));
                const auto made=MakeSurface(d,surface,TileW*Columns,TileH*Rows,name);surface.copyResult=made;if(SUCCEEDED(made))surface.copyResult=d->ColorFill(surface.p,nullptr,0xff202020);f.atlas.push_back(std::move(surface));}
                const auto& atlas=f.atlas[out.page];RECT tile{LONG((out.tile%Columns)*TileW),LONG((out.tile/Columns)*TileH),LONG((out.tile%Columns+1)*TileW),LONG((out.tile/Columns+1)*TileH)};
                out.preview=atlas.p?Copy(d,f,source,atlas.p,&tile):E_OUTOFMEMORY;
            }else if(preview)out.preview=E_BOUNDS;
            if(detail){Surface surface;wchar_t name[32];swprintf(name,32,L"draw-%05u",f.draws);out.detailIndex=unsigned(f.details.size());
                out.detail=MakeSurface(d,surface,640,360,name);if(SUCCEEDED(out.detail))out.detail=Copy(d,f,source,surface.p,nullptr);surface.copyResult=out.detail;f.details.push_back(std::move(surface));}
            source->Release();
        }
        if(preview&&FAILED(out.preview))++session->copyFail;Append(f,2,&out,sizeof(out));f.drawOpen=false;
    }
    insideTrace=false;
}catch(...){AllocationFailure();}
void BeforeCapture(IDirect3DDevice9* d,IDirect3DSurface9* back,const Scene& scene) try {
    if(!Active()||insideTrace)return;std::lock_guard<std::recursive_mutex> lock(session->dataLock);
    if(!Active()||!session->frame)return;insideTrace=true;auto& f=*session->frame;f.scene=scene;f.beforeCapture=true;
    // Video may have begun after this frame was armed. Avoid all synchronous
    // readbacks and release its diagnostic resources before returning to VR.
    if(Protected(GetTickCount())){session->active=false;session->frame.reset();session->remaining=0;insideTrace=false;return;}
    D3DSURFACE_DESC desc{};if(back&&SUCCEEDED(back->GetDesc(&desc))&&desc.Width&&desc.Height){const float scale=std::min(1.f,2048.f/float(std::max(desc.Width,desc.Height)));
        auto hr=MakeSurface(d,f.final,std::max(1u,unsigned(desc.Width*scale)),std::max(1u,unsigned(desc.Height*scale)),L"final-native");if(SUCCEEDED(hr))hr=Copy(d,f,back,f.final.p,nullptr);f.final.copyResult=hr;if(FAILED(hr))++session->copyFail;}
    for(const auto& surface:f.atlas)ReadImage(d,f,surface);for(const auto& surface:f.details)ReadImage(d,f,surface);ReadImage(d,f,f.final);
    for(auto& surface:f.atlas)Release(surface.p);for(auto& surface:f.details)Release(surface.p);Release(f.final.p);Release(f.resolve);for(auto*& shader:f.shaderRefs)Release(shader);f.shaderRefs.clear();
    session->active=false;{std::lock_guard<std::mutex> lock(session->lock);if(session->queue.size()<2)session->queue.push_back(std::move(session->frame));else{++session->queueDrops;session->frame.reset();}}
    SetEvent(session->event);insideTrace=false;
}catch(...){AllocationFailure();}
void FrameBoundary(const Scene& completed) try {
    if(!session)return;std::lock_guard<std::recursive_mutex> data(session->dataLock);
    auto& s=*session;if(s.frame){s.active=false;s.frame.reset();++s.queueDrops;}
    const auto now=GetTickCount();if(!s.enabled||s.budget||s.frames>=MaxFrames)return;
    const bool acquired=completed.support&&!s.support;s.support=completed.support;
    if(!completed.gameplay||!completed.tracked||!completed.applied){s.remaining=0;return;}
    if(Protected(now)){s.remaining=0;return;}
    if(acquired&&(!s.lastArm||DWORD(now-s.lastArm)>=IntervalMs)){s.remaining=2;s.lastArm=now;}
    if(!s.remaining){if(s.lastArm&&DWORD(now-s.lastArm)<IntervalMs)return;s.remaining=2;s.lastArm=now;}
    if(s.inflight.load()>=1)return;
    s.frame=std::make_unique<Frame>();s.frame->inflight=&s.inflight;++s.inflight;
    s.frame->number=unsigned(++s.frames);s.frame->start=now;--s.remaining;s.active=true;
}catch(...){AllocationFailure();}
void Reset(){if(!session)return;std::lock_guard<std::recursive_mutex> data(session->dataLock);session->active=false;session->frame.reset();session->remaining=0;}
Stats Snapshot(){Stats out;if(!session)return out;const auto& s=*session;out.enabled=s.enabled;out.active=s.active;out.budgetReached=s.budget;
    out.frames=s.frames;out.written=s.written;out.draws=s.draws;out.droppedDraws=s.dropped;out.images=s.images;out.copyFailures=s.copyFail;out.readbackFailures=s.readFail;out.writeFailures=s.writeFail;out.queueDrops=s.queueDrops;out.bytes=s.bytes;
    out.recordingSuspended=s.recordingSuspended;out.memorySuspended=s.memorySuspended;out.previewSkipped=s.previewSkipped;
    wcsncpy(out.directory,s.directory.c_str(),MAX_PATH-1);return out;}
}
