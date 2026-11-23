#pragma once
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "OVR_CAPI.h"
#include "OVR_CAPI_D3D.h"
#include "recording_ipc.hpp"

namespace RecordingMirror {
// Newer runtime ABI, kept separate from the vendored LibOVR1.10 descriptor.
// Layout/flags match the current official SDK binding reference. Default is
// rectilinear and includes application layers; PostDistortion is NEVER set.
struct MirrorDesc {ovrTextureFormat Format;int Width,Height;unsigned MiscFlags,MirrorOptions;};
static_assert(sizeof(MirrorDesc)==20&&offsetof(MirrorDesc,MirrorOptions)==16,"MirrorWithOptions ABI");
constexpr unsigned LeftEyeOnly=2,RightEyeOnly=4,ForceSymmetricFov=0x40;
using CreateMirror=ovrResult(OVR_CDECL*)(ovrSession,IUnknown*,const MirrorDesc*,ovrMirrorTexture*);
using GetBuffer=decltype(&ovr_GetMirrorTextureBufferDX);
using DestroyMirror=decltype(&ovr_DestroyMirrorTexture);
template<class T>inline void Release(T*& object){if(object){object->Release();object=nullptr;}}
struct Api {
    CreateMirror create=nullptr;GetBuffer buffer=nullptr;DestroyMirror destroy=nullptr;
    void Resolve(HMODULE runtime){
        create=reinterpret_cast<CreateMirror>(GetProcAddress(runtime,"ovr_CreateMirrorTextureWithOptionsDX"));
        buffer=reinterpret_cast<GetBuffer>(GetProcAddress(runtime,"ovr_GetMirrorTextureBufferDX"));
        destroy=reinterpret_cast<DestroyMirror>(GetProcAddress(runtime,"ovr_DestroyMirrorTexture"));
    }
    bool Complete()const{return create&&buffer&&destroy;}
};
class Capture {
public:
    explicit Capture(DWORD gamePid):gamePid_(gamePid){}
    ~Capture(){Close();}
    Capture(const Capture&)=delete;Capture& operator=(const Capture&)=delete;
    // Called after a successful native submit. Polling while absent is limited
    // to one OpenFileMapping per500ms. All failures affect recording only.
    void Tick(HMODULE runtime,ovrSession session,ID3D11Device* device,ID3D11DeviceContext* context,
              DWORD now,unsigned sourceKind,std::uint64_t submitId,bool submitted){
        Poll(now);
        if(!shared_)return;
        if(!RecordingIPC::Compatible(shared_,gamePid_)){Close();return;}
        RecordingIPC::Request next{};
        if(RecordingIPC::ReadRequest(shared_,next))std::memcpy(&request_,&next,sizeof(next));
        if(!RecordingIPC::Valid(request_,now)){PublishState(now,sourceKind,0);Close();return;}
        if(sourceKind!=observedKind_){
            // A pre-menu readback must never reappear as the next mission's
            // first scene frame. GPU copies can finish, but discard their IDs.
            for(auto& slot:slots_)slot.pending=false;
            cadence_.Reset();observedKind_=sourceKind;
        }
        if(!apiResolved_){api_.Resolve(runtime);apiResolved_=true;}
        if(!api_.Complete()){PublishState(now,sourceKind,static_cast<int>(RecordingIPC::Error::ExportsMissing));return;}
        if(sourceKind<1||sourceKind>4){PublishState(now,sourceKind,0);return;}
        if(!submitted||!device||!context||!session)return;
        if(mirror_&&(request_.eye!=eye_||request_.width!=width_||request_.height!=height_))CloseGraphics();
        if(!mirror_){
            if(!RecordingIPC::Due(now,lastAttempt_,1))return;
            lastAttempt_=now;
            if(!OpenGraphics(session,device,now))return;
        }
        // OpenXR mirrors are composed on the next submit after creation.
        // Keep that mirror alive while not ready, and recheck subsequent
        // composition failures before copying any stale/partial texture.
        if(!OpenBuffer(device,now))return;
        // Nonblocking readback of an earlier copy never stalls VR waiting for
        // the GPU. If all staging slots remain busy, drop a recording frame.
        for(unsigned attempt=0;attempt<3;++attempt){
            Slot* oldest=nullptr;for(auto& entry:slots_)if(entry.pending&&(!oldest||entry.submitId<oldest->submitId))oldest=&entry;
            if(!oldest)break;auto& slot=*oldest;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT result=context->Map(slot.texture,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(result==DXGI_ERROR_WAS_STILL_DRAWING)break;
            slot.pending=false;
            if(FAILED(result)){PublishState(now,observedKind_,static_cast<int>(RecordingIPC::Error::GraphicsFailed));continue;}
            PublishPixels(mapped,now,sourceKind,slot.submitId);context->Unmap(slot.texture,0);
        }
        if(!cadence_.Due(now,request_.fps))return;
        Slot* free=nullptr;for(auto& slot:slots_)if(!slot.pending){free=&slot;break;}
        if(!free)return;
        free->submitId=submitId;context->CopyResource(free->texture,texture_);free->pending=true;
    }
    void Close(){CloseGraphics();if(shared_){UnmapViewOfFile(shared_);shared_=nullptr;}
        if(mapping_){CloseHandle(mapping_);mapping_=nullptr;}request_={};}
private:
    struct Slot {ID3D11Texture2D* texture=nullptr;bool pending=false;std::uint64_t submitId=0;};
    DWORD gamePid_=0,nextPoll_=0,lastAttempt_=0;RecordingIPC::Cadence cadence_;
    HANDLE mapping_=nullptr;RecordingIPC::Shared* shared_=nullptr;RecordingIPC::Request request_{};
    Api api_{};bool apiResolved_=false;ovrSession session_=nullptr;ovrMirrorTexture mirror_=nullptr;
    ID3D11Texture2D* texture_=nullptr;Slot slots_[3]{};
    unsigned eye_=0,width_=0,height_=0;std::uint64_t frameId_=0;
    unsigned publishedKind_=~0u,observedKind_=~0u;int publishedError_=1;DWORD lastStateTick_=0;
    void Poll(DWORD now){
        if(shared_||(nextPoll_&&static_cast<LONG>(now-nextPoll_)<0))return;
        nextPoll_=now+500;wchar_t name[100];RecordingIPC::MappingName(name,100,gamePid_);
        mapping_=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!mapping_)return;
        shared_=static_cast<RecordingIPC::Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecordingIPC::Shared)));
        if(!RecordingIPC::Compatible(shared_,gamePid_)){Close();return;}
        publishedKind_=~0u;publishedError_=1;
        InterlockedExchange(&shared_->response.sequence,0);PublishState(now,0,0);
    }
    void CloseGraphics(){
        for(auto& slot:slots_){Release(slot.texture);slot={};}Release(texture_);
        if(mirror_&&api_.destroy&&session_)api_.destroy(session_,mirror_);
        mirror_=nullptr;session_=nullptr;eye_=width_=height_=0;cadence_.Reset();lastAttempt_=0;
    }
    bool OpenGraphics(ovrSession session,ID3D11Device* device,DWORD now){
        session_=session;eye_=request_.eye;width_=request_.width;height_=request_.height;
        MirrorDesc desc{OVR_FORMAT_B8G8R8A8_UNORM_SRGB,static_cast<int>(width_),static_cast<int>(height_),0,
            (eye_?RightEyeOnly:LeftEyeOnly)|ForceSymmetricFov};
        ovrResult result=api_.create(session_,device,&desc,&mirror_);
        if(OVR_FAILURE(result)||!mirror_){CloseGraphics();lastAttempt_=now;PublishState(now,observedKind_,result<0?result:static_cast<int>(RecordingIPC::Error::MirrorFailed));return false;}
        return true;
    }
    bool OpenBuffer(ID3D11Device* device,DWORD now){
        ID3D11Texture2D* current=nullptr;
        const auto result=api_.buffer(session_,mirror_,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&current));
        if(OVR_FAILURE(result)||!current){Release(current);for(auto& slot:slots_)slot.pending=false;
            PublishState(now,observedKind_,static_cast<int>(RecordingIPC::Error::GraphicsFailed));return false;}
        if(texture_){Release(current);return true;}
        texture_=current;
        D3D11_TEXTURE2D_DESC native{};texture_->GetDesc(&native);
        if(native.Width!=width_||native.Height!=height_||native.SampleDesc.Count!=1||
           (native.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&native.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB&&native.Format!=DXGI_FORMAT_B8G8R8A8_TYPELESS)){
            CloseGraphics();lastAttempt_=now;PublishState(now,observedKind_,static_cast<int>(RecordingIPC::Error::GraphicsFailed));return false;}
        native.Usage=D3D11_USAGE_STAGING;native.BindFlags=0;native.CPUAccessFlags=D3D11_CPU_ACCESS_READ;native.MiscFlags=0;
        for(auto& slot:slots_)if(FAILED(device->CreateTexture2D(&native,nullptr,&slot.texture))){
            CloseGraphics();lastAttempt_=now;PublishState(now,observedKind_,static_cast<int>(RecordingIPC::Error::GraphicsFailed));return false;}
        publishedKind_=~0u;return true;
    }
    void Begin(){InterlockedIncrement(&shared_->response.sequence);MemoryBarrier();}
    void End(){MemoryBarrier();InterlockedIncrement(&shared_->response.sequence);}
    void PublishState(DWORD now,unsigned kind,int error){
        if(kind==publishedKind_&&error==publishedError_&&now-lastStateTick_<500)return;
        Begin();auto& response=shared_->response;response.hostPid=GetCurrentProcessId();response.tick=now;
        response.error=error;response.sourceKind=kind;response.width=response.height=response.stride=0;
        response.frameId=frameId_;response.submitId=0;std::memset(response.fov,0,sizeof(response.fov));End();
        publishedKind_=kind;publishedError_=error;lastStateTick_=now;
    }
    void PublishPixels(const D3D11_MAPPED_SUBRESOURCE& mapped,DWORD now,unsigned kind,std::uint64_t submitId){
        if(!mapped.pData||mapped.RowPitch<width_*4)return;
        Begin();auto& response=shared_->response;response.hostPid=GetCurrentProcessId();response.tick=now;
        response.error=0;response.sourceKind=kind;response.width=width_;response.height=height_;response.stride=width_*4;
        response.frameId=++frameId_;response.submitId=submitId;std::memset(response.fov,0,sizeof(response.fov));
        for(unsigned y=0;y<height_;++y)std::memcpy(shared_->pixels+std::size_t(y)*width_*4,
            static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,width_*4);
        End();publishedKind_=kind;publishedError_=0;lastStateTick_=now;
    }
};
}
