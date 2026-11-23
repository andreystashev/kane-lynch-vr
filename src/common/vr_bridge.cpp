#include "profile_timing.hpp"
#include "background_diagnostic.hpp"
#include "hands_ipc.hpp"
#include "native_weapon_kl.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "vr_bridge.hpp"
#include "frame_copy.hpp"
#include "cpu_frame_ipc.hpp"
#include "gpu_frame_producer.hpp"
#include "gpu_shared9_producer.hpp"
#include "d3d9_normalization.hpp"
#include "kl1_launch_connection.hpp"
#include "vr_host_selection.hpp"
#include "kl1_startup_status.hpp"
#include "diagnostic_session.hpp"
#include "team_command_ipc.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace VRBridge {
namespace {
std::wstring packageDirectory;
Kl1LaunchConnection::Plan launchedPlan;
std::atomic<bool> autoFallbackUsed{false};
LogCallback logCallback=nullptr;
std::atomic<bool> requested{false},hostRunning{false};
std::atomic<LONG> bridgeError{0};
std::atomic<VRIPC::SharedFrames*> mapping{nullptr};
HANDLE mapHandle=nullptr,hostProcess=nullptr;
HANDLE teamMapHandle=nullptr;
std::atomic<TeamCommandIPC::Shared*> teamMapping{nullptr};
SRWLOCK teamPublishLock=SRWLOCK_INIT;
HANDLE vehicleMapHandle=nullptr;
std::atomic<VehicleControlIPC::Shared*> vehicleMapping{nullptr};
SRWLOCK vehiclePublishLock=SRWLOCK_INIT;
CPUFrame::Writer cpuPixels;
SRWLOCK publishLock=SRWLOCK_INIT;
bool closing=false;
std::atomic<bool> gpuEnabled{true};
std::atomic<bool> dxvkGPUEnabled{false};
bool shared9Enabled=false,shared9EarlyPoll=false;
std::unique_ptr<SharedD3D9::Producer> shared9;
std::atomic<DWORD> shared9Thread{0};
std::atomic<bool> shared9Pending{false};
bool publishingShared9=false; // protected with the GPU and publication locks
DWORD shared9Admission=0;
std::atomic<GPUFrame::Producer*> gpu{nullptr};
SRWLOCK gpuLock=SRWLOCK_INIT;
unsigned transport=VRIPC::kCPU,transportEpoch=1;
IDirect3DSurface9* gpuResolve=nullptr;
IDirect3DSurface9* gpuNativeResolve=nullptr;
IDirect3DTexture9* gpuResolveTexture=nullptr;
IDirect3DDevice9* gpuResolveDevice=nullptr;
D3DFORMAT gpuResolveSourceFormat=D3DFMT_UNKNOWN;
bool gpuResolveNeedsFlush=false;
bool RejectUnsupportedNormalization(GPUFrame::Producer& producer,IDirect3DDevice9* source) {
    if(producer.active||!producer.retired.empty())return false;
    // Confirm that both interop interfaces are absent before allocating any
    // full-size render targets for a new transport. Other failures still go
    // through the producer's normal capability and recovery checks.
    GPUFrame::Object<Device9On12> on12;
    if(source->QueryInterface(iid9on12,reinterpret_cast<void**>(on12.Put()))!=E_NOINTERFACE)
        return false;
    GPUFrame::Object<DXVKGPU::Device> dxvk;
    if(source->QueryInterface(DXVKGPU::DeviceIID,reinterpret_cast<void**>(dxvk.Put()))!=E_NOINTERFACE)
        return false;
    const auto host=GPUFrame::Atomic(producer.shared->consumerPid);
    if(host!=producer.host){producer.Invalidate();producer.host=host;}
    wchar_t optIn[4]{};
    const bool dxvkRequested=producer.dxvkEnabled||
        (GetEnvironmentVariableW(L"KL1_DXVK_GPU_TRANSPORT",optIn,4)==1&&optIn[0]==L'1');
    producer.createResult=E_NOINTERFACE;
    producer.reason=dxvkRequested?"dxvk_not_initialized":"source_is_not_d3d9on12";
    producer.rejected=source;
    producer.rejectedAt=GetTickCount();
    producer.creationRetryable=false;
    return true;
}
void SetTransport(unsigned kind){if(transport!=kind){transport=kind;++transportEpoch;}}
void Log(const char* text) {if(logCallback)logCallback(text);}
LONG Read(volatile LONG& value) {return InterlockedCompareExchange(&value,0,0);}
void BackgroundHeadPose(float orientation[4],float position[3],DWORD now) {
    orientation[3]=1.f;
    position[1]=1.7f;
    static const auto path=[](){
        wchar_t enabled[4]{},run[MAX_PATH]{};
        if(!GetEnvironmentVariableW(L"KL1_BACKGROUND_DRIVEN",enabled,4)||enabled[0]!=L'1')return std::wstring{};
        const auto length=GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",run,MAX_PATH);
        return length&&length<MAX_PATH?std::wstring(run)+L"\\drive-pose.txt":std::wstring{};
    }();
    if(path.empty())return;
    static unsigned long long lastSequence=0;
    static DWORD lastPoll=0;
    static float q[4]{0.f,0.f,0.f,1.f},offset[3]{};
    if(now-lastPoll>=50) {
        lastPoll=now;
        if(FILE* file=_wfopen(path.c_str(),L"rb")) {
            unsigned long long sequence=0;
            int x=0,y=0,z=0,yaw=0,pitch=0,roll=0;
            char extra=0;
            if(std::fscanf(file,"%llu %d %d %d %d %d %d %c",&sequence,&x,&y,&z,&yaw,&pitch,&roll,&extra)==7&&
               sequence>lastSequence&&x>=-350&&x<=350&&y>=-350&&y<=350&&z>=-350&&z<=350&&
               yaw>=-180&&yaw<=180&&pitch>=-80&&pitch<=80&&roll>=-45&&roll<=45) {
                constexpr float halfDegree=0.00872664626f;
                const float a=yaw*halfDegree,b=pitch*halfDegree,c=roll*halfDegree;
                const float sa=std::sin(a),ca=std::cos(a),sb=std::sin(b),cb=std::cos(b),sc=std::sin(c),cc=std::cos(c);
                q[0]=ca*sb*cc+sa*cb*sc;
                q[1]=sa*cb*cc-ca*sb*sc;
                q[2]=ca*cb*sc-sa*sb*cc;
                q[3]=ca*cb*cc+sa*sb*sc;
                offset[0]=x*.001f;offset[1]=y*.001f;offset[2]=z*.001f;
                lastSequence=sequence;
                char line[168]{};
                std::snprintf(line,sizeof(line),"Private head pose sequence=%llu positionMm=%d,%d,%d yawPitchRoll=%d,%d,%d.",
                    sequence,x,y,z,yaw,pitch,roll);
                Log(line);
            }
            std::fclose(file);
        }
    }
    std::memcpy(orientation,q,sizeof(q));
    position[0]=offset[0];position[1]+=offset[1];position[2]=offset[2];
}
void OpenTeamMapping() {
    if(teamMapHandle||teamMapping.load())return;
    wchar_t name[100];TeamCommandIPC::MappingName(name,100,GetCurrentProcessId());
    auto handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(TeamCommandIPC::Shared),name);
    if(!handle){Log("VR team command mailbox unavailable; squad commands disabled.");return;}
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(handle);Log("VR team command mailbox already exists; refusing a stale command session.");return;}
    auto* shared=static_cast<TeamCommandIPC::Shared*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(TeamCommandIPC::Shared)));
    if(!shared){CloseHandle(handle);Log("VR team command mailbox mapping failed; squad commands disabled.");return;}
    std::memset(shared,0,sizeof(*shared));shared->magic=TeamCommandIPC::Magic;shared->version=TeamCommandIPC::Version;
    shared->bytes=sizeof(*shared);shared->gamePid=GetCurrentProcessId();
    teamMapHandle=handle;teamMapping=shared;
}
void OpenVehicleMapping(){
    if(vehicleMapHandle||vehicleMapping.load())return;
    wchar_t name[100];VehicleControlIPC::MappingName(name,100,GetCurrentProcessId());
    const auto handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(VehicleControlIPC::Shared),name);
    if(!handle)return;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(handle);return;}
    auto* shared=static_cast<VehicleControlIPC::Shared*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(VehicleControlIPC::Shared)));
    if(!shared){CloseHandle(handle);return;}std::memset(shared,0,sizeof(*shared));
    shared->magic=VehicleControlIPC::Magic;shared->version=VehicleControlIPC::Version;shared->bytes=sizeof(*shared);shared->gamePid=GetCurrentProcessId();
    vehicleMapHandle=handle;vehicleMapping=shared;
}
bool OpenMapping() {
    if(mapping.load())return true;
    wchar_t name[100];VRIPC::MappingName(name,100,GetCurrentProcessId());
    mapHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(VRIPC::SharedFrames),name);
    if(!mapHandle){bridgeError=static_cast<LONG>(GetLastError());Log("VR IPC allocation failed.");return false;}
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        CloseHandle(mapHandle);mapHandle=nullptr;bridgeError=ERROR_ALREADY_EXISTS;
        Log("VR IPC name already exists; refusing to overwrite another host session.");return false;
    }
    auto* frames=static_cast<VRIPC::SharedFrames*>(MapViewOfFile(mapHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(VRIPC::SharedFrames)));
    if(!frames) {
        bridgeError=static_cast<LONG>(GetLastError());CloseHandle(mapHandle);mapHandle=nullptr;
        Log("VR IPC mapping failed.");return false;
    }
    std::memset(frames,0,sizeof(*frames));
    frames->magic=VRIPC::kMagic;frames->version=VRIPC::kVersion;
    frames->bytes=sizeof(*frames);frames->gamePid=GetCurrentProcessId();
    mapping=frames;OpenTeamMapping();OpenVehicleMapping();return true;
}
void RetireCPUFramesLocked() {
    // Reset and host shutdown both invalidate any incomplete pair. A reader
    // can keep its old section, so the next writer needs a new epoch.
    transport=VRIPC::kCPU;
    if(!++transportEpoch)++transportEpoch;
    if(auto* frames=mapping.load())for(auto& frame:frames->eyes){
        InterlockedIncrement(&frame.sequence);frame.transport=VRIPC::kCPU;frame.transportEpoch=transportEpoch;
        frame.gpuSlot=0;frame.metadata={};MemoryBarrier();InterlockedIncrement(&frame.sequence);
    }
    cpuPixels.Close();
}
void StopCPUFrames() {
    // Keep the GPU -> publish lock order. A GPU submission may still be
    // reading the current transport epoch before it takes the publish lock.
    AcquireSRWLockExclusive(&gpuLock);
    AcquireSRWLockExclusive(&publishLock);
    if(cpuPixels.shared)RetireCPUFramesLocked();
    ReleaseSRWLockExclusive(&publishLock);
    ReleaseSRWLockExclusive(&gpuLock);
}
bool StartupFailed(LONG code,const char* message){
    if(!code)code=ERROR_GEN_FAILURE;
    if(!mapping.load()&&!OpenMapping())code=bridgeError.load();
    bridgeError=code;
    if(auto* frames=mapping.load()){
        InterlockedExchange(&frames->hostReady,0);InterlockedExchange(&frames->captureEnabled,0);
        InterlockedExchange(&frames->shutdown,0);InterlockedExchange(&frames->hostError,code);
        StopCPUFrames();
    }
    Log(message);return false;
}
void OpenGpuPublishers() {
    if(!gpuEnabled.load())return;
    AcquireSRWLockExclusive(&gpuLock);
    if(!gpu.load()){
        auto* producer=new GPUFrame::Producer;producer->dxvkEnabled=dxvkGPUEnabled;
        producer->Open(GetCurrentProcessId());gpu=producer;
    }
    if(shared9Enabled&&!shared9){shared9=std::make_unique<SharedD3D9::Producer>();shared9->Open();}
    ReleaseSRWLockExclusive(&gpuLock);
}
bool Start(bool fallback=false) {
    if(BackgroundDiagnostic::Active()){
        if(!OpenMapping())return false;
        wchar_t diagnosticGPU[4]{};
        if(GetEnvironmentVariableW(L"KL1_BACKGROUND_GPU_SINK",diagnosticGPU,4)&&diagnosticGPU[0]==L'1')OpenGpuPublishers();
        auto* frames=mapping.load();frames->captureEnabled=1;frames->hostReady=1;frames->hostVisible=1;frames->hostHmdPresent=1;
        for(unsigned eye=0;eye<2;++eye){frames->eyeOffset[eye][0]=eye?.032f:-.032f;for(float& value:frames->eyeFov[eye])value=1.f;frames->recommendedWidth[eye]=640;frames->recommendedHeight[eye]=480;}
        Log("PRIVATE BACKGROUND DIAGNOSTIC: synthetic tracking, no real VR host.");return true;
    }
    Kl1LaunchConnection::Plan plan;std::wstring error;
    const bool prepared=fallback?Kl1LaunchConnection::Prepare(packageDirectory,VrModConnection::Mode::Auto,launchedPlan.customRuntime,false,plan,error):
        Kl1LaunchConnection::Prepare(packageDirectory,plan,error);
    if(!prepared){const auto text=MetaRuntime::Utf8(error);return StartupFailed(Kl1StartupStatus::ProfileUnavailable,text.c_str());}
    const auto backend=VrModConnection::IsOpenXR(plan.connection.mode)?VRHostSelection::Backend::OpenXR:VRHostSelection::Backend::Meta;
    std::vector<wchar_t> environment;
    if(!VRHostSelection::ChildEnvironment(backend,plan.connection.runtimeJson,environment)){
        return StartupFailed(Kl1StartupStatus::ChildEnvironmentFailed,"Cannot prepare the selected compositor's private runtime environment.");
    }
    if(!OpenMapping())return false;
    OpenGpuPublishers();
    auto* frames=mapping.load();
    AcquireSRWLockExclusive(&publishLock);
    InterlockedExchange(&frames->hostReady,0);InterlockedExchange(&frames->shutdown,0);
    InterlockedExchange(&frames->captureEnabled,1);InterlockedExchange(&frames->hostError,0);
    InterlockedExchange(&frames->hostSubmittedFrames,0);
    InterlockedExchange(&frames->menuCommandSequence,0);InterlockedExchange(&frames->menuCommandAck,0);
    InterlockedExchange(&frames->menuCommand,0);InterlockedExchange(&frames->menuCommandTick,0);
    InterlockedExchange(&frames->hudSequence,0);InterlockedExchange(&frames->hudTick,0);InterlockedExchange(&frames->hudFlags,0);
    InterlockedExchange(&frames->eyes[0].sequence,0);InterlockedExchange(&frames->eyes[1].sequence,0);
    ReleaseSRWLockExclusive(&publishLock);
    const auto text=L"\""+plan.host+L"\" --pid "+std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> command(text.begin(),text.end());command.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(plan.host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
        environment.data(),packageDirectory.c_str(),&startup,&process)) {
        const auto failure=static_cast<LONG>(GetLastError());
        return StartupFailed(failure,"Cannot start the selected x64 VR host; keep the complete package beside KaneLynchVR.dll.");
    }
    CloseHandle(process.hThread);hostProcess=process.hProcess;hostRunning=true;closing=false;bridgeError=0;launchedPlan=std::move(plan);
    Log("Kane & Lynch VR host started; awaiting D3D11 swapchains and headset focus.");return true;
}
}
void Initialize(const wchar_t* folder,LogCallback callback) {
    logCallback=callback;packageDirectory=folder?folder:L"";
    const auto ini=packageDirectory+L"\\vr.ini";
    dxvkGPUEnabled=GetPrivateProfileIntW(L"VR",L"DXVKGPUTransport",0,ini.c_str())!=0;
    shared9Enabled=GetPrivateProfileIntW(L"VR",L"GPUSharedD3D9",0,ini.c_str())!=0;
    shared9EarlyPoll=shared9Enabled&&GetPrivateProfileIntW(L"VR",L"GPUSharedD3D9EarlyPoll",1,ini.c_str())!=0;
}
bool Requested() {return requested.load();}
void Request(bool enable) {if(enable&&!requested.exchange(true))autoFallbackUsed=false;else requested=enable;}
void Tick() {
    NativeWeaponKL::ServiceReloadFeedback();
    if(BackgroundDiagnostic::Active()){
        if(!requested.load())return;
        // Direct scene entry first finishes its authored startup on the native
        // camera. Do not open synthetic VR while SkipCutSeq retires that camera.
        // A delayed controller exercise must not postpone scene admission.
        if(BackgroundDiagnostic::GameAge()<25000)return;
        if(!mapping.load()&&!Start())return;
        auto* frames=mapping.load();const auto now=GetTickCount();
        HandsIPC::Frame sample{};
        BackgroundHeadPose(sample.head.orientation,sample.head.position,now);
        InterlockedIncrement(&frames->trackingSequence);
        std::memcpy(frames->trackingOrientation,sample.head.orientation,sizeof(sample.head.orientation));
        std::memcpy(frames->trackingPosition,sample.head.position,sizeof(sample.head.position));
        frames->trackingTick=now;frames->trackingSensorTime=double(GetTickCount64())/1000.;frames->trackingMounted=1;
        MemoryBarrier();InterlockedIncrement(&frames->trackingSequence);frames->hostHeartbeatTick=now;frames->hostReady=1;
        static HandsIPC::Producer hands;static bool opened=false;if(!opened)opened=hands.Open(GetCurrentProcessId());
        sample.tick=now;sample.present=sample.mounted=1;sample.sensorTime=frames->trackingSensorTime;sample.head.flags=3;
        for(unsigned side=0;side<2;++side){sample.hands[side].orientation[3]=1;sample.hands[side].position[0]=side?.2f:-.2f;sample.hands[side].position[1]=1.4f;sample.hands[side].position[2]=-.4f;sample.hands[side].flags=3;sample.inputValid[side]=3;}
        // Give the second native weapon switch time to finish its equip
        // animation and publish the exact new rendered muzzle before firing.
        const auto age=BackgroundDiagnostic::Age();
        const bool rackReplay=BackgroundDiagnostic::Controls()&&BackgroundDiagnostic::Rack();
        const bool scopedReplay=BackgroundDiagnostic::ScopedReload();
        const bool secondFire=rackReplay?(age>=40000&&age<41900):(age>=36000&&age<37500);
        if(BackgroundDiagnostic::Exercise()&&((age>=29000&&age<30500)||(BackgroundDiagnostic::Rack()&&age>=32100&&age<32300)||secondFire)){
            NativeWeaponKL::AimFrame aim;if(!BackgroundDiagnostic::Controls()||
                (scopedReplay&&NativeWeaponKL::ReadScope().active)||
                (NativeWeaponKL::SnapshotAim(aim)&&now-aim.tick<=50))sample.trigger[1]=1;
        }
        if(scopedReplay&&age>=29000&&age<31500){
            // A scripted physical pull stays down after aim qualification.
            // Exercise one brief neutral report after the last shot, then a
            // deliberate empty press with an uninterrupted release between.
            static bool firstPull=false;
            static DWORD spent=0;
            if(age<30500){
                firstPull=firstPull||sample.trigger[1]>.55f;
                if(firstPull&&!spent){
                    const auto weapon=NativeWeaponKL::Snapshot();
                    if(weapon.reloadAmmoCategory==15&&weapon.reloadAmmoCount==0)spent=age;
                }
                const bool dip=spent&&age-spent>=110&&age-spent<135;
                sample.trigger[1]=firstPull&&!dip?1.f:0.f;
            }else if(age>=31200)sample.trigger[1]=1.f;
        }
        // Two distinct dry presses with a neutral interval, before the first rack.
        if(rackReplay&&((age>=33050&&age<33320)||(age>=33550&&age<33880)))sample.trigger[1]=1;
        if(scopedReplay&&age>=30800){
            sample.hands[1].position[0]=sample.head.position[0]+.09f;
            sample.hands[1].position[1]=sample.head.position[1]-.075f;
            sample.hands[1].position[2]=sample.head.position[2]-.19f;
        }
        if(BackgroundDiagnostic::Controls()&&age>=25000){
            static DWORD reload=0;
            if(age>=32000&&!reload&&((!BackgroundDiagnostic::Driven()&&!BackgroundDiagnostic::Rack()&&!BackgroundDiagnostic::ScopedReload())||
                (age<46000&&NativeWeaponKL::BackgroundReloadIntentReady())))reload=now;
            VRIPC::PublishHudIntent(*frames,false,now,false,2,false,false,false,false,false,reload);
            NativeWeaponKL::BackgroundReloadControls(sample);
        }
        if(BackgroundDiagnostic::TeamPanelTest()){
            const bool shown=BackgroundDiagnostic::TeamPanelHeld(age);
            const auto grip=BackgroundDiagnostic::TeamPanelGrip(age);
            const DWORD selection=grip?now-(age-grip):0;
            VRIPC::PublishHudIntent(*frames,shown,now,false,2,shown,shown,false,false,false,0,false,selection);
            if(age>=34400&&age<35800)sample.hands[0].position[0]-=.10f;
            if(age>=34100&&age<35500){
                // Point the rendered gun down toward the nearby walkable ground.
                sample.hands[1].orientation[0]=-.24f;
                sample.hands[1].orientation[3]=std::sqrt(1.f-.24f*.24f);
            }
            static std::uint32_t serial=0;
            static unsigned sent=0;
            const auto order=BackgroundDiagnostic::TeamPanelOrder(age);
            if(order&&order!=sent){
                auto* shared=teamMapping.load();
                if(shared&&TeamCommandIPC::PublishCommand(*shared,GetCurrentProcessId(),now,frames->recenterAck,
                   order==1?TeamCommandIPC::Command::Recall:TeamCommandIPC::Command::Move,selection,serial))sent=order;
            }
        }else if(BackgroundDiagnostic::VisualBench()){
            const bool shown=BackgroundDiagnostic::BenchInventory(age);
            VRIPC::PublishHudIntent(*frames,shown,now,false,2,shown,shown,false,false,false,0);
        }
        hands.Publish(sample);return;
    }
    auto* frames=mapping.load();
    if(hostProcess&&WaitForSingleObject(hostProcess,0)==WAIT_OBJECT_0) {
        const auto hostPid=GetProcessId(hostProcess);const auto creation=DiagnosticsSession::Creation(hostProcess);
        DWORD exitCode=0;GetExitCodeProcess(hostProcess,&exitCode);CloseHandle(hostProcess);hostProcess=nullptr;
        char ended[240]{};std::snprintf(ended,sizeof(ended),"VR host exit: pid=%lu creation=%llu code=0x%08lx expectedStop=%u.",
            hostPid,static_cast<unsigned long long>(creation),exitCode,unsigned(closing));Log(ended);
        hostRunning=false;
        if(frames){InterlockedExchange(&frames->hostReady,0);InterlockedExchange(&frames->captureEnabled,0);}
        StopCPUFrames();
        if(!closing&&requested.load()) {
            bridgeError=frames&&Read(frames->hostError)?Read(frames->hostError):Kl1StartupStatus::HostExitedWithoutStatus;
            if(frames)InterlockedExchange(&frames->hostError,bridgeError.load());
            if(VRHostSelection::AutoMetaFallback(launchedPlan.requested==VrModConnection::Mode::Auto,
                launchedPlan.connection.mode==VrModConnection::Mode::Meta,autoFallbackUsed.load(),bridgeError.load())){
                autoFallbackUsed=true;Log("Automatic connection: Meta has no HMD/runtime; trying the active OpenXR runtime once.");
                // The first Meta failure is not a final startup result while
                // Auto is resolving/starting its one OpenXR fallback.
                if(frames)InterlockedExchange(&frames->hostError,0);
                if(Start(true))return;
            }
            requested=false;Log("Kane & Lynch VR host exited. See KaneLynchVRHost.log, then press F9 to retry.");
        } else Log("Kane & Lynch VR host stopped.");
        closing=false;
    }
    if(hostProcess&&!requested.load()&&!closing) {
        closing=true;InterlockedExchange(&frames->captureEnabled,0);InterlockedExchange(&frames->shutdown,1);
        StopCPUFrames();
        Log("Headset output stopping.");
    }
    if(!hostProcess&&requested.load()&&!Start())requested=false;
}
bool Ready() {
    auto* frames=mapping.load();
    return frames&&requested.load()&&Read(frames->captureEnabled)&&!Read(frames->shutdown)&&Read(frames->hostReady)&&
        GetTickCount()-static_cast<DWORD>(Read(frames->hostHeartbeatTick))<3000;
}
bool EyeConfig(int eye,float* offset,float* fov) {
    if(eye<0||eye>1||!Ready())return false;
    auto* frames=mapping.load();
    std::memcpy(offset,frames->eyeOffset[eye],3*sizeof(float));std::memcpy(fov,frames->eyeFov[eye],4*sizeof(float));
    for(int i=0;i<3;++i)if(!std::isfinite(offset[i])||std::fabs(offset[i])>0.2f)return false;
    for(int i=0;i<4;++i)if(!std::isfinite(fov[i])||fov[i]<=0.01f||fov[i]>10.0f)return false;
    return Ready();
}
Tracking HeadPose() {
    Tracking sample{};sample.valid=false;
    auto* frames=mapping.load();
    if(!frames||!Ready())return sample;
    const LONG sequence=Read(frames->trackingSequence);
    if(sequence&1)return sample;
    const DWORD tick=static_cast<DWORD>(Read(frames->trackingTick));
    if(!tick||GetTickCount()-tick>250)return sample;
    sample.tick=tick;
    std::memcpy(sample.orientation,frames->trackingOrientation,4*sizeof(float));
    std::memcpy(sample.position,frames->trackingPosition,3*sizeof(float));
    sample.mounted=frames->trackingMounted!=0;
    sample.sensorTime=frames->trackingSensorTime;
    sample.turnYaw=frames->trackingTurnYaw;
    sample.recenterAck=Read(frames->recenterAck);
    MemoryBarrier();
    if(Read(frames->trackingSequence)!=sequence)return sample;
    double squared=0;for(float v:sample.orientation)squared+=double(v)*v;
    if(!std::isfinite(squared)||squared<0.9||squared>1.1)return sample;
    for(float v:sample.position)if(!std::isfinite(v)){sample.valid=false;return sample;}
    if(!std::isfinite(sample.sensorTime)||sample.sensorTime<=0)return sample;
    if(!std::isfinite(sample.turnYaw)||std::fabs(sample.turnYaw)>3.142f)return sample;
    sample.valid=true;return sample;
}
void RequestRecenter() {
    if(auto* frames=mapping.load())InterlockedExchange(&frames->recenterRequest,1);
}
long RecenterAck() {
    auto* frames=mapping.load();
    return frames?Read(frames->recenterAck):0;
}
void SetNativeMenuReady(bool ready) {
    if(auto* frames=mapping.load())InterlockedExchange(&frames->nativeMenuReadyTick,ready?static_cast<LONG>(GetTickCount()):0);
}
bool GameplayHudVisible(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadHudIntent(*frames,GetTickCount());}
bool TeamHudVisible(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadTeamHudIntent(*frames,GetTickCount());}
bool WeaponMenuVisible(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadWeaponMenuIntent(*frames,GetTickCount());}
bool CoverHeld(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadCoverIntent(*frames,GetTickCount());}
bool ClimbHeld(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadClimbIntent(*frames,GetTickCount());}
DWORD TeamNextTick(){const auto* frames=mapping.load();return frames&&Ready()?VRIPC::ReadTeamNextIntent(*frames,GetTickCount()):0;}
bool GrenadeVisualActive(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadGrenadeVisualIntent(*frames,GetTickCount());}
bool MountedStickActive(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadMountedStickIntent(*frames,GetTickCount());}
namespace {
bool VehicleInputReady(){const auto* frames=mapping.load();return frames&&Ready()&&VRIPC::ReadSecondaryFireIntent(*frames,GetTickCount()).valid&&!TeamHudVisible()&&!WeaponMenuVisible();}
void SetVehicleReceiver(const VehicleControlIPC::Certificate& receiver,VehicleControlIPC::Kind kind){
    auto* shared=vehicleMapping.load();if(!shared)return;auto next=receiver;
    if(!VehicleInputReady()||!VehicleControlIPC::Fresh(next,GetTickCount(),static_cast<std::uint32_t>(RecenterAck()),kind))next={};
    AcquireSRWLockExclusive(&vehiclePublishLock);VehicleControlIPC::Publish(kind==VehicleControlIPC::Kind::Mounted?shared->mounted:shared->passenger,next);ReleaseSRWLockExclusive(&vehiclePublishLock);
}
}
void SetMountedReceiver(const VehicleControlIPC::Certificate& receiver){SetVehicleReceiver(receiver,VehicleControlIPC::Kind::Mounted);}
void SetPassengerReceiver(const VehicleControlIPC::Certificate& receiver){SetVehicleReceiver(receiver,VehicleControlIPC::Kind::Passenger);}
MountedInput ReadMountedInput(const VehicleControlIPC::Certificate& receiver){
    const auto* shared=vehicleMapping.load();VehicleControlIPC::Request input;
    if(!shared||!VehicleInputReady()||!VehicleControlIPC::Input(*shared,GetCurrentProcessId(),GetTickCount(),static_cast<std::uint32_t>(RecenterAck()),VehicleControlIPC::Kind::Mounted,receiver,input))return {};
    return {true,input.tick,input.x,input.y};
}
PassengerNavigation ReadPassengerNavigation(){
    const auto* shared=vehicleMapping.load();VehicleControlIPC::Certificate receiver;VehicleControlIPC::Request input;
    const auto now=GetTickCount();const auto ack=static_cast<std::uint32_t>(RecenterAck());
    if(!shared||!VehicleInputReady()||!VehicleControlIPC::Receiver(*shared,GetCurrentProcessId(),now,ack,VehicleControlIPC::Kind::Passenger,receiver)||
       !VehicleControlIPC::Input(*shared,GetCurrentProcessId(),now,ack,VehicleControlIPC::Kind::Passenger,receiver,input))return {};
    return {true,input.tick,input.x,input.y,(input.buttons&1)!=0,receiver.epoch,receiver.recenter};
}
void SetTeamReceiver(const TeamCommandIPC::ReceiverCertificate& receiver) {
    auto* shared=teamMapping.load();if(!shared)return;
    auto next=receiver;
    if(!Ready()||!TeamHudVisible()||!TeamCommandIPC::Fresh(next,GetTickCount(),static_cast<std::uint32_t>(RecenterAck())))next={};
    AcquireSRWLockExclusive(&teamPublishLock);TeamCommandIPC::Publish(shared->receiver,next);ReleaseSRWLockExclusive(&teamPublishLock);
}
bool ReadTeamCommand(TeamCommandIPC::Request& request) {
    auto* frames=mapping.load();const auto* shared=teamMapping.load();
    if(!frames||!shared||!Ready()||!TeamHudVisible()||Read(frames->inputContext)!=VRIPC::kInputGame)return false;
    const auto now=GetTickCount();const auto recenter=static_cast<std::uint32_t>(RecenterAck());
    TeamCommandIPC::Request next;
    if(!TeamCommandIPC::ReadRequest(*shared,GetCurrentProcessId(),now,recenter,next)||
       static_cast<std::uint32_t>(RecenterAck())!=recenter)return false;
    request=next;return true;
}
void PublishTeamCommandResult(const TeamCommandIPC::Result& result) {
    auto* shared=teamMapping.load();if(!shared||!result.serial||!TeamCommandIPC::Known(result.command)||!TeamCommandIPC::Known(result.code))return;
    AcquireSRWLockExclusive(&teamPublishLock);TeamCommandIPC::Publish(shared->result,result);ReleaseSRWLockExclusive(&teamPublishLock);
}
bool ReadTeamCommandResult(TeamCommandIPC::Result& result) {
    const auto* shared=teamMapping.load();return shared&&TeamCommandIPC::ReadResult(*shared,GetCurrentProcessId(),result);
}
SecondaryFireInput ReadSecondaryFire(){
    const auto* frames=mapping.load();const auto pid=GetCurrentProcessId();
    if(!frames||frames->gamePid!=pid||!Ready())return {};
    const auto input=VRIPC::ReadSecondaryFireIntent(*frames,GetTickCount());
    if(!input.valid||mapping.load()!=frames||frames->gamePid!=pid||!Ready())return {};
    return {true,input.pressed,input.tick,input.reloadTick};
}
PickupInput ReadPickup(){
    const auto* frames=mapping.load();const auto pid=GetCurrentProcessId();
    if(!frames||frames->gamePid!=pid||!Ready())return {};
    const auto input=VRIPC::ReadPickupIntent(*frames,GetTickCount());
    if(!input.valid||mapping.load()!=frames||frames->gamePid!=pid||!Ready())return {};
    return {true,input.hand,input.tick};
}
void SetGameplayState(bool known,bool covered,bool left,bool humanShield,bool sniperAim,bool stanceKnown,bool crouched,unsigned pickupMask,bool firstPersonSkip,bool nativeReloadReady){
    if(auto* frames=mapping.load()){
        const LONG sequence=Read(frames->gameplaySequence);
        InterlockedExchange(&frames->gameplaySequence,sequence+1);
        InterlockedExchange(&frames->gameplayFlags,(known?1:0)|(covered?2:0)|(left?4:0)|(humanShield?8:0)|(sniperAim?16:0)|
            (stanceKnown?32:0)|(crouched?64:0)|((pickupMask&3)<<7)|(firstPersonSkip?512:0)|(nativeReloadReady?1024:0));
        InterlockedExchange(&frames->gameplayTick,static_cast<LONG>(GetTickCount()));
        MemoryBarrier();InterlockedExchange(&frames->gameplaySequence,sequence+2);
    }
}
unsigned ConsumeMenuCommand() {
    auto* frames=mapping.load();if(!frames)return 0;
    const auto before=Read(frames->menuCommandSequence);
    if(!before||(before&1)||before==Read(frames->menuCommandAck))return 0;
    const auto command=Read(frames->menuCommand),tick=Read(frames->menuCommandTick);
    MemoryBarrier();if(before!=Read(frames->menuCommandSequence))return 0;
    InterlockedExchange(&frames->menuCommandAck,before);
    if(!tick||GetTickCount()-static_cast<DWORD>(tick)>250)return 0;
    switch(command){case 0x7B:case 0x7C:case 0x7D:case 0x7E:case 0x7F:case 0x81:case 0x86:break;default:return 0;}
    return static_cast<unsigned>(command);
}
bool Publish(unsigned eye,const VRIPC::EyeMetadata& metadata,const std::uint8_t* pixels,
             std::uint32_t sourceStride,bool forceOpaque) {
    if(!sourceStride)sourceStride=metadata.stride;
    if(eye>1||!Ready()||!pixels||!metadata.width||!metadata.height||
       metadata.width>VRIPC::kMaxWidth||metadata.height>VRIPC::kMaxHeight||metadata.stride!=metadata.width*4||
       !FrameCopy::ValidLayout(metadata.width,metadata.height,sourceStride)||
       (metadata.poseValid!=VRIPC::kUiFrame&&metadata.poseValid!=VRIPC::kContextFrame&&
        metadata.poseValid!=VRIPC::kSceneFrame&&metadata.poseValid!=VRIPC::kScopeFrame))return false;
    AcquireSRWLockExclusive(&publishLock);
    if(!Ready()){ReleaseSRWLockExclusive(&publishLock);return false;}
    SetTransport(VRIPC::kCPU);
    if(cpuPixels.shared&&!cpuPixels.Matches(GetCurrentProcessId(),transportEpoch,metadata.width,metadata.height)&&
       cpuPixels.pid==GetCurrentProcessId()&&cpuPixels.epoch==transportEpoch) {
        ++transportEpoch;if(!transportEpoch)++transportEpoch;
    }
    if(!cpuPixels.Open(GetCurrentProcessId(),transportEpoch,metadata.width,metadata.height)){bridgeError=static_cast<LONG>(GetLastError());ReleaseSRWLockExclusive(&publishLock);return false;}
    bool committed=false;
    const bool accepted=cpuPixels.WriteEye(eye,metadata,[&](std::uint8_t* destination){
        FrameCopy::BGRA(destination,pixels,metadata.width,metadata.height,sourceStride,forceOpaque);
    },committed);
    if(committed){
        CPUFrame::Snapshot publication;
        if(!CPUFrame::ReadSnapshot(cpuPixels.shared,publication)){ReleaseSRWLockExclusive(&publishLock);return false;}
        for(unsigned side=0;side<2;++side){auto& frame=mapping.load()->eyes[side];
            InterlockedIncrement(&frame.sequence);
            frame.transport=VRIPC::kCPU;frame.transportEpoch=transportEpoch;frame.gpuSlot=publication.bank;
            frame.metadata=publication.metadata[side];MemoryBarrier();InterlockedIncrement(&frame.sequence);
        }
    }
    ReleaseSRWLockExclusive(&publishLock);return accepted;
}
void SetGPUTransport(bool enable){gpuEnabled=enable;}
void InvalidateGPU() {
    AcquireSRWLockExclusive(&gpuLock);
    if(auto* producer=gpu.load())producer->Invalidate();
    if(shared9)shared9->Invalidate();
    shared9Pending=false;publishingShared9=false;shared9Admission=0;
    if(gpuResolve){gpuResolve->Release();gpuResolve=nullptr;}
    if(gpuNativeResolve){gpuNativeResolve->Release();gpuNativeResolve=nullptr;}
    if(gpuResolveTexture){gpuResolveTexture->Release();gpuResolveTexture=nullptr;}gpuResolveDevice=nullptr;
    gpuResolveSourceFormat=D3DFMT_UNKNOWN;
    gpuResolveNeedsFlush=false;
    AcquireSRWLockExclusive(&publishLock);
    RetireCPUFramesLocked();
    ReleaseSRWLockExclusive(&publishLock);
    ReleaseSRWLockExclusive(&gpuLock);
}
bool GPUSceneReady() {
    if(!TryAcquireSRWLockShared(&gpuLock))return false;
    const auto* producer=gpu.load();
    const bool ready=(publishingShared9&&shared9&&shared9->active&&!shared9->lost)||
        (producer&&producer->active&&!producer->active->compactPanel);
    ReleaseSRWLockShared(&gpuLock);
    return ready;
}
bool UseShared9Polling(){return shared9EarlyPoll;}
void RenderProgress(IDirect3DDevice9* device){
    if(!shared9Pending.load(std::memory_order_relaxed)||shared9Thread.load(std::memory_order_relaxed)!=GetCurrentThreadId())return;
    thread_local std::uint64_t previous=0;
    const auto now=FrameMetrics::Counter();
    if(previous&&FrameMetrics::Microseconds(previous)<2000)return;
    previous=now;
    if(!TryAcquireSRWLockExclusive(&gpuLock))return;
    if(shared9&&shared9->source==device&&shared9->active&&!shared9->lost){
        shared9->Poll();bool pending=false;
        for(bool command:shared9->active->pending)pending|=command;
        shared9Pending=pending&&!shared9->lost;
    }
    ReleaseSRWLockExclusive(&gpuLock);
}
int PublishGPU(IDirect3DDevice9* device,IDirect3DSurface9* back,unsigned mask,const VRIPC::EyeMetadata& metadata) {
    ProfileTiming::Scope profile(ProfileTiming::GPUBridge);
    if(!gpuEnabled.load()||!gpu.load()||!Ready()||!device||!back||mask<1||mask>3)return -1;
    AcquireSRWLockExclusive(&gpuLock);auto* producer=gpu.load();
    struct Unlock{~Unlock(){ReleaseSRWLockExclusive(&gpuLock);}} unlock;
    const auto nextEpoch=[](bool useShared9){
        return transportEpoch+(transport!=VRIPC::kGPU||publishingShared9!=useShared9?1u:0u);
    };
    const auto announce=[&](unsigned epoch,unsigned slot,bool sourceIsShared9){
        AcquireSRWLockExclusive(&publishLock);
        if(!Ready()){ReleaseSRWLockExclusive(&publishLock);return 0;}
        transport=VRIPC::kGPU;transportEpoch=epoch;publishingShared9=sourceIsShared9;
        cpuPixels.Close();
        for(unsigned eye=0;eye<2;++eye)if(mask&(1u<<eye)){
            auto& frame=mapping.load()->eyes[eye];InterlockedIncrement(&frame.sequence);
            frame.transport=VRIPC::kGPU;frame.transportEpoch=epoch;frame.gpuSlot=slot;frame.metadata=metadata;
            MemoryBarrier();InterlockedIncrement(&frame.sequence);
        }
        ReleaseSRWLockExclusive(&publishLock);return 1;
    };
    if(shared9&&shared9->section){
        if(shared9->source!=device)shared9Admission=GetTickCount();
        const auto epoch=nextEpoch(true);unsigned slot=0;
        const int result=shared9->Try(device,back,mask,metadata,epoch,slot);
        if(result==1){
            if(shared9EarlyPoll){shared9Thread=GetCurrentThreadId();shared9Pending=true;}
            if(producer->active)producer->Invalidate();
            producer->Collect();
            return announce(epoch,slot,true);
        }
        if(result==0)return 0;
        // Let a matching host finish offering/importing textures before
        // allocating the old relay or a large CPU pixel arena during startup.
        if(shared9->supported&&!shared9->active&&!shared9->rejectedOffer&&
           GetTickCount()-shared9Admission<1500)return 0;
        InterlockedExchange(&shared9->section->shared->source.accepted,0);
        shared9Pending=false;
    }
    // CPU readback allocates more resources on this same removed device. It
    // cannot recover it; do not turn device loss into more OOM allocations.
    if(producer->unavailable==device)return 0;
    if(!producer->CanAttempt(device))return -1;
    if(!producer->HostReady()){producer->reason="cpu_requested_or_gpu_consumer_unavailable";return -1;}
    D3DSURFACE_DESC source{};if(FAILED(back->GetDesc(&source)))return -1;
    if(source.Width!=metadata.width||source.Height!=metadata.height){producer->reason="gpu_native_dimensions_required";return -1;}
    IDirect3DSurface9* normalized=back;
    IDirect3DResource9* interop=back;
    if(source.Format!=D3DFMT_A8R8G8B8||source.MultiSampleType!=D3DMULTISAMPLE_NONE||
       source.Width!=metadata.width||source.Height!=metadata.height) {
        D3DSURFACE_DESC current{};
        if(!gpuResolve||gpuResolveDevice!=device||FAILED(gpuResolve->GetDesc(&current))||
           current.Width!=metadata.width||current.Height!=metadata.height||gpuResolveSourceFormat!=source.Format) {
            if(gpuResolve){gpuResolve->Release();gpuResolve=nullptr;}
            if(gpuNativeResolve){gpuNativeResolve->Release();gpuNativeResolve=nullptr;}
            if(gpuResolveTexture){gpuResolveTexture->Release();gpuResolveTexture=nullptr;}
            if(RejectUnsupportedNormalization(*producer,device))return -1;
            producer->reason="gpu_normalization_create";
            if(FAILED(device->CreateTexture(metadata.width,metadata.height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
                D3DPOOL_DEFAULT,&gpuResolveTexture,nullptr))||FAILED(gpuResolveTexture->GetSurfaceLevel(0,&gpuResolve))){producer->reason="gpu_normalization_unavailable";return -1;}
            gpuResolveDevice=device;
            gpuResolveSourceFormat=source.Format;
            gpuResolveNeedsFlush=true;
        }
        if(D3D9Normalization::NeedsNativeResolve(source)&&!gpuNativeResolve){
            producer->reason="gpu_native_msaa_resolve_create";
            if(FAILED(device->CreateRenderTarget(source.Width,source.Height,source.Format,D3DMULTISAMPLE_NONE,
                0,FALSE,&gpuNativeResolve,nullptr))){producer->reason="gpu_native_msaa_resolve_unavailable";return -1;}
            gpuResolveNeedsFlush=true;
        }else if(!D3D9Normalization::NeedsNativeResolve(source)&&gpuNativeResolve){gpuNativeResolve->Release();gpuNativeResolve=nullptr;}
        normalized=gpuResolve;
        interop=gpuResolveTexture;
    }else if(gpuResolve||gpuNativeResolve||gpuResolveTexture){
        // A native A8 single-sample source no longer needs either cached
        // default-pool target, even if a secondary swapchain changed format
        // without a device Reset notification.
        if(gpuResolve){gpuResolve->Release();gpuResolve=nullptr;}
        if(gpuNativeResolve){gpuNativeResolve->Release();gpuNativeResolve=nullptr;}
        if(gpuResolveTexture){gpuResolveTexture->Release();gpuResolveTexture=nullptr;}
        gpuResolveDevice=nullptr;gpuResolveSourceFormat=D3DFMT_UNKNOWN;gpuResolveNeedsFlush=false;
    }
    // A full ring or a generation still awaiting acknowledgement needs no
    // resolve/copy work. Producer calls this only after claiming a safe slot.
    const auto prepare=[&]()->HRESULT {
        if(normalized==back)return S_OK;
        producer->reason="gpu_normalization_copy";D3D9Normalization::Step step{};
        const auto result=D3D9Normalization::Copy(device,back,source,gpuNativeResolve,gpuResolve,step);
        if(FAILED(result)){
            producer->reason=step==D3D9Normalization::Step::NativeResolve?"gpu_native_msaa_resolve_failed":"gpu_normalization_failed";return result;
        }
        if(gpuResolveNeedsFlush) {
            IDirect3DQuery9* event=nullptr;
            const auto created=device->CreateQuery(D3DQUERYTYPE_EVENT,&event);
            if(FAILED(created)){producer->reason="gpu_resource_flush_unavailable";return created;}
            const HRESULT issued=event->Issue(D3DISSUE_END);
            const HRESULT flushed=SUCCEEDED(issued)?event->GetData(nullptr,0,D3DGETDATA_FLUSH):issued;
            event->Release();if(FAILED(flushed)){producer->reason="gpu_resource_flush_failed";return flushed;}
            gpuResolveNeedsFlush=false;
        }
        return S_OK;
    };
    const unsigned epoch=nextEpoch(false);unsigned slot=0;
    const auto result=producer->Try(device,normalized,mask,metadata,epoch,slot,interop,prepare);
    if(result==GPUFrame::PublishResult::Unavailable)return 0;
    if(result==GPUFrame::PublishResult::Fallback)return -1;
    if(result==GPUFrame::PublishResult::Busy)return 0;
    return announce(epoch,slot,false);
}
void SetPanelSize(unsigned widthMm,unsigned distanceMm) {
    if(auto* shared=mapping.load()){InterlockedExchange(&shared->panelWidthMm,static_cast<LONG>(widthMm));InterlockedExchange(&shared->panelDistanceMm,static_cast<LONG>(distanceMm));}
}
void SetInputContext(LONG context) {
    if(auto* shared=mapping.load())InterlockedExchange(&shared->inputContext,context);
}
void SetMovementYaw(float radians,bool valid,bool lookLocked,bool nativeMovementAxes,bool nativeCarried,bool nativeMounted) {
    valid=valid&&std::isfinite(radians);
    if(auto* shared=mapping.load()) {
        InterlockedIncrement(&shared->movementSequence);
        shared->movementYaw=valid?radians:0.f;
        shared->reservedV6[0]=valid?((lookLocked?1u:0u)|(nativeMovementAxes?2u:0u)|(nativeCarried?4u:0u)|((nativeCarried&&nativeMounted)?8u:0u)):0u;
        InterlockedExchange(&shared->movementTick,valid?static_cast<LONG>(GetTickCount()):0);
        MemoryBarrier();
        InterlockedIncrement(&shared->movementSequence);
    }
}
Stats Snapshot() {
    Stats result{};result.requested=requested.load();result.running=hostRunning.load();result.ready=Ready();result.error=bridgeError.load();
    auto* frames=mapping.load();if(!frames)return result;
    if(Read(frames->hostError))result.error=Read(frames->hostError);
    result.submitted=Read(frames->hostSubmittedFrames);result.visible=Read(frames->hostVisible);result.hmdPresent=Read(frames->hostHmdPresent);
    result.leftSequence=Read(frames->eyes[0].sequence);result.rightSequence=Read(frames->eyes[1].sequence);
    result.gpuRequested=gpuEnabled;
    AcquireSRWLockShared(&gpuLock);
    if(auto* producer=gpu.load()) {
        result.gpuReason=producer->reason;result.gpuCopies=producer->copies;result.gpuBusy=producer->busy;result.gpuFailures=producer->failures;
        result.gpuCreateResult=producer->createResult;
        result.gpuRetiredGenerations=static_cast<unsigned>(producer->retired.size());
        for(const auto& retired:producer->retired)if(retired){result.gpuRetiredTextureBytes+=retired->textureBytes;result.gpuRetiredIntermediateBytes+=retired->intermediateBytes;}
        if(producer->active){result.gpuPhysicalTextures=producer->active->physicalTextures;
            result.gpuSharedTextureBytes=producer->active->textureBytes;result.gpuCompactPanel=producer->active->compactPanel;}
        if(producer->active){result.gpuIntermediateTextures=producer->active->intermediateTextures;result.gpuIntermediateTextureBytes=producer->active->intermediateBytes;}
    }
    AcquireSRWLockShared(&publishLock);result.gpuActive=transport==VRIPC::kGPU&&result.ready;
    if(publishingShared9&&shared9&&shared9->active&&result.gpuActive){
        result.gpuReason=shared9->reason;result.gpuCopies=shared9->copies;result.gpuBusy=shared9->busy;result.gpuFailures=shared9->failures;
        result.gpuCreateResult=shared9->result;result.gpuPhysicalTextures=SharedD3D9::Slots;
        result.gpuSharedTextureBytes=std::uint64_t(shared9->width)*shared9->height*4*SharedD3D9::Slots;
        result.gpuCompactPanel=false;
    }
    if(shared9)result.gpuPanelCopiesPaced=shared9->panelCopiesPaced;
    result.cpuPixelMappingBytes=cpuPixels.shared?cpuPixels.layout.mappingBytes:0;ReleaseSRWLockShared(&publishLock);
    ReleaseSRWLockShared(&gpuLock);
    return result;
}
}
