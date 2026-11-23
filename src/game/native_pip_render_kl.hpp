#pragma once
#include "native_cinematic_kl.hpp"

// The PIP controller continues its normal simulation and registration. This
// optional guard acts only at its owned camera's render-command producer.
namespace NativePipRenderKL {
inline constexpr std::uintptr_t CameraEntryVtableRva=0x540F40,ProducerRva=0xBF600;
inline constexpr std::uintptr_t OverlayCameraVtableRva=0x56AF04;
struct Entry {
    bool valid=false;
    std::uintptr_t camera=0,geometry=0;
};
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    if(!NativePipKL::VerifyImage(image,read))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[40]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    // ZRenderEntryCameraD3D::vtable+10 is a void __thiscall producer with
    // six stack arguments (ret 18). The hook returns before any packet is
    // reserved; it does not leave an incomplete camera/world command list.
    unsigned char prologue[]={0x6A,0xFF,0x68,0,0,0,0,0x64,0xA1,0,0,0,0,
        0x50,0xB8,0x74,0xD1,3,0,0xE8,0x98,0xB5,0x3F,0};
    const auto handler=static_cast<std::uint32_t>(image+0x4F103B);
    std::memcpy(prologue+3,&handler,4);
    const unsigned char cameraSource[]={0x8B,0x87,0xF4,0,0,0,0x89,0x84,0x24,0x94,0,0,0,
        0x8B,0x40,0x60,0x83,0xC4,0x18,0x50};
    const unsigned char epilogue[]={0x81,0xC4,0x80,0xD1,3,0,0xC2,0x18,0};
    const unsigned char overlayActivate[]={0x8B,0x96,0x28,6,0,0,0xC6,0x86,0x38,5,0,0,1};
    const unsigned char overlayDeactivate[]={0xC6,0x86,0x38,5,0,0,0};
    std::uint32_t producer=0,overlayVisibility=0,overlayEligible=0;
    return read(image+CameraEntryVtableRva+0x10,&producer,4)&&producer==image+ProducerRva&&
        match(ProducerRva,prologue)&&match(0xBF6C9,cameraSource)&&match(0xC1B79,epilogue)&&
        match(0x2368D2,overlayActivate)&&match(0x23695B,overlayDeactivate)&&
        read(image+OverlayCameraVtableRva+0x220,&overlayVisibility,4)&&overlayVisibility==image+0xF6220&&
        read(image+OverlayCameraVtableRva+0x228,&overlayEligible,4)&&overlayEligible==image+0xF5770;
}
template<class Reader> Entry OverlayCamera(std::uintptr_t image,const NativePipKL::State& pip,Reader read) {
    Entry result;
    if(!pip.valid||!pip.active||pip.mode!=6)return result;
    const auto get=[&](std::uintptr_t at,auto& value){
        return NativePipKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));
    };
    std::uint8_t registered=0,againRegistered=0;
    std::uint32_t camera=0,vt=0,geometry=0,backlink=0,again=0;
    if(!get(pip.controller+0x538,registered)||registered!=1||!get(pip.controller+0x628,camera)||
       !get(camera,vt)||vt!=image+OverlayCameraVtableRva||!get(camera+4,geometry)||
       !get(geometry+0x60,backlink)||backlink!=camera||
       !get(pip.controller+0x538,againRegistered)||againRegistered!=registered||
       !get(pip.controller+0x628,again)||again!=camera||!get(camera+4,again)||again!=geometry)return result;
    result.valid=true;result.camera=camera;result.geometry=geometry;return result;
}
template<class Reader> Entry CameraEntry(std::uintptr_t image,std::uintptr_t at,Reader read) {
    Entry result;
    const auto get=[&](std::uintptr_t address,std::uint32_t& value){
        return NativePipKL::Address(address,4)&&read(address,&value,4);
    };
    std::uint32_t vt=0,geometry=0,camera=0,cameraVt=0,cameraGeometry=0,again=0;
    if(!NativePipKL::Address(at,0xF8)||!get(at,vt)||vt!=image+CameraEntryVtableRva||
       !get(at+0xF4,geometry)||!NativePipKL::Address(geometry,0x64)||
       !get(geometry+0x60,camera)||!NativePipKL::Address(camera,8)||
       !get(camera,cameraVt)||(cameraVt!=image+NativePipKL::CameraVtableRva&&cameraVt!=image+OverlayCameraVtableRva)||
       !get(camera+4,cameraGeometry)||cameraGeometry!=geometry||
       !get(at+0xF4,again)||again!=geometry||!get(geometry+0x60,again)||again!=camera)return result;
    result.valid=true;result.camera=camera;result.geometry=geometry;return result;
}
inline bool ShouldSuppress(bool enabled,const NativePipKL::State& pip,
                           const NativeCinematicKL::State& cinematic,
                           std::uintptr_t localCamera,const Entry& entry,
                           std::uintptr_t view,std::uintptr_t ownerView,
                           bool /*firstCamera*/,std::uintptr_t parentGeometry,unsigned nativeMode=0) {
    // Full authored cinematics, unknown states, the main/native-scope camera,
    // and full-screen PIP compositions retain their complete original path.
    // This exact D3D camera producer never reads its sixth first-camera
    // argument. Its caller advances/reset scratch regardless of the call.
    return enabled&&nativeMode==0&&!parentGeometry&&view&&view==ownerView&&
        pip.valid&&pip.active&&pip.mode>=1&&pip.mode<=6&&
        cinematic.valid&&!cinematic.full&&localCamera&&entry.valid&&
        entry.camera!=localCamera&&entry.camera==pip.camera&&entry.geometry==pip.geometry&&
        pip.left>0&&pip.top>0&&pip.right>pip.left&&pip.bottom>pip.top;
}
using LogCallback=void(*)(const char*);
// Must run on the exact hash-verified pristine image BEFORE NativeCamera's
// BF600 detour is installed. Runtime registration uses that same owned hook.
bool InitializeVerifiedImage(std::uintptr_t image,LogCallback log);
struct Status {
    bool installed=false,enabled=false,verified=false;
    std::uint64_t calls=0,suppressed=0,rejected=0;
    std::uint64_t activePipSeen=0,parentRejected=0,viewRejected=0,entryRejected=0;
    std::uint64_t fullRejected=0,localRejected=0,insetRejected=0,compositeSuppressed=0;
    std::uint32_t mode=0;
    std::int32_t rectangle[4]{};
    std::uintptr_t parent=0,view=0,ownerView=0,camera=0;
};
bool Install(bool hideAuxiliaryPip,LogCallback log);
void Shutdown();
Status Snapshot();
}
