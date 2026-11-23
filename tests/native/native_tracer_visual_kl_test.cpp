#include "native_tracer_visual_kl.cpp"
#include "native_hud_kl.cpp"
#include "native_effects_math_kl.hpp"
#include "profile.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {
using namespace NativeCameraMath;
unsigned checks=0,failures=0,nativeCalls=0;
bool mutateCamera=false;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",text);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t entry=0,manager=0;
std::uintptr_t __thiscall NativeEmit(void* renderer,std::uint32_t texture,const void* geometry,const void* transform,
    const void* position,std::uint32_t type,std::uint32_t flags,std::uint32_t extra) {
    ++nativeCalls;
    Check(reinterpret_cast<std::uintptr_t>(renderer)==manager&&texture==0xAABBCCDDu&&
        reinterpret_cast<std::uintptr_t>(geometry)==NativeTracerVisualKL::image+0x50054&&type==9&&!flags&&!extra&&
        reinterpret_cast<std::uintptr_t>(position)==reinterpret_cast<std::uintptr_t>(transform)-0x18,
        "actual HUD hook forwards all original native SpriteEmit arguments unchanged");
    if(mutateCamera){std::uint32_t camera=0;std::memcpy(&camera,static_cast<const unsigned char*>(transform)+0x2C,4);
        static_cast<CameraPose*>(reinterpret_cast<void*>(camera))->position.x+=1.f;}
    return entry;
}
CameraPose Pose(float yaw,float roll=0.f){
    CameraPose out;out.basis={{0,0,1},{0,1,0},{1,0,0}};
    const auto q=Multiply(Quat{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},Quat{0,0,std::sin(roll*.5f),std::cos(roll*.5f)});
    out.basis={Rotate(q,out.basis.forward),Rotate(q,out.basis.up),Rotate(q,out.basis.right)};return out;
}
Vec3 View(const CameraPose& pose,Vec3 world){return NativeEffectsKLMath::ToView(pose.basis,world);}
bool Near(Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.0001f;}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){std::abort();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_ApplyQueued(){std::abort();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){std::abort();}
}
namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return true;}}
int main(int argc,char** argv){
    using namespace NativeTracerVisualKL;using namespace NativeEffectsKLMath;
    Check(argc==2,"pinned EXE path supplied for primary source proof");if(argc!=2)return 1;
    std::wstring exe;for(const auto* p=argv[1];*p;++p)exe.push_back(wchar_t(static_cast<unsigned char>(*p)));
    char hash[65]{};Check(HashFile(exe.c_str(),hash)&&IsSupportedExecutableHash(hash),"supported native EXE digest matches");
    std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    const auto nativeRead=[&](std::uintptr_t at,void* out,std::size_t count){if(at<0x400000)return false;const auto rva=at-0x400000;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){if(rva>=sections[i].VirtualAddress&&rva+count<=sections[i].VirtualAddress+sections[i].SizeOfRawData){
            std::memcpy(out,file.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress,count);return true;}}
        return false;};
    Check(Verify(0x400000,nativeRead),"actual tracer camera transform and exact emitter caller signatures match");
    const auto poisoned=[&](std::uintptr_t at,void* out,std::size_t count){if(!nativeRead(at,out,count))return false;
        if(at==0x6964F1)static_cast<unsigned char*>(out)[0]^=1;return true;};
    Check(!Verify(0x400000,poisoned),"changed producer camera math revokes the hook proof");
    auto* memory=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory!=nullptr,"private native queue allocated");if(!memory)return 1;
    image=reinterpret_cast<std::uintptr_t>(memory);const auto at=[&](unsigned off){return std::uint32_t(image+off);};
    entry=at(0x10000);manager=at(0x20000);const auto geometry=at(0x30000),records=at(0x40000);
    Put(image+0x7678A0,0u);Put(image+0x7678A4,0u);Put(entry,at(0x540EAC));Put(entry+0xFC,std::uint8_t(0));
    Put(entry+0x100,geometry);Put(entry+0x130,37u);const std::uint32_t header[]{1,2,3,6,5};Put(entry+0x108,header);
    Put(geometry,1u);Put(geometry+8,records);unsigned char record[116]{};Put(records,record);
    // Execute a private CPU-only native caller with the original prologue and
    // exact original SpriteEmit instruction window at their actual RVAs. Math
    // and game calls are skipped; the native frame/LEA/call/RET layout is real.
    for(const auto span:{std::pair{DrawRva,20u},std::pair{0x296475u,7u},std::pair{0x2964F1u,11u},std::pair{0x2966A9u,42u}})
        Check(nativeRead(0x400000+span.first,reinterpret_cast<void*>(image+span.first),span.second),"actual native producer proof bytes copied to private CPU fixture");
    const unsigned char prepare[]={0x8D,0x43,0x54,0x89,0x44,0x24,0x1C,0x8D,0x7B,0x10,0xE9};
    std::memcpy(reinterpret_cast<void*>(image+DrawRva+20),prepare,sizeof(prepare));
    const auto jump=std::int32_t(0x2966A9u-(DrawRva+20+sizeof(prepare)+4));Put(image+DrawRva+20+sizeof(prepare),jump);
    unsigned char epilogue[15]{};Check(nativeRead(0x6966EB,epilogue,sizeof(epilogue)),"actual native ret8 epilogue copied");
    std::memcpy(reinterpret_cast<void*>(image+0x2966D3),epilogue,sizeof(epilogue));
    Put(manager,at(0x20080));Put(image+0x20080+0x14,reinterpret_cast<std::uintptr_t>(&NativeHudKL::SpriteEmitHook));Put(image+0x5004C,0xAABBCCDDu);
    NativeHudKL::image=image;NativeHudKL::originalSpriteEmit=NativeEmit;
    DWORD previousProtection=0;Check(VirtualProtect(reinterpret_cast<void*>(image+0x296000),4096,PAGE_EXECUTE_READ,&previousProtection)!=0,"private native caller page executable");
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image+0x296000),4096);
    unsigned char entryBytes[32]{};std::memcpy(entryBytes,reinterpret_cast<void*>(image+DrawRva),sizeof(entryBytes));
    Check(Install(image,nullptr),"actual production installer accepts native signatures without any MinHook dependency");
    Check(!std::memcmp(entryBytes,reinterpret_cast<void*>(image+DrawRva),sizeof(entryBytes)),"tracer Draw entry is never patched");
    using NativeCaller=bool(__thiscall*)(void*,void*,const CameraPose*);
    const auto nativeDraw=reinterpret_cast<NativeCaller>(image+DrawRva);
    const auto draw=[&](const CameraPose* camera){return nativeDraw(reinterpret_cast<void*>(image+0x50000),reinterpret_cast<void*>(manager),camera);};
    const auto foreign=[&](std::uint32_t caller){return ForwardSpriteEmission(manager,caller,9,0,0,nullptr,nullptr,[&]{++nativeCalls;return entry;});};
    const auto producerCamera=Pose(.7f,.2f),wrongReference=Pose(-.4f);const auto sourceBytes=producerCamera;
    Check(draw(&producerCamera)&&nativeCalls==1,
        "actual native ABI caller forwards SpriteEmit exactly once without a tracer Draw hook");
    CameraPose reference;Check(ReadReference(entry,geometry,records,6,0,1,reference)&&!std::memcmp(&reference,&producerCamera,sizeof(reference)),
        "deferred source reader returns the exact native producer camera instead of a generic third-person camera");
    Check(!std::memcmp(&sourceBytes,&producerCamera,sizeof(sourceBytes)),"source camera and native record are never modified");
    float world[12]{0,0,1,0,1,0,1,0,0,300,150,-800};const Vec3 anchor{300,150,-800};
    const Vec3 offsets[]{{-.2f,0,0},{.2f,0,0},{.2f,0,90},{-.2f,0,90}};
    for(const auto& eye:{Pose(0.f),Pose(.3f,.4f),Pose(-.9f,-.2f),producerCamera}){
        Vertex native[4];for(unsigned i=0;i<4;++i){const auto p=Add(View(eye,anchor),View(producerCamera,offsets[i]));native[i]={p.x,p.y,p.z,0xAABBCCDDu,float(i),float(3-i)};}
        Vertex corrected[4],incorrect[4];std::memcpy(corrected,native,sizeof(native));std::memcpy(incorrect,native,sizeof(native));
        Check(MappedWorldSpriteQuads(corrected,sizeof(corrected),record,6,world,eye,reference),"proved tracer corners can map into actual eye");
        for(unsigned i=0;i<4;++i){const auto expected=View(eye,Add(anchor,offsets[i]));
            Check(Near({corrected[i].x,corrected[i].y,corrected[i].z},expected),"trace corner stays on its world flight segment across yaw/roll");
            Check(corrected[i].colour==native[i].colour&&corrected[i].u==native[i].u&&corrected[i].v==native[i].v,"native tracer colour and UV remain bit-exact");}
        Check(MappedWorldSpriteQuads(incorrect,sizeof(incorrect),record,6,world,eye,wrongReference)&&
            !Near({incorrect[2].x,incorrect[2].y,incorrect[2].z},View(eye,Add(anchor,offsets[2]))),"negative generic-camera control reproduces a sideways flight trail");
    }
    Check(!ReadReference(entry,geometry,records,6,1,1,reference)&&!ReadReference(entry,geometry,records,6,0,2,reference),"foreign prefix/count cannot borrow tracer provenance");
    Put(entry+0x130,38u);Check(!ReadReference(entry,geometry,records,6,0,1,reference),"recycled native stamp retires the certificate");Put(entry+0x130,37u);
    Put(records+20,1u);Check(!ReadReference(entry,geometry,records,6,0,1,reference),"mutated queued geometry cannot inherit a previous camera");Put(records+20,0u);
    foreign(0x12345);Check(!ReadReference(entry,geometry,records,6,0,1,reference),"foreign emission revokes stale same-pointer same-stamp same-material provenance");
    draw(&producerCamera);Put(image+0x7678A4,1u);
    Check(!ReadReference(entry,geometry,records,6,0,1,reference),"other native buffer slot never inherits the tracer camera");Put(image+0x7678A4,0u);
    Check(ReadReference(entry,geometry,records,6,0,1,reference),"original slot remains qualified independently");
    foreign(EmitCallerRva);Check(!ReadReference(entry,geometry,records,6,0,1,reference),"caller address without exact stack anchors is insufficient");
    auto invalid=producerCamera;invalid.basis.forward={};draw(&invalid);
    Check(!ReadReference(entry,geometry,records,6,0,1,reference),"invalid producer camera cannot authorize a visual correction");
    auto changingCamera=producerCamera;mutateCamera=true;draw(&changingCamera);mutateCamera=false;
    Check(!ReadReference(entry,geometry,records,6,0,1,reference),"camera changed during native emission cannot certify queued corners");
    Check(Snapshot().drawScopes==0&&Snapshot().producerReads>0&&Snapshot().captured>0,
        "production correction remains active while separate tracer Draw detours/read scopes stay zero");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("Native tracer visual producer: %u checks, %u failures; pinned PE + CPU only.\n",checks,failures);return failures?1:0;
}
