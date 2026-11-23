#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_gameplay_hud_kl.cpp"
#include <cstdio>
#include <cstdlib>
namespace NativeHudKL {
bool OriginalScopeOverlayActive(){return false;}bool ReticleEnabled(){return false;}
void BeginNativeMarkers(std::uintptr_t,void*,void*){}void EndNativeMarkers(){}
void BeginReticle(std::uintptr_t,void*){}void EndReticle(){}
bool WristHudReady(){return false;}
WristHudSource ReplaceWristHudSource(const WristHudSource& source){static WristHudSource current;const auto previous=current;current=source;return previous;}
}
namespace NativeTeamHudKL {void DrawWrist(std::uintptr_t,std::uintptr_t,void*,void*){}}
namespace {
unsigned checks=0,draws=0;bool scene=true;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void __thiscall NativeDraw(void*,void*,void*){++draws;}
}
int main(){using namespace NativeGameplayHudKL;constexpr std::uintptr_t base=0x30000000;
 auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 Check(memory==reinterpret_cast<void*>(base),"allocate private native-layout memory");image=base;
 constexpr std::uint32_t data=base+0x10000,player=base+0x20000,manager=base+0x30000,frontend=base+0x40000,
     text=base+0x50000,briefing=base+0x60000,speech=base+0x70000,use=base+0x80000;
 const auto setup=[&]{Put(base+0x68760C,data);Put(data+0xA2C,player);Put(player,std::uint32_t(base+0x56582C));Put(player+0x804,std::int8_t(0));
  Put(base+0x8883A4,std::uint8_t(0));Put(data+0x154C,manager);Put(manager,frontend);Put(manager+8,std::int8_t(0));Put(manager+20,std::int8_t(-1));
  Put(frontend,std::uint32_t(base+0x5755FC));Put(frontend+0x8FC,text);Put(frontend+0x90C,briefing);Put(base+0x888104,speech);
  Put(text,std::uint32_t(base+0x575A4C));Put(text+0x10,player);Put(briefing,std::uint32_t(base+0x575C3C));Put(briefing+0x10,player);
  Put(speech,std::uint32_t(base+0x575A4C));Put(speech+0x10,std::uint32_t(0));Put(use,std::uint32_t(base+0x5759C4));Put(use+0x10,player);};
 sceneProvider=[](){return scene;};originals[6]=NativeDraw;originals[7]=NativeDraw;originals[4]=NativeDraw;setup();
 for(auto mode:{std::uint8_t(0),std::uint8_t(3)}){
  Put(base+0x8883A4,mode);const auto before=draws;
  SetGameHintsEnabled(false);
  DrawHook<6>(reinterpret_cast<void*>(text),nullptr,nullptr,nullptr);DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);DrawHook<4>(reinterpret_cast<void*>(use),nullptr,nullptr,nullptr);
  Check(draws==before,"disabled hints suppress owned tutorial, briefing advice and keyboard-use draws in gameplay/inventory");
  SetGameHintsEnabled(true);
  DrawHook<6>(reinterpret_cast<void*>(text),nullptr,nullptr,nullptr);DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);
  Check(draws==before+2,"enabled hints preserve both original owned instruction producers");
  DrawHook<4>(reinterpret_cast<void*>(use),nullptr,nullptr,nullptr);
  Check(draws==before+2,"native keyboard-use label cannot fall back to the screen outside its wrist route");
 }
 SetGameHintsEnabled(false);
 setup();const auto before=draws;DrawHook<6>(reinterpret_cast<void*>(speech),nullptr,nullptr,nullptr);
 Check(draws==before+1,"global speech subtitles preserve their original draw");
 Put(frontend+0x90C,std::uint32_t(0));DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);
 Check(draws==before+2,"unowned briefing objects retain native drawing");
 setup();scene=false;DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);Check(draws==before+3,"menus and unproved VR scenes keep native drawing");scene=true;
 setup();Put(base+0x8883A4,std::uint8_t(1));DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);Check(draws==before+4,"native pause mode is not mistaken for gameplay advice");
 setup();Put(briefing+0x10,player+4);DrawHook<7>(reinterpret_cast<void*>(briefing),nullptr,nullptr,nullptr);Check(draws==before+5,"another player's UI is never hidden");
 VirtualFree(memory,0,MEM_RELEASE);std::printf("Legacy hints actual producers: %u checks PASS.\n",checks);
}
