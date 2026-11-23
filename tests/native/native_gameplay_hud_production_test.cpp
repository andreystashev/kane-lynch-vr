// Production class/owner/mode suppression with private CPU objects only.
#include "native_gameplay_hud_kl.cpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
}
namespace Fixture {unsigned checks=0,draws=0,reticles=0,mapDraws=0,weaponDraws=0,actionDraws=0,wheelDraws=0,compactBacking=0;
bool scene=true,held=false;
NativeHudKL::WristHudElement actionElement;
NativeHudKL::WristHudSource source;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
bool Scene(){return scene;}void __thiscall Draw(void*,void*,void*){++draws;}
bool Held(){return held;}
bool Ledge(std::uintptr_t player){return player!=0;}
const char* Reload(std::uintptr_t player){return player?"Left Grip  Rack bolt":nullptr;}
std::uintptr_t reloadLabel=0;
char shownAction[192]{};
bool __thiscall ActionText(void* box,const char* text){
    std::uint32_t storage=0;
    std::memcpy(&storage,static_cast<unsigned char*>(box)+0x94,sizeof(storage));
    if(!storage)return false;
    std::snprintf(reinterpret_cast<char*>(storage+12),180,"%s",text);
    return true;
}
char tutorialText[256]{};
std::uint32_t observedSpeechWidth=0;
float observedDuration=0.f;
void __thiscall Tutorial(void* box,const char* text,float duration){
    std::snprintf(tutorialText,sizeof(tutorialText),"%s",text);
    std::memcpy(&observedSpeechWidth,static_cast<const unsigned char*>(box)+0x838,sizeof(observedSpeechWidth));
    observedDuration=duration;
}
void __thiscall DrawMap(void*,void*,void*){++mapDraws;}
void __thiscall DrawWeapon(void* widget,void*,void*){
    std::memcpy(&compactBacking,static_cast<unsigned char*>(widget)+0x88,sizeof(compactBacking));++weaponDraws;
}
void __thiscall DrawInventory(void*,void*,void*){
    Check(source.element==NativeHudKL::WristHudElement::WeaponWheel&&source.player!=0,"expanded wheel only draws in its tracked scope");
    ++wheelDraws;
}
void __thiscall DrawAction(void*,void*,void*){
    actionElement=source.element;
    if(reloadLabel)NativeGameplayHudKL::ReadNativeText(reloadLabel,shownAction);
    Check(source.player!=0&&source.anchor.z==6.f,
          "native action emits under one wrist scope for text and pickup icon");
    ++actionDraws;
}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}}
namespace NativeHudKL {bool OriginalScopeOverlayActive(){return false;}bool ReticleEnabled(){return true;}
void BeginReticle(std::uintptr_t,void*){++Fixture::reticles;}void EndReticle(){}
void BeginNativeMarkers(std::uintptr_t,void*,void*){}void EndNativeMarkers(){}
bool WristHudReady(){return true;}
WristHudSource ReplaceWristHudSource(const WristHudSource& source){auto old=Fixture::source;Fixture::source=source;return old;}}
namespace NativeTeamHudKL {void DrawWrist(std::uintptr_t,std::uintptr_t,void*,void*){}}
int main(){using namespace Fixture;using namespace NativeGameplayHudKL;
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(image!=0,"allocate private HUD source");
    const auto data=image+0x10000,player=image+0x20000,display=image+0x30000;Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));Put(display,std::uint32_t(image+entries[0].vtable));Put(display+0x10,std::uint32_t(player));
    originals[0]=Fixture::Draw;sceneProvider=Scene;Put(image+0x8883A4,std::uint8_t(0));DrawHook<0>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);
    Check(draws==1&&reticles==1,"ordinary exact gameplay aiming still reaches original reticle producer");
    Put(image+0x8883A4,std::uint8_t(3));DrawHook<0>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);
    Check(draws==1&&reticles==1&&hidden[0]==1,"native weapon-wheel mode suppresses only exact local gameplay crosshair before reticle routing");
    Put(display+0x10,std::uint32_t(player+4));DrawHook<0>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);Check(draws==2,"foreign native display is preserved");Put(display+0x10,std::uint32_t(player));
    Put(image+0x8883A4,std::uint8_t(2));DrawHook<0>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);Check(draws==3,"native expanded-map/pause mode remains outside the inventory exception");
    Put(image+0x8883A4,std::uint8_t(3));scene=false;DrawHook<0>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);Check(draws==4,"outside immersive VR scene native drawing remains unchanged");
    Check(!Hide(Producer::Inventory,true,true,3)&&!Hide(Producer::ActionMarker,true,true,3),"native wheel/content and world icons are not hidden by the narrow aiming-sprite rule");
    constexpr auto direction=static_cast<unsigned>(Producer::Direction);
    constexpr auto inventory=static_cast<unsigned>(Producer::Inventory);
    constexpr auto minimap=static_cast<unsigned>(Producer::Minimap);
    const auto manager=image+0x40000,frontend=image+0x50000,map=image+0x60000,weapon=image+0x70000,layout=image+0x80000;
    Put(data+0x154C,std::uint32_t(manager));Put(player+0x804,std::uint8_t(0));
    Put(manager,std::uint32_t(frontend));Put(manager+20,std::uint8_t(1));
    Put(frontend,std::uint32_t(image+0x5755FC));Put(frontend+0x8F4,std::uint32_t(weapon));Put(frontend+0x8F8,std::uint32_t(map));
    Put(display,std::uint32_t(image+entries[direction].vtable));
    Put(map,std::uint32_t(image+entries[minimap].vtable));Put(map+0x10,std::uint32_t(player));
    Put(weapon,std::uint32_t(image+entries[inventory].vtable));Put(weapon+0x10,std::uint32_t(player));
    Put(map+0x6C+0x2CC,std::uint32_t(layout));Put(weapon+0x3CC+0x2CC,std::uint32_t(layout));
    Put(layout+8,6.f);
    scene=true;wristHudHeld=Held;originals[direction]=Fixture::Draw;originals[minimap]=DrawMap;compactStatusDraw=DrawWeapon;
    originals[inventory]=DrawInventory;compactBackingVerified=true;
    const auto resources=image+0x81000;
    Put(weapon+8,std::uint32_t(resources));Put(resources+0x1F8,std::uint32_t(42));Put(weapon+0x454,std::uint32_t(17));
    const auto drawDirection=[&](){DrawHook<direction>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);};
    const auto drawInventory=[&](){DrawHook<inventory>(reinterpret_cast<void*>(weapon),nullptr,nullptr,nullptr);};
    Put(image+0x8883A4,std::uint8_t(0));drawDirection();
    Check(!mapDraws&&!weaponDraws,"released Y leaves the native wrist widgets hidden");
    held=true;drawDirection();drawInventory();
    Check(!mapDraws&&!weaponDraws&&!wheelDraws,"Y request cannot flash compact widgets before the native wheel opens");
    Put(weapon+0xF3C+0x488,std::uint32_t(layout));
    Put(weapon+0x21,std::uint8_t(1));drawDirection();drawInventory();
    Check(!mapDraws&&!weaponDraws&&!wheelDraws,"an expanded flag without native inventory mode cannot show a partial panel");
    Put(image+0x8883A4,std::uint8_t(3));
    Put(weapon+0x21,std::uint8_t(0));drawInventory();
    Check(!mapDraws&&!weaponDraws&&!wheelDraws,"native inventory mode waits for its expanded display");
    Put(weapon+0x21,std::uint8_t(1));
    Put(weapon+0xF3C+0x488,std::uint32_t(0));drawInventory();
    Check(!mapDraws&&!weaponDraws&&!wheelDraws,"a wheel without layout cannot show the remaining widgets early");
    Put(weapon+0xF3C+0x488,std::uint32_t(layout));drawInventory();
    std::uint32_t restoredBacking=0;
    Check(mapDraws==1&&weaponDraws==1&&wheelDraws==1&&compactBacking==42&&
          Read(weapon+0x454,restoredBacking)&&restoredBacking==17,
          "the first ready wheel submits the map, weapon backing and wheel together");
    drawInventory();
    Check(mapDraws==2&&weaponDraws==2&&wheelDraws==2,"the next held view submits the same complete panel");
    Put(map+0x10,std::uint32_t(player+4));drawInventory();
    Check(mapDraws==2&&weaponDraws==3&&wheelDraws==3,"a foreign map cannot be submitted on the local wrist");
    held=false;drawInventory();
    Check(mapDraws==2&&weaponDraws==3&&wheelDraws==3,"release cannot return a still-expanded wheel to the monitor centre");
    Put(image+0x8883A4,std::uint8_t(0));
    constexpr auto action=static_cast<unsigned>(Producer::UseAction);
    Put(display,std::uint32_t(image+entries[action].vtable));Put(display+0xC,std::uint32_t(layout));
    Put(display+0x128,std::uint8_t(1));originals[action]=DrawAction;held=false;
    DrawHook<action>(reinterpret_cast<void*>(display),nullptr,reinterpret_cast<void*>(layout),nullptr);
    Check(actionDraws==1&&actionElement==NativeHudKL::WristHudElement::Action&&!source.player,
          "ordinary X appears at the left-hand action scope without holding Y");
    held=true;DrawHook<action>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);
    Check(actionDraws==1,"the team panel takes precedence over the context action");
    held=false;SetGameHintsEnabled(false);DrawHook<action>(reinterpret_cast<void*>(display),nullptr,nullptr,nullptr);
    Check(actionDraws==1,"the game hints setting hides contextual actions");
    SetGameHintsEnabled(true);SetLedgeProvider(Ledge);
    Put(display+0x128,std::uint16_t(0x2600));Put(display+0x120,std::uint32_t(42));
    DrawHook<action>(reinterpret_cast<void*>(display),nullptr,reinterpret_cast<void*>(layout),nullptr);
    std::uint16_t visibility=0;std::uint32_t storedWeapon=0;
    Check(actionDraws==2&&actionElement==NativeHudKL::WristHudElement::ClimbAction&&!source.player&&
          Read(display+0x128,visibility)&&visibility==0x2600&&
          Read(display+0x120,storedWeapon)&&storedWeapon==42,
          "a climb candidate reuses the stock action producer and restores its inactive fields");
    SetLedgeProvider(nullptr);SetReloadActionProvider(Reload);setNativeText=ActionText;
    reloadLabel=image+0x92000;const auto outline=image+0x92200;
    const auto labelText=image+0x92400,outlineText=image+0x92500;
    Put(display+0x1C,std::uint32_t(reloadLabel));Put(display+0x20,std::uint32_t(outline));
    Put(reloadLabel,std::uint32_t(image+0x558414));Put(outline,std::uint32_t(image+0x558414));
    Put(reloadLabel+0x94,std::uint32_t(labelText));Put(outline+0x94,std::uint32_t(outlineText));
    Put(labelText,std::uint16_t(192));Put(outlineText,std::uint16_t(192));
    DrawHook<action>(reinterpret_cast<void*>(display),nullptr,reinterpret_cast<void*>(layout),nullptr);
    char after[192]{};
    Check(actionDraws==3&&actionElement==NativeHudKL::WristHudElement::ClimbAction&&
          !std::strcmp(shownAction,"Left Grip  Rack bolt")&&ReadNativeText(reloadLabel,after)&&!after[0]&&
          Read(display+0x128,visibility)&&visibility==0x2600,
          "a reload label is rendered and restored by the original right-wrist text widget");
    Put(display,std::uint32_t(image+0x575A4C));Put(frontend+0x8FC,std::uint32_t(display));
    Put(image+0x888104,std::uint32_t(image+0x90000));
    originalTutorial=Tutorial;SetTutorialBindingsActive(Scene);SetGameHintsEnabled(true);
    constexpr char prompt[]="Use {TapUse} and {TapCycleWeapon} {objective}";
    SetTutorialHook(reinterpret_cast<void*>(display+0x1C),nullptr,prompt,2.f);
    Check(!std::strcmp(tutorialText,"Use \xEE\x84\x80" "X" "\xEE\x84\x80 and \xEE\x84\x80" "Y" "\xEE\x84\x80 {objective}"),
          "the real tutorial setter maps controller inputs and retains non-button markup");
    Put(image+0x888104,std::uint32_t(display));
    SetTutorialHook(reinterpret_cast<void*>(display+0x1C),nullptr,prompt,2.f);
    Check(!std::strcmp(tutorialText,prompt),"speech on the same native class keeps the untouched text");
    const auto subtitleBox=display+0x1C;
    const auto wrapWidth=subtitleBox+0x838;
    Put(wrapWidth,std::uint32_t(640));
    subtitleWidthVerified=true;SetReadableSubtitlesActive(Scene);SetGameHintsEnabled(false);
    constexpr char speechLine[]="An extended subtitle keeps its original words, timing, UTF-8, and authored\nline break.";
    SetTutorialHook(reinterpret_cast<void*>(subtitleBox),nullptr,speechLine,4.5f);
    std::uint32_t restoredWidth=0;
    Check(observedSpeechWidth==400&&!std::strcmp(tutorialText,speechLine)&&observedDuration==4.5f&&
          Read(wrapWidth,restoredWidth)&&restoredWidth==640,
          "owned subtitles pass their original text and timing through the narrower native wrapper and restore the width");
    scene=false;SetTutorialHook(reinterpret_cast<void*>(subtitleBox),nullptr,speechLine,4.5f);
    Check(observedSpeechWidth==640,"desktop speech retains the authored width");
    scene=true;Put(image+0x888104,std::uint32_t(image+0x90000));
    SetTutorialHook(reinterpret_cast<void*>(subtitleBox),nullptr,speechLine,4.5f);
    Check(observedSpeechWidth==640,"a different display is outside the speech layout change");
    SetGameHintsEnabled(true);
    Put(display,std::uint32_t(image+0x575C3C));Put(frontend+0x90C,std::uint32_t(display));
    Put(image+0x888104,std::uint32_t(image+0x90000));
    char adapted[2048];
    Check(AdaptInstruction(display,Producer::BriefingAdvice,"Hold {IngameMapActive} {objective}",adapted)&&
          !std::strcmp(adapted,"Hold \xEE\x84\x80" "Y" "\xEE\x84\x80 {objective}"),
          "briefing resolves the VR action before native localization expands the remaining objective icon");
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);std::printf("Native gameplay HUD mode/source producer: %u checks PASS.\n",checks);
}
