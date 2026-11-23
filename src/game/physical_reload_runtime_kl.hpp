// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "physical_reload_kl.hpp"
#include "reload_slide_capture_kl.hpp"
#include "reload_comfort_kl.hpp"
#include "reload_presentation_kl.hpp"

// A manual ammunition journal. Each change is tentative until the native
// adapter acknowledges the exact count/reserve transaction. ready is our
// private chamber output, independent of the native preparation/fire gate. No native
// reload animation or action timer is part of this state machine.
namespace PhysicalReloadRuntimeKL {
using namespace PhysicalReloadKL;
enum Event : unsigned {Eject=1,Take=2,Latch=4,Insert=8,Back=16,Forward=32,Drop=64,Impact=128,ActionTake=256,Recovered=512,RoundEject=1024,DryFire=2048,Stow=4096};
struct DryTrigger {
    std::uint64_t item=0;std::uint32_t scene=0,reference=UINT32_MAX,lastTick=0;bool armed=false,down=false;
    int remaining=-1;
    bool releasing=false;std::uint32_t releasedAt=0;
    std::uint32_t lastRoundAt=0;
    bool Step(std::uint64_t weapon,std::uint32_t epoch,std::uint32_t ack,std::uint32_t tick,
              float value,bool valid,bool empty,int count){
        if(!valid||!weapon||!std::isfinite(value)||value<0||value>1){*this={};return false;}
        if(item!=weapon||scene!=epoch||reference!=ack||(lastTick&&tick-lastTick>150)){*this={};item=weapon;scene=epoch;reference=ack;}
        lastTick=tick;
        const bool lastRoundSpent=remaining==1&&count==0;
        if(lastRoundSpent)lastRoundAt=tick;
        remaining=count;
        if(!armed){if(value<.2f)armed=true;return false;}
        if(down){
            if(value>.2f){releasing=false;return false;}
            // One neutral report can come from a brief loss of aim/input.
            // It must not let a held trigger retrigger after the last shot.
            constexpr std::uint32_t ReleaseMs=50;
            if(!releasing){releasing=true;releasedAt=tick;}
            if(tick-releasedAt>=ReleaseMs){down=false;releasing=false;}
            return false;
        }
        down=value>.55f;
        // The observer can see a pending press and the native ammo debit
        // in the same update. That press already spent the final round.
        constexpr std::uint32_t LastRoundQuietMs=180;
        const bool shotRecently=lastRoundAt&&tick-lastRoundAt<LastRoundQuietMs;
        return down&&empty&&!shotRecently;
    }
};
constexpr std::uint32_t ActionPickupMs=360;
enum class DiscardPolicy {Physical,Recover,Lose};
constexpr unsigned CosmeticMagazineLimit=3;
constexpr float CosmeticMagazineLifetime=6.f;
struct Ammo {int count=0,reserve=0,capacity=0;bool ready=false;};
inline bool Valid(const Ammo& a){return a.count>=0&&a.count<=10001&&a.reserve>=0&&a.reserve<=1000000&&a.capacity>0&&a.capacity<=10000&&a.count<=a.capacity+1;}
inline float SegmentDistance(Vec3 point,Vec3 a,Vec3 b,float units){
    const auto ab=Subtract(b,a);const float length=Dot(ab,ab);
    const float t=length>.0001f?std::clamp(Dot(Subtract(point,a),ab)/length,0.f,1.f):0.f;
    const auto d=Scale(Subtract(point,Add(a,Scale(ab,t))),1.f/units);return Dot(d,d);
}
struct Frame : Input {
    CameraPose gun{},slide{},magazine{},seatedReceiver{};bool magazineValid=false,seatedReceiverValid=false,eject=false,allowSupply=true,returnToPocket=false;
    // Monolithic SMG receivers expose a real magazine but no separately
    // rendered bolt. Complete their feed on insertion, without a fake rack.
    bool slideAvailable=true,readyOnInsert=false;float magazineBodyLength=.13f;
    CameraPose slideBox{};Vec3 slideHalf{};bool slideBoxValid=false,supportGripHeld=false;
    Vec3 slideA{},slideB{},pickupPoint{},slideDriverPoint{};bool slideSegmentValid=false,pickupPointValid=false,slideDriverValid=false;float slideRadius=.045f;
    CameraPose pickupRay{};bool pickupRayValid=false,actionPickup=false,allowActionPickup=true;
};
struct Journal {std::uintptr_t player=0,model=0;std::uint64_t item=0;bool inserted=true,chamber=false;int returnRounds=0;};
struct MagazineSource {std::uintptr_t player=0,model=0;std::uint64_t item=0;std::uint32_t serial=0;};
struct Engine {
    DiscardPolicy discardPolicy=DiscardPolicy::Physical;
    Ammo ammo{};Phase phase=Phase::Inactive;bool inserted=true,chamber=false,selected=false;
    std::uintptr_t player=0,model=0;std::uint64_t item=0;unsigned category=0;
    std::uint32_t scene=0,reference=UINT32_MAX,lastTick=0,lastSample=0,contactSince=0,seatSince=0,seatReleaseSince=0,pickupSince=0;
    bool armed=false,down=false,consumed=false,contact=false,pulled=false,seatReleased=false,slideCycled=false,worldPickupBlocked=true;
    bool actionArmed=false,actionDown=false,actionCarry=false,pickupAnimating=false,ejectDown=false;std::uint32_t pickupAnimationSince=0;CameraPose pickupStart{};
    int lastRecoveredRounds=0;MagazineSource lastRecoveredSource{};bool lastRecoveryQueued=false;unsigned ejectedRounds=0;
    int pickupCandidate=-1;
    int heldRounds=0;bool held=false;float slideTravel=0;Vec3 slideStart{};
    CameraPose lastMagazine{},seatStart{},seatReleaseStart{};WorldMagazine world[24]{},deferredMagazine{};MagazineSource worldSources[24]{},deferredSource{};Journal journals[64]{};std::uint32_t nextSerial=0;
    bool cosmetic[24]{};float cosmeticAge[24]{};
    bool AllowsSlide(bool sharesForegrip)const{return !sharesForegrip||!chamber||phase==Phase::Slide;}
    bool ReservesSupportHand()const{return held||!chamber||phase==Phase::Slide||phase==Phase::Finishing;}
    bool NeedsFloor(unsigned index)const{return index<24&&world[index].valid&&!world[index].grounded;}
    void SpawnCosmetic(const CameraPose& pose,unsigned type,std::uintptr_t token,float units){
        unsigned at=0;
        for(unsigned i=0;i<CosmeticMagazineLimit;++i){if(!world[i].valid){at=i;break;}
            if(cosmeticAge[i]>cosmeticAge[at]||(cosmeticAge[i]==cosmeticAge[at]&&world[i].serial<world[at].serial))at=i;}
        world[at]={};worldSources[at]={};cosmetic[at]=true;cosmeticAge[at]=0;
        auto& w=world[at];w.valid=true;w.pose=pose;w.category=type;w.model=token;w.serial=++nextSerial;
        w.velocity=Scale(pose.basis.forward,-.3f*units);
    }
    void Neutral(){armed=actionArmed=false;contact=pulled=slideCycled=false;down=actionDown=false;worldPickupBlocked=true;pickupCandidate=-1;slideTravel=0;if(phase==Phase::Slide)phase=inserted?(chamber||!ammo.count?Phase::Ready:Phase::NeedsChamber):Phase::Supply;}
    int Slot()const{for(unsigned i=0;i<24;++i)if(!world[i].valid)return int(i);for(unsigned i=0;i<24;++i)if(world[i].grounded&&!world[i].rounds)return int(i);return -1;}
    unsigned FreeSlots()const{unsigned total=0;for(const auto& w:world)if(!w.valid||(w.grounded&&!w.rounds))++total;return total;}
    bool Spawn(const CameraPose& pose,int rounds,unsigned type,std::uintptr_t token,float units,Vec3 velocity={},bool releaseHeld=false){
        // A held magazine reserves one slot for release or weapon switching.
        // Other spawns must not consume the only place it can safely go.
        if(held&&!releaseHeld&&FreeSlots()<2)return false;
        const int at=Slot();if(at<0)return false;world[at]={};cosmetic[at]=false;cosmeticAge[at]=0;auto& w=world[at];w.valid=true;w.pose=pose;w.rounds=rounds;w.category=type;w.model=token;
        w.serial=++nextSerial;
        worldSources[at]={};
        if(selected&&token==model){worldSources[at]={player,model,item,w.serial};CurrentJournal();}
        w.velocity=Add(velocity,Scale(pose.basis.forward,-.3f*units));return true;
    }
    void FlushDeferred(float units){
        if(!deferredMagazine.valid||!std::isfinite(units)||units<1||units>1000)return;
        if(discardPolicy!=DiscardPolicy::Physical){
            // An unused spare always belongs to its source inventory, even
            // in lose-ammo mode. A saturated journal keeps a hidden credit.
            Journal source;source.player=deferredSource.player;source.item=deferredSource.item;source.model=deferredSource.model;
            if(selected&&SameSource(source,player,item,model)){
                if(deferredMagazine.rounds>1000000-ammo.reserve)return;
                ammo.reserve+=deferredMagazine.rounds;
            }else{auto* slot=SourceJournal(source.player,source.item,source.model,true);
                if(!slot||deferredMagazine.rounds>1000000-slot->returnRounds)return;
                slot->returnRounds+=deferredMagazine.rounds;
            }
            deferredMagazine={};deferredSource={};return;
        }
        const int at=Slot();
        if(at>=0&&Spawn(deferredMagazine.pose,deferredMagazine.rounds,deferredMagazine.category,deferredMagazine.model,units)){
            worldSources[at]=deferredSource;worldSources[at].serial=world[at].serial;deferredMagazine={};deferredSource={};
        }
    }
    static bool SameSource(const Journal& j,std::uintptr_t actor,std::uint64_t weapon,std::uintptr_t token){
        if(j.player!=actor||j.model!=token)return false;
        if(j.item==weapon)return true;
        // Native inventory stow/draw can recreate the item object while its
        // +54 reference and template remain the same. The reference, not the
        // transient low32 address, identifies that source across the swap.
        const auto ref=std::uint32_t(weapon>>32);
        return ref&&ref!=UINT32_MAX&&std::uint32_t(j.item>>32)==ref;
    }
    Journal* SourceJournal(std::uintptr_t actor,std::uint64_t weapon,std::uintptr_t token,bool create){
        for(auto& j:journals)if(SameSource(j,actor,weapon,token))return &j;
        if(create&&actor&&weapon&&token)for(auto& j:journals)if(!j.item){j.player=actor;j.item=weapon;j.model=token;return &j;}
        return nullptr;
    }
    Journal* CurrentJournal(bool create=true){return selected?SourceJournal(player,item,model,create):nullptr;}
    void Remember(){if(auto* slot=CurrentJournal()){slot->inserted=inserted;slot->chamber=chamber;}}
    void NativeReloadCompleted(std::uintptr_t actor,std::uint64_t weapon,std::uintptr_t token,const Ammo& native){
        if(!actor||!weapon||!token||!Valid(native))return;
        Journal* source=SourceJournal(actor,weapon,token,true);
        if(source){source->inserted=true;source->chamber=native.count>0;}
        if(selected&&player==actor&&item==weapon&&model==token){
            // Keep an outstanding spare/return credit; only the completed
            // source's native loader and loaded chamber are acknowledged.
            ammo=native;inserted=true;chamber=native.count>0;ammo.ready=chamber;
            Neutral();phase=held?Phase::Holding:Phase::Ready;Remember();
        }
    }
    bool ReturnHeld(){
        if(!held||heldRounds<0||heldRounds>1000000-ammo.reserve)return false;
        ammo.reserve+=heldRounds;held=false;heldRounds=0;actionCarry=false;contact=false;
        phase=inserted?(chamber||!ammo.count?Phase::Ready:Phase::NeedsChamber):Phase::Supply;return true;
    }
    bool QueueHeldReturn(){
        auto* slot=CurrentJournal();if(!held||!slot||heldRounds<0||heldRounds>1000000-slot->returnRounds)return false;
        // Selection has already changed native ownership. Keep a bounded,
        // source-qualified credit instead of writing the incoming gun's ammo.
        slot->returnRounds+=heldRounds;held=false;heldRounds=0;return true;
    }
    unsigned FlushReturn(){
        auto* slot=CurrentJournal(false);
        if(!slot||slot->returnRounds<=0||slot->returnRounds>1000000-ammo.reserve)return 0;
        ammo.reserve+=slot->returnRounds;slot->returnRounds=0;return Stow;
    }
    void Select(std::uintptr_t actor,std::uint64_t weapon,std::uintptr_t token,unsigned type,const Ammo& native,float units){
        if(player&&actor!=player){const auto policy=discardPolicy;*this={};discardPolicy=policy;}
        if(selected&&item==weapon&&model==token){Observe(native);return;}
        if(held&&!QueueHeldReturn()){
            if(discardPolicy!=DiscardPolicy::Physical||!Spawn(lastMagazine,heldRounds,category,model,units,{},true)){
                // Recover an already full-pool state without deleting rounds.
                // New gestures reserve a slot, so a second deferred magazine
                // cannot be produced by the normal runtime transitions.
                if(deferredMagazine.valid)return;
                deferredMagazine={};deferredMagazine.valid=true;deferredMagazine.pose=lastMagazine;
                deferredMagazine.rounds=heldRounds;deferredMagazine.category=category;deferredMagazine.model=model;
                deferredSource={player,model,item,0};
            }
            held=false;heldRounds=0;
        }
        // Native item+E4 is a shared preparation/fire gate. A newly
        // observed loaded weapon starts ready; our journal owns later racks.
        Remember();player=actor;item=weapon;model=token;category=type;ammo=native;selected=weapon&&Valid(native);inserted=true;chamber=native.count>0;
        for(const auto& j:journals)if(SameSource(j,actor,weapon,token)){inserted=j.inserted;chamber=j.chamber&&native.count>0;break;}
        ammo.ready=chamber&&ammo.count>0;
        phase=selected?(inserted?(chamber||!ammo.count?Phase::Ready:Phase::NeedsChamber):Phase::Supply):Phase::Inactive;
        Neutral();lastSample=lastTick=0;consumed=false;seatReleased=false;seatReleaseSince=0;actionCarry=pickupAnimating=false;pickupAnimationSince=0;
    }
    void Observe(const Ammo& native){
        if(!Valid(native))return;ammo.count=native.count;ammo.reserve=native.reserve;ammo.capacity=native.capacity;
        if(!ammo.count)chamber=false;
        // Trigger press/release may toggle native.ready every update. It
        // cannot unload the journal's chamber or authorize a native shot.
        ammo.ready=chamber&&ammo.count>0;
    }
    bool CanRecover(unsigned index)const{
        if(index>=24||cosmetic[index]||!world[index].valid||world[index].rounds<=0)return false;
        const auto& w=world[index];if(w.model==model)return w.rounds<=1000000-ammo.reserve;
        const auto& source=worldSources[index];
        if(!source.serial||source.serial!=w.serial||source.player!=player||source.model!=w.model)return false;
        for(const auto& j:journals)if(SameSource(j,source.player,source.item,source.model))return w.rounds<=1000000-j.returnRounds;
        return false;
    }
    bool Recover(unsigned index){
        if(!CanRecover(index))return false;
        const auto& w=world[index];lastRecoveredRounds=w.rounds;lastRecoveredSource=worldSources[index];lastRecoveryQueued=w.model!=model;
        if(!lastRecoveredSource.item)lastRecoveredSource={player,model,item,w.serial};
        if(w.model==model)ammo.reserve+=w.rounds;
        else {const auto& source=worldSources[index];auto* j=SourceJournal(source.player,source.item,source.model,false);
            if(!j)return false;j->returnRounds+=w.rounds;}
        world[index]={};worldSources[index]={};return true;
    }
    int ActionTarget(const Frame& f)const{
        if(discardPolicy!=DiscardPolicy::Physical)return -1;
        if(!selected||!f.valid||f.blocked||!f.allowActionPickup||phase==Phase::Slide||phase==Phase::Finishing||!std::isfinite(f.units)||f.units<1||f.units>1000)return -1;
        const auto palm=f.pickupPointValid&&Finite(f.pickupPoint)?f.pickupPoint:f.palm.position;if(!Finite(palm))return -1;
        int nearest=-1;float best=2.f;
        const bool ray=f.pickupRayValid&&ValidatePose(f.pickupRay,.05f);
        for(unsigned i=0;i<24;++i)if(CanRecover(i)&&ValidatePose(world[i].pose,.05f)){
            const auto end=Subtract(world[i].pose.position,Scale(world[i].pose.basis.forward,ForCategory(world[i].category).length*f.units));
            const auto centre=Scale(Add(world[i].pose.position,end),.5f);float score=2.f;
            const float distance=SegmentDistance(palm,world[i].pose.position,end,f.units);
            if(distance<=1.f)score=distance;
            if(ray){const auto delta=Scale(Subtract(centre,f.pickupRay.position),1.f/f.units);const float along=Dot(delta,f.pickupRay.basis.forward);
                const auto lateral=Subtract(delta,Scale(f.pickupRay.basis.forward,along));const float width=.10f+.08f*along;
                const float rayReach=world[i].grounded&&delta.y<-.6f?2.9f:2.2f;
                if(along>=0&&along<=rayReach&&Dot(lateral,lateral)<=width*width)score=-2.f+Dot(lateral,lateral)/(width*width)+along*.1f;
                // Ordinary item actions use body reach. A floor magazine near
                // a standing player must not require lowering a controller to
                // within one metre or aiming through a tiny head-ray capsule.
                const Vec3 horizontal{delta.x,0,delta.z};const float planar=Dot(horizontal,horizontal);
                Vec3 facing=f.pickupRay.basis.forward;facing.y=0;const float facingLength=Dot(facing,facing);
                const bool inFront=planar<.2f*.2f||facingLength<.01f||
                    Dot(horizontal,facing)>=-.1f*std::sqrt(planar*facingLength);
                if(world[i].grounded&&delta.y<=.15f&&delta.y>=-2.2f&&planar<=1.4f*1.4f&&inFront)
                    score=std::min(score,.25f+planar*.25f);
            }
            if(score<best){best=score;nearest=int(i);}
        }
        return nearest;
    }
    unsigned Tick(const Frame& f){
        if(!selected)return 0;
        const bool valid=f.valid&&!f.blocked&&f.sample&&f.tick-f.sample<=150&&std::isfinite(f.grip)&&f.grip>=0&&f.grip<=1&&
            f.units>=1&&f.units<=1000&&ValidatePose(f.palm,.05f)&&ValidatePose(f.receiver,.05f)&&ValidatePose(f.slide,.05f)&&ValidatePose(f.gun,.05f);
        if(!valid||(lastTick&&f.tick-lastTick>250)||reference!=f.reference||scene!=f.scene){Neutral();reference=f.reference;scene=f.scene;lastTick=f.tick;return 0;}
        // Buttons can change between controller pose publications. Consume
        // their edges against the still-fresh pose, without replaying held
        // buttons or ordinary gestures on duplicate pose samples.
        lastTick=f.tick;if(f.sample==lastSample&&f.actionPickup==actionDown&&f.eject==ejectDown)return 0;lastSample=f.sample;
        if(!f.actionPickup)actionArmed=true;
        const bool actionPress=actionArmed&&f.actionPickup&&!actionDown;actionDown=f.actionPickup;
        // Consume every request edge, including one rejected during seating.
        // A held native reload request must not eject the newly latched clip.
        const bool ejectPress=f.eject&&!ejectDown;ejectDown=f.eject;
        if(discardPolicy!=DiscardPolicy::Physical)FlushDeferred(f.units);
        unsigned events=FlushReturn();
        if(!armed){
            if(f.grip<.4f){armed=true;down=worldPickupBlocked=false;if(!actionPress&&!ejectPress)return events;}
            else if(actionPress||ejectPress){armed=true;down=f.grip>.7f;}
            else return events;
        }
        const bool next=f.grip>(down?.4f:.7f),press=next&&!down,release=!next&&down;down=next;if(!next){consumed=false;worldPickupBlocked=false;}
        if(next&&f.supportGripHeld){worldPickupBlocked=true;pickupCandidate=-1;}
        if(ejectPress&&category==1&&!inserted&&!held&&phase!=Phase::Finishing){inserted=true;chamber=ammo.count>0;phase=Phase::Ready;events|=Forward;Remember();}
        else if(ejectPress&&inserted&&phase!=Phase::Finishing&&phase!=Phase::Slide&&(category==1||discardPolicy!=DiscardPolicy::Physical||Slot()>=0)){
            const int rounds=std::max(0,ammo.count-(chamber?1:0));
            const auto& seated=f.seatedReceiverValid&&ValidatePose(f.seatedReceiver,.05f)?f.seatedReceiver:f.receiver;
            if(discardPolicy!=DiscardPolicy::Physical){
                const int removed=category==1?ammo.count:rounds;
                if(discardPolicy==DiscardPolicy::Lose||removed<=1000000-ammo.reserve){
                    if(discardPolicy==DiscardPolicy::Recover)ammo.reserve+=removed;
                    if(category!=1)SpawnCosmetic(seated,category,model,f.units);
                    ammo.count=category==1?0:(chamber?1:0);if(category==1)chamber=false;
                    inserted=false;phase=held?Phase::Holding:Phase::Supply;events|=Eject;contact=false;Remember();
                }
            }
            else if(category==1){ammo.reserve+=ammo.count;ammo.count=0;chamber=false;inserted=false;phase=held?Phase::Holding:Phase::Supply;events|=Eject;contact=false;Remember();}
            else if(Spawn(seated,rounds,category,model,f.units)){ammo.count=chamber?1:0;inserted=false;phase=held?Phase::Holding:Phase::Supply;events|=Eject;contact=false;}
        }
        const auto segmentDistance=[&](Vec3 point,Vec3 a,Vec3 b){return SegmentDistance(point,a,b,f.units);};
        const auto palm=f.pickupPointValid&&Finite(f.pickupPoint)?f.pickupPoint:f.palm.position;
        if(actionPress&&f.allowActionPickup&&phase!=Phase::Slide&&phase!=Phase::Finishing){
            const int nearest=ActionTarget(f);
            // The action button is ordinary inventory recovery. Only grip
            // pickup makes a physical magazine follow the free hand.
            if(nearest>=0&&Recover(unsigned(nearest))){
                consumed=true;pickupCandidate=-1;events|=Take|ActionTake|Recovered;}
        }
        if(pickupAnimating&&f.tick-pickupAnimationSince>=ActionPickupMs)pickupAnimating=false;
        if(actionCarry&&press&&!pickupAnimating)actionCarry=false;
        const bool slideSegment=f.slideSegmentValid&&Finite(f.slideA)&&Finite(f.slideB);
        const auto slideDriver=f.slideDriverValid&&Finite(f.slideDriverPoint)?f.slideDriverPoint:f.palm.position;
        const bool freePress=press&&!consumed&&!held&&!worldPickupBlocked&&phase!=Phase::Finishing&&phase!=Phase::Slide;
        if(freePress&&f.slideAvailable&&ReloadSlideCaptureKL::HandNear(palm,slideDriver,f.units,category,!inserted||!chamber,
            slideSegment?f.slideA:f.slide.position,slideSegment?f.slideB:f.slide.position,f.slideRadius,f.slideBox,f.slideHalf,f.slideBoxValid)){
            phase=Phase::Slide;slideStart=ReloadMathKL::Inverse(f.gun.basis,Subtract(slideDriver,f.gun.position));
            pulled=slideCycled=false;consumed=true;
        }
        if(discardPolicy==DiscardPolicy::Physical&&!held&&next&&!consumed&&!worldPickupBlocked&&phase!=Phase::Slide&&phase!=Phase::Finishing){
            int nearest=-1;float best=(press?.24f:.18f)*(press?.24f:.18f);
            // World pose is the feed lip. Reach the visible body of a magazine
            // even while it lies on its side; do not require a grip edge at it.
            for(unsigned i=0;i<24;++i)if(world[i].valid&&world[i].model==model){
                const auto end=Subtract(world[i].pose.position,Scale(world[i].pose.basis.forward,ForCategory(world[i].category).length*f.units));
                const float distance=segmentDistance(palm,world[i].pose.position,end);
                if(distance<best){best=distance;nearest=int(i);}
            }
            if(nearest<0)pickupCandidate=-1;
            else{
                if(pickupCandidate!=nearest){pickupCandidate=nearest;pickupSince=f.tick;}
                if(press||f.tick-pickupSince>=100){heldRounds=world[nearest].rounds;world[nearest]={};worldSources[nearest]={};held=true;phase=Phase::Holding;consumed=true;pickupCandidate=-1;events|=Take;}
            }
        }
        if(!next)pickupCandidate=-1;
        const int supplyCapacity=ammo.capacity;
        // Gear changes hands only on a fresh squeeze at the pocket.
        if(freePress&&!consumed&&f.allowSupply&&!held&&pickupCandidate<0&&ammo.reserve>0&&supplyCapacity>0&&
           (category==1||discardPolicy!=DiscardPolicy::Physical||Slot()>=0)){
            heldRounds=std::min(supplyCapacity,ammo.reserve);ammo.reserve-=heldRounds;held=true;phase=Phase::Holding;consumed=true;events|=Take;
        }
        if(held&&!pickupAnimating&&phase!=Phase::Finishing){
            lastMagazine=f.magazineValid?f.magazine:Magazine(f.palm,f.units);
            const bool at=!inserted&&ReloadComfortKL::Seat(lastMagazine,f.receiver,f.units,category,f.magazineBodyLength);
            if(!at)contact=false;else if(!contact){contact=true;contactSince=f.tick;}
            // A close release is an intentional hand-off to the well. Capture
            // before the ordinary release/drop path, without requiring that
            // the grip edge coincide with the end of a dwell timer.
            if(at&&((contact&&f.tick-contactSince>=70)||release)){
                phase=Phase::Finishing;seatSince=f.tick;
                seatStart=seatReleaseStart=ReloadMathKL::ToLocal(f.receiver,lastMagazine);
                seatReleased=false;seatReleaseSince=0;contact=false;events|=Latch;
            }
            // The accepted well latch above owns a close release. Everywhere
            // else an unused held magazine returns to reserve, as a comfort
            // inventory object. Failed native Apply restores the held state.
            else if(release&&ReturnHeld())events|=Stow;
        }
        if(phase==Phase::Finishing){
            const auto seated=f.seatedReceiverValid?ReloadMathKL::ToLocal(f.receiver,f.seatedReceiver):CameraPose{};
            if(!seatReleased){
                if(next){
                    const auto guide=ReloadPresentationKL::Blend(CameraPose{},seated,.85f);
                    const float t=std::clamp(float(f.tick-seatSince)/170.f,0.f,1.f);
                    seatReleaseStart=ReloadPresentationKL::Blend(seatStart,guide,t*t*(3-2*t));
                }else{seatReleased=true;seatReleaseSince=f.tick;}
                lastMagazine=ReloadMathKL::ToWorld(f.receiver,seatReleaseStart);
            }else{
                const float t=std::clamp(float(f.tick-seatReleaseSince)/160.f,0.f,1.f);
                lastMagazine=ReloadMathKL::ToWorld(f.receiver,ReloadPresentationKL::Blend(seatReleaseStart,seated,t*t*(3-2*t)));
            }
        }
        if(phase==Phase::Finishing&&seatReleased&&f.tick-seatReleaseSince>=160){
            const int limit=ammo.capacity+(category!=1&&chamber?1:0);
            const int loaded=std::min(heldRounds,std::max(0,limit-ammo.count));ammo.reserve+=heldRounds-loaded;ammo.count+=loaded;
            heldRounds=0;held=false;actionCarry=false;inserted=true;if(f.readyOnInsert)chamber=ammo.count>0;ammo.ready=chamber&&ammo.count>0;
            phase=chamber?Phase::Ready:Phase::NeedsChamber;consumed=next;events|=Insert;Remember();
        }
        if(phase==Phase::Slide){
            const auto position=ReloadMathKL::Inverse(f.gun.basis,Subtract(slideDriver,f.gun.position));
            const auto movement=Scale(Subtract(position,slideStart),1.f/f.units);
            slideTravel=std::clamp(-movement.z,0.f,category==0?.05f:.075f);
            if(!release&&!pulled&&slideTravel>=(category==0?.04f:.055f)){
                pulled=true;slideCycled=false;events|=Back;
                if(chamber&&ammo.count>0){--ammo.count;chamber=false;ammo.ready=false;++ejectedRounds;events|=RoundEject;Remember();}
            }
            const float forwardDetent=category==0?.012f:.022f;
            if(pulled&&(release||slideTravel<=forwardDetent)){
                pulled=false;slideCycled=true;
                chamber=inserted&&ammo.count>0;ammo.ready=chamber;events|=Forward;
                Remember();
            }
            if(!pulled&&slideTravel<=forwardDetent)slideTravel=0;
            // The hand stays captured after chambering, but the mechanism
            // remains live: each new full rear/forward stroke can eject one
            // more chambered round without releasing and regrasping.
            if(release){phase=inserted?(chamber||!ammo.count?Phase::Ready:Phase::NeedsChamber):Phase::Supply;slideTravel=0;consumed=next;Remember();}
        }
        ammo.ready=chamber&&ammo.count>0;return events;
    }
    void Advance(float seconds,float units){
        if(discardPolicy==DiscardPolicy::Physical)FlushDeferred(units);
        const float dt=std::clamp(seconds,0.f,.04f);
        for(unsigned i=0;i<24;++i){auto& w=world[i];if(!w.valid)continue;
            if(cosmetic[i]){cosmeticAge[i]+=dt;if(cosmeticAge[i]>=CosmeticMagazineLifetime){w={};worldSources[i]={};cosmetic[i]=false;cosmeticAge[i]=0;continue;}}
            if(w.grounded)continue;w.velocity.y-=9.81f*units*dt;w.pose.position=Add(w.pose.position,Scale(w.velocity,dt));
            const float sine=std::sin(dt*1.7f);const auto axis=Scale(w.pose.basis.right,sine);const Quat turn{axis.x,axis.y,axis.z,std::cos(dt*1.7f)};
            w.pose.basis.forward=Rotate(turn,w.pose.basis.forward);w.pose.basis.up=Rotate(turn,w.pose.basis.up);w.pose.basis.right=Rotate(turn,w.pose.basis.right);}
    }
    bool Land(unsigned index,float floor,float units,float halfThickness=0){
        if(index>=24||!world[index].valid||world[index].grounded||!std::isfinite(floor))return false;
        auto& w=world[index];const float height=std::isfinite(halfThickness)&&halfThickness>0?halfThickness:ForCategory(w.category).depth*.5f*units;
        if(w.pose.position.y>floor+height)return false;
        auto forward=w.pose.basis.up;forward.y=0;float n=std::sqrt(Dot(forward,forward));if(n<.01f)forward={1,0,0};else forward=Scale(forward,1.f/n);
        // Grip R is the thin axis, U is the broad face, F is the feed axis.
        // Rest on the broad face, with the measured thickness above the floor.
        w.pose.basis={forward,Cross(forward,{0,1,0}),{0,1,0}};w.pose.position.y=floor+height;w.velocity={};w.grounded=true;if(cosmetic[index])cosmeticAge[index]=CosmeticMagazineLifetime-.8f;return true;
    }
    int WorldRounds()const{int total=heldRounds+(deferredMagazine.valid?deferredMagazine.rounds:0);for(const auto& w:world)if(w.valid)total+=w.rounds;for(const auto& j:journals)total+=j.returnRounds;return total;}
};
}
