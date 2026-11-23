#pragma once
#include <algorithm>
#include "native_camera_math.hpp"
#include "native_weapon_ownership.hpp"
#include "native_team_hud_kl.hpp"
#include "native_context_hud_kl.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

// The card renderer consumes current, owned gameplay observations. Unknown
// native counters/actions never acquire plausible-looking fallback values.
namespace NativeHandHudKL {
using namespace NativeCameraMath;
enum class Action : std::uint32_t { None,PickupWeapon,OpenDoor,CloseCombat,Use,Pickup,Revive,LeaveMounted,
    Select,Talk,Rappel,EnterVehicle,ExitVehicle,TakeMounted,ThrowBackGrenade,GiveHealth,Climb };
struct Telemetry {
    bool ammoKnown=false,magazineKnown=false,reserveKnown=false;
    unsigned magazine=0,reserve=0;
    bool contextKnown=false;
    Action action=Action::None;
    wchar_t contextText[96]{};
};
struct Frame {
    std::uintptr_t player=0;
    std::uint64_t pairId=0;
    std::uint32_t tick=0,recenterAck=UINT32_MAX;
    float unitsPerMetre=100.f;
    CameraPose actualEye{},centre{},wrist[2]{};
    bool tracked[2]{};
    float projection[16]{};
    bool held=false;
    bool controlHints=true;
    bool gameHints=true;
    bool teamHeld=false;
    bool weaponMenuHeld=false,supportGripEnabled=true;
    NativeTeamMarkerKL::Snapshot commandMarkers{};
    // Native ActionMarkerDisplay now retains the original world icons.
    // Vector glyphs remain available only to an explicit private fixture.
    bool nativeCommandMarkers=true;
    unsigned weaponHand=1;
    Telemetry telemetry{};
};
inline bool Fresh(const Frame& frame,std::uint32_t now) {
    if(!frame.player||!frame.pairId||!frame.tick||frame.recenterAck==UINT32_MAX||
       std::int32_t(now-frame.tick)<0||now-frame.tick>150||frame.weaponHand>1||
       !std::isfinite(frame.unitsPerMetre)||frame.unitsPerMetre<1||frame.unitsPerMetre>1000||
       !ValidatePose(frame.actualEye,.05f)||!ValidatePose(frame.centre,.05f)||
       (!frame.tracked[0]&&!frame.tracked[1]))return false;
    for(unsigned side=0;side<2;++side)if(frame.tracked[side]&&!ValidatePose(frame.wrist[side],.05f))return false;
    for(float value:frame.projection)if(!std::isfinite(value))return false;
    return frame.projection[0]>0&&frame.projection[5]>0&&frame.projection[11]>0;
}
inline const wchar_t* ActionText(const Telemetry& telemetry) {
    if(!telemetry.contextKnown)return nullptr;
    if(telemetry.contextText[0])return telemetry.contextText;
    switch(telemetry.action) {
    case Action::PickupWeapon:return L"X  Pick up weapon";
    case Action::OpenDoor:return L"X  Open door";
    case Action::CloseCombat:return L"X  Close combat";
    case Action::Use:return L"X  Use";
    case Action::Pickup:return L"X  Pick up";
    case Action::Revive:return L"X  Revive";
    case Action::LeaveMounted:return L"X  Leave mounted weapon";
    case Action::Select:return L"X  Select";
    case Action::Talk:return L"X  Talk";
    case Action::Rappel:return L"X  Rappel";
    case Action::EnterVehicle:return L"X  Enter vehicle";
    case Action::ExitVehicle:return L"X  Exit vehicle";
    case Action::TakeMounted:return L"X  Use mounted weapon";
    case Action::ThrowBackGrenade:return L"X  Throw back grenade";
    case Action::GiveHealth:return L"X  Give health";
    case Action::Climb:return L"X  Climb";
    default:return nullptr;
    }
}
inline void SetContext(Telemetry& telemetry,const NativeContextHudKL::State& context) {
    telemetry.contextKnown=false;telemetry.action=Action::None;telemetry.contextText[0]=0;
    const auto* label=NativeContextHudKL::EnglishAction(context);if(!label)return;
    const int count=std::swprintf(telemetry.contextText,std::size(telemetry.contextText),L"X  %ls",label);
    telemetry.contextKnown=count>0&&static_cast<std::size_t>(count)<std::size(telemetry.contextText);
    if(!telemetry.contextKnown)telemetry.contextText[0]=0;
}
inline bool AmmoKnown(const Telemetry& telemetry) {
    return (telemetry.ammoKnown||(telemetry.magazineKnown&&telemetry.reserveKnown))&&
        telemetry.magazine<=10000&&telemetry.reserve<=1000000;
}
inline std::wstring AmmoText(const Telemetry& telemetry) {
    wchar_t magazine[20]=L"--",reserve[20]=L"--";
    if((telemetry.ammoKnown||telemetry.magazineKnown)&&telemetry.magazine<=10000)
        std::swprintf(magazine,std::size(magazine),L"%u",telemetry.magazine);
    if((telemetry.ammoKnown||telemetry.reserveKnown)&&telemetry.reserve<=1000000)
        std::swprintf(reserve,std::size(reserve),L"%u",telemetry.reserve);
    return std::wstring(L"AMMO\n")+magazine+L" / "+reserve;
}

inline bool Address(std::uintptr_t address,std::size_t bytes) {
    return address>=0x10000u&&bytes<=0xffffffffu&&address<=0xffffffffu-bytes;
}
// Inventory Update 0x6563A8/0x656464 uses virtual +308 (0x51DDB0, D8)
// together with direct 0x5216E0(true, DC): magazine and outside-magazine ammo.
// Both native 999 overrides are respected without calling game methods.
template<class Reader> bool VerifyAmmoImage(std::uintptr_t image,Reader read) {
    if(!Address(image,0x8860BC))return false;
    unsigned char expected[]={0x83,0x3D,0,0,0,0,0,0x74,6,0xB8,0xE7,3,0,0,0xC3,
        0x8B,0x81,0xD8,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0xC3};
    const auto cheat=static_cast<std::uint32_t>(image+0x873680);std::memcpy(expected+2,&cheat,4);
    unsigned char actual[sizeof(expected)]{};
    if(!read(image+0x11DDB0,actual,sizeof(actual))||std::memcmp(actual,expected,sizeof(actual)))return false;
    unsigned char reserveHead[]={0xA1,0,0,0,0,0x56,0x8B,0xF1,0x8B,0x88,0xA8,0,0,0,0x80,0xB9,0xB4,0,0,0,0};
    const auto system=static_cast<std::uint32_t>(image+0x6875F0);std::memcpy(reserveHead+1,&system,4);
    const unsigned char reserveTail[]={0x8B,0x86,0xDC,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0x5E,0xC2,4,0};
    unsigned char typeGetter[]={0xA1,0,0,0,0,0xC3};
    const auto playerType=static_cast<std::uint32_t>(image+0x8860B8);std::memcpy(typeGetter+1,&playerType,4);
    const auto match=[&](std::uintptr_t rva,const auto& bytes){return read(image+rva,actual,sizeof(bytes))&&!std::memcmp(actual,bytes,sizeof(bytes));};
    return match(0x1216E0,reserveHead)&&match(0x121726,reserveTail)&&match(0x1F1C60,typeGetter);
}
struct AmmoObservation {std::uintptr_t player=0,weapon=0;unsigned side=1,magazine=0;bool reserveKnown=false;unsigned reserve=0;};
template<class Reader> bool CaptureReserve(std::uintptr_t image,std::uintptr_t player,std::uintptr_t weapon,Reader read,unsigned& output) {
    if(!Address(image,0x8860BC)||!Address(player,4)||!Address(weapon,0xE0))return false;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t system=0,config=0,reference=0,encoded=0,type=0,mask=0,ownerType=0,playerVtable=0,typeMethod=0;
    std::uint8_t override=0;
    if(!get(image+0x6875F0,system)||!get(std::uintptr_t(system)+0xA8,config)||
       !get(std::uintptr_t(config)+0xB4,override)||!get(weapon+0xDC,encoded)||!get(weapon+0x58,reference))return false;
    unsigned count=override?999u:(encoded^0xBEEBBEEBu);
    std::uintptr_t geometry=0;
    if(!override&&reference) {
        // Native 4F7670/45C6C0 resolves this ZREF to geometry+60, never a
        // direct pointer. Compressed references are left unknown here.
        std::uint32_t owner=0;
        if(!NativeWeaponOwnership::ResolveGeometry(image,reference,read,geometry)||
           !get(geometry+0x60,owner)||owner!=player||!get(player,playerVtable)||playerVtable!=image+0x56582C||
           !get(std::uintptr_t(playerVtable)+0x30,typeMethod)||typeMethod!=image+0x1F1C60||
           !get(image+0x8860B8,type)||!get(image+0x8531FC,mask)||!get(image+0x8531F8,ownerType))return false;
        if((type&mask)==ownerType)count=999;
    }
    if(count>1000000)return false;
    std::uint32_t afterSystem=0,afterConfig=0,afterReference=0,afterEncoded=0;
    std::uint8_t afterOverride=0;
    if(!get(image+0x6875F0,afterSystem)||afterSystem!=system||!get(std::uintptr_t(system)+0xA8,afterConfig)||afterConfig!=config||
       !get(std::uintptr_t(config)+0xB4,afterOverride)||afterOverride!=override||
       !get(weapon+0xDC,afterEncoded)||afterEncoded!=encoded||!get(weapon+0x58,afterReference)||afterReference!=reference)return false;
    if(geometry) {
        std::uintptr_t afterGeometry=0;std::uint32_t owner=0,afterType=0,afterMask=0,afterOwnerType=0;
        if(!NativeWeaponOwnership::ResolveGeometry(image,reference,read,afterGeometry)||afterGeometry!=geometry||
           !get(geometry+0x60,owner)||owner!=player||!get(image+0x8860B8,afterType)||afterType!=type||
           !get(image+0x8531FC,afterMask)||afterMask!=mask||!get(image+0x8531F8,afterOwnerType)||afterOwnerType!=ownerType)return false;
    }
    output=count;return true;
}
template<class Reader> bool CaptureAmmo(std::uintptr_t image,std::uintptr_t expectedPlayer,Reader read,AmmoObservation& output) {
    if(!Address(image,0x8860BC))return false;
    const auto get=[&](std::uintptr_t address,auto& value){return Address(address,sizeof(value))&&read(address,&value,sizeof(value));};
    std::uint32_t data=0,player=0,playerVtable=0;
    if(!expectedPlayer||!get(image+0x68760C,data)||!get(data+0xA2C,player)||player!=expectedPlayer||
       !get(player,playerVtable)||playerVtable!=image+0x56582C)return false;
    // Prefer the firearm actually held in the right native hand. The same
    // long gun may also occupy the left support hand, so duplicate isn't dual.
    for(unsigned choice=0;choice<2;++choice) {
        const unsigned side=1-choice;const auto hand=std::uintptr_t(player)+(side?0x358:0x2D8);
        std::uint32_t handVtable=0,weapon=0,attacher=0,vtable=0,getter=0,setter=0,encoded=0,cheat=0;
        std::uint8_t enabled=0;
        if(!get(hand,handVtable)||handVtable!=image+0x5508B4||!get(hand+0x60,weapon)||!weapon||
           !get(hand+0x64,enabled)||!enabled||!get(hand+0x68,attacher)||attacher!=(side?31u:32u)||
           !get(weapon,vtable)||(vtable!=image+0x54EA4C&&vtable!=image+0x577B74)||
           !get(std::uintptr_t(vtable)+0x308,getter)||getter!=image+0x11DDB0||
           !get(std::uintptr_t(vtable)+0x2D4,setter)||setter!=image+0x11DF40||
           !get(std::uintptr_t(weapon)+0xD8,encoded)||!get(image+0x873680,cheat))continue;
        const unsigned magazine=cheat?999u:(encoded^0xBEEBBEEBu);
        if(magazine>10000)return false;
        unsigned reserve=0;const bool reserveKnown=CaptureReserve(image,player,weapon,read,reserve);
        std::uint32_t afterData=0,afterPlayer=0,afterWeapon=0,afterVtable=0,afterEncoded=0,afterCheat=0,afterAttacher=0;
        std::uint8_t afterEnabled=0;
        if(!get(image+0x68760C,afterData)||afterData!=data||!get(std::uintptr_t(data)+0xA2C,afterPlayer)||afterPlayer!=player||
           !get(hand+0x60,afterWeapon)||afterWeapon!=weapon||!get(hand+0x64,afterEnabled)||afterEnabled!=enabled||
           !get(hand+0x68,afterAttacher)||afterAttacher!=attacher||!get(weapon,afterVtable)||afterVtable!=vtable||
           !get(std::uintptr_t(weapon)+0xD8,afterEncoded)||afterEncoded!=encoded||
           !get(image+0x873680,afterCheat)||afterCheat!=cheat)return false;
        output={player,weapon,side,magazine,reserveKnown,reserve};return true;
    }
    return false;
}
inline const wchar_t* ControlsText(unsigned side,bool supportGrip=true) {
    return side?L"RIGHT HAND\nTrigger: fire\nA tap release: reload\nA hold: sprint\nB tap: crouch / stand\nB hold: cover\nGrip hold: grenade\nRelease Grip: throw\nX: cancel / Y: type\nStick: turn / click: recenter":
                (supportGrip?L"LEFT HAND\nX tap release: interact\nX hold: team / ammo / help\nY tap: switch weapon\nY hold: weapon inventory\nGrip: hold fore-end\nStick: move / click: walk-run\nMenu: pause":
                             L"LEFT HAND\nX tap release: interact\nX hold: team / ammo / help\nY tap: switch weapon\nY hold: weapon inventory\nForegrip disabled\nStick: move / click: walk-run\nMenu: pause");
}
struct Panel {Vec3 centre{},right{},up{};float width=0,height=0;};
inline bool NearHand(const Frame& frame,unsigned side,float width,float height,float across,float above,Panel& output) {
    if(side>1||!frame.tracked[side]||!ValidatePose(frame.wrist[side],.05f)||!ValidatePose(frame.centre,.05f)||
       !std::isfinite(frame.unitsPerMetre)||frame.unitsPerMetre<1||frame.unitsPerMetre>1000||
       !std::isfinite(width)||!std::isfinite(height)||width<=0||width>.5f||height<=0||height>.4f||
       !std::isfinite(across)||!std::isfinite(above)||std::fabs(across)>.4f||std::fabs(above)>.4f)return false;
    const auto delta=Subtract(frame.wrist[side].position,frame.centre.position);
    const float distance=Dot(delta,delta)/(frame.unitsPerMetre*frame.unitsPerMetre);
    if(!std::isfinite(distance)||distance<.0144f||distance>2.25f)return false;
    Panel next;next.right=frame.centre.basis.right;next.up=frame.centre.basis.up;
    next.centre=Add(frame.wrist[side].position,Scale(Add(Scale(next.right,across),Scale(next.up,above)),frame.unitsPerMetre));
    next.width=width*frame.unitsPerMetre;next.height=height*frame.unitsPerMetre;output=next;return true;
}
struct Vertex {float x=0,y=0,z=0,rhw=1;std::uint32_t color=0xFFFFFFFFu;float u=0,v=0;};
inline bool Project(const CameraPose& eye,const float* projection,unsigned width,unsigned height,Vec3 world,float u,float v,Vertex& output) {
    if(!projection||!width||!height||width>16384||height>16384||!ValidatePose(eye,.05f)||!Finite(world))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
    const auto delta=Subtract(world,eye.position);
    const Vec3 camera{Dot(delta,eye.basis.right),Dot(delta,eye.basis.up),Dot(delta,eye.basis.forward)};
    const float x=camera.x*projection[0]+camera.y*projection[4]+camera.z*projection[8]+projection[12];
    const float y=camera.x*projection[1]+camera.y*projection[5]+camera.z*projection[9]+projection[13];
    const float w=camera.x*projection[3]+camera.y*projection[7]+camera.z*projection[11]+projection[15];
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||w<=1e-4f||std::fabs(x/w)>8||std::fabs(y/w)>8)return false;
    output={(x/w+1)*float(width)*.5f-.5f,(1-y/w)*float(height)*.5f-.5f,0,1.f/w,0xFFFFFFFFu,u,v};return true;
}
struct RightWristLayout {float x=0,wheelY=0,weaponY=0,wheelScale=0,weaponScale=0;};
inline bool PlaceRightWrist(const Frame& frame,unsigned width,unsigned height,RightWristLayout& output) {
    Panel anchor;
    if(!NearHand(frame,1,.28f,.06f,.010f,.22f,anchor))return false;
    const float depth=Dot(Subtract(anchor.centre,frame.centre.position),frame.centre.basis.forward)/frame.unitsPerMetre;
    Vertex centre;
    if(!std::isfinite(depth)||depth<.12f||!Project(frame.actualEye,frame.projection,width,height,anchor.centre,.5f,.5f,centre))return false;
    const float scale=std::clamp(.42f/depth,.45f,1.35f);
    const float y=1.f-(centre.y+.5f)*2.f/height;
    // Both native elements form one stack. Their centres and spacing use
    // the same projection and scale, including at the near/far clamps.
    // The outer edge of the wheel follows the minimap's lower edge. The
    // compact weapon face starts at the same height as the first team row.
    output={(centre.x+.5f)*2.f/width-1.f,y-.004f*scale,y-.214f*scale,.36f*scale,(.32f/.42f)*scale};
    return true;
}
inline bool Quad(const Panel& panel,const CameraPose& eye,const float* projection,unsigned width,unsigned height,std::array<Vertex,6>& output) {
    if(!Finite(panel.centre)||!Finite(panel.right)||!Finite(panel.up)||!std::isfinite(panel.width)||!std::isfinite(panel.height)||
       panel.width<=0||panel.height<=0||std::fabs(Dot(panel.right,panel.right)-1)>.05f||
       std::fabs(Dot(panel.up,panel.up)-1)>.05f||std::fabs(Dot(panel.right,panel.up))>.05f)return false;
    const auto corner=[&](float x,float y){return Add(panel.centre,Add(Scale(panel.right,x*panel.width*.5f),Scale(panel.up,y*panel.height*.5f)));};
    Vertex a,b,c,d;
    if(!Project(eye,projection,width,height,corner(-1,1),0,0,a)||!Project(eye,projection,width,height,corner(1,1),1,0,b)||
       !Project(eye,projection,width,height,corner(1,-1),1,1,c)||!Project(eye,projection,width,height,corner(-1,-1),0,1,d))return false;
    output={a,b,c,a,c,d};return true;
}
} // namespace NativeHandHudKL
