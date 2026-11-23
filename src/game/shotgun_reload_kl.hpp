#pragma once
#include "physical_reload_runtime_kl.hpp"
#include "reload_contact_geometry_kl.hpp"
#include "reload_palm_kl.hpp"
#include "reload_parts_kl.hpp"
#include <cstring>

namespace ShotgunReloadKL {
using namespace NativeCameraMath;
namespace Geometry = ReloadContactGeometryKL;
namespace Runtime = PhysicalReloadRuntimeKL;
using PhysicalReloadKL::Phase;

constexpr unsigned Category=5;
constexpr unsigned ShellsPerGrip=4;
constexpr float PumpRearMetres=.05f, PumpForwardMetres=.020f;
constexpr unsigned PumpReturnMs=135, InsertMs=190, ShellStaggerMs=145;
constexpr float PumpTravelMetres=.075f;

struct Parts {
    std::uintptr_t resourceModel=0;
    bool catalogRead=false;
    ReloadPartsKL::Part shell{}, hatch{};
    CameraPose frameSkin{};
    std::uint32_t frameTick=0, slideTick=0;
};

// The hatch in the observed seven-bone shotgun rig is a socket without a
// rigid primitive. Its exact named bind pose supplies the loading contact.
template<class Read> void FindSockets(std::uintptr_t image, const ReloadPartsKL::Part& frame,
                                      Read read, Parts& parts) {
    if(parts.resourceModel==frame.model&&parts.catalogRead)return;
    parts={};parts.resourceModel=frame.model;
    ReloadPartsKL::Catalog catalog;
    if(!ReloadPartsKL::ModelCatalog(image,frame.model,read,catalog))return;
    parts.catalogRead=true;
    for(unsigned index=0;index<catalog.count;++index){
        ReloadPartsKL::Part part;
        if(!ReloadPartsKL::Bone(catalog,index,read,part)||!part.boundsValid||!part.bindValid)continue;
        if(!std::strcmp(part.name,"w_bone_shell"))parts.shell=part;
        if(!std::strcmp(part.name,"w_bone_shellhatch"))parts.hatch=part;
    }
}

struct Rig {
    CameraPose contact{}, seated{};
    Vec3 shellHalf{};
};

inline Vec3 FingerOffset(const CameraPose& palm,Vec3 half,float units,bool breech){
    return Add(Scale(palm.basis.right,(breech?.028f:.032f)*units),
               Add(Scale(palm.basis.up,.022f*units),Scale(palm.basis.forward,-half.x-(breech?.016f:.007f)*units)));
}

inline CameraPose ShellGrip(const CameraPose& palm,Vec3 half,float units,bool breech=false){
    const auto frame=ReloadPalmKL::Grasp(palm);
    return {frame.basis,Add(palm.position,FingerOffset(palm,half,units,breech))};
}

inline CameraPose HeldShell(const CameraPose& first,unsigned index,Vec3 half,float units){
    auto shell=first;
    const float spacing=std::max(2.f*half.x+.001f*units,.018f*units);
    shell.position=Add(first.position,Add(Scale(first.basis.right,float(index)*spacing),
        Scale(first.basis.forward,-.008f*float(index)*units)));
    return shell;
}

inline CameraPose PalmForShell(const CameraPose& shell,Vec3 half,float units,bool breech=false){
    auto palm=ReloadPalmKL::FromGrasp(shell);
    palm.position=Subtract(shell.position,FingerOffset(palm,half,units,breech));
    return palm;
}

inline bool Measure(const CameraPose& shellBone, Vec3 centre, Vec3 half,
                    const CameraPose& hatch, const CameraPose& gun, float units, Rig& out) {
    if(!ValidatePose(hatch,.05f)||!ValidatePose(gun,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    Geometry::Bounds shell;
    if(!Geometry::BuildBounds(shellBone,centre,half,shell))return false;
    // The actual shell primitive is elongated on its forward axis. Its
    // cross-section and gap from the authored hatch bound this mechanism.
    if(half.x<.003f*units||half.y<.003f*units||half.x>.023f*units||half.y>.023f*units||
       half.z<.018f*units||half.z>.065f*units||half.z<2.f*std::max(half.x,half.y))return false;
    const auto axis=shellBone.basis.forward;
    if(Dot(axis,gun.basis.forward)<.75f)return false;
    const auto offset=Subtract(shell.world.position,hatch.position);
    const float along=Dot(offset,axis);
    const auto across=Subtract(offset,Scale(axis,along));Vec3 approach;
    const float acrossLength=Dot(across,across);
    if(std::fabs(along)>.085f*units||acrossLength<.010f*.010f*units*units||
       acrossLength>.090f*.090f*units*units||!ReloadPresentationKL::Unit(across,approach))return false;
    Rig result;
    result.seated={ {axis,approach,Cross(approach,axis)},shell.world.position };
    result.contact=result.seated;result.contact.position=hatch.position;
    result.shellHalf={Geometry::Extent(shell,result.contact.basis.right),
                      Geometry::Extent(shell,result.contact.basis.up),half.z};
    if(!ValidatePose(result.seated,.05f)||!ValidatePose(result.contact,.05f))return false;
    out=result;return true;
}

inline bool AtHatch(const CameraPose& held,const CameraPose& palm,const CameraPose& contact,float units,bool guiding,bool integrated) {
    const float margin=guiding?.045f:0.f;
    if(integrated){
        // Match the whole underside of the receiver when its loading port
        // is part of the frame mesh. A rolled palm still reaches this area.
        const auto nearReceiver=[&](Vec3 point){
            auto d=Scale(ReloadMathKL::Inverse(contact.basis,Subtract(point,contact.position)),1.f/units);
            d.z-=std::clamp(d.z,-.062f,.082f);
            const float x=d.x/(.12f+margin),y=(d.y+.018f)/(.095f+margin),z=d.z/(.075f+margin);
            return x*x+y*y+z*z<=1.f;
        };
        return nearReceiver(held.position)||nearReceiver(palm.position);
    }
    const auto d=Scale(ReloadMathKL::Inverse(contact.basis,Subtract(held.position,contact.position)),1.f/units);
    return std::fabs(d.x)<=.105f+margin&&std::fabs(d.z)<=.115f+margin&&d.y>=-.125f-margin&&d.y<=.055f+margin&&
        Dot(held.basis.forward,contact.basis.forward)>=(guiding?.05f:.15f);
}

inline bool TubeWithoutParts(const CameraPose& gun,Vec3 muzzle,float units,Rig& rig,Geometry::SlideContact& pump,
                             const Geometry::Bounds* frame=nullptr,const Vec3* foregrip=nullptr){
    if(!ValidatePose(gun,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    const auto delta=Scale(Subtract(muzzle,gun.position),1.f/units);
    const float length=Dot(delta,gun.basis.forward);
    const auto lateral=Subtract(delta,Scale(gun.basis.forward,length));
    if(!Finite(delta)||length<.39f||length>.95f||Dot(lateral,lateral)>.25f*.25f)return false;
    // The wrist is below the weapon on some authored grips. Use the visible
    // barrel line and measured receiver for both contacts in every pose.
    const auto barrel=Scale(ReloadMathKL::Inverse(gun.basis,lateral),units);
    Vec3 port{barrel.x,barrel.y-.055f*units,.25f*length*units};
    if(frame&&ValidatePose(frame->world,.05f)){
        const auto centre=ReloadMathKL::ToLocal(gun,frame->world).position;
        const float height=Geometry::Extent(*frame,gun.basis.up);
        if(Finite(centre)&&height>.025f*units&&height<.24f*units){
            port.x=std::clamp(centre.x,barrel.x-.018f*units,barrel.x+.018f*units);
            port.y=std::clamp(centre.y-.38f*height,port.y-.030f*units,port.y+.040f*units);
        }
    }
    rig.contact={gun.basis,Add(gun.position,ReloadMathKL::Transform(gun.basis,port))};
    rig.seated=rig.contact;
    rig.seated.position=Add(rig.contact.position,Scale(gun.basis.up,.035f*units));
    rig.shellHalf=Scale(Vec3{.009f,.009f,.0325f},units);
    Vec3 pumpCentre{barrel.x,barrel.y-.026f*units,.63f*length*units};
    if(foregrip&&Finite(*foregrip)){
        const auto grip=ReloadMathKL::Inverse(gun.basis,Subtract(*foregrip,gun.position));
        if(grip.z>.38f*length*units&&grip.z<.80f*length*units&&
           std::fabs(grip.x-barrel.x)<.075f*units&&std::fabs(grip.y-pumpCentre.y)<.13f*units){
            pumpCentre.z=grip.z;
            // The socket is a left wrist pose below the fore-end. Use it to
            // locate the grip along the barrel and keep the contact on the mesh.
        }
    }
    pump.box.world={gun.basis,Add(gun.position,ReloadMathKL::Transform(gun.basis,pumpCentre))};
    pump.box.half=Scale(Vec3{.026f,.027f,.083f},units);
    return true;
}

inline bool CanGrabPump(bool sharedForegrip,bool needsPump,int rounds){
    return !sharedForegrip||(needsPump&&rounds>0);
}

inline bool PumpContact(const Geometry::SlideContact& pump,Vec3 palm,float units,Vec3& anchor,bool breech=false,bool integrated=false){
    const auto& box=pump.box;
    if(!ValidatePose(box.world,.05f)||!Finite(box.half)||!Finite(palm)||!std::isfinite(units)||units<1||units>1000||
       box.half.x<=0||box.half.y<=0||box.half.z<=0)return false;
    auto q=ReloadMathKL::Inverse(box.world.basis,Subtract(palm,box.world.position));
    const float sideMargin=breech?.07f:.04f;
    if(std::fabs(q.x)>box.half.x+sideMargin*units||std::fabs(q.z)>box.half.z+(breech?.055f:.034f)*units||
       q.y < -box.half.y-(integrated?.030f:.065f)*units||q.y>box.half.y+(breech?.065f:.025f)*units)return false;
    q.x=std::clamp(q.x,-box.half.x,box.half.x);
    q.y=std::clamp(q.y,-box.half.y-.018f*units,box.half.y);
    q.z=std::clamp(q.z,-box.half.z,box.half.z);
    anchor=Add(box.world.position,ReloadMathKL::Transform(box.world.basis,q));
    return true;
}

struct Frame {
    bool valid=false, supply=false, supportHeld=false, breech=false, integrated=false, boltButton=false, scoped=false;
    std::uint32_t tick=0,sample=0,reference=0;
    float units=100.f,grip=0;
    CameraPose gun{}, palm{}, cartridge{};
    Vec3 driver{};
    Geometry::SlideContact pump{};
    Rig rig{};
};

struct State {
    Phase phase=Phase::Ready;
    bool needsPump=false,held=false,armed=false,down=false,returning=false,opening=false,handClosing=false;
    unsigned carried=0,committed=0;bool boltOpen=false;
    bool hadAmmo=false,ammoKnown=false;
    std::uint32_t reference=0,lastTick=0,lastSample=0,contactSince=0,lastContact=0,releaseSince=0;
    bool touching=false,seating=false,pumpPulled=false;
    float startZ=0,slideTravel=0,pumpReturn=0;
    std::array<CameraPose,ShellsPerGrip> shellPoses{},releaseStarts{};
    Vec3 pumpAnchorLocal{};

    void Suspend() {
        phase=needsPump?Phase::NeedsChamber:Phase::Ready;
        held=armed=down=returning=opening=handClosing=touching=seating=pumpPulled=false;carried=committed=0;
        slideTravel=boltOpen?PumpTravelMetres:0;
    }

    void AnimateBolt(bool open,std::uint32_t tick){
        opening=open;returning=!open;handClosing=false;releaseSince=tick;pumpReturn=slideTravel;phase=Phase::Slide;
    }

    unsigned Step(const Frame& f, Runtime::Ammo& ammo) {
        if(!Runtime::Valid(ammo)){Suspend();return 0;}
        if(!ammo.count)needsPump=true;
        if(!ammoKnown){ammoKnown=true;hadAmmo=ammo.count!=0;}
        else if(!hadAmmo&&ammo.count)needsPump=false; // An external native refill.
        hadAmmo=ammo.count!=0;
        ammo.ready=ammo.count>0&&!needsPump&&!boltOpen;
        const bool tracking=f.valid&&f.sample&&f.tick-f.sample<=150&&std::isfinite(f.units)&&f.units>=1&&f.units<=1000&&
            std::isfinite(f.grip)&&f.grip>=0&&f.grip<=1&&ValidatePose(f.gun,.05f)&&ValidatePose(f.palm,.05f)&&
            ValidatePose(f.cartridge,.05f)&&ValidatePose(f.rig.contact,.05f)&&ValidatePose(f.rig.seated,.05f)&&Finite(f.driver);
        if(!tracking||(lastTick&&(f.tick-lastTick>250||reference!=f.reference))){
            Suspend();lastTick=f.tick;reference=f.reference;return 0;
        }
        lastTick=f.tick;reference=f.reference;
        if(f.sample==lastSample)return 0;
        lastSample=f.sample;
        if(!armed){
            if(f.grip<.4f)armed=true;
            if(!(f.breech&&(f.boltButton||opening||returning)))return 0;
        }
        const bool gripping=armed&&f.grip>(down?.4f:.7f),pressed=gripping&&!down,released=!gripping&&down;
        down=gripping;
        unsigned events=0;
        if(f.breech&&f.boltButton&&!held&&phase!=Phase::Slide)
            AnimateBolt(!boltOpen,f.tick);
        if(held) {
            if(!seating){
                const bool inRange=AtHatch(f.cartridge,f.palm,f.rig.contact,f.units,touching,f.integrated);
                if(inRange)lastContact=f.tick;
                // Keep the visible snap across a small wrist motion or a
                // sample between the last contact and opening the grip.
                const bool at=inRange||(f.integrated&&touching&&
                    (released?f.tick-lastContact<=320: f.tick-lastContact<=200));
                if(at&&!touching)contactSince=f.tick;
                touching=at;
                phase=at?Phase::Seated:Phase::Holding;
                auto guide=f.rig.contact;
                guide.position=Subtract(guide.position,Scale(guide.basis.up,.014f*f.units));
                const auto t=at?std::clamp(float(f.tick-contactSince)/80.f,0.f,1.f):0.f;
                shellPoses[0]=ReloadPresentationKL::Blend(f.cartridge,guide,t*t*(3.f-2.f*t));
                for(unsigned i=1;i<carried;++i)shellPoses[i]=HeldShell(shellPoses[0],i,f.rig.shellHalf,f.units);
                if(released){
                    if(at){
                        seating=true;releaseSince=f.tick;
                        for(unsigned i=0;i<carried;++i)releaseStarts[i]=ReloadMathKL::ToLocal(f.gun,shellPoses[i]);
                        events|=Runtime::Latch;
                    }
                    else {held=false;touching=false;events|=Runtime::Stow;}
                }
            }
            if(seating){
                phase=Phase::Finishing;
                for(unsigned round=committed;round<carried;++round){
                    const auto t=std::clamp((float(f.tick-releaseSince)-round*ShellStaggerMs)/InsertMs,0.f,1.f);
                    shellPoses[round]=ReloadPresentationKL::Blend(ReloadMathKL::ToWorld(f.gun,releaseStarts[round]),f.rig.seated,t*t*(3.f-2.f*t));
                    if(t<1.f)continue;
                    if(ammo.reserve>0&&ammo.count<ammo.capacity){
                        ++ammo.count;--ammo.reserve;hadAmmo=true;
                        events|=Runtime::Insert;
                    }
                    ++committed;
                }
                if(committed==carried){
                    held=seating=touching=false;
                    if(f.breech&&ammo.count>0)AnimateBolt(false,f.tick);
                }
            }
        }else if(phase==Phase::Slide){
            if(opening||returning){
                const float t=std::min(1.f,float(f.tick-releaseSince)/PumpReturnMs);
                const float end=opening?PumpTravelMetres:0.f;
                slideTravel=pumpReturn+(end-pumpReturn)*t*t*(3.f-2.f*t);
                if(t>=1.f){
                    boltOpen=opening;needsPump=boltOpen||!ammo.count;
                    events|=opening?Runtime::Back:Runtime::Forward;
                    if(opening&&ammo.count>0){--ammo.count;hadAmmo=ammo.count!=0;events|=Runtime::RoundEject;}
                    opening=returning=pumpPulled=false;
                    phase=needsPump?Phase::NeedsChamber:Phase::Ready;
                }
            }else{
                const auto position=ReloadMathKL::Inverse(f.gun.basis,Subtract(f.driver,f.gun.position));
                slideTravel=std::clamp((handClosing?PumpTravelMetres:0.f)+(startZ-position.z)/f.units,0.f,PumpTravelMetres);
                if(f.breech){
                    if(!released&&!boltOpen&&slideTravel>=.035f){
                        boltOpen=needsPump=true;events|=Runtime::Back;
                        if(ammo.count){--ammo.count;hadAmmo=ammo.count!=0;events|=Runtime::RoundEject;}
                    }else if(boltOpen&&slideTravel<=.008f){
                        boltOpen=false;needsPump=!ammo.count;events|=Runtime::Forward;
                    }
                    if(released){
                        slideTravel=boltOpen?PumpTravelMetres:0.f;handClosing=false;
                        phase=needsPump?Phase::NeedsChamber:Phase::Ready;
                    }
                }else{
                    if(!released&&!pumpPulled&&slideTravel>=PumpRearMetres){
                        pumpPulled=true;events|=Runtime::Back;
                        if(!needsPump&&ammo.count>0){
                            --ammo.count;hadAmmo=ammo.count!=0;events|=Runtime::RoundEject;
                        }
                        needsPump=true;
                    }
                    if(pumpPulled&&slideTravel<=PumpForwardMetres){
                        pumpPulled=false;needsPump=!ammo.count;events|=Runtime::Forward;
                    }
                    if(!pumpPulled&&slideTravel<=PumpForwardMetres)slideTravel=0;
                    if(released){
                        if(pumpPulled)AnimateBolt(false,f.tick);
                        else {slideTravel=0;phase=needsPump?Phase::NeedsChamber:Phase::Ready;}
                    }
                }
            }
        }else if(pressed){
            auto pump=f.pump;
            if(f.breech&&boltOpen)pump.box.world.position=Subtract(pump.box.world.position,Scale(f.gun.basis.forward,slideTravel*f.units));
            Vec3 anchor;
            const bool pumpContact=!(f.breech&&f.scoped)&&!f.supportHeld&&CanGrabPump(f.integrated,needsPump,ammo.count)&&
                PumpContact(pump,f.palm.position,f.units,anchor,f.breech,f.integrated);
            if(pumpContact){
                phase=Phase::Slide;startZ=Dot(Subtract(f.driver,f.gun.position),f.gun.basis.forward);
                handClosing=f.breech&&boltOpen;pumpPulled=false;
                if(handClosing)anchor=Add(anchor,Scale(f.gun.basis.forward,slideTravel*f.units));
                pumpAnchorLocal=ReloadMathKL::Inverse(f.gun.basis,Subtract(anchor,f.gun.position));
            }else if(f.supply&&(!f.breech||boltOpen)&&!f.supportHeld&&ammo.reserve>0&&ammo.count<ammo.capacity){
                phase=Phase::Holding;held=true;touching=false;committed=0;
                carried=unsigned(std::min({f.breech?1:int(ShellsPerGrip),ammo.reserve,ammo.capacity-ammo.count}));
                for(unsigned i=0;i<carried;++i)shellPoses[i]=HeldShell(f.cartridge,i,f.rig.shellHalf,f.units);
                events|=Runtime::Take;
            }
        }
        if(!held&&phase!=Phase::Slide)phase=needsPump&&(f.breech||ammo.count>0)?Phase::NeedsChamber:Phase::Ready;
        ammo.ready=ammo.count>0&&!needsPump&&!boltOpen;
        return events;
    }
};
}
