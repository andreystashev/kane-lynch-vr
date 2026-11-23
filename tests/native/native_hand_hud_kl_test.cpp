#include "native_hand_hud_kl.hpp"
#include <cstdio>
#include <map>
#include <limits>

namespace {
unsigned checks=0,failures=0;
void Check(bool okay,const char* text){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",text);}}
constexpr std::uint32_t Image=0x400000,Data=0x10000000,Player=0x11000000,Weapon=0x12000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t address,const T& value){const auto* source=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i)bytes[address+i]=source[i];}
    bool Read(std::uintptr_t address,void* output,std::size_t size)const{
        auto* result=static_cast<unsigned char*>(output);for(std::size_t i=0;i<size;++i){auto at=bytes.find(address+i);if(at==bytes.end())return false;result[i]=at->second;}return true;
    }
    auto Reader()const{return [&](auto address,void* output,auto size){return Read(address,output,size);};}
    Memory(){
        const unsigned char getter[]={0x83,0x3D,0x80,0x36,0xC7,0,0,0x74,6,0xB8,0xE7,3,0,0,0xC3,
            0x8B,0x81,0xD8,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0xC3};
        const unsigned char reserveHead[]={0xA1,0xF0,0x75,0xA8,0,0x56,0x8B,0xF1,0x8B,0x88,0xA8,0,0,0,0x80,0xB9,0xB4,0,0,0,0};
        const unsigned char reserveTail[]={0x8B,0x86,0xDC,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0x5E,0xC2,4,0};
        const unsigned char typeGetter[]={0xA1,0xB8,0x60,0xC8,0,0xC3};
        Set(Image+0x1216E0,reserveHead);Set(Image+0x121726,reserveTail);Set(Image+0x1F1C60,typeGetter);
        Set(Image+0x11DDB0,getter);Set(Image+0x68760C,Data);Set(Data+0xA2C,Player);Set(Player,std::uint32_t(Image+0x56582C));
        for(unsigned side=0;side<2;++side){const auto hand=Player+(side?0x358:0x2D8);
            Set(hand,std::uint32_t(Image+0x5508B4));Set(hand+0x60,side?Weapon:std::uint32_t(0));
            Set(hand+0x64,std::uint8_t(1));Set(hand+0x68,side?31u:32u);}
        Set(Weapon,std::uint32_t(Image+0x577B74));Set(Image+0x577B74+0x308,std::uint32_t(Image+0x11DDB0));
        Set(Image+0x577B74+0x2D4,std::uint32_t(Image+0x11DF40));Set(Weapon+0xD8,std::uint32_t(27^0xBEEBBEEBu));Set(Image+0x873680,std::uint32_t(0));
        Set(Image+0x6875F0,std::uint32_t(0x13000000));Set(0x130000A8,std::uint32_t(0x14000000));Set(0x140000B4,std::uint8_t(0));
        Set(Weapon+0xDC,std::uint32_t(90^0xBEEBBEEBu));Set(Weapon+0x58,std::uint32_t(0));
    }
    void Owner(){
        Set(Weapon+0x58,std::uint32_t(1));Set(Image+0x6C99B8,std::uint32_t(0x15000000));Set(0x1500000C,std::uint32_t(0x16000000));
        Set(0x16000004,std::uint32_t(0x70));Set(0x16000008,std::uint32_t(4));Set(0x1600000C,std::uint32_t(0x17000000));
        Set(0x16000010,std::uint32_t(0x18000000));Set(0x18000000,std::uint16_t(0));Set(0x17000060,Player);
        Set(Image+0x56582C+0x30,std::uint32_t(Image+0x1F1C60));Set(Image+0x8860B8,std::uint32_t(0x1111));
        Set(Image+0x8531FC,std::uint32_t(0xF0));Set(Image+0x8531F8,std::uint32_t(0x30));
    }
};
NativeHandHudKL::Frame ValidFrame(){
    NativeHandHudKL::Frame frame;frame.player=Player;frame.pairId=3;frame.tick=1000;frame.recenterAck=0;
    frame.tracked[0]=frame.tracked[1]=true;
    frame.wrist[0].position={45,-15,18};frame.wrist[1].position={45,-15,-18};
    const float fov[]={1,1,1,1};NativeCameraMath::ApplyEyeProjection(frame.projection,fov,3,10000,false);return frame;
}
}
int main(){
    using namespace NativeHandHudKL;
    auto frame=ValidFrame();Check(Fresh(frame,1000),"fresh same-origin tracked frame accepted");
    Check(Fresh(frame,1150)&&!Fresh(frame,1151)&&!Fresh(frame,999),"tracking expiry and future tick rejected");
    for(unsigned property=0;property<7;++property){auto invalid=frame;
        switch(property){case 0:invalid.player=0;break;case 1:invalid.pairId=0;break;case 2:invalid.recenterAck=UINT32_MAX;break;
        case 3:invalid.weaponHand=2;break;case 4:invalid.unitsPerMetre=0;break;case 5:invalid.tracked[0]=invalid.tracked[1]=false;break;
        case 6:invalid.projection[11]=0;break;}Check(!Fresh(invalid,1000),"incomplete scene/tracking identity rejected");}
    auto wrap=frame;wrap.tick=0xfffffff0u;Check(Fresh(wrap,0x20),"tick wrap retains a genuinely fresh frame");
    auto lost=frame;lost.tracked[0]=false;lost.wrist[0].position.x=std::numeric_limits<float>::quiet_NaN();
    Check(Fresh(lost,1000),"inactive wrist cannot invalidate the surviving controller");
    Panel panel;Check(!NearHand(lost,0,.2f,.05f,0,0,panel),"tracking loss never creates a centre-screen card");
    Check(NearHand(frame,0,.2f,.05f,0,0,panel),"panel follows the actual left wrist");
    Check(std::fabs(panel.centre.x-45)<.001f&&std::fabs(panel.centre.z-18)<.001f&&std::fabs(panel.width-20)<.001f,"metres converted once to game units");
    std::array<Vertex,6> left{},right{};auto leftEye=frame.actualEye,rightEye=frame.actualEye;
    leftEye.position=Scale(frame.centre.basis.right,-3.2f);rightEye.position=Scale(frame.centre.basis.right,3.2f);
    Check(Quad(panel,leftEye,frame.projection,1920,1080,left)&&Quad(panel,rightEye,frame.projection,1920,1080,right),"same wrist panel projects independently into both real eyes");
    Check(left[0].x>right[0].x&&std::fabs(left[0].y-right[0].y)<.001f,"64 mm IPD gives horizontal stereo disparity only");
    Check(left[0].u==0&&left[0].v==0&&left[2].u==1&&left[2].v==1,"card texture orientation retained");
    RightWristLayout wrist{},moved{},nearRight{},farRight{};
    Check(PlaceRightWrist(frame,1920,1080,wrist)&&wrist.wheelY>wrist.weaponY&&wrist.wheelScale>0&&wrist.weaponScale>0,"native inventory sits above weapon and ammo on one right wrist axis");
    auto leftMoved=frame;leftMoved.wrist[0].position={75,35,45};leftMoved.tracked[0]=false;
    Check(PlaceRightWrist(leftMoved,1920,1080,moved)&&moved.x==wrist.x&&moved.weaponY==wrist.weaponY&&moved.wheelY==wrist.wheelY,"left wrist motion and tracking cannot shift either right widget");
    auto moving=frame;moving.wrist[1].position.y+=10;
    Check(PlaceRightWrist(moving,1920,1080,moved)&&std::fabs((moved.wheelY-wrist.wheelY)-(moved.weaponY-wrist.weaponY))<.0001f,"raising the right controller moves both widgets together");
    moving.wrist[1].position.x=20;Check(PlaceRightWrist(moving,1920,1080,nearRight),"near right wrist clamps together");
    moving.wrist[1].position.x=130;Check(PlaceRightWrist(moving,1920,1080,farRight)&&farRight.wheelScale<nearRight.wheelScale&&farRight.weaponScale<nearRight.weaponScale&&
        std::fabs((farRight.wheelY-farRight.weaponY)/farRight.wheelScale-(nearRight.wheelY-nearRight.weaponY)/nearRight.wheelScale)<.0001f,"distance scales both widgets and their spacing by the same factor");
    moving.tracked[1]=false;Check(!PlaceRightWrist(moving,1920,1080,moved),"tracking loss suppresses the entire right stack");
    auto far=frame;far.wrist[0].position={151,0,0};Check(!NearHand(far,0,.2f,.05f,0,0,panel),"unreachable tracked wrist suppressed");
    far.wrist[0].position={1,0,0};Check(!NearHand(far,0,.2f,.05f,0,0,panel),"wrist at eye cannot explode into a full-screen card");
    Check(!NearHand(frame,2,.2f,.05f,0,0,panel)&&!NearHand(frame,0,.51f,.05f,0,0,panel),"invalid side and oversized cards rejected");
    Check(NearHand(frame,0,.2f,.05f,0,0,panel),"valid panel rebuilt");panel.centre={-50,0,0};
    Check(!Quad(panel,frame.actualEye,frame.projection,1920,1080,left),"card behind actual eye rejected");
    Check(!Project(frame.actualEye,frame.projection,16385,1080,{50,0,0},0,0,left[0]),"D3D native dimension bound respected");
    Telemetry telemetry;Check(AmmoText(telemetry)==L"AMMO\n-- / --"&&!ActionText(telemetry),"unknown native telemetry is explicit, no fabricated counters or interaction");
    telemetry.magazineKnown=true;telemetry.magazine=27;Check(AmmoText(telemetry)==L"AMMO\n27 / --"&&!AmmoKnown(telemetry),"partial native observation never invents reserve ammo");
    telemetry.reserveKnown=true;telemetry.reserve=90;Check(AmmoKnown(telemetry)&&AmmoText(telemetry)==L"AMMO\n27 / 90","confirmed counts displayed");
    telemetry.magazine=10001;Check(AmmoText(telemetry)==L"AMMO\n-- / 90"&&!AmmoKnown(telemetry),"corrupt magazine does not display as plausible ammo");
    telemetry.contextKnown=true;
    for(auto action:{Action::PickupWeapon,Action::OpenDoor,Action::CloseCombat,Action::Use,Action::Pickup,Action::Revive,Action::LeaveMounted,
                    Action::Select,Action::Talk,Action::Rappel,Action::EnterVehicle,Action::ExitVehicle,Action::TakeMounted,Action::ThrowBackGrenade,Action::GiveHealth,Action::Climb}){
        telemetry.action=action;const auto* text=ActionText(telemetry);Check(text&&text[0]==L'X',"eligible interaction uses actual X mapping");}
    telemetry.action=static_cast<Action>(999);Check(!ActionText(telemetry),"unknown native action ID stays hidden");
    NativeContextHudKL::State context;context.valid=context.eligible=context.textOverride=true;context.prompt=NativeContextHudKL::Prompt::Generic;
    std::strcpy(context.nativeText,"PickupPhone");SetContext(telemetry,context);
    Check(ActionText(telemetry)&&std::wcscmp(ActionText(telemetry),L"X  Pick up phone")==0,"near-hand prompt shows specific actual English X action without legacy keyboard tokens");
    context.eligible=false;SetContext(telemetry,context);Check(!ActionText(telemetry),"context retirement clears the previous near-hand X label");
    Check(std::wstring(ControlsText(1)).find(L"A tap release: reload")!=std::wstring::npos&&std::wstring(ControlsText(1)).find(L"B tap: crouch")!=std::wstring::npos&&
          std::wstring(ControlsText(1)).find(L"B hold: cover")!=std::wstring::npos&&std::wstring(ControlsText(1)).find(L"Grip hold: grenade")!=std::wstring::npos,
          "controls distinguish A release reload B cover and right grip grenade");
    Check(std::wstring(ControlsText(0)).find(L"X hold: team")!=std::wstring::npos&&std::wstring(ControlsText(0)).find(L"Y hold: weapon inventory")!=std::wstring::npos&&
          std::wstring(ControlsText(0,false)).find(L"X hold: team")!=std::wstring::npos&&std::wstring(ControlsText(1)).find(L"click: recenter")!=std::wstring::npos,
          "controls show X and Y hold owners independent of optional foregrip");
    Memory memory;const auto original=memory.bytes;AmmoObservation ammo;
    Check(VerifyAmmoImage(Image,memory.Reader()),"pinned magazine getter code verified");
    Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&ammo.magazine==27&&ammo.weapon==Weapon&&ammo.side==1,"owned right native firearm magazine captured");
    Check(ammo.reserveKnown&&ammo.reserve==90,"native outside-magazine DC count captured independently");
    Check(memory.bytes==original,"native ammo observation performs no writes");
    memory.Set(Image+0x873680,std::uint32_t(1));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&ammo.magazine==999,"native infinite-ammo display override retained");
    memory=Memory{};memory.Set(0x140000B4,std::uint8_t(1));
    Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&ammo.reserveKnown&&ammo.reserve==999,"native spare-ammo config override retained");
    memory=Memory{};memory.Owner();Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&ammo.reserveKnown&&ammo.reserve==90,"normal ZREF proves exact local owner before class-dependent spare count");
    memory.Set(Image+0x8860B8,std::uint32_t(0x1131));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&ammo.reserveKnown&&ammo.reserve==999,"native owner-type spare override retained");
    memory.Set(0x17000060,std::uint32_t(Player+4));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&!ammo.reserveKnown&&ammo.magazine==27,"foreign owner suppresses only unproven reserve");
    memory=Memory{};memory.Owner();memory.Set(0x18000000,std::uint16_t(1));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&!ammo.reserveKnown,"reused ZREF generation cannot produce spare ammo");
    memory=Memory{};memory.Set(Weapon+0x58,std::uint32_t(0xC0000001));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&!ammo.reserveKnown,"compressed native owner references remain unknown");
    memory=Memory{};memory.Set(Weapon+0xDC,std::uint32_t(1000001^0xBEEBBEEBu));Check(CaptureAmmo(Image,Player,memory.Reader(),ammo)&&!ammo.reserveKnown,"corrupt spare count retains known magazine only");
    memory=Memory{};memory.Set(Weapon+0xD8,std::uint32_t(10001^0xBEEBBEEBu));Check(!CaptureAmmo(Image,Player,memory.Reader(),ammo),"malformed encoded counter rejected");
    for(unsigned broken=0;broken<7;++broken){memory=Memory{};
        switch(broken){case 0:memory.Set(Data+0xA2C,std::uint32_t(Player+4));break;case 1:memory.Set(Player,std::uint32_t(0));break;
        case 2:memory.Set(Player+0x358+0x64,std::uint8_t(0));break;case 3:memory.Set(Player+0x358+0x68,std::uint32_t(32));break;
        case 4:memory.Set(Weapon,std::uint32_t(Image+0x577B78));break;case 5:memory.Set(Image+0x577B74+0x308,std::uint32_t(0));break;
        case 6:memory.bytes.erase(Weapon+0xD8);break;}Check(!CaptureAmmo(Image,Player,memory.Reader(),ammo),"ownership/class/getter/read failure cannot leak stale ammo");}
    memory=Memory{};unsigned reads=0;auto switched=[&](auto at,void* output,auto size){if(at==Player+0x358+0x60&&++reads==2)memory.Set(at,std::uint32_t(0));return memory.Read(at,output,size);};
    Check(!CaptureAmmo(Image,Player,switched,ammo),"weapon replacement while sampling rejected");
    memory=Memory{};reads=0;auto fired=[&](auto at,void* output,auto size){if(at==Weapon+0xD8&&++reads==2)memory.Set(at,std::uint32_t(26^0xBEEBBEEBu));return memory.Read(at,output,size);};
    Check(!CaptureAmmo(Image,Player,fired,ammo),"concurrent native shot produces no torn ammo snapshot");
    memory=Memory{};memory.Set(Image+0x11DDB0,std::uint8_t(0x90));Check(!VerifyAmmoImage(Image,memory.Reader()),"changed native getter disables observation");
    memory=Memory{};memory.Set(Image+0x121726,std::uint8_t(0x90));Check(!VerifyAmmoImage(Image,memory.Reader()),"changed native reserve getter disables observation");
    Check(!CaptureAmmo(0,Player,memory.Reader(),ammo)&&!CaptureAmmo(0xffff0000u,Player,memory.Reader(),ammo),"wrapped module addresses rejected before reads");
    std::printf("Native KL1 hand HUD: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
