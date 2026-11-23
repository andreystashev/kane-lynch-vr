// Actual native constructor/position, count/index callsites and production
// presentation code in private CPU arenas. No game, runtime, GPU or input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace GoalFixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount GoalFixture::Clock
#include "native_objective_visibility_kl.cpp"
#undef GetTickCount
#include "native_hud_kl.cpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
}
namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return true;}}
namespace GoalFixture {
using namespace NativeObjectiveVisibilityKL;using NativeCameraMath::Vec3;
unsigned checks=0;std::uintptr_t data=0,player=0,list=0,live=0,goals=0,slot=0,entity=0,geometry=0,camera=0;
LONG WINAPI Exception(EXCEPTION_POINTERS* p){std::printf("private goal exception=%lx pc=%p fault=%p checks=%u\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,reinterpret_cast<void*>(p->ExceptionRecord->ExceptionInformation[1]),checks);auto* stack=reinterpret_cast<unsigned*>(p->ContextRecord->Esp);for(unsigned i=0;i<8;++i)std::printf("stack%u=%08x\n",i,stack[i]);return EXCEPTION_CONTINUE_SEARCH;}
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Allocate(std::size_t bytes,DWORD access=PAGE_READWRITE){const auto at=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,access));Check(at!=0,"allocate private fixture memory");return at;}
void Jump(std::uintptr_t at,std::uintptr_t destination){Put(at,std::uint8_t(0xE9));Put(at+1,std::int32_t(destination-at-5));}
bool __thiscall False(void*){return false;}unsigned __thiscall EntityClass(void*){return 0;}
void __thiscall World(void* object,Vec3* point){std::uint32_t geometry=0;Read(reinterpret_cast<std::uintptr_t>(object)+4,&geometry,4);NativePlayerPose::CameraPose pose;
    Check(NativePlayerPose::World(geometry,Read,pose),"native position requests the certified target's actual reciprocal geometry");*point=NativeCameraMath::Add(NativePlayerPose::Transform(pose.basis,*point),pose.position);}
std::uint32_t __thiscall Colour(void* object,void*){const auto at=reinterpret_cast<std::uintptr_t>(object);std::uint32_t colour=0;std::uint8_t timed=0;float remaining=0,duration=0;
    Read(at+0x48,&colour,4);Read(at+0x4C,&timed,1);if(timed){Read(at+0x24,&remaining,4);Read(at+0x2C,&duration,4);const auto alpha=duration>0?unsigned((colour>>24)*std::max(0.f,std::min(1.f,remaining/duration))):0;colour=(colour&0xFFFFFF)|(alpha<<24);}return colour;}
using CallCount=unsigned(__thiscall*)(void*);using CallIndex=void*(__thiscall*)(void*,unsigned);using CallColour=unsigned(__stdcall*)(void*,void*);
CallCount countCall=nullptr;CallIndex indexCall=nullptr;CallColour colourCall=nullptr;
void Begin(bool held){SetContext(true,held);NativeHudKL::BeginNativeMarkers(player,reinterpret_cast<void*>(list),reinterpret_cast<void*>(camera));}void End(){NativeHudKL::EndNativeMarkers();}
}
int main(int argc,char** argv){using namespace GoalFixture;using namespace NativeObjectiveVisibilityKL;
    std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,Exception);
    if(argc<2){std::puts("SKIP provide pinned EXE read-only.");return 0;}NativeHudFixture::Image exe;
    Check(exe.Load(argv[1])&&Verify(NativeHudFixture::Base,exe.Reader()),"actual Objective26/resource/goal availability and all hooked native boundaries verify");
    image=Allocate(0x900000,PAGE_EXECUTE_READWRITE);data=Allocate(0x2000);player=Allocate(0x2000);list=Allocate(0x20);live=Allocate(0x100);goals=Allocate(0x1400);entity=Allocate(0x100);geometry=Allocate(0x100);camera=Allocate(0x100);slot=goals+4;
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));Put(player+0x804,std::int8_t(0));Put(player+0x805,std::int8_t(0));Put(player+0x9A0,std::uint32_t(camera));
    Put(data+0x1550,std::uint32_t(list));Put(list+0x14,std::uint32_t(live));Put(data+0x1554,std::uint32_t(goals));Put(goals+0x1384,std::uint32_t(1));Put(goals+0x12C4,std::uint32_t(slot));
    Put(slot,std::uint32_t(entity));Put(slot+0x58,std::uint32_t(26));Put(slot+0x18,std::uint32_t(0));Put(slot+0x2C,std::uint8_t(1));
    Put(entity,std::uint32_t(image+0x540000));Put(image+0x540030,std::uint32_t(image+0x2000));Jump(image+0x2000,reinterpret_cast<std::uintptr_t>(EntityClass));Put(entity+4,std::uint32_t(geometry));
    NativePlayerPose::Geometry pose;pose.pose.position={100,20,300};Put(geometry,pose);Put(geometry+0x60,std::uint32_t(entity));
    for(auto span:std::array<std::array<unsigned,2>,2>{{{0x252F50,0x89},{0x253210,0xFC}}})Check(exe.Read(NativeHudFixture::Base+span[0],reinterpret_cast<void*>(image+span[0]),span[1]),"copy exact native constructor or position-update body");
    for(auto at:{0x252FB8u,0x253236u,0x25323Cu,0x253251u,0x253257u,0x25327Du}){std::uint32_t original=0;exe.Read(NativeHudFixture::Base+at,&original,4);Put(image+at,std::uint32_t(image+original-NativeHudFixture::Base));}
    Put(image+0x545CB8,30.f);Put(image+0x885CAC,std::uint32_t(1));Put(image+0x8860B8,std::uint32_t(1));
    Jump(image+0x66CC0,reinterpret_cast<std::uintptr_t>(False));Jump(image+0x35EF00,reinterpret_cast<std::uintptr_t>(False));Jump(image+0xF8320,reinterpret_cast<std::uintptr_t>(World));
    constructor=reinterpret_cast<Constructor>(image+0x252F50);positionUpdate=reinterpret_cast<PositionUpdate>(image+0x253210);originalColor=Colour;
    const auto countOriginal=Allocate(0x100,PAGE_EXECUTE_READWRITE),indexOriginal=Allocate(0x100,PAGE_EXECUTE_READWRITE);
    exe.Read(NativeHudFixture::Base+0x12F500,reinterpret_cast<void*>(countOriginal),4);exe.Read(NativeHudFixture::Base+0x254180,reinterpret_cast<void*>(indexOriginal),23);
    originalCount=reinterpret_cast<Count>(countOriginal);originalIndex=reinterpret_cast<Index>(indexOriginal);Jump(image+0x12F500,reinterpret_cast<std::uintptr_t>(CountHook));Jump(image+0x254180,reinterpret_cast<std::uintptr_t>(IndexHook));Jump(image+ColorRva,reinterpret_cast<std::uintptr_t>(ColorHook));
    exe.Read(NativeHudFixture::Base+0x253BCA,reinterpret_cast<void*>(image+0x253BCA),5);Put(image+0x253BCF,std::uint8_t(0xC3));countCall=reinterpret_cast<CallCount>(image+0x253BCA);
    const unsigned char indexPush[]={0xFF,0x74,0x24,4};std::memcpy(reinterpret_cast<void*>(image+0x253C1E),indexPush,4);exe.Read(NativeHudFixture::Base+0x253C22,reinterpret_cast<void*>(image+0x253C22),5);const unsigned char indexRet[]={0xC2,4,0};std::memcpy(reinterpret_cast<void*>(image+0x253C27),indexRet,3);indexCall=reinterpret_cast<CallIndex>(image+0x253C1E);
    const unsigned char colourEntry[]={0x57,0x8B,0x7C,0x24,8,0x8B,0x44,0x24,0x0C};std::memcpy(reinterpret_cast<void*>(image+0x253C2D),colourEntry,9);exe.Read(NativeHudFixture::Base+0x253C36,reinterpret_cast<void*>(image+0x253C36),8);const unsigned char colourLeave[]={0x5F,0xC2,8,0};std::memcpy(reinterpret_cast<void*>(image+0x253C3E),colourLeave,4);colourCall=reinterpret_cast<CallColour>(image+0x253C2D);
    installed=true;NativeHudKL::SetNativeMarkerObserver(Observe);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x900000);
    std::array<unsigned char,0x1400> goalBefore{};std::array<unsigned char,0x100> actorBefore{},geometryBefore{},liveBefore{};Read(goals,goalBefore.data(),goalBefore.size());Read(entity,actorBefore.data(),actorBefore.size());Read(geometry,geometryBefore.data(),geometryBefore.size());Read(live,liveBefore.data(),liveBefore.size());
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"actual Count producer reveals active native objective even with ZERO faded render records");auto* held=indexCall(reinterpret_cast<void*>(list),0);
    Check(CountHook(reinterpret_cast<void*>(list),nullptr)==0&&CurrentView()->valid&&indexCall(reinterpret_cast<void*>(list),0)==held,
        "another shared Count getter inside native drawing cannot invalidate the private marker sequence");
    Check(held!=reinterpret_cast<void*>(live)&&PrivateRecord(CurrentView(),reinterpret_cast<std::uintptr_t>(held)),"actual native Index caller receives only scoped private presentation record");
    Vec3 target{};Read(reinterpret_cast<std::uintptr_t>(held)+4,&target,sizeof(target));Check(target.x==100&&target.y==50&&target.z==300,"actual native ctor and position body preserve target WORLD plus original30-unit offset");
    Check(colourCall(held,reinterpret_cast<void*>(camera))==0xFF00CC66,"actual native Color producer uses the original green Objective atlas colour");End();
    Check(!std::memcmp(goalBefore.data(),reinterpret_cast<void*>(goals),goalBefore.size())&&!std::memcmp(actorBefore.data(),reinterpret_cast<void*>(entity),actorBefore.size())&&!std::memcmp(geometryBefore.data(),reinterpret_cast<void*>(geometry),geometryBefore.size())&&!std::memcmp(liveBefore.data(),reinterpret_cast<void*>(live),liveBefore.size()),"revealing the goal changes no mission slots, source actor/geometry or native render list");
    Begin(false);Check(countCall(reinterpret_cast<void*>(list))==0,"release drops only forced presentation and does not enter map or persist a goal record");End();
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"a later Y hold reconstructs the native objective AGAIN after prior view is gone");End();
    constructor(reinterpret_cast<void*>(live),reinterpret_cast<void*>(entity),26,6.f,0);Put(live+0x48,std::uint32_t(0xFF00CC66));Put(live+0x24,3.f);Put(list+8,std::uint32_t(1));
    Put(live+0x50,75.f);Put(live+0x3C,2.5f);positionUpdate(reinterpret_cast<void*>(live));
    std::array<unsigned char,0x64> authored{};Read(live,authored.data(),authored.size());
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"existing native brief and forced view deduplicate the same exact objective entity");End();
    for(unsigned eye=0;eye<2;++eye){Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"each eye gets one canonical active goal");auto* copy=indexCall(reinterpret_cast<void*>(list),0);Vec3 point;float offset=0,scale=0;
        Read(reinterpret_cast<std::uintptr_t>(copy)+4,&point,sizeof(point));Read(reinterpret_cast<std::uintptr_t>(copy)+0x50,&offset,4);Read(reinterpret_cast<std::uintptr_t>(copy)+0x3C,&scale,4);
        Check(point.x==100&&point.y==95&&offset==75&&scale==2.5f,"both eye sources preserve actual live authored75-unit height/scale instead of resetting constructor30");End();}
    const auto foreignEntity=Allocate(0x100),foreignGeometry=Allocate(0x100);Put(foreignEntity,std::uint32_t(image+0x540000));Put(foreignEntity+4,std::uint32_t(foreignGeometry));NativePlayerPose::Geometry foreignPose;foreignPose.pose.position={900,20,600};Put(foreignGeometry,foreignPose);Put(foreignGeometry+0x60,std::uint32_t(foreignEntity));
    constructor(reinterpret_cast<void*>(live+0x64),reinterpret_cast<void*>(foreignEntity),26,6.f,0);positionUpdate(reinterpret_cast<void*>(live+0x64));Put(list+8,std::uint32_t(2));
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"one active goal suppresses a second stale/other native Objective26 only in the held presentation");End();
    Begin(false);Check(countCall(reinterpret_cast<void*>(list))==2&&indexCall(reinterpret_cast<void*>(list),1)==reinterpret_cast<void*>(live+0x64),"release exposes the complete untouched native brief list including the other fading goal");End();
    Put(slot+0x2C,std::uint8_t(0));Begin(true);Check(countCall(reinterpret_cast<void*>(list))==2&&indexCall(reinterpret_cast<void*>(list),0)==reinterpret_cast<void*>(live),"without an active mission goal, held X neither fabricates a clone nor suppresses native brief records");End();Put(slot+0x2C,std::uint8_t(1));
    Check(!std::memcmp(authored.data(),reinterpret_cast<void*>(live),authored.size()),"canonical presentation changes no source height, scale, timers or native record bytes");Put(list+8,std::uint32_t(1));
    Begin(false);Check(countCall(reinterpret_cast<void*>(list))==1&&indexCall(reinterpret_cast<void*>(list),0)==reinterpret_cast<void*>(live)&&colourCall(reinterpret_cast<void*>(live),reinterpret_cast<void*>(camera))==0x7F00CC66,"release preserves the current original timed brief instead of hiding it or extending its deadline");End();
    Put(list+8,std::uint32_t(0));pose.pose.position.x=150;Put(geometry,pose);Put(geometry+0x60,std::uint32_t(entity));
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"held reveal remains available after every native render record fades");auto* styled=indexCall(reinterpret_cast<void*>(list),0);Vec3 movedPoint;Read(reinterpret_cast<std::uintptr_t>(styled)+4,&movedPoint,sizeof(movedPoint));
    Check(movedPoint.x==150&&movedPoint.y==95,"fade recovery recomputes current native WORLD point while retaining authored height; no stale point cache");End();
    Put(list+8,std::uint32_t(0));Put(slot+0x2C,std::uint8_t(0));Begin(true);Check(countCall(reinterpret_cast<void*>(list))==0,"completed/disabled mission objective cannot be revived by X");End();Put(slot+0x2C,std::uint8_t(1));
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"fresh active goal is eligible again");held=indexCall(reinterpret_cast<void*>(list),0);SetContext(true,false);Check(colourCall(held,reinterpret_cast<void*>(camera))==0,"release during a draw cancels private goal alpha immediately");End();
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"held context obtains a new producer generation");held=indexCall(reinterpret_cast<void*>(list),0);SetContext(true,false);SetContext(true,true);
    Check(colourCall(held,reinterpret_cast<void*>(camera))==0,"release and rehold cannot revive the previous generation inside the same native draw");End();
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"new draw gets a fresh native availability snapshot");held=indexCall(reinterpret_cast<void*>(list),0);Put(slot,std::uint32_t(0));Check(colourCall(held,reinterpret_cast<void*>(camera))==0,"retired source goal invalidates its scoped clone before native colour evaluation");End();
    Put(slot,std::uint32_t(entity));Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"listed active goal gets its own new scope");held=indexCall(reinterpret_cast<void*>(list),0);Put(goals+0x1384,std::uint32_t(0));
    Check(colourCall(held,reinterpret_cast<void*>(camera))==0,"removing a pointer from the native active list cancels its clone even if old slot bytes remain");End();Put(goals+0x1384,std::uint32_t(1));
    Begin(true);Check(countCall(reinterpret_cast<void*>(list))==1,"outer draw owns an augmented native Count");held=indexCall(reinterpret_cast<void*>(list),0);
    NativeHudKL::BeginNativeMarkers(0,nullptr,nullptr);NativeHudKL::EndNativeMarkers();
    Check(CurrentView()&&CurrentView()->valid&&indexCall(reinterpret_cast<void*>(list),0)==held,"invalid nested producer preserves the outer augmented Count/Index sequence");
    for(unsigned i=0;i<8;++i)NativeHudKL::BeginNativeMarkers(player,nullptr,nullptr);
    Check(depth==9&&!CurrentView(),"ninth nested producer has no private authority but retains balanced stack depth");
    for(unsigned i=0;i<8;++i)NativeHudKL::EndNativeMarkers();
    Check(depth==1&&CurrentView()->valid&&indexCall(reinterpret_cast<void*>(list),0)==held,"overflow nested draw cannot redirect the outer augmented index to the native out-of-range assert");
    NativeHudKL::InvalidateDevice();Check(!CurrentView()&&!depth,"explicit reset drops every nested private presentation authority");
    auto* cancelled=indexCall(reinterpret_cast<void*>(list),0);Check(cancelled==cancelledRecord.data()&&colourCall(cancelled,reinterpret_cast<void*>(camera))==0,"Reset after augmented Count returns a safe invisible sentinel instead of out-of-range native Index");
    const auto filterCode=Allocate(0x100,PAGE_EXECUTE_READWRITE);exe.Read(NativeHudFixture::Base+0x2531C0,reinterpret_cast<void*>(filterCode),0x40);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(filterCode),0x100);
    const auto filter=reinterpret_cast<bool(__thiscall*)(void*,unsigned)>(filterCode);
    Check(!filter(cancelled,0)&&!filter(cancelled,1),"actual native filter rejects cancelled sentinel for both player indices before following an entity pointer");
    Check(countCall(reinterpret_cast<void*>(list))==0,"foreign draw outside owned ActionMarkers scope never receives appended records");
    std::printf("Native held-goal producer: %u checks PASS.\n",checks);
}
