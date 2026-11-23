#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_gameplay_hud_kl.hpp"
#include "native_hud_kl.hpp"
#include "native_hud_math.hpp"
#include "native_hand_hud_kl.hpp"
#include "native_context_hud_kl.hpp"
#include "background_diagnostic.hpp"
#include "native_reticle_style_kl.hpp"
#include "profile.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace NativeGameplayHudKL {
namespace {
static_assert(sizeof(void*)==4,"KL1 gameplay HUD is native x86");
constexpr unsigned Count=static_cast<unsigned>(Producer::Count);
using Draw=void(__thiscall*)(void*,void*,void*);
using PrepareMapCaption=bool(__thiscall*)(void*,void*,const NativeCameraMath::Vec3*);
using UpdateMap=void(__thiscall*)(void*);
using SetNativeText=bool(__thiscall*)(void*,const char*);
using SetTutorial=void(__thiscall*)(void*,const char*,float);
using FormatLocalized=std::uintptr_t(__thiscall*)(void*,void*,const char*,unsigned,int);
struct Entry {std::uintptr_t vtable,draw,returnRva;unsigned char prefix[6];unsigned length;};
constexpr Entry entries[Count]={
    {0x573F2C,0x2545A0,0x25488A,{0x83,0xEC,0x58,0x53,0x8B,0x5C},6},
    {0x575BFC,0x261CA0,0x26200E,{0x81,0xEC,0x98,0,0,0},6},
    {0x573ED4,0x253B90,0x253FB9,{0x83,0xEC,0x50,0x56,0x8B,0xF1},6},
    {0x573F74,0x254D40,0x254D80,{0x83,0xEC,0x08,0x53,0x56,0x8B},6},
    {0x5759C4,0x25C070,0x25C2A1,{0x83,0xEC,0x58,0x56,0x8B,0xF1},6},
    {0x5760C4,0x266C60,0x266E46,{0x83,0xEC,0x34,0xD9,0xEE,0x53},6},
    {0x575A4C,0x25CD50,0x25C8F6,{0x81,0xC1,0x6C,8,0,0},6},
    {0x575C3C,0x262250,0x26234F,{0x83,0xEC,0x34,0x56,0x8B,0xF1},6}
};
Draw originals[Count]{};SceneProvider sceneProvider=nullptr;
SceneProvider wristHudHeld=nullptr;
LedgeProvider ledgeAvailable=nullptr;
ReloadActionProvider reloadActionProvider=nullptr;
Draw compactStatusDraw=nullptr;
bool compactBackingVerified=false;
PrepareMapCaption prepareMapCaption=nullptr;
UpdateMap updateMap=nullptr;
SetNativeText setNativeText=nullptr;
SetTutorial originalTutorial=nullptr;
UpdateMap originalBriefingUpdate=nullptr;
FormatLocalized originalFormatLocalized=nullptr;
thread_local std::uintptr_t briefingDisplay=0;
std::atomic<SceneProvider> tutorialBindingsActive{nullptr},readableSubtitlesActive{nullptr};
bool subtitleWidthVerified=false;
std::atomic<std::uint64_t> tutorialBindings{0},personalWheel{0},exchangeWheel{0},subtitleLayouts{0};
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<bool> immersiveEnabled{true};
std::atomic<bool> gameHintsEnabled{true};
std::atomic<unsigned> reticleStyle{1};
std::atomic<std::uint64_t> reticleStyled{0},reticleStyleRejected{0};
std::atomic<std::uint64_t> calls[Count]{},hidden[Count]{};
bool ReadSpan(std::uintptr_t address,void* value,std::size_t size){
    SIZE_T got=0;return address>=0x10000u&&address<=0xffffffffu-size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),value,size,&got)&&got==size;
}
template<class T> bool Read(std::uintptr_t address,T& value){return ReadSpan(address,&value,sizeof(value));}
bool ReadTutorial(std::uintptr_t address,char* text,std::size_t capacity){
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto offset=address-reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    const auto length=std::min<std::size_t>(capacity,region.RegionSize-offset);
    return length&&ReadSpan(address,text,length)&&std::memchr(text,0,length);
}
bool AdaptInstruction(std::uintptr_t display,Producer producer,const char* text,char (&adapted)[2048]){
    std::uint32_t owner=0;
    const auto enabled=tutorialBindingsActive.load(std::memory_order_relaxed);
    if(!enabled||!enabled()||!gameHintsEnabled||!Read(display+0x10,owner))return false;
    const bool owned=producer==Producer::TutorialText?OwnedTutorial(image,display,owner,ReadSpan):
        OwnedBriefing(image,display,owner,ReadSpan);
    char source[sizeof(adapted)];
    if(!owned||!ReadTutorial(reinterpret_cast<std::uintptr_t>(text),source,sizeof(source))||
       !TutorialBindings(source,adapted,sizeof(adapted)))return false;
    ++tutorialBindings;
    return true;
}
void __fastcall SetTutorialHook(void* textBox,void*,const char* text,float duration){
    char adapted[2048];
    const auto box=reinterpret_cast<std::uintptr_t>(textBox);
    const auto display=box-0x1C;
    std::uint32_t speech=0,vtable=0,width=0;
    const auto subtitles=readableSubtitlesActive.load(std::memory_order_relaxed);
    if(subtitleWidthVerified&&subtitles&&subtitles()&&Read(image+0x888104,speech)&&speech==display&&
       Read(speech,vtable)&&vtable==image+entries[static_cast<unsigned>(Producer::TutorialText)].vtable&&
       Read(box+0x838,width)&&width>=96&&width<=4096){
        // The native subtitle setter already measures glyphs and paginates
        // crowded lines. Fit the same text at the enlarged VR font size.
        auto* wrap=reinterpret_cast<std::uint32_t*>(box+0x838);
        struct RestoreWidth {std::uint32_t* field;std::uint32_t width;~RestoreWidth(){*field=width;}} restore{wrap,width};
        *wrap=static_cast<std::uint32_t>(width/NativeHudMath::ReadableTextScale);
        originalTutorial(textBox,text,duration);
        ++subtitleLayouts;
        return;
    }
    originalTutorial(textBox,AdaptInstruction(display,Producer::TutorialText,text,adapted)?adapted:text,duration);
}
void __fastcall BriefingUpdateHook(void* display,void*){
    struct Restore {std::uintptr_t previous;~Restore(){briefingDisplay=previous;}} restore{briefingDisplay};
    briefingDisplay=reinterpret_cast<std::uintptr_t>(display);
    originalBriefingUpdate(display);
}
std::uintptr_t __fastcall FormatLocalizedHook(void* data,void*,void* output,const char* text,unsigned flags,int controller){
    // The original formatter expands bindings through the current keyboard
    // layout. Replace VR actions before that step so action IDs and native
    // goal/crew markup survive regardless of the Windows keyboard layout.
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    char adapted[2048];
    if(briefingDisplay&&caller==image+0x262CAC&&AdaptInstruction(briefingDisplay,Producer::BriefingAdvice,text,adapted))
        text=adapted;
    return originalFormatLocalized(data,output,text,flags,controller);
}
template<std::size_t N> bool ReadNativeText(std::uintptr_t object,char (&text)[N]){
    std::uint32_t vtable=0,storage=0;
    if(!Read(object,vtable)||vtable!=image+0x558414||!Read(object+0x94,storage))return false;
    if(!storage){text[0]=0;return true;}
    // The small-string header stores its allocation size and precedes the
    // characters by twelve bytes. Reject arrays outside the owned string.
    std::uint16_t allocated=0;
    if(!Read(storage,allocated)||allocated<=12)return false;
    const auto bytes=std::min<std::size_t>(allocated-12,N);
    return ReadSpan(storage+0xC,text,bytes)&&std::memchr(text,0,bytes);
}
struct MapHeading {std::uintptr_t player=0;DWORD tick=0;float right=0,forward=0;};
SRWLOCK mapHeadingLock=SRWLOCK_INIT;
MapHeading mapHeading;
struct MapDraw {
    std::uintptr_t map=0,player=0;
    float saved[5]{};
    bool patched=false;
    ~MapDraw(){if(patched)std::memcpy(reinterpret_cast<void*>(map+0x1F68),saved,sizeof(saved));}
};
thread_local MapDraw* drawingMap=nullptr;
void __fastcall UpdateMapHook(void* map,void*){
    updateMap(map);
    auto* draw=drawingMap;
    if(!draw||draw->map!=reinterpret_cast<std::uintptr_t>(map)||draw->patched)return;
    AcquireSRWLockShared(&mapHeadingLock);const auto view=mapHeading;ReleaseSRWLockShared(&mapHeadingLock);
    if(view.player!=draw->player||!view.tick||GetTickCount()-view.tick>250||
       !Read(draw->map+0x1F68,draw->saved))return;
    // The stock map transforms player-minus-target in world X/Z, then
    // negates the screen Y.
    // Substitute the tracked heading in that rotation, leaving its native
    // distances, compass sprites, goal text and modal-map state intact.
    const float rotation[]{view.right,-view.forward,draw->saved[2],-view.forward,-view.right};
    std::memcpy(reinterpret_cast<void*>(draw->map+0x1F68),rotation,sizeof(rotation));
    draw->patched=true;
}

