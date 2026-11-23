// Execute the actual installers against a private read-only-PE-derived image.
// MinHook is a patch-only fixture: no game code/detour is executed, no GPU.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
namespace InstallFixture {
inline std::uintptr_t image=0;inline std::size_t imageBytes=0;inline std::wstring game;
DWORD WINAPI ModuleFile(HMODULE,wchar_t* out,DWORD capacity){if(!capacity)return 0;if(game.size()>=capacity)return capacity;std::memcpy(out,game.c_str(),(game.size()+1)*sizeof(wchar_t));return DWORD(game.size());}
HMODULE WINAPI ModuleHandle(const wchar_t*){return reinterpret_cast<HMODULE>(image);}
BOOL WINAPI ReadMemory(HANDLE process,const void* source,void* out,SIZE_T bytes,SIZE_T* got){
    const auto result=::ReadProcessMemory(process,source,out,bytes,got);const auto address=reinterpret_cast<std::uintptr_t>(source);
    // The pinned PE has no relocation directory. Only scalar native image
    // pointers are rebased for the private module; opcode spans stay EXACT.
    if(result&&bytes==4&&address>=image&&address<image+imageBytes){std::uint32_t value=0;std::memcpy(&value,out,4);
        if(value>=0x400000&&value-0x400000<imageBytes){value=std::uint32_t(image+value-0x400000);std::memcpy(out,&value,4);}}
    // This verifier builds the constructor's vtable immediate from image.
    // Rebase that ONE typed pointer operand, leaving the C7/06 opcode intact.
    if(result&&bytes==6&&address==image+0x26269A){std::uint32_t value=0;std::memcpy(&value,static_cast<unsigned char*>(out)+2,4);
        if(value==0x975C3C){value=std::uint32_t(image+0x575C3C);std::memcpy(static_cast<unsigned char*>(out)+2,&value,4);}}
    return result;
}
}
#define GetModuleFileNameW InstallFixture::ModuleFile
#define GetModuleHandleW InstallFixture::ModuleHandle
#define ReadProcessMemory InstallFixture::ReadMemory
#include "native_hud_kl.cpp"
#include "native_team_hud_kl.cpp"
#include "native_objective_visibility_kl.cpp"
#include "native_gameplay_hud_kl.cpp"
#undef GetModuleFileNameW
#undef GetModuleHandleW
#undef ReadProcessMemory
#include "native_hud_test_fixture.hpp"
#include "native_team_commands_kl_image.hpp"
#include <cstdio>
#include <cstdlib>
namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return true;}}
namespace InstallFixture {
unsigned checks=0;struct Hook {void* target=nullptr;void* detour=nullptr;bool queued=false,enabled=false;std::array<unsigned char,7> original{};};std::vector<Hook> hooks;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
Hook* Find(void* target){for(auto& hook:hooks)if(hook.target==target)return &hook;return nullptr;}
void Patch(Hook& hook,bool enabled){auto* data=static_cast<unsigned char*>(hook.target);if(enabled){data[0]=0xE9;const auto delta=std::int32_t(reinterpret_cast<std::uintptr_t>(hook.detour)-reinterpret_cast<std::uintptr_t>(hook.target)-5);std::memcpy(data+1,&delta,4);
    if(reinterpret_cast<std::uintptr_t>(hook.target)==image+0x253480){data[5]=data[6]=0x90;}}
    else std::memcpy(data,hook.original.data(),hook.original.size());hook.enabled=enabled;}
bool Scene(){return true;}bool Frame(NativeHudKL::Frame&){return false;}void Log(const char* value){std::printf("%s\n",value);}
bool Wrist(NativeHudKL::WristHudElement,std::uintptr_t,NativeHudKL::WristHudEye&){return false;}
bool DeclineReticle(NativeHudKL::ReticleFrame&){return false;}
bool ForeignReticle(NativeHudKL::ReticleFrame& output){output.player=0x12340004;return true;}
void Reset(const NativeHudFixture::Image& source){std::memcpy(reinterpret_cast<void*>(image),source.mapped.data(),source.mapped.size());hooks.clear();
    NativeHudKL::installed=false;NativeHudKL::SetNativeMarkerObserver(nullptr);NativeHudKL::SetWristHudProvider(nullptr);
    NativeTeamHudKL::installed=false;NativeObjectiveVisibilityKL::installed=false;NativeGameplayHudKL::installed=false;}
bool CommandsProof(){return NativeTeamCommandsImage::Verify(image,[](std::uintptr_t at,void* out,std::size_t size){SIZE_T got=0;return ReadMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;});}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original){using namespace InstallFixture;if(Find(target))return MH_ERROR_ALREADY_CREATED;Hook hook;hook.target=target;hook.detour=detour;std::memcpy(hook.original.data(),target,hook.original.size());hooks.push_back(hook);*original=target;return MH_OK;}
MH_STATUS WINAPI MH_EnableHook(LPVOID target){auto* hook=InstallFixture::Find(target);if(!hook)return MH_ERROR_NOT_CREATED;InstallFixture::Patch(*hook,true);return MH_OK;}
MH_STATUS WINAPI MH_DisableHook(LPVOID target){auto* hook=InstallFixture::Find(target);if(!hook)return MH_ERROR_NOT_CREATED;InstallFixture::Patch(*hook,false);return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID target){using namespace InstallFixture;const auto at=std::find_if(hooks.begin(),hooks.end(),[&](const auto& hook){return hook.target==target;});if(at==hooks.end())return MH_ERROR_NOT_CREATED;Patch(*at,false);hooks.erase(at);return MH_OK;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID target){auto* hook=InstallFixture::Find(target);if(!hook)return MH_ERROR_NOT_CREATED;hook->queued=true;return MH_OK;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID target){auto* hook=InstallFixture::Find(target);if(!hook)return MH_ERROR_NOT_CREATED;hook->queued=false;return MH_OK;}
MH_STATUS WINAPI MH_ApplyQueued(){for(auto& hook:InstallFixture::hooks)InstallFixture::Patch(hook,hook.queued||hook.enabled);return MH_OK;}
}
int main(int argc,char** argv){using namespace InstallFixture;if(argc!=2)return 2;const auto chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,nullptr,0);Check(chars>1,"supported path UTF8");game.resize(std::size_t(chars));MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,game.data(),chars);game.resize(game.size()-1);
    NativeHudFixture::Image source;Check(source.Load(argv[1]),"read supported original PE without executing it");imageBytes=source.mapped.size();image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,imageBytes,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(image!=0,"private installer image allocation");
    for(unsigned half:{0u,24u}){Reset(source);auto* changed=reinterpret_cast<unsigned char*>(image+0x634728+half);*changed^=1;
        Check(!NativeHudKL::Install(Frame,Log)&&!NativeHudKL::installed&&hooks.empty(),"either changed half of the exact48-byte native corner table rejects actual HUD installation before hooks");}
    Reset(source);Check(CommandsProof(),"independent native command proof before any shared hooks");
    NativeHudKL::SetWristHudProvider(Wrist);
    Check(!NativeTeamHudKL::Install(Scene,Log)&&hooks.empty(),"the native roster requires an installed wrist HUD even with a provider");
    Check(NativeObjectiveVisibilityKL::Install(Log),"actual .43 early Objective installer succeeds on original proofs");
    Check(NativeHudKL::Install(Frame,Log),"actual .43 HUD installer succeeds after early Objective");
    Check(NativeGameplayHudKL::Install(Scene,Log),"actual .43 gameplay HUD installer succeeds after early Objective");
    Check(NativeHudKL::WristHudReady(),"the native roster reaches its shared signature proof with wrist HUD ready");
    Check(!NativeTeamHudKL::Install(Scene,Log),"actual TEAM installer reproduces .43 shared Count signature regression after early Objective detour");
    Reset(source);NativeHudKL::SetWristHudProvider(Wrist);
    Check(NativeHudKL::Install(Frame,Log)&&NativeGameplayHudKL::Install(Scene,Log),"actual main HUD installers retain TEAM dependencies outside their stolen prefixes");
    NativeHudKL::SetWristHudProvider(nullptr);
    Check(!NativeTeamHudKL::Install(Scene,Log),"the installed HUD must provide wrist poses before the native roster can attach");
    NativeHudKL::SetWristHudProvider(Wrist);
    Check(NativeTeamHudKL::Install(Scene,Log),"native TEAM attaches before the shared Objective Count patch");
    Check(CommandsProof(),"independent command proof succeeds before the shared Objective Count patch");
    Check(NativeObjectiveVisibilityKL::Install(Log),"late Objective installer still verifies its distinct653210 updater after HUD patched653480 getter");
    Check(NativeHudKL::installed&&NativeTeamHudKL::installed&&NativeGameplayHudKL::installed&&NativeObjectiveVisibilityKL::installed,"fixed main order retains all four independently qualified modules");
    NativeHudKL::SetNativeScopeProvider(nullptr);NativeHudKL::SetReticleEnabled(true);NativeHudKL::SetReticleProvider(DeclineReticle);
    NativeHudKL::reticleDrawScope.player=0x12340000;
    const auto rejected=NativeHudKL::Snapshot().reticleRejected;
    Check(NativeHudKL::DrawReticle(nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK&&NativeHudKL::Snapshot().reticleRejected==rejected+1,
        "actual native reticle consumer suppresses provider-denied handheld or mounted graphics before GPU access; no centre fallback");
    NativeHudKL::SetReticleProvider(ForeignReticle);
    Check(NativeHudKL::DrawReticle(nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK&&NativeHudKL::Snapshot().reticleRejected==rejected+2,
        "actual reticle consumer suppresses foreign source rather than showing the unrelocated native centre crosshair");
    NativeHudKL::SetReticleEnabled(false);
    Check(NativeHudKL::DrawReticle(nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK&&NativeHudKL::Snapshot().reticleRejected==rejected+3,
        "disabled reticle suppresses an already queued source packet before any GPU read or fallback");
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);std::printf("Native HUD installer dependency CPU producer: %u checks PASS.\n",checks);
}
