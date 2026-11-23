// Real D3D11 software staging/readback and production recorder IPC. The three
// runtime mirror exports are private callbacks; no game, HMD or microphone.
#define WIN32_LEAN_AND_MEAN
#define OVR_CAPI_NO_UTILS
#include <windows.h>
void* RecorderTestExport(HMODULE,const char*);
#define GetProcAddress RecorderTestExport
#include "recording_mirror.hpp"
#undef GetProcAddress
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0,created=0,destroyed=0;
bool bufferReady=false;
ID3D11Texture2D* runtimeTexture=nullptr;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
ovrResult OVR_CDECL Create(ovrSession,IUnknown* device,const RecordingMirror::MirrorDesc* desc,ovrMirrorTexture* output){
    Check(desc&&desc->Width==1920&&desc->Height==1080&&desc->MirrorOptions==0x42,"request is one rectilinear left view at fixed video aspect");
    ID3D11Device* native=nullptr;Check(SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device),reinterpret_cast<void**>(&native))),"actual software D3D11 device accepted");
    D3D11_TEXTURE2D_DESC texture{};texture.Width=1920;texture.Height=1080;texture.MipLevels=texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
    const HRESULT result=native->CreateTexture2D(&texture,nullptr,&runtimeTexture);native->Release();
    if(FAILED(result))return ovrError_MemoryAllocationFailure;
    ++created;*output=reinterpret_cast<ovrMirrorTexture>(runtimeTexture);return ovrSuccess;
}
ovrResult OVR_CDECL Buffer(ovrSession,ovrMirrorTexture mirror,IID iid,void** output){
    if(!bufferReady){*output=nullptr;return ovrError_InvalidOperation;}
    return SUCCEEDED(reinterpret_cast<ID3D11Texture2D*>(mirror)->QueryInterface(iid,output))?ovrSuccess:ovrError_InvalidParameter;
}
void OVR_CDECL Destroy(ovrSession,ovrMirrorTexture mirror){reinterpret_cast<ID3D11Texture2D*>(mirror)->Release();runtimeTexture=nullptr;++destroyed;}
void Demand(RecordingIPC::Shared& shared,DWORD now){
    InterlockedIncrement(&shared.request.sequence);MemoryBarrier();
    shared.request.heartbeatTick=now;shared.request.recorderPid=GetCurrentProcessId();shared.request.eye=0;
    shared.request.width=1920;shared.request.height=1080;shared.request.fps=60;shared.request.flags=0;
    MemoryBarrier();InterlockedIncrement(&shared.request.sequence);
}
}
void* RecorderTestExport(HMODULE,const char* name){
    if(!std::strcmp(name,"ovr_CreateMirrorTextureWithOptionsDX"))return reinterpret_cast<void*>(&Create);
    if(!std::strcmp(name,"ovr_GetMirrorTextureBufferDX"))return reinterpret_cast<void*>(&Buffer);
    if(!std::strcmp(name,"ovr_DestroyMirrorTexture"))return reinterpret_cast<void*>(&Destroy);
    return nullptr;
}
int main(){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"software D3D11 device created without a headset");
    wchar_t name[100];const auto pid=GetCurrentProcessId();RecordingIPC::MappingName(name,100,pid);
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RecordingIPC::Shared),name);
    Check(mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"test owns its PID-scoped recorder request");
    auto* shared=static_cast<RecordingIPC::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecordingIPC::Shared)));
    Check(shared!=nullptr,"production wire mapped");std::memset(shared,0,sizeof(*shared));
    shared->magic=RecordingIPC::Magic;shared->version=RecordingIPC::Version;shared->bytes=sizeof(*shared);shared->gamePid=pid;
    const auto session=reinterpret_cast<ovrSession>(std::uintptr_t(1));DWORD now=100;std::uint64_t submit=1,previousFrame=0;
    std::vector<std::uint32_t> pixels(1920*1080);
    {
        RecordingMirror::Capture capture(pid);Demand(*shared,now);
        capture.Tick(nullptr,session,device,context,now,1,submit++,true);
        Check(runtimeTexture&&created==1,"mirror allocated only on active recording demand");
        Check(shared->response.error==static_cast<int>(RecordingIPC::Error::GraphicsFailed)&&destroyed==0,
            "not-yet-composed mirror stays alive for the next submit and reports recording-only unavailability");
        bufferReady=true;
        // Scene, native menu, loading screen and scope all use the same
        // recording request and mirror. No close/reopen or video segmentation.
        for(unsigned kind:{1u,2u,4u,3u,1u}){
            const auto colour=0xFF102030u+kind*0x00030303u;std::fill(pixels.begin(),pixels.end(),colour);
            context->UpdateSubresource(runtimeTexture,0,nullptr,pixels.data(),1920*4,0);context->Flush();
            bool found=false;
            for(unsigned retry=0;retry<100&&!found;++retry){now+=20;Demand(*shared,now);
                capture.Tick(nullptr,session,device,context,now,kind,submit++,true);context->Flush();
                if(shared->response.frameId>previousFrame&&shared->response.sourceKind==kind&&shared->response.width==1920){
                    std::uint32_t first=0,last=0;std::memcpy(&first,shared->pixels,4);std::memcpy(&last,shared->pixels+1920*1080*4-4,4);
                    found=first==colour&&last==colour;
                }
                if(!found)Sleep(2);
            }
            Check(found,"real staging copy preserves full pixels through scene/menu/loading/scope changes");
            Check(!shared->response.error&&shared->response.height==1080&&shared->response.stride==1920*4,"response remains one fixed complete video frame");
            Check(!(shared->response.sequence&1)&&created==1&&destroyed==0,"each transition preserves the same mirror and completed seqlock");
            previousFrame=shared->response.frameId;
        }
        bufferReady=false;now+=20;Demand(*shared,now);capture.Tick(nullptr,session,device,context,now,1,submit++,true);
        Check(shared->response.error==static_cast<int>(RecordingIPC::Error::GraphicsFailed)&&shared->response.width==0&&
            shared->response.frameId==previousFrame&&created==1&&destroyed==0,
            "later mirror failure cannot publish stale pixels as a fresh successful frame or destroy a recoverable mirror");
        bufferReady=true;bool recovered=false;
        for(unsigned retry=0;retry<100&&!recovered;++retry){now+=20;Demand(*shared,now);
            capture.Tick(nullptr,session,device,context,now,1,submit++,true);context->Flush();
            recovered=!shared->response.error&&shared->response.frameId>previousFrame&&shared->response.width==1920;
            if(!recovered)Sleep(2);
        }
        Check(recovered&&created==1&&destroyed==0,"same recorder mirror resumes complete pixels after composition recovery");
        now+=20;Demand(*shared,now);capture.Tick(nullptr,session,device,context,now,0,submit,false);
        Check(shared->response.sourceKind==0&&shared->response.width==0&&created==1&&destroyed==0,"no-image interval keeps request/mirror alive for recorder timeline hold");
        now+=20;Demand(*shared,now);capture.Tick(nullptr,session,device,context,now,2,submit++,true);
        Check(created==1,"menu recovery reuses the existing recording session");
    }
    Check(destroyed==1&&!runtimeTexture,"recorder teardown releases its mirror once");
    UnmapViewOfFile(shared);CloseHandle(mapping);context->Release();device->Release();
    std::printf("KL1 recording mirror software readback: %u checks PASS; private WARP/IPC only.\n",checks);return 0;
}