struct WristWeaponBacking {
    std::uint32_t* slot=nullptr;
    std::uint32_t saved=0;
    explicit WristWeaponBacking(std::uintptr_t display){
        // Opening the native wheel swaps out the compact circle. Reuse the
        // same texture the game writes when it returns to compact mode.
        std::uint32_t resources=0,texture=0;
        if(!compactBackingVerified||!Read(display+8,resources)||!resources||
           !Read(resources+0x1F8,texture)||!texture||!Read(display+0x454,saved)||saved==texture)return;
        slot=reinterpret_cast<std::uint32_t*>(display+0x454);
        *slot=texture;
    }
    ~WristWeaponBacking(){if(slot)*slot=saved;}
};
struct WristAmmoText {
    void* text=nullptr;
    char saved[24]{};
    WristAmmoText(std::uintptr_t widget,std::uintptr_t player){
        if(!setNativeText)return;
        std::uint32_t object=0;
        if(!Read(widget+0x2D4,object)||!ReadNativeText(object,saved))return;
        NativeHandHudKL::AmmoObservation ammo;
        if(!NativeHandHudKL::CaptureAmmo(image,player,ReadSpan,ammo)||!ammo.reserveKnown)return;
        char current[24]{};
        std::snprintf(current,sizeof(current),"%u/%u",ammo.magazine,ammo.reserve);
        if(std::strcmp(saved,current)){
            text=reinterpret_cast<void*>(std::uintptr_t(object));
            setNativeText(text,current);
        }
    }
    ~WristAmmoText(){if(text)setNativeText(text,saved);}
};
struct WristActionKey {
    void* widget=nullptr;
    char saved[192]{};
    bool applied=false;
    WristActionKey(std::uintptr_t slot,bool weapon,bool climb,const char* reload){
        std::uint32_t object=0;
        if(!setNativeText||!Read(slot,object)||!ReadNativeText(object,saved))return;
        char current[sizeof(saved)]{};
        if(reload){
            if(std::snprintf(current,sizeof(current),"%s",reload)>=int(sizeof(current)))return;
        }else if(!FormatWristAction(climb?"Climb":saved,weapon,current,sizeof(current),climb))return;
        if(!std::strcmp(current,saved)){applied=true;return;}
        widget=reinterpret_cast<void*>(std::uintptr_t(object));
        setNativeText(widget,current);
        char actual[sizeof(saved)]{};
        applied=!reload||(ReadNativeText(object,actual)&&!std::strcmp(current,actual));
    }
    ~WristActionKey(){if(widget)setNativeText(widget,saved);}
};
std::uintptr_t LocalFrontend(std::uintptr_t player){
    std::uint32_t data=0,manager=0,frontend=0,vt=0;
    std::int8_t index=-1;
    if(!Read(player+0x804,index)||index<0||index>1||!Read(image+0x68760C,data)||!Read(data+0x154C,manager))return 0;
    for(unsigned i=0;i<2;++i){
        std::int8_t id=-1;if(!Read(manager+i*12+8,id))return 0;
        if(id==index&&(frontend||!Read(manager+i*12,frontend)||!frontend))return 0;
    }
    return Read(frontend,vt)&&vt==image+0x5755FC?frontend:0;
}
std::uintptr_t LocalMinimap(std::uintptr_t frontend,std::uintptr_t player){
    if(!frontend)return 0;
    std::uint32_t display=0,vt=0,owner=0;
    if(!Read(frontend+0x8F8,display)||
       !Read(display,vt)||vt!=image+entries[static_cast<unsigned>(Producer::Minimap)].vtable||
       !Read(display+0x10,owner)||owner!=player)return 0;
    std::uint32_t state=0;
    return Read(display+0x14,state)&&state<=3?display:0;
}
bool StatusAnchor(std::uintptr_t widget,NativeCameraMath::Vec3& anchor){
    std::uint32_t layout=0;float xy[2]{},basis[7]{};
    if(!Read(widget+0x2CC,layout)||!Read(widget+4,xy)||!Read(layout,basis))return false;
    anchor={basis[0]+xy[0]*basis[5],basis[1]+xy[1]*basis[6],basis[2]};
    return NativeCameraMath::Finite(anchor)&&std::fabs(anchor.x)<10.f&&std::fabs(anchor.y)<10.f&&
        anchor.z>=5.5f&&anchor.z<=6.5f;
}
Draw ResolveCompactStatus(){
    const std::array<unsigned char,12> head{0x83,0xEC,0x7C,0x56,0x8B,0xF1,0x83,0xBE,0xCC,0x02,0,0};
    std::array<unsigned char,12> actual{};
    const std::array<unsigned char,5> call{0xE8,0x58,0x53,0,0};
    std::array<unsigned char,5> foundCall{};
    if(!Read(image+0x25A250,actual)||actual!=head||!Read(image+0x254EF3,foundCall)||foundCall!=call)return nullptr;
    return reinterpret_cast<Draw>(image+0x25A250);
}
PrepareMapCaption ResolveMapCaption(){
    const std::array<unsigned char,13> head{0x6A,0xFF,0x68,0x08,0x13,0x90,0,0x64,0xA1,0,0,0,0};
    const std::array<unsigned char,9> call{0xE8,0x1C,0xFC,0xFF,0xFF,0x84,0xC0,0xD9,0xEE};
    std::array<unsigned char,13> actual{};std::array<unsigned char,9> foundCall{};
    if(!Read(image+0x266E50,actual)||actual!=head||!Read(image+0x26722F,foundCall)||foundCall!=call)return nullptr;
    return reinterpret_cast<PrepareMapCaption>(image+0x266E50);
}
std::uint32_t MapCaptionTexture(std::uintptr_t map,std::uintptr_t player){
    std::uint32_t resources=0,texture=0,text=0,vt=0;
    if(!prepareMapCaption||!Read(map+8,resources)||!Read(resources+0x3E4,texture)||!texture||
       !Read(map+0x340,text)||!Read(text,vt)||vt!=image+0x558414)return 0;
    struct Caption {std::uintptr_t map=0,player=0;DWORD tick=0;bool valid=false;};
    static thread_local Caption caption;
    const auto now=GetTickCount();
    if(caption.map!=map||caption.player!=player||!caption.tick||now-caption.tick>=200){
        // Native compact-map update supplies this offset and resolves the
        // player's current localized objective into the live text widget.
        const NativeCameraMath::Vec3 offset{73.f,25.f,0.f};
        caption={map,player,now,prepareMapCaption(reinterpret_cast<void*>(map),reinterpret_cast<void*>(map+0x6C),&offset)};
    }
    return caption.valid?texture:0;
}
bool DrawWrist(Producer producer,std::uintptr_t display,std::uintptr_t player,void* renderer,void* parameter){
    using NativeHudKL::WristHudElement;
    const bool weapon=producer==Producer::Inventory;
    const auto draw=weapon?compactStatusDraw:originals[static_cast<unsigned>(producer)];
    if(!display||!draw||!NativeHudKL::WristHudReady())return false;
    const auto widget=display+(weapon?0x3CC:0x6C);
    NativeCameraMath::Vec3 anchor;
    if(!StatusAnchor(widget,anchor))return false;
    std::uint32_t previousTexture=0;
    const auto captionTexture=weapon?0:MapCaptionTexture(display,player);
    auto* texture=reinterpret_cast<std::uint32_t*>(display+0x100);
    const bool hasCaption=captionTexture&&Read(display+0x100,previousTexture);
    if(hasCaption)*texture=captionTexture;
    const auto previous=NativeHudKL::ReplaceWristHudSource({renderer,player,display,
        weapon?WristHudElement::Weapon:WristHudElement::Minimap,anchor,{},hasCaption});
    // Draw the live primary status widget even after the parent inventory
    // stops requesting it. The game's update still owns its icon and text.
    if(weapon){
        const WristWeaponBacking backing(display);
        WristAmmoText ammo(widget,player);
        draw(reinterpret_cast<void*>(widget),renderer,parameter);
    }else{
        MapDraw map{display,player};
        auto* outer=drawingMap;drawingMap=&map;
        draw(reinterpret_cast<void*>(display),renderer,parameter);
        drawingMap=outer;
    }
    NativeHudKL::ReplaceWristHudSource(previous);
    if(hasCaption)*texture=previousTexture;
    return true;
}
void DrawLeftWrist(std::uintptr_t frontend,std::uintptr_t player,void* renderer,void* parameter){
    DrawWrist(Producer::Minimap,LocalMinimap(frontend,player),player,renderer,parameter);
    std::uint32_t team=0;
    if(frontend&&Read(frontend+0x904,team))NativeTeamHudKL::DrawWrist(team,player,renderer,parameter);
}
bool WeaponWheelAnchor(std::uintptr_t display,NativeCameraMath::Vec3& anchor){
    const auto ring=display+0xF3C;
    std::uint32_t layout=0;
    float pivot[2]{},basis[7]{};
    if(!NativeHudKL::WristHudReady()||!Read(ring,pivot)||!Read(ring+0x488,layout)||!Read(layout,basis))return false;
    anchor={basis[0]+basis[5]*pivot[0],basis[1]+basis[6]*pivot[1],basis[2]};
    return NativeCameraMath::Finite(anchor)&&std::fabs(anchor.x)<=10.f&&std::fabs(anchor.y)<=10.f&&
        anchor.z>=5.5f&&anchor.z<=6.5f;
}
void DrawWeaponWheel(std::uintptr_t display,std::uintptr_t player,void* renderer,void* parameter,
                     const NativeCameraMath::Vec3& anchor){
    using Element=NativeHudKL::WristHudElement;
    constexpr auto index=static_cast<unsigned>(Producer::Inventory);
    const auto previous=NativeHudKL::ReplaceWristHudSource({renderer,player,display,Element::WeaponWheel,anchor,{}});
    std::uint8_t exchange=0;
    if(Read(display+0x22,exchange)&&exchange<=1)++(exchange?exchangeWheel:personalWheel);
    originals[index](reinterpret_cast<void*>(display),renderer,parameter);
    NativeHudKL::ReplaceWristHudSource(previous);
}
bool Owned(void* display,unsigned index,std::uintptr_t& player){
    std::uint32_t vt=0,owner=0,data=0,current=0,playerVt=0;
    unsigned char nativeMode=0xff;
    const auto address=reinterpret_cast<std::uintptr_t>(display);
    const bool text=index==static_cast<unsigned>(Producer::UseAction)||index==static_cast<unsigned>(Producer::TutorialText)||index==static_cast<unsigned>(Producer::BriefingAdvice);
    const bool wheel=index==static_cast<unsigned>(Producer::Crosshair)||index==static_cast<unsigned>(Producer::Inventory);
    const bool markers=index==static_cast<unsigned>(Producer::ActionMarker)&&wristHudHeld&&wristHudHeld();
    if(!Read(image+0x8883A4,nativeMode)||(nativeMode!=0&&!((text||wheel||markers)&&nativeMode==3))||
       !Read(address,vt)||vt!=image+entries[index].vtable||!Read(address+0x10,owner)||
       !Read(image+0x68760C,data)||!Read(data+0xA2C,current)||owner!=current||
       !Read(owner,playerVt)||playerVt!=image+0x56582C)return false;
    if(index==static_cast<unsigned>(Producer::Minimap)){
        if(LocalMinimap(LocalFrontend(owner),owner)!=address)return false;
    }
    player=owner;return true;
}
void DrawWristAction(void* display,std::uintptr_t player,void* renderer,void* parameter,bool previewClimb=false){
    const auto address=reinterpret_cast<std::uintptr_t>(display);
    std::uint32_t layout=0,kind=0,weapon=0;
    std::uint8_t visibility[2]{};
    float basis[9]{};
    if(!gameHintsEnabled||!NativeHudKL::WristHudReady()||(wristHudHeld&&wristHudHeld())||
       !Read(address+0x128,visibility)||visibility[0]>1||!Read(address+0xC,layout)||!Read(layout,basis)||
       !Read(address+0x118,kind)||!Read(address+0x120,weapon))return;
    const bool climb=!visibility[0]&&(previewClimb||(ledgeAvailable&&ledgeAvailable(player)));
    const char* reload=!visibility[0]&&!climb&&reloadActionProvider?reloadActionProvider(player):nullptr;
    if(!visibility[0]&&!climb&&!reload)return;
    const NativeCameraMath::Vec3 anchor{
        basis[0]+basis[7]*.5f*basis[5],basis[1]+(basis[8]-54.f)*basis[6],basis[2]};
    if(!NativeCameraMath::Finite(anchor)||anchor.z<5.5f||anchor.z>6.5f||
       std::fabs(anchor.x)>10.f||std::fabs(anchor.y)>10.f)return;
    const auto previous=NativeHudKL::ReplaceWristHudSource({renderer,player,address,
        (climb||reload)?NativeHudKL::WristHudElement::ClimbAction:NativeHudKL::WristHudElement::Action,anchor,{}});
    const bool pickup=!climb&&weapon&&kind==static_cast<std::uint32_t>(NativeContextHudKL::Prompt::PickupWeapon);
    const WristActionKey label(address+0x1C,pickup,climb,reload),outline(address+0x20,pickup,climb,reload);
    if(reload&&(!label.applied||!outline.applied)){
        NativeHudKL::ReplaceWristHudSource(previous);return;
    }
    if(climb||reload){
        *reinterpret_cast<std::uint32_t*>(address+0x120)=0;
        auto* active=reinterpret_cast<std::uint8_t*>(address+0x128);
        active[0]=1;active[1]=255;
    }
    originals[static_cast<unsigned>(Producer::UseAction)](display,renderer,parameter);
    if(climb||reload){
        *reinterpret_cast<std::uint32_t*>(address+0x120)=weapon;
        std::memcpy(reinterpret_cast<void*>(address+0x128),visibility,sizeof(visibility));
    }
    NativeHudKL::ReplaceWristHudSource(previous);
}
// Isolated GOG screenshot fixture. Ask the real text/atlas widget to render
// representative prompts, with no interaction sent to the mission.
bool PreviewWristAction(void* display,std::uintptr_t player,void* renderer,void* parameter){
    if(!BackgroundDiagnostic::Active())return false;
    static const bool enabled=[](){wchar_t flag[4]{};return GetEnvironmentVariableW(L"KL1_BACKGROUND_ACTION_HUD",flag,4)==1&&flag[0]==L'1';}();
    const auto age=BackgroundDiagnostic::Age();
    if(!enabled||age<29000||age>=38500)return false;
    const auto address=reinterpret_cast<std::uintptr_t>(display);
    std::uint32_t vtable=0,method=0,fields[3]{};
    std::uint8_t active=0;
    if(!Read(address+0x128,active)||active||!Read(address+0x118,fields)||
       !Read(address,vtable)||!Read(vtable+0x24,method)||method!=image+0x25C4B0)return false;
    if(age>=36500){DrawWristAction(display,player,renderer,parameter,true);return true;}
    const bool pickup=age>=33500;
    NativeHandHudKL::AmmoObservation ammo;
    if(pickup&&!NativeHandHudKL::CaptureAmmo(image,player,ReadSpan,ammo))return false;
    const std::uint32_t preview[]{pickup?2u:14u,0,static_cast<std::uint32_t>(ammo.weapon)};
    std::memcpy(reinterpret_cast<void*>(address+0x118),preview,sizeof(preview));
    auto* visibility=reinterpret_cast<std::uint8_t*>(address+0x128);
    const auto originalAlpha=visibility[1];
    visibility[0]=1;visibility[1]=255;
    using Refresh=void(__thiscall*)(void*);
    reinterpret_cast<Refresh>(method)(display);
    DrawWristAction(display,player,renderer,parameter);
    std::memcpy(reinterpret_cast<void*>(address+0x118),fields,sizeof(fields));
    visibility[0]=active;visibility[1]=originalAlpha;
    return true;
}
void DrawScreenText(unsigned index,void* display,void* renderer,void* parameter,std::uintptr_t owner,bool speech){
    const auto previous=NativeHudKL::ReplaceWristHudSource({renderer,owner,reinterpret_cast<std::uintptr_t>(display),
        speech?NativeHudKL::WristHudElement::Speech:NativeHudKL::WristHudElement::Instruction,{},{}});
    originals[index](display,renderer,parameter);
    NativeHudKL::ReplaceWristHudSource(previous);
}
template<unsigned Index> void __fastcall DrawHook(void* display,void*,void* renderer,void* parameter){
    ++calls[Index];std::uintptr_t player=0;
    if constexpr(Index==static_cast<unsigned>(Producer::TutorialText)){
        std::uint32_t speech=0,vt=0;
        if(Read(image+0x888104,speech)&&speech&&speech==reinterpret_cast<std::uintptr_t>(display)&&
           Read(speech,vt)&&vt==image+entries[Index].vtable){
            DrawScreenText(Index,display,renderer,parameter,speech,true);
            return;
        }
    }
    if constexpr(Index==static_cast<unsigned>(Producer::Crosshair)) {
        // Scope_Dragunov is original camera geometry with its own green
        // filter/crosshair. Mode13's ordinary Add/Sub sprite would duplicate
        // it; only an applied, independently proved original optic skips it.
        if(NativeHudKL::OriginalScopeOverlayActive()&&Owned(display,Index,player)){++hidden[Index];return;}
    }
    if(sceneProvider&&sceneProvider()&&Owned(display,Index,player)){
        if constexpr(Index==static_cast<unsigned>(Producer::Crosshair)){
            unsigned char mode=0xFF;if(Read(image+0x8883A4,mode)&&mode==3){++hidden[Index];return;}
        }
        if constexpr(Index==static_cast<unsigned>(Producer::TutorialText)||Index==static_cast<unsigned>(Producer::BriefingAdvice)) {
            const auto address=reinterpret_cast<std::uintptr_t>(display);
            const bool owned=Index==static_cast<unsigned>(Producer::TutorialText)?OwnedTutorial(image,address,player,ReadSpan):
                OwnedBriefing(image,address,player,ReadSpan);
            if(!owned){originals[Index](display,renderer,parameter);return;}
            if(gameHintsEnabled){DrawScreenText(Index,display,renderer,parameter,player,false);return;}
            ++hidden[Index];return;
        }
        if constexpr(Index==static_cast<unsigned>(Producer::UseAction)){
            unsigned char mode=0xff;
            if(immersiveEnabled&&Read(image+0x8883A4,mode)&&mode==0){
                if(!PreviewWristAction(display,player,renderer,parameter))
                    DrawWristAction(display,player,renderer,parameter);
                ++hidden[Index];return;
            }
        }
        if constexpr(Index==static_cast<unsigned>(Producer::ActionMarker)) {
            NativeHudKL::BeginNativeMarkers(player,renderer,parameter);
            struct End {~End(){NativeHudKL::EndNativeMarkers();}} end;
            originals[Index](display,renderer,parameter);return;
        }
        if constexpr(Index==static_cast<unsigned>(Producer::Crosshair)){
            if(NativeHudKL::ReticleEnabled()){
                NativeHudKL::BeginReticle(player,renderer);
                struct End {~End(){NativeHudKL::EndReticle();}} end;
                NativeReticleStyleKL::Display copy{};
                const auto reader=[](std::uintptr_t at,void* output,std::size_t bytes){SIZE_T got=0;return at>=0x10000u&&at<=0xffffffffu-bytes&&
                    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,bytes,&got)&&got==bytes;};
                if(NativeReticleStyleKL::Prepare(image,reinterpret_cast<std::uintptr_t>(display),player,
                   static_cast<NativeReticleStyleKL::Style>(reticleStyle.load(std::memory_order_relaxed)),reinterpret_cast<std::uintptr_t>(copy.data()),reader,copy)){
                    ++reticleStyled;originals[Index](copy.data(),renderer,parameter);
                }else{++reticleStyleRejected;originals[Index](display,renderer,parameter);}
                return;
            }
        } else if(!immersiveEnabled.load(std::memory_order_relaxed)){
            originals[Index](display,renderer,parameter);return;
        }
        unsigned mapState=~0u;
        if constexpr(Index==static_cast<unsigned>(Producer::Inventory)){
            const auto address=reinterpret_cast<std::uintptr_t>(display);
            unsigned char value=0;
            if(!Read(address+0x21,value)||value>1){originals[Index](display,renderer,parameter);return;}
            if(value){
                const auto frontend=LocalFrontend(player);
                std::uint32_t ownedDisplay=0;
                if(wristHudHeld&&frontend&&Read(frontend+0x8F4,ownedDisplay)&&ownedDisplay==address){
                    // The Y request arrives before the native wheel enters mode 3.
                    // Submit the panel only once the expanded wheel has a valid layout.
                    unsigned char mode=0;
                    NativeCameraMath::Vec3 anchor;
                    if(wristHudHeld()&&Read(image+0x8883A4,mode)&&mode==3&&WeaponWheelAnchor(address,anchor)){
                        DrawLeftWrist(frontend,player,renderer,parameter);
                        DrawWrist(Producer::Inventory,address,player,renderer,parameter);
                        DrawWeaponWheel(address,player,renderer,parameter,anchor);
                    }
                    return;
                }
                originals[Index](display,renderer,parameter);return;
            }
        }
        if constexpr(Index==static_cast<unsigned>(Producer::Minimap)){
            std::uint32_t value=0;
            if(!Read(reinterpret_cast<std::uintptr_t>(display)+0x14,value)||value>3){originals[Index](display,renderer,parameter);return;}
            mapState=value;
        }
        if(Hide(static_cast<Producer>(Index),true,true,0,mapState,
                Index==static_cast<unsigned>(Producer::Minimap))){++hidden[Index];return;}
    }
    originals[Index](display,renderer,parameter);
}
}
bool Install(SceneProvider scene,LogCallback log){
    if(installed.load())return true;if(!scene)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto readSpan=[](std::uintptr_t at,void* output,std::size_t bytes){SIZE_T got=0;return at>=0x10000u&&at<=0xffffffffu-bytes&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,bytes,&got)&&got==bytes;};
    if(!VerifyTutorialImage(image,readSpan)||!NativeReticleStyleKL::VerifyImage(image,readSpan)){if(log)log("Native HUD/reticle resource channel differs; original HUD retained.");return false;}
    for(unsigned i=0;i<Count;++i){
        std::uint32_t method=0;unsigned char bytes[6]{},returnBytes[3]{};SIZE_T got=0;
        const unsigned char expectedReturn[]={0xC2,8,0};
        if(!Read(image+entries[i].vtable+8,method)||method!=image+entries[i].draw||
           !Read(image+entries[i].returnRva,returnBytes)||std::memcmp(returnBytes,expectedReturn,sizeof(returnBytes))||
           (entries[i].length&&(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(method),bytes,entries[i].length,&got)||
             got!=entries[i].length||std::memcmp(bytes,entries[i].prefix,entries[i].length)))){
            if(log)log("Native gameplay HUD signatures differ; original indicators retained.");return false;
        }
    }
    void* hooks[Count]={reinterpret_cast<void*>(&DrawHook<0>),reinterpret_cast<void*>(&DrawHook<1>),reinterpret_cast<void*>(&DrawHook<2>),
        reinterpret_cast<void*>(&DrawHook<3>),reinterpret_cast<void*>(&DrawHook<4>),reinterpret_cast<void*>(&DrawHook<5>),reinterpret_cast<void*>(&DrawHook<6>),reinterpret_cast<void*>(&DrawHook<7>)};
    sceneProvider=scene;unsigned created=0;bool okay=true;
    for(;created<Count;++created)if(MH_CreateHook(reinterpret_cast<void*>(image+entries[created].draw),hooks[created],reinterpret_cast<void**>(&originals[created]))!=MH_OK){okay=false;break;}
    if(okay)for(unsigned i=0;i<Count;++i)if(MH_QueueEnableHook(reinterpret_cast<void*>(image+entries[i].draw))!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay){
        for(unsigned i=0;i<created;++i){MH_QueueDisableHook(reinterpret_cast<void*>(image+entries[i].draw));MH_DisableHook(reinterpret_cast<void*>(image+entries[i].draw));}
        MH_ApplyQueued();for(unsigned i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(image+entries[i].draw));
        sceneProvider=nullptr;return false;
    }
    compactStatusDraw=ResolveCompactStatus();
    prepareMapCaption=ResolveMapCaption();
    const unsigned char mapHead[]{0x83,0xEC,0x4C,0x56,0x8B,0xF1,0x8B,0x4E,0x10};
    const unsigned char mapCall[]{0xE8,0xAD,0xE9,0xFF,0xFF};
    const unsigned char textHead[]{0x53,0x8B,0x5C,0x24,0x08,0x85,0xDB,0x56,0x8B,0xF1};
    const unsigned char tutorialHead[]{0x81,0xEC,0x2C,0x08,0x00,0x00};
    const unsigned char tutorialCall[]{0xE8,0x8B,0xEB,0xFF,0xFF};
    const unsigned char subtitleWidthRead[]{0x3B,0x8E,0x38,0x08,0,0,0x89,0x6C,0x24,0x34,0x77};
    const unsigned char briefingHead[]{0x6A,0xFF,0x68,0x20,0x12,0x90,0x00};
    const unsigned char formatterHead[]{0x6A,0xFF,0x68,0x88,0xE0,0x8E,0x00};
    const unsigned char formatterCall[]{0xE8,0xA4,0xAF,0xDF,0xFF};
    const auto matches=[](std::uintptr_t rva,const auto& bytes){
        std::array<unsigned char,sizeof(bytes)> actual{};
        return ReadSpan(image+rva,actual.data(),actual.size())&&!std::memcmp(bytes,actual.data(),actual.size());
    };
    const unsigned char compactBacking[]{0x8B,0x91,0xF8,0x01,0,0,0x89,0x96,0x54,0x04,0,0};
    compactBackingVerified=matches(0x254F9C,compactBacking);
    auto* mapUpdate=reinterpret_cast<void*>(image+0x265620);
    if(matches(0x265620,mapHead)&&matches(0x266C6E,mapCall)&&
       MH_CreateHook(mapUpdate,reinterpret_cast<void*>(&UpdateMapHook),reinterpret_cast<void**>(&updateMap))==MH_OK){
        if(MH_EnableHook(mapUpdate)!=MH_OK){MH_RemoveHook(mapUpdate);updateMap=nullptr;}
    }
    std::uint32_t textMethod=0;
    if(matches(0x162700,textHead)&&Read(image+0x558414+0x244,textMethod)&&textMethod==image+0x162700&&
       NativeHandHudKL::VerifyAmmoImage(image,ReadSpan))setNativeText=reinterpret_cast<SetNativeText>(image+0x162700);
    subtitleWidthVerified=matches(0x25BC0A,subtitleWidthRead);
    auto* tutorial=reinterpret_cast<void*>(image+0x25B980);
    if(matches(0x25B980,tutorialHead)&&matches(0x25CDF0,tutorialCall)&&
       MH_CreateHook(tutorial,reinterpret_cast<void*>(&SetTutorialHook),reinterpret_cast<void**>(&originalTutorial))==MH_OK){
        if(MH_EnableHook(tutorial)!=MH_OK){MH_RemoveHook(tutorial);originalTutorial=nullptr;}
    }
    auto* briefing=reinterpret_cast<void*>(image+0x262AF0);
    auto* formatter=reinterpret_cast<void*>(image+0x5DC50);
    if(matches(0x262AF0,briefingHead)&&matches(0x5DC50,formatterHead)&&matches(0x262CA7,formatterCall)&&
       MH_CreateHook(formatter,reinterpret_cast<void*>(&FormatLocalizedHook),reinterpret_cast<void**>(&originalFormatLocalized))==MH_OK){
        const bool created=MH_CreateHook(briefing,reinterpret_cast<void*>(&BriefingUpdateHook),reinterpret_cast<void**>(&originalBriefingUpdate))==MH_OK;
        if(!created||MH_EnableHook(formatter)!=MH_OK||MH_EnableHook(briefing)!=MH_OK){
            if(created){MH_DisableHook(briefing);MH_RemoveHook(briefing);}
            MH_DisableHook(formatter);MH_RemoveHook(formatter);
            originalBriefingUpdate=nullptr;originalFormatLocalized=nullptr;
        }
    }
    if(!compactStatusDraw&&log)log("Native compact weapon HUD signature unavailable; wrist weapon indicator disabled.");
    installed=true;if(log)log("Native localized actions and pickup icons follow the left wrist. Game Hints controls the original tutorial prompts.");return true;
}
void PublishMapView(std::uintptr_t player,float forwardX,float forwardZ){
    const float length=std::hypot(forwardX,forwardZ);
    if(!player||!std::isfinite(length)||length<.05f)return;
    const MapHeading next{player,GetTickCount(),forwardX/length,forwardZ/length};
    AcquireSRWLockExclusive(&mapHeadingLock);mapHeading=next;ReleaseSRWLockExclusive(&mapHeadingLock);
}
void SetGameHintsEnabled(bool enabled){gameHintsEnabled=enabled;}
void SetTutorialBindingsActive(SceneProvider enabled){tutorialBindingsActive=enabled;}
void SetReadableSubtitlesActive(SceneProvider enabled){readableSubtitlesActive=enabled;}
void SetImmersiveEnabled(bool enabled){immersiveEnabled.store(enabled,std::memory_order_relaxed);}
void SetWristHudHeld(SceneProvider held){wristHudHeld=held;}
void SetLedgeProvider(LedgeProvider available){ledgeAvailable=available;}
void SetReloadActionProvider(ReloadActionProvider action){reloadActionProvider=action;}
void SetReticleStyle(unsigned style){reticleStyle.store(style==0?0:1,std::memory_order_relaxed);}
Stats Snapshot(){Stats out;out.installed=installed;out.reticleStyled=reticleStyled;out.reticleStyleRejected=reticleStyleRejected;out.tutorialBindings=tutorialBindings;out.subtitleLayouts=subtitleLayouts;out.personalWheel=personalWheel;out.exchangeWheel=exchangeWheel;for(unsigned i=0;i<Count;++i){out.calls[i]=calls[i];out.hidden[i]=hidden[i];}return out;}
}
