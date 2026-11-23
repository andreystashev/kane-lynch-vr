// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "reload_math_kl.hpp"
#include <algorithm>
#include <cstdint>

// Portable gestures only. NativeReloadKL owns the actual game's action and
// ammunition; this state machine never writes inventory or grants rounds.
namespace PhysicalReloadKL {
using namespace NativeCameraMath;
enum class Phase : unsigned {Inactive,Supply,Holding,Seated,Finishing,Tracking,Ready,NeedsChamber,Slide};
// This is a family whitelist only. The native adapter additionally requires
// measured detachable-clip contacts for non-pistols; category6 also contains
// shell-loaded shotguns and must never bypass that proof.
inline bool Supported(unsigned category){return (category<=6&&category!=3)||category==15||category==16||category==17;}
enum class Loader : unsigned {Native,MagazineOnly,MagazineAndSlide,Cylinder,Shell};
inline Loader LoaderForContacts(unsigned category,bool frame,bool magazine,bool slide,bool cylinder=false,bool magazineOnly=false){
    if(!Supported(category))return Loader::Native;
    // Keep the accepted pistol fallback until measured contact arrives.
    if(category==0)return Loader::MagazineAndSlide;
    if(category==1)return frame&&magazine&&cylinder?Loader::Cylinder:Loader::Native;
    if(!frame||!magazine)return Loader::Native;
    if(slide)return Loader::MagazineAndSlide;
    // Some SMGs have a real detachable magazine and no rendered bolt.
    // Insertion readies their magazine-only mechanism.
    // Measured AK and Hawk magazines permit insertion without a separate slide.
    // The Type 88 uses the original reload animation.
    // Never manufacture a moving bolt or contact from whole-frame bounds.
    return category==2||((category==4||category==17)&&magazineOnly)?Loader::MagazineOnly:Loader::Native;
}
// Offsets use the shared native helper's right/up/forward coordinates.
struct Profile {float length=.115f,width=.027f,depth=.035f;Vec3 well{0,-.065f,.015f};};
inline Profile ForCategory(unsigned category){
    Profile p;if(category==2){p.length=.18f;p.well={0,-.055f,.045f};}
    else if(category!=0){p.length=category==15?.11f:.19f;p.width=.035f;p.depth=.06f;p.well={0,-.055f,.14f};}return p;
}
inline CameraPose Receiver(const CameraPose& gun,const Profile& p,float units){
    CameraPose out;out.position=Add(gun.position,Scale(ReloadMathKL::Transform(gun.basis,p.well),units));
    out.basis={gun.basis.up,gun.basis.forward,Scale(gun.basis.right,-1)};return out;
}
// The renderer publishes a WRIST-LOCAL socket in metres. Its F axis need
// not be the barrel (the recorded rifle uses a different authored axis).
// Preserve that local translation, and construct the loading frame from
// the actual barrel ray expressed in the SAME wrist coordinates.
inline bool ReceiverLocal(CameraPose socketMetres,Vec3 localBarrel,unsigned category,float units,CameraPose& out){
    if(!Supported(category)||!ValidatePose(socketMetres,.05f)||!Finite(localBarrel)||!std::isfinite(units)||units<1||units>1000)return false;
    const auto unit=[](Vec3 v,Vec3& result){const float n=std::sqrt(Dot(v,v));if(!std::isfinite(n)||n<.001f)return false;result=Scale(v,1.f/n);return true;};
    Vec3 forward,up;if(!unit(localBarrel,forward)||!unit(Subtract(socketMetres.basis.up,Scale(forward,Dot(socketMetres.basis.up,forward))),up))return false;
    socketMetres.position=Scale(socketMetres.position,units);socketMetres.basis={forward,up,Cross(up,forward)};
    const auto next=Receiver(socketMetres,ForCategory(category),units);if(!ValidatePose(next,.05f))return false;out=next;return true;
}
// Pose origin is the feed tip, forward points into the receiver. The solid
// magazine extends back toward the palm, keeping the feed lips visible.
inline CameraPose Magazine(const CameraPose& palm,float units){
    auto out=palm;out.position=Add(palm.position,Scale(palm.basis.forward,.065f*units));return out;
}
struct Input {
    bool valid=false,blocked=false;float grip=0,units=100;
    std::uint32_t tick=0,sample=0,scene=0,reference=UINT32_MAX;
    CameraPose palm{},receiver{};
};
struct State {
    Phase phase=Phase::Inactive;std::uint32_t scene=0,reference=UINT32_MAX,lastSample=0,lastTick=0,since=0,contactSince=0;
    bool armed=false,down=false,holdingTimer=false,contactTimer=false;
    void Begin(std::uint32_t epoch,std::uint32_t ack){*this={};scene=epoch;reference=ack;phase=Phase::Supply;}
    bool Active()const{return phase!=Phase::Inactive;}
    bool Held()const{return phase==Phase::Holding||phase==Phase::Seated;}
    bool Tick(const Input& in){
        if(!Active()||phase==Phase::Finishing)return false;
        const bool fresh=in.valid&&in.scene==scene&&in.reference==reference&&in.sample&&in.tick-in.sample<=150&&
            std::isfinite(in.grip)&&in.grip>=0&&in.grip<=1&&std::isfinite(in.units)&&in.units>=1&&in.units<=1000&&
            ValidatePose(in.palm,.05f)&&ValidatePose(in.receiver,.05f);
        if(!fresh||in.blocked||(lastTick&&in.tick-lastTick>250)){
            armed=false;holdingTimer=contactTimer=false;if(phase==Phase::Seated)phase=Phase::Holding;lastTick=in.tick;return false;
        }
        lastTick=in.tick;
        if(in.sample==lastSample)return false;lastSample=in.sample;
        if(!armed){if(in.grip<.4f){armed=true;down=false;}return false;}
        const bool next=in.grip>(down?.4f:.7f),press=next&&!down,release=!next&&down;down=next;
        if(phase==Phase::Supply){
            if(!next)holdingTimer=false;
            else if(press){holdingTimer=true;since=in.tick;}
            else if(holdingTimer&&in.tick-since>=400){phase=Phase::Holding;holdingTimer=false;}
            return false;
        }
        const auto magazine=Magazine(in.palm,in.units);const auto delta=Scale(Subtract(magazine.position,in.receiver.position),1.f/in.units);
        const float along=Dot(delta,in.receiver.basis.forward);const auto lateral=Subtract(delta,Scale(in.receiver.basis.forward,along));
        const bool contact=std::fabs(along)<.105f&&Dot(lateral,lateral)<.13f*.13f&&
            Dot(magazine.basis.forward,in.receiver.basis.forward)>-.3421f;
        if(!contact){contactTimer=false;if(phase==Phase::Seated)phase=Phase::Holding;}
        else if(!contactTimer){contactTimer=true;contactSince=in.tick;}
        else if(in.tick-contactSince>=100)phase=Phase::Seated;
        if(release){
            if(phase==Phase::Seated&&contact){phase=Phase::Finishing;return true;}
            phase=Phase::Supply;holdingTimer=contactTimer=false;
        }
        return false;
    }
};
inline const wchar_t* Hint(Phase phase){
    switch(phase){case Phase::Supply:return L"Hold free-hand GRIP: new magazine";
        case Phase::Holding:return L"Bring magazine to the highlighted receiver";
        case Phase::Seated:return L"Release GRIP to seat magazine";
        case Phase::Finishing:return L"Seating magazine...";case Phase::Tracking:return L"Waiting for tracked hands...";
        case Phase::NeedsChamber:return L"GRIP the slide, pull back and release";case Phase::Slide:return L"Pull back, then release the slide";default:return nullptr;}
}
struct WorldMagazine {bool valid=false,grounded=false;CameraPose pose{};Vec3 velocity{};unsigned category=0;int rounds=0;std::uintptr_t model=0;std::uint32_t serial=0;};
struct Visual {
    bool active=false,held=false,seated=false;unsigned hand=0,category=0;Phase phase=Phase::Inactive;
    std::uintptr_t player=0,item=0;std::uint32_t tick=0,scene=0,reference=UINT32_MAX;
    float units=100;CameraPose magazine{},receiver{},ejected{};std::uint32_t ejectedTick=0;
    bool gunValid=false,inserted=true,fireBlocked=false;float slideTravel=0;CameraPose slide{},gun{},weaponWrist{};
    WorldMagazine world[24]{};
    int magazineRounds=15;
};
}
