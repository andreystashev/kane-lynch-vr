#pragma once
#include <algorithm>
#include <cstdint>
#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
namespace NativeGameplayHudKL {
enum class Producer : unsigned {Crosshair,Direction,ActionMarker,Inventory,UseAction,Minimap,TutorialText,BriefingAdvice,Count};
inline bool FormatWristAction(std::string_view label,bool weapon,char* output,std::size_t capacity,bool climb=false){
    using namespace std::literals;
    constexpr auto accent="\xEE\x84\x80"sv;
    const auto button=climb?"\xEE\x84\x80" "B" "\xEE\x84\x80" " (hold)"sv:"\xEE\x84\x80" "X" "\xEE\x84\x80"sv;
    const auto trim=[](std::string_view& text){text.remove_prefix(std::min(text.find_first_not_of(" \t"),text.size()));};
    trim(label);
    if(weapon)label={};  // The native atlas follows the measured width of the button.
    else if(label.substr(0,accent.size())==accent){
        const auto end=label.find(accent,accent.size());
        if(end!=label.npos)label.remove_prefix(end+accent.size());
    }else if(label.size()>=3&&static_cast<unsigned char>(label[0])==0xEE&&
             static_cast<unsigned char>(label[1])>=0x80&&static_cast<unsigned char>(label[1])<=0x83){
        // The localized gamepad binding may arrive as a private-use glyph.
        label.remove_prefix(3);
    }
    trim(label);
    const std::size_t gap=label.empty()?0:1;
    const auto size=button.size()+gap+label.size();
    if(!output||size>=capacity)return false;
    std::memcpy(output,button.data(),button.size());
    if(gap){output[button.size()]=' ';std::memcpy(output+button.size()+1,label.data(),label.size());}
    output[size]=0;
    return true;
}
inline bool TutorialBindings(std::string_view text,char* output,std::size_t capacity){
    using namespace std::literals;
    if(!output||!capacity||text.size()>=capacity)return false;
    struct Binding {std::string_view token,label;};
    constexpr Binding bindings[]{
        {"TapUse","X"},{"HoldUse","X"},{"TapReload","A"},{"HoldReload","A"},
        {"TapCycleWeapon","Y"},{"InventoryWheelActive","Y"},{"IngameMapActive","Y"},
        {"ActivateMenu","Menu"},{"HoldCrouch","B"},{"HoldSprint","A (hold)"},
        {"HoldFire","Right Trigger"},{"HoldGrenade","Right Grip"},
        {"HoldAttack","Y (hold) + Right Trigger"},{"HoldDefend","Y (hold) + B"},{"HoldFollow","Y (hold) + A"},
        {"PrevCommandReceiver","Y (hold) + Right Grip"},{"NextCommandReceiver","Y (hold) + Right Grip"},
        {"AnalogRightX","Right Stick"},{"WheelLeft","Right Stick Left"},{"WheelRight","Right Stick Right"},
        {"WheelUp","Right Stick Up"},{"WheelDown","Right Stick Down"}
    };
    const bool inventory=text.find("{InventoryWheelActive}")!=text.npos;
    const bool climb=text.find("climb")!=text.npos||text.find("Climb")!=text.npos;
    // The original training strings for mouse aiming and automatic cover
    // describe actions with different controls here.
    std::string_view replacement;
    if(text.find("{PrevCommandReceiver}")!=text.npos)
        replacement="Hold Y and press the Right Grip to cycle through teammates.";
    else if(text.find("{HoldPrecisionAim} to aim while in cover")!=text.npos)
        replacement="Aim the weapon by moving the right controller.";
    else if(text.find("{HoldCrouch} to crouch.")!=text.npos)
        replacement="Tap B to crouch or stand.";
    else if(text.find("you will snap into cover")!=text.npos||text.find("You will snap into suitable cover")!=text.npos)
        replacement="Use the stick to move behind cover. Tap B to crouch.";
    else if(text.find("{HoldFire} when you are in cover")!=text.npos)
        replacement="Aim around the corner and pull the Right Trigger.";
    else if(text.find("{HoldGrenade} to throw grenades")!=text.npos)
        replacement="Hold the Right Grip to ready a grenade. Swing and release the grip to throw.";
    if(!replacement.empty()){
        if(replacement.size()>=capacity)return false;
        std::memcpy(output,replacement.data(),replacement.size());output[replacement.size()]=0;
        return true;
    }

    constexpr auto accent="\xEE\x84\x80"sv;
    std::size_t size=0;
    const auto append=[&](std::string_view chunk){
        if(chunk.size()>=capacity-size)return false;
        std::memcpy(output+size,chunk.data(),chunk.size());size+=chunk.size();return true;
    };
    bool changed=false;
    while(!text.empty()){
        const auto open=text.find('{');
        if(open==text.npos){if(!append(text))return false;break;}
        const auto close=text.find('}',open+1);
        if(close==text.npos){if(!append(text))return false;break;}
        if(!append(text.substr(0,open)))return false;
        auto token=text.substr(open+1,close-open-1);
        std::string_view label;
        for(const auto& binding:bindings)if(token==binding.token){label=binding.label;break;}
        if(climb&&(token=="TapUse"||token=="HoldUse"))label="B (hold)";
        if(token=="HoldMoveForward")label=inventory?"Right Stick Up":"Left Stick Up";
        if(token=="HoldMoveBackward")label=inventory?"Right Stick Down":"Left Stick Down";
        if(token=="HoldDefend"&&text.substr(close+1).find(" to skip")==0)label="A";
        if(label.empty()){
            if(!append(text.substr(open,close-open+1)))return false;
        }else{
            if(!append(accent)||!append(label)||!append(accent))return false;
            changed=true;
        }
        text.remove_prefix(close+1);
    }
    output[size]=0;
    return changed;
}
inline bool Hide(Producer producer,bool vrScene,bool localOwner,
                 unsigned nativeMode=0,unsigned minimapState=~0u,bool exactMinimap=false){
    // Native mode 2 selects the expanded map; mode 3 selects inventory.
    // Neither is necessarily represented by the generic scene/menu test.
    if(!vrScene||!localOwner)return false;
    const bool action=producer==Producer::UseAction;
    // Weapon-wheel mode3 retains its native wheel, but a gameplay aiming
    // sprite has no purpose there and must not become a doubled white dot.
    if(nativeMode!=0)return nativeMode==3&&(action||producer==Producer::Crosshair);
    if(producer==Producer::Inventory)return true;
    if(action)return true; // The live native action is routed to the left wrist.
    // Preserve the game's real world markers and their authored resources.
    // These include order icons, not the old roster or minimap.
    if(producer==Producer::ActionMarker)return false;
    // +14 is the map display's state, including the ordinary gameplay
    // transitions. The expanded map is selected by native mode 2 above.
    if(producer==Producer::Minimap)return exactMinimap&&minimapState<=3;
    return producer==Producer::Crosshair;
}
using SceneProvider=bool(*)();
using LedgeProvider=bool(*)(std::uintptr_t);
using LogCallback=void(*)(const char*);
bool Install(SceneProvider scene,LogCallback log);
void SetImmersiveEnabled(bool enabled);
void SetWristHudHeld(SceneProvider held);
void SetLedgeProvider(LedgeProvider available);
using ReloadActionProvider=const char*(*)(std::uintptr_t);
void SetReloadActionProvider(ReloadActionProvider action);
void SetGameHintsEnabled(bool enabled);
void SetTutorialBindingsActive(SceneProvider enabled);
void SetReadableSubtitlesActive(SceneProvider enabled);
void PublishMapView(std::uintptr_t player,float forwardX,float forwardZ);
// 0: original Default dot. 1: original weapon/MountedMG crosshair. Changes
// presentation only; no native precision/FOV/aim-state writes.
void SetReticleStyle(unsigned style);
struct Stats {bool installed=false;std::array<std::uint64_t,static_cast<unsigned>(Producer::Count)> calls{},hidden{};
    std::uint64_t reticleStyled=0,reticleStyleRejected=0,tutorialBindings=0,subtitleLayouts=0,personalWheel=0,exchangeWheel=0;};
Stats Snapshot();

// ZTextDisplay is also used for spoken subtitles. Only frontend+8FC is the
// local script instruction channel; the separately allocated C88104 speech
// object and every unproven display retain their native drawing.
template<class Reader> bool OwnedTextChannel(std::uintptr_t image,std::uintptr_t display,std::uintptr_t expected,Reader read,
                                            unsigned field,std::uintptr_t displayVtable) {
    const auto get=[&](std::uintptr_t at,auto& value){return at>=0x10000u&&at<=0xffffffffu-sizeof(value)&&read(at,&value,sizeof(value));};
    std::uint32_t data=0,player=0,vt=0,owner=0,manager=0,frontend=0,text=0,speech=0;
    std::int8_t index=-1;unsigned char mode=0xff;unsigned selected=2;
    if((field!=0x8FC&&field!=0x90C)||!get(image+0x8883A4,mode)||(mode!=0&&mode!=3)||!get(display,vt)||vt!=image+displayVtable||!get(display+0x10,owner)||owner!=expected||
       !get(image+0x68760C,data)||!get(data+0xA2C,player)||player!=expected||!get(player,vt)||vt!=image+0x56582C||
       !get(player+0x804,index)||index<0||index>1||!get(data+0x154C,manager)||!get(image+0x888104,speech)||speech==display)return false;
    for(unsigned slot=0;slot<2;++slot){std::int8_t id=-1;if(!get(manager+slot*12+8,id))return false;
        if(id==index){if(frontend||!get(manager+slot*12,frontend)||!frontend)return false;selected=slot;}}
    if(!frontend||!get(frontend,vt)||vt!=image+0x5755FC||!get(frontend+field,text)||text!=display)return false;
    std::uint32_t afterData=0,afterPlayer=0,afterManager=0,afterText=0,afterSpeech=0,afterOwner=0,afterFrontend=0;
    std::int8_t afterIndex=-1,afterSlot=-1;unsigned char afterMode=0xff;
    return get(image+0x68760C,afterData)&&afterData==data&&get(data+0xA2C,afterPlayer)&&afterPlayer==player&&
        get(data+0x154C,afterManager)&&afterManager==manager&&get(frontend+field,afterText)&&afterText==display&&
        get(image+0x888104,afterSpeech)&&afterSpeech==speech&&get(display+0x10,afterOwner)&&afterOwner==owner&&
        get(player+0x804,afterIndex)&&afterIndex==index&&get(manager+selected*12+8,afterSlot)&&afterSlot==index&&
        get(manager+selected*12,afterFrontend)&&afterFrontend==frontend&&get(image+0x8883A4,afterMode)&&afterMode==mode;
}
template<class Reader> bool OwnedTutorial(std::uintptr_t image,std::uintptr_t display,std::uintptr_t expected,Reader read) {
    return OwnedTextChannel(image,display,expected,read,0x8FC,0x575A4C);
}
template<class Reader> bool OwnedBriefing(std::uintptr_t image,std::uintptr_t display,std::uintptr_t expected,Reader read) {
    return OwnedTextChannel(image,display,expected,read,0x90C,0x575C3C);
}
template<class Reader> bool VerifyTutorialImage(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[16]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));};
    const unsigned char field[]={0x8B,0x89,0xFC,8,0,0},store[]={0x89,0x85,0xFC,8,0,0};
    const unsigned char speech[]={0xA3,4,0x81,0xC8,0},thunk[]={0x81,0xC1,0x6C,8,0,0};
    const unsigned char briefingStore[]={0x89,0x85,0x0C,9,0,0};
    unsigned char briefingCtor[]={0xC7,6,0,0,0,0};const auto briefingVtable=static_cast<std::uint32_t>(image+0x575C3C);std::memcpy(briefingCtor+2,&briefingVtable,4);
    std::uint32_t draw=0,briefingDraw=0;
    return read(image+0x575A4C+8,&draw,4)&&draw==image+0x25CD50&&match(0x257469,field)&&match(0x2578D1,store)&&
        match(0x258CF9,speech)&&match(0x25CD50,thunk)&&read(image+0x575C3C+8,&briefingDraw,4)&&briefingDraw==image+0x262250&&
        match(0x257987,briefingStore)&&match(0x26269A,briefingCtor);
}
}
