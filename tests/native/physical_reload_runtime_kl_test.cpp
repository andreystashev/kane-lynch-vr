// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#include "physical_reload_runtime_kl.hpp"
#include "physical_reload_geometry_kl.hpp"
#include "reload_contact_geometry_kl.hpp"
#include <cstdio>
#include <cstdlib>
using namespace PhysicalReloadRuntimeKL;
namespace {
unsigned checks=0;
void Check(bool v,const char* message){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Replay {
    Engine e;Frame f;int total=0;
    Replay(unsigned category,float units,float yaw,int count,bool ready,int capacity=15){
        e.Select(1,2,3,category,{count,42,capacity,ready},units);total=count+42;
        f.valid=true;f.units=units;f.scene=3;f.reference=7;f.tick=1000;
        const Quat q{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
        f.gun.basis={Rotate(q,{0,0,1}),Rotate(q,{0,1,0}),Rotate(q,{1,0,0})};f.gun.position={2*units,1.4f*units,-3*units};
        f.receiver=Receiver(f.gun,ForCategory(category),units);f.slide=f.gun;f.slide.position=Add(f.gun.position,Scale(f.gun.basis.forward,.07f*units));
        f.palm=f.gun;f.palm.position=Subtract(f.gun.position,Scale(f.gun.basis.right,.7f*units));Tick(3);
    }
    unsigned Tick(unsigned n=1){unsigned events=0;for(unsigned i=0;i<n;++i){f.tick+=20;f.sample=f.tick;events|=e.Tick(f);f.eject=false;
        Check(Valid(e.ammo),"valid ammo bounds after each gesture");Check(e.ammo.count+e.ammo.reserve+e.WorldRounds()+e.ejectedRounds==unsigned(total),"rounds conserved at every gesture step including extracted live rounds");}return events;}
    void EjectMag(){f.eject=true;Check(Tick()&Eject,"eject event");Check(!e.inserted,"magazine removed");}
    void SupplyMag(){f.grip=0;Tick();f.grip=1;Check(Tick()&Take,"fresh grip inside inventory supplies magazine immediately");Check(e.held,"fresh inventory grip holds supplied magazine");}
    void SeatMag(){f.palm=f.receiver;f.palm.position=Subtract(f.receiver.position,Scale(f.receiver.basis.forward,.065f*f.units));Tick(6);
        Check(e.phase==Phase::Finishing,"proximity dwell captures magazine in well");Tick(9);Check(!e.inserted&&e.held&&!e.seatReleased,"well holds guided magazine while grip is held");
        f.grip=0;Tick();Check(e.seatReleased,"release starts final magazine travel");Tick(9);Check(e.inserted&&!e.held,"seating commits exactly one magazine after release");}
    void RackSlide(){f.grip=0;Tick();f.palm=f.slide;f.grip=1;Tick();f.palm.position=Subtract(f.palm.position,Scale(f.gun.basis.forward,(e.category==0?.048f:.07f)*f.units));
        Check(Tick()&Back,"rear detent reached in gun coordinates");f.grip=0;Check(Tick()&Forward,"release chambers round");Check(e.chamber&&e.ammo.ready,"round chambered after full rack");}
};
}
int main(){
    for(unsigned category:{2u,3u,4u,6u,15u,16u,17u}){
        Replay r(category,100,.7f,7,true);r.f.allowSupply=false;r.f.slideAvailable=r.e.AllowsSlide(true);r.f.palm=r.f.slide;r.f.grip=1;r.Tick(8);
        Check(r.e.phase!=Phase::Slide&&!r.e.ReservesSupportHand(),"loaded long gun leaves an overlapping foregrip available and does not capture its charging handle");
        r.f.grip=0;r.Tick();r.EjectMag();
        Check(!r.e.AllowsSlide(true)&&!r.e.ReservesSupportHand(),"a round retained after magazine removal still leaves the shared foregrip available");
        auto fired=r.e.ammo;--fired.count;--r.total;r.e.Observe(fired);
        r.f.slideAvailable=r.e.AllowsSlide(true);r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide&&r.e.ReservesSupportHand(),"an empty chamber gives the shared handle priority");
        Replay empty(category,100,.7f,0,false);empty.SupplyMag();empty.EjectMag();empty.SeatMag();empty.f.slideAvailable=empty.e.AllowsSlide(true);empty.f.palm=empty.f.slide;empty.f.grip=1;empty.Tick();
        Check(empty.e.phase==Phase::Slide&&empty.e.ReservesSupportHand(),"inserted ammunition with an empty chamber still gives the mechanism priority over support");
    }
    {
        Replay r(4,100,.7f,7,true);r.f.allowSupply=false;r.f.slideAvailable=r.e.AllowsSlide(false);
        ReloadContactGeometryKL::SlideContact bolt;
        Check(ReloadContactGeometryKL::AK74Bolt(r.f.slide,r.f.gun,100,bolt),"AK anchored bolt uses the actual rifle axis");
        r.f.slide=bolt.top;r.f.slideA=bolt.a;r.f.slideB=bolt.b;r.f.slideSegmentValid=true;
        r.f.slideRadius=bolt.radius/100.f;r.f.slideBox=bolt.box.world;r.f.slideHalf=bolt.box.half;r.f.slideBoxValid=true;
        r.f.palm=r.f.slide;r.f.palm.position=Add(r.f.palm.position,Scale(r.f.gun.basis.forward,20.f));
        r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Ready,"moved AK foregrip is outside the bolt contact");
        r.f.grip=0;r.Tick();r.f.palm=r.f.slide;r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"a loaded rifle accepts a separate charging handle");
        const auto grasp=r.f.palm;
        for(int left=6;left>=5;--left){
            r.f.palm.position=Subtract(grasp.position,Scale(r.f.gun.basis.forward,7.f));
            Check((r.Tick()&(Back|RoundEject))==(Back|RoundEject)&&r.e.ammo.count==left,"each rear stroke extracts one chambered round");
            r.f.palm=grasp;r.f.palm.position=Subtract(grasp.position,Scale(r.f.gun.basis.forward,1.7f));
            Check((r.Tick()&Forward)&&r.e.chamber&&r.e.phase==Phase::Slide,"rifle returns to battery with the hand slightly short of its exact origin");
        }
        r.f.grip=0;r.Tick();Check(r.e.phase==Phase::Ready&&r.e.chamber,"a fresh squeeze can recapture the loaded handle after release");
        r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Slide,"a separate handle can be reacquired after cycling under load");
    }
    for(auto policy:{DiscardPolicy::Recover,DiscardPolicy::Lose})for(unsigned category:{0u,2u,3u,4u,5u,6u,15u,16u,17u})
    for(int capacity:{8,30,150})for(bool chambered:{false,true})for(int count:{0,1,capacity/2,capacity}){
        Replay r(category,100,.7f,count,chambered,capacity);r.e.discardPolicy=policy;r.e.chamber=chambered&&count>0;r.e.Remember();
        const int retained=r.e.chamber?1:0,removed=count-retained;
        if(policy==DiscardPolicy::Lose)r.total-=removed;
        r.EjectMag();Check(r.e.ammo.count==retained&&r.e.ammo.reserve==42+(policy==DiscardPolicy::Recover?removed:0),"discard policy credits only magazine rounds and keeps the independent chamber");
        Check(r.e.world[0].valid&&r.e.cosmetic[0]&&r.e.world[0].rounds==0&&r.e.NeedsFloor(0)&&!r.e.Land(0,0,100),"decorative drop carries no ammo, requests a real floor, and never teleports down to it");
        r.f.eject=true;r.Tick(5);Check(r.e.ammo.count==retained&&r.e.ammo.reserve==42+(policy==DiscardPolicy::Recover?removed:0),"held eject input cannot duplicate recovered ammunition");
        Check(!r.e.CanRecover(0)&&r.e.ActionTarget(r.f)<0,"decorative drop cannot offer an action or return ammunition twice");
        r.f.allowSupply=false;r.f.slideAvailable=false;r.f.palm=r.e.world[0].pose;r.f.actionPickup=true;r.f.grip=1;
        Check(!(r.Tick(8)&(Take|Recovered))&&!r.e.held,"action and grip cannot collect a decorative magazine");
        const auto initial=r.e.world[0].pose.position;
        for(unsigned i=0;i<18;++i)r.e.Advance(.04f,100);
        Check(r.e.world[0].valid&&r.e.world[0].pose.position.y<initial.y,"decorative drop falls for its short lifetime");
        for(unsigned i=0;i<160;++i)r.e.Advance(.04f,100);Check(!r.e.world[0].valid,"decorative drop disappears at its bounded lifetime");
    }
    for(float units:{10.f,100.f,250.f}){
        Engine e;e.discardPolicy=DiscardPolicy::Recover;CameraPose pose;pose.position={0,1.4f*units,0};e.SpawnCosmetic(pose,4,3,units);
        const float floor=-.2f*units,thickness=.008f*units;
        for(unsigned i=0;i<100&&!e.world[0].grounded;++i){e.Advance(.02f,units);e.Land(0,floor,units,thickness);}
        Check(e.world[0].valid&&e.world[0].grounded&&ValidatePose(e.world[0].pose,.001f),"measured decorative mesh reaches a stable orthogonal floor pose");
        Check(std::fabs(e.world[0].pose.position.y-floor-thickness)<.0001f&&e.world[0].pose.basis.right.y==1&&std::fabs(e.world[0].pose.basis.forward.y)<.0001f,"native magazine lies on its broad face at its measured thickness, not balancing on an edge");
        for(unsigned i=0;i<19;++i)e.Advance(.04f,units);Check(e.world[0].valid,"landed magazine remains visible briefly on the floor");
        for(unsigned i=0;i<3;++i)e.Advance(.04f,units);Check(!e.world[0].valid,"landed magazine expires after its floor interval");
    }
    for(unsigned heldTicks:{0u,1u,4u}){
        Replay r(4,100,.7f,7,true);r.e.discardPolicy=DiscardPolicy::Recover;r.EjectMag();r.SupplyMag();
        r.f.magazineValid=r.f.seatedReceiverValid=true;r.f.magazine=r.f.receiver;r.f.seatedReceiver=r.f.receiver;
        r.f.seatedReceiver.position=Add(r.f.receiver.position,Scale(r.f.receiver.basis.forward,8.f));
        for(unsigned i=0;i<8&&r.e.phase!=Phase::Finishing;++i)r.Tick();Check(r.e.phase==Phase::Finishing,"early release fixture reaches the real well latch");if(heldTicks)r.Tick(heldTicks);
        const auto previous=ReloadMathKL::ToLocal(r.f.receiver,r.e.lastMagazine);
        const auto move=Scale(r.f.gun.basis.right,12.f);
        r.f.receiver.position=Add(r.f.receiver.position,move);r.f.seatedReceiver.position=Add(r.f.seatedReceiver.position,move);
        r.f.grip=0;r.Tick();
        const auto release=ReloadMathKL::ToLocal(r.f.receiver,r.e.lastMagazine);
        Check(Dot(Subtract(previous.position,release.position),Subtract(previous.position,release.position))<1e-8f,"release retains the visible pose in the moving receiver frame");
        r.f.receiver.position=Add(r.f.receiver.position,move);r.f.seatedReceiver.position=Add(r.f.seatedReceiver.position,move);
        Check((r.Tick(9)&Insert)&&r.e.inserted&&!r.e.held&&ValidatePose(r.e.lastMagazine,.001f),"moving well still finishes one native magazine transaction");
        Check(!(r.Tick(9)&Insert),"settled magazine emits no second insert");
    }
    for(auto policy:{DiscardPolicy::Recover,DiscardPolicy::Lose}){
        Replay r(0,100,.4f,7,true);r.e.discardPolicy=policy;r.SupplyMag();
        if(policy==DiscardPolicy::Lose)r.total-=6;r.EjectMag();r.SeatMag();
        Check(r.e.ammo.count==16&&r.e.chamber&&r.e.ammo.reserve==(policy==DiscardPolicy::Recover?33:27),"pre-grab plus tactical insertion preserves full magazine plus one in both modes");
        Replay cancelled(0,100,.4f,7,true);cancelled.e.discardPolicy=policy;cancelled.SupplyMag();cancelled.f.grip=0;cancelled.Tick();
        Check(cancelled.e.ammo.count==7&&cancelled.e.ammo.reserve==42&&!cancelled.e.held,"unused spare is returned in either discard policy");
        Replay cylinder(1,100,.4f,5,true,6);cylinder.e.discardPolicy=policy;
        if(policy==DiscardPolicy::Lose)cylinder.total-=5;cylinder.EjectMag();
        Check(cylinder.e.ammo.count==0&&cylinder.e.ammo.reserve==(policy==DiscardPolicy::Recover?47:42)&&!cylinder.e.world[0].valid,"revolver eject uses the same ammo policy without detaching its cylinder");
        Replay switched(0,100,.4f,7,true);switched.e.discardPolicy=policy;switched.SupplyMag();
        switched.e.Select(1,4,5,2,{9,100,30,true},100);
        Check(switched.e.ammo.reserve==100&&!switched.e.world[0].valid,"switching with an unused spare queues credit for its source without a floor object or incoming-weapon credit");
        switched.e.Select(1,2,3,0,{7,27,15,true},100);switched.f.grip=0;switched.Tick();
        Check(switched.e.ammo.reserve==42,"returning to source weapon credits its spare exactly once");
    }
    {
        Engine e;e.discardPolicy=DiscardPolicy::Recover;CameraPose pose;pose.position={0,140,0};
        for(unsigned i=0;i<40;++i){e.SpawnCosmetic(pose,0,3,100);unsigned active=0;for(const auto& w:e.world)if(w.valid)++active;
            Check(active<=CosmeticMagazineLimit,"rapid drops cannot exceed three decorative objects");}
        unsigned oldest=UINT32_MAX;for(const auto& w:e.world)if(w.valid)oldest=std::min(oldest,w.serial);
        Check(oldest==38,"rapid drop replaces the oldest cosmetic without deleting recoverable ammunition");
        for(unsigned i=0;i<160;++i)e.Advance(.04f,100);for(const auto& w:e.world)Check(!w.valid,"all decorative objects expire without floor queries");
        e.Select(1,2,3,0,{7,42,15,true},100);e.Select(8,9,10,0,{7,42,15,true},100);
        Check(e.discardPolicy==DiscardPolicy::Recover,"new player reset preserves configured discard policy");
    }
    {
        Replay r(0,100,.4f,7,true);r.e.discardPolicy=DiscardPolicy::Recover;r.e.ammo.reserve=1000000;r.total=1000007;
        r.f.eject=true;Check(!(r.Tick()&Eject)&&r.e.inserted&&!r.e.world[0].valid,"reserve overflow rejects refund and decorative spawn atomically");
        r.e.discardPolicy=DiscardPolicy::Lose;r.f.eject=false;r.Tick();r.total-=6;r.EjectMag();
        Check(r.e.ammo.count==1&&r.e.ammo.reserve==1000000,"lose mode never needs room in reserve and preserves chamber");
        Replay pending(0,100,.4f,7,true);pending.e.discardPolicy=DiscardPolicy::Lose;pending.SupplyMag();
        for(unsigned i=0;i<64;++i)pending.e.journals[i]={1,100+i,1000+i,true,true,0};
        pending.e.Select(1,4,5,2,{9,100,30,true},100);
        Check(pending.e.deferredMagazine.valid&&pending.e.deferredMagazine.rounds==15,"saturated source journals keep an unused spare as a hidden credit");
        for(unsigned i=0;i<24;++i)Check(!pending.e.world[i].valid,"hidden spare credit never becomes a pickup or a decorative ammo object");
        pending.e.Advance(.04f,100);Check(pending.e.deferredMagazine.valid&&pending.e.ammo.reserve==100,"physics cannot apply pending ammunition outside native transaction");
        pending.e.Select(1,2,3,0,{7,27,15,true},100);pending.f.grip=0;pending.Tick();
        Check(!pending.e.deferredMagazine.valid&&pending.e.ammo.reserve==42,"pending spare returns once when its source is active, even in lose mode");
    }
    for(float units:{10.f,100.f,250.f})for(float angle:{0.f,1.570796f,3.14159f}){
        Replay r(2,units,1.2f,0,false,30);r.EjectMag();r.SupplyMag();r.f.magazineValid=true;r.f.magazineBodyLength=.25f;r.f.magazine=r.f.receiver;
        const Quat q{0,std::sin(angle*.5f),0,std::cos(angle*.5f)};r.f.magazine.basis={Rotate(q,r.f.magazine.basis.forward),Rotate(q,r.f.magazine.basis.up),Rotate(q,r.f.magazine.basis.right)};
        r.f.magazine.position=Add(r.f.receiver.position,Scale(r.f.magazine.basis.forward,.22f*units));
        Check(r.Tick(5)&Latch,"Nianshi-style long magazine body contact latches without a feed-tip trajectory at every approach angle");
        r.f.grip=0;Check(r.Tick(10)&Insert,"body contact inserts once on grip release");Check(r.e.ammo.count==30&&r.e.ammo.reserve==12,"body magnet preserves the accepted ammunition accounting");
    }
    for(unsigned family:{0u,2u,3u,4u,6u})for(int capacity:{6,15,30}){
        Replay r(family,100,1.2f,1,true,capacity);r.EjectMag();r.SupplyMag();
        Check(r.e.heldRounds==capacity,"spare is a full magazine even with one round already in chamber");r.SeatMag();
        Check(r.e.ammo.count==capacity+1&&r.e.chamber,"tactical reload yields full magazine plus exactly one chambered round");
        r.f.grip=0;r.Tick();r.EjectMag();Check(r.e.ammo.count==1&&r.e.world[1].rounds==capacity,"re-ejection distinguishes full magazine from the separate chambered round");
        auto saved=r.e.ammo;r.e.Select(1,8,9,family,{1,42,capacity,true},100);r.e.Select(1,2,3,family,saved,100);
        Check(r.e.chamber&&r.e.ammo.count==1,"weapon switch preserves the retained chamber without manufacturing more rounds");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(0,units,yaw,0,false);r.EjectMag();r.f.allowSupply=false;r.f.palm=r.f.slide;r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"empty pistol with no magazine still permits an immersive rack");
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,.048f*units));Check(r.Tick()&Back,"empty pistol mechanical stroke remains active");
        r.f.grip=0;Check(r.Tick()&Forward,"empty pistol returns forward without manufacturing rounds");
        Check(!r.e.chamber&&r.e.ammo.count==0,"empty rack leaves chamber empty");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f})for(int count:{0,1,6}){
        Replay r(1,units,yaw,count,count>0,6);r.f.slideAvailable=false;r.f.readyOnInsert=true;
        r.f.eject=true;Check(r.Tick()&Eject,"revolver opens on the reload edge");
        Check(!r.e.inserted&&!r.e.chamber&&r.e.ammo.count==0&&r.e.ammo.reserve==42+count,"opening revolver recovers all unspent rounds without a fictitious separate chamber");
        Check(r.e.WorldRounds()==0,"cylinder stays attached rather than falling on the ground");
        r.SupplyMag();r.SeatMag();Check(r.e.ammo.count==6&&r.e.chamber,"six-round cluster readies the revolver without a slide rack");
        r.f.eject=true;r.Tick();r.Tick();r.f.eject=true;Check(r.Tick()&Forward,"second reload edge can close an empty cylinder");
        Check(r.e.inserted&&!r.e.chamber,"cancelled cylinder closure does not conjure ammunition");
    }
    for(unsigned category:{2u,4u,6u})for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(category,units,yaw,0,false);r.EjectMag();r.SupplyMag();r.f.magazineValid=true;r.f.magazine=r.f.receiver;
        r.f.magazine.position=Add(r.f.receiver.position,Scale(r.f.gun.basis.right,.14f*units));
        const auto forward=r.f.magazine.basis.forward;r.f.magazine.basis.forward=r.f.magazine.basis.right;r.f.magazine.basis.right=Scale(forward,-1);
        r.Tick(5);Check(r.e.phase==Phase::Finishing,"sideways close approach latches a long gun magazine without a prescribed entry trajectory");
        r.f.grip=0;r.Tick(10);Check(r.e.inserted&&!r.e.held,"sideways latch completes exactly one insertion on release");
    }
    for(unsigned category:{0u,2u,3u,6u})for(bool beginInPocket:{false,true}){
        Replay r(category,100,1.2f,7,true);r.f.slideAvailable=false;r.f.allowSupply=beginInPocket;
        r.f.supportGripHeld=true;r.f.grip=1;
        Check(!(r.Tick(8)&Take)&&!r.e.held&&r.e.ammo.reserve==42,"support-hand squeeze cannot buy a magazine even inside the inventory");
        r.f.allowSupply=true;r.f.supportGripHeld=false;
        Check(!(r.Tick(20)&Take)&&!r.e.held&&r.e.worldPickupBlocked,"losing support contact while still squeezing cannot convert the occupied grip to supply");
        r.f.grip=0;r.Tick();r.f.grip=1;
        Check((r.Tick()&Take)&&r.e.held&&r.e.ammo.reserve==27,"opening the hand then squeezing empty in the pocket starts a new intentional supply grip");
    }
    {
        Replay r(3,100,0,7,true);r.f.allowSupply=false;r.f.slideAvailable=false;r.f.grip=1;r.Tick();
        r.f.supportGripHeld=true;r.Tick();r.f.supportGripHeld=false;r.f.allowSupply=true;
        Check(!(r.Tick(20)&Take)&&!r.e.held,"a free squeeze that later captures support remains occupied until release");
        r.f.grip=0;r.Tick();r.f.grip=1;Check(r.Tick()&Take,"a new empty grip can supply after support ownership ends");
    }
    for(bool temporaryEmpty:{false,true}){
        Replay r(3,100,1.2f,7,true);const auto source=(std::uint64_t(10049)<<32)|0x7b64b7d0u;
        const auto recreated=(std::uint64_t(10049)<<32)|0x7b64b440u;
        r.e.Select(1,source,3,3,r.e.ammo,100);r.Tick(3);r.EjectMag();
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;
        r.e.Select(1,temporaryEmpty?0:8,temporaryEmpty?0:9,3,temporaryEmpty?Ammo{}:Ammo{1,42,15,true},100);
        if(temporaryEmpty)r.e.Select(1,8,9,3,{1,42,15,true},100);
        r.f.grip=0;r.Tick(3);Check(r.e.ActionTarget(r.f)==0,"Take ammo remains available for a partial magazine while another weapon is selected");
        const auto before=r.e;r.f.actionPickup=true;
        Check((r.Tick()&Recovered)&&!r.e.world[0].valid&&!r.e.held&&r.e.ammo.reserve==42&&r.e.WorldRounds()==6,"foreign-magazine action recovery stores its exact rounds without changing incoming weapon ammo");
        Check(r.e.lastRecoveryQueued&&r.e.lastRecoveredSource.player==1&&r.e.lastRecoveredSource.item==source&&r.e.lastRecoveredSource.model==3,"queued inventory pickup diagnostics preserve the original weapon source rather than attributing its rounds to the incoming weapon");
        r.e=before;Check(r.e.world[0].valid&&r.e.WorldRounds()==6,"failed native Apply can roll back foreign recovery without losing the magazine or its source credit");
        r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;Check(r.Tick()&Recovered,"foreign recovery can retry after transaction rollback");
        r.e.Select(1,recreated,3,3,{1,42,15,true},100);r.f.actionPickup=false;r.Tick(3);
        Check(!r.e.inserted&&r.e.chamber&&r.e.ammo.reserve==48&&r.e.WorldRounds()==0,"native item recreation with the same reference and model restores source state and credits recovered rounds");
        Check(!(r.Tick(5)&Stow)&&r.e.ammo.reserve==48,"recreated source can receive the recovered magazine credit only once");
    }
    {
        Replay r(3,100,0,7,true);r.EjectMag();r.e.Select(1,8,9,3,{1,42,15,true},100);r.Tick(3);
        const auto provenance=r.e.worldSources[0];
        Check(r.e.CanRecover(0),"foreign partial magazine with its captured source remains recoverable");
        ++r.e.worldSources[0].serial;Check(!r.e.CanRecover(0),"reused world slot cannot inherit an older magazine's source credit");
        r.e.worldSources[0]=provenance;r.e.worldSources[0].model=4;Check(!r.e.CanRecover(0),"foreign recovery rejects a provenance model that differs from the actual prop");
        r.e.worldSources[0]=provenance;r.e.worldSources[0].player=99;Check(!r.e.CanRecover(0),"foreign recovery cannot queue credit to another player's journal");
        r.e.worldSources[0]=provenance;auto* source=r.e.SourceJournal(1,2,3,false);Check(source!=nullptr,"spawned magazine retains a bounded source journal");
        source->returnRounds=999999;Check(!r.e.CanRecover(0)&&!r.e.Recover(0)&&r.e.world[0].valid,"source journal overflow preserves the visible partial magazine without deleting rounds");
    }
    {
        Replay r(0,100,0,0,false);const auto source=(std::uint64_t(77)<<32)|2;
        r.e.Select(1,source,3,0,r.e.ammo,100);r.Tick(3);r.EjectMag();r.SupplyMag();
        r.e.Select(1,(std::uint64_t(78)<<32)|2,3,0,{0,27,15,false},100);r.Tick(3);
        Check(r.e.ammo.reserve==27&&r.e.WorldRounds()==15,"reused native address with a different reference cannot claim the previous source's credit");
        r.e.Select(1,(std::uint64_t(77)<<32)|8,4,0,{0,27,15,false},100);r.Tick(3);
        Check(r.e.ammo.reserve==27&&r.e.WorldRounds()==15,"matching native reference on a different model cannot claim a source credit");
        r.e.Select(1,(std::uint64_t(77)<<32)|8,3,0,{0,27,15,false},100);r.Tick(3);
        Check(!r.e.inserted&&r.e.ammo.reserve==42&&r.e.WorldRounds()==0,"native item recreation also restores a pre-staged magazine's pending return exactly once");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(6,units,yaw,7,true);r.f.allowSupply=false;r.f.slideBox=r.f.slide;r.f.slideHalf=Scale(Vec3{.012599f,.0092226f,.0075661f},units);r.f.slideBoxValid=true;
        r.f.palm=r.f.slide;r.f.palm.position=Add(r.f.slide.position,Scale(r.f.gun.basis.forward,.09f*units));r.f.grip=1;r.Tick();
        Check(r.e.phase!=Phase::Slide,"shotgun foregrip beyond the expanded actual latch stays outside mechanism capture");
        r.f.grip=0;r.Tick();r.f.palm.position=Add(r.f.slide.position,Scale(r.f.gun.basis.forward,.015f*units));r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"fresh press close to the actual shotgun latch still captures the mechanism");
        r.f.grip=0;r.Tick();r.f.supportGripHeld=true;r.f.palm.position=Add(r.f.slide.position,Scale(r.f.gun.basis.forward,.2f*units));r.f.grip=1;r.Tick();
        r.f.palm=r.f.slide;r.Tick(8);Check(r.e.phase!=Phase::Slide,"an already held foregrip cannot turn into a slide grab while the closed hand passes the bolt");
        r.f.grip=0;r.f.supportGripHeld=false;r.Tick();r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"a fresh deliberate press at the bolt captures it after support release");
    }
    for(float units:{10.f,100.f,250.f})for(int count:{0,1,7}){
        Replay r(2,units,1.2f,count,count>0);r.f.slideAvailable=false;r.f.readyOnInsert=true;
        r.EjectMag();r.SupplyMag();r.SeatMag();
        Check(r.e.chamber&&r.e.ammo.ready&&r.e.phase==Phase::Ready,"magazine-only SMG completes empty or tactical reload on real magazine insertion");
        r.f.grip=0;r.Tick();r.f.palm=r.f.slide;r.f.grip=1;r.Tick(8);
        Check(r.e.phase!=Phase::Slide&&r.e.slideTravel==0,"missing rendered SMG bolt has no invisible interaction region");
    }
    for(unsigned category:{0u,2u,3u,4u,15u,16u,17u})for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f,3.1f})for(int count:{0,1,7,15}){
        Replay r(category,units,yaw,count,count>0);r.EjectMag();Check(r.e.ammo.count==(count?1:0),"existing chamber survives eject");
        r.SupplyMag();r.SeatMag();if(!count){Check(!r.e.chamber,"empty reload requires slide");r.RackSlide();}
        else Check(r.e.chamber,"tactical reload needs no redundant rack");
        const auto countAfter=r.e.ammo.count;r.Tick(10);Check(r.e.ammo.count==countAfter,"holding near receiver cannot insert twice");
        for(unsigned i=0;i<100;++i){r.e.Advance(.02f,units);r.e.Land(0,0,units);}Check(r.e.world[0].grounded,"falling magazine lands on actual floor height");
        const auto floorPose=r.e.world[0].pose;r.e.Advance(.02f,units);Check(r.e.world[0].pose.position.y==floorPose.position.y,"ground magazine remains fixed");
        r.e.Select(1,8,9,category,{5,20,15,true},units);r.e.Select(1,2,3,category,r.e.ammo,units);Check(r.e.world[0].valid,"ground magazines persist across weapon switches");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.f.allowSupply=false;r.f.grip=1;r.Tick();const auto original=r.f.sample;r.f.allowSupply=true;
        for(unsigned i=0;i<30;++i){r.f.tick+=5;r.f.sample=original;r.e.Tick(r.f);}Check(!r.e.held,"duplicate controller samples cannot manufacture magazine");
        r.f.blocked=true;r.Tick();r.f.blocked=false;r.Tick(22);Check(!r.e.held,"menu interruption demands fresh grip");r.SupplyMag();
        r.f.palm=r.f.receiver;r.f.palm.position=Subtract(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6.5f));r.f.palm.basis.forward=Scale(r.f.palm.basis.forward,-1);r.f.palm.basis.right=Scale(r.f.palm.basis.right,-1);
        r.Tick(10);Check(!r.e.inserted,"reversed feed tip cannot latch");r.f.palm.position.x+=100;r.f.grip=0;Check(r.Tick()&Stow,"released held magazine returns to inventory away from the well");
        Check(r.e.WorldRounds()==0&&r.e.ammo.reserve==42,"unused magazine returns every round without creating a ground prop");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();r.e.Select(1,8,3,0,{0,20,15,false},100);
        Check(!r.e.held&&r.e.WorldRounds()==15&&!r.e.world[1].valid&&r.e.ammo.reserve==20,"switching escrows held rounds for their source without a ground prop or incoming credit");r.e.Select(1,2,3,0,{0,27,15,false},100);
        Check(!r.e.inserted&&r.e.phase==Phase::Supply,"removed magazine state survives weapon switching");
        Check((r.Tick(3)&Stow)&&r.e.ammo.reserve==42&&r.e.WorldRounds()==0,"returning to the source weapon commits its held-magazine credit exactly once");
        Check(!(r.Tick(5)&Stow)&&r.e.ammo.reserve==42,"continued source selection cannot replay an escrow credit");
        Visual v;v.active=true;v.tick=1000;v.world[0]=r.e.world[0];v.world[1]=r.e.world[1];v.gunValid=false;
        Check(PhysicalReloadGeometryKL::Build(v,1000).vertices.empty(),"unbound magazines never switch to procedural fallback boxes");
        v.tick=5000;Check(PhysicalReloadGeometryKL::Build(v,5000).vertices.empty(),"native-only magazine policy remains consistent across frame time");
    }
    {
        Replay r(0,100,1.2f,7,true);r.EjectMag();r.SupplyMag();r.SeatMag();
        r.f.allowSupply=false;
        r.f.palm=r.f.gun;r.f.palm.position=Subtract(r.f.gun.position,Scale(r.f.gun.basis.right,80));r.f.grip=1;r.Tick();
        const auto old=r.e.world[0];r.f.pickupPointValid=true;
        r.f.pickupPoint=Subtract(old.pose.position,Scale(old.pose.basis.forward,.10f*r.f.units));
        // The solved wrist/feed frame can be farther away; the physical
        // fingertip/palm contact is what reaches the visible magazine body.
        Check(r.Tick(6)&Take,"already-held grip acquires old magazine by approaching its visible body");
        Check(r.e.held&&r.e.heldRounds==6&&r.e.inserted&&r.e.ammo.count==16&&!r.e.world[0].valid,"recovered magazine retains rounds while another magazine is inserted");
        r.EjectMag();r.f.pickupPointValid=false;r.SeatMag();
        Check(r.e.ammo.count==7,"partial recovered magazine seats its actual six rounds alongside chamber");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();
        r.f.palm=r.f.receiver;r.f.palm.position=Subtract(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6.5f));
        r.f.grip=0;const auto events=r.Tick();
        Check((events&Latch)&&!(events&Drop)&&r.e.held&&r.e.phase==Phase::Finishing&&r.e.seatReleased,"release near well hands magazine over without waiting for dwell or dropping it");
        r.f.palm.position.x+=100;r.Tick(9);Check(r.e.inserted&&!r.e.held&&r.e.ammo.count==15,"released guided magazine finishes in weapon even when hand moves away");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.f.allowSupply=false;r.f.grip=1;r.Tick(17);r.f.allowSupply=true;
        r.f.pickupPointValid=true;r.f.pickupPoint=Subtract(r.e.world[0].pose.position,Scale(r.e.world[0].pose.basis.forward,8));
        r.Tick(4);Check(!r.e.held&&r.e.pickupCandidate>=0&&r.e.ammo.reserve==42,"nearby old-magazine dwell takes priority over quick fresh-supply timer");
        Check(r.Tick(2)&Take,"pending old magazine pickup completes without fresh supply stealing its grip");
        Check(r.e.held&&r.e.heldRounds==6&&r.e.ammo.reserve==42&&!r.e.world[0].valid,"pickup priority retains old rounds without debiting reserve or creating spare magazine");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f})for(float along:{-.1f,.14f}){
        Replay r(0,units,yaw,0,false);r.EjectMag();r.SupplyMag();r.SeatMag();
        r.f.slideSegmentValid=true;r.f.slideA=Add(r.f.gun.position,Scale(r.f.gun.basis.forward,-.1f*units));
        r.f.slideB=Add(r.f.gun.position,Scale(r.f.gun.basis.forward,.14f*units));
        r.f.palm=r.f.gun;r.f.palm.position=Add(Add(r.f.gun.position,Scale(r.f.gun.basis.forward,along*units)),Scale(r.f.gun.basis.up,.01f*units));
        r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Slide,"either end of the full slide is grabbable");
        const auto translation=Scale(r.f.gun.basis.right,.25f*units);r.f.gun.position=Add(r.f.gun.position,translation);r.f.palm.position=Add(r.f.palm.position,translation);
        r.Tick();Check(r.e.phase==Phase::Slide&&r.e.slideTravel<.001f,"moving gun and grasped hand together does not rack or release slide");
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,.047f*units));Check(r.Tick()&Back,"pull through a grasp at either slide end reaches rear detent");
        Check(r.e.phase==Phase::Slide,"slide grip remains latched through pull");r.f.grip=0;Check(r.Tick()&Forward,"release from full-slide grasp chambers round");
    }
    {
        Replay r(0,100,1.2f,7,true);r.f.allowSupply=false;r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Ready,"grip held away from slide does not capture it");
        r.f.palm=r.f.slide;r.Tick(10);Check(r.e.phase==Phase::Ready,"passing the slide with a closed hand cannot capture it");
        r.f.grip=0;r.Tick();r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide&&r.e.down,"a fresh squeeze captures the slide without a dwell");
        r.f.palm.position=Add(r.f.palm.position,Scale(r.f.gun.basis.right,.20f*r.f.units));r.Tick();
        Check(r.e.phase==Phase::Slide&&r.e.slideTravel<.001f&&r.e.chamber,"lateral hand motion retains the captured slide without falsely extracting a round");
        r.f.grip=0;r.Tick();Check(r.e.phase==Phase::Ready&&r.e.chamber,"releasing a lateral slide hold preserves its chamber");
    }
    for(bool temporaryEmpty:{false,true}){
        Replay r(0,100,0,1,true);r.EjectMag();Check(r.e.chamber&&r.e.ammo.count==1&&!r.e.inserted,"one chambered round remains ready without a magazine");
        r.e.Select(1,temporaryEmpty?0:8,temporaryEmpty?0:9,0,temporaryEmpty?Ammo{}:Ammo{5,20,15,false},100);
        r.e.Select(1,2,3,0,{1,42,15,false},100);r.f.grip=0;r.Tick(3);
        Check(r.e.chamber&&r.e.ammo.ready&&r.e.ammo.count==1&&!r.e.inserted,"one chambered round remains fireable across selection gaps and another weapon");
        auto fired=r.e.ammo;fired.count=0;r.e.Observe(fired);--r.total;r.Tick();
        r.e.Select(1,8,9,0,{5,20,15,false},100);r.e.Select(1,2,3,0,{0,42,15,false},100);r.Tick(3);
        Check(!r.e.chamber&&!r.e.ammo.ready&&r.e.ammo.count==0,"a spent last round cannot reappear when returning to its empty pistol");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();r.SeatMag();const auto native=r.e.ammo;
        r.e.Select(1,8,9,0,{5,20,15,false},100);r.e.Select(1,2,3,0,native,100);r.Tick(3);
        Check(!r.e.chamber&&!r.e.ammo.ready&&r.e.ammo.count==15&&r.e.inserted,"switching never bypasses a deliberate empty-reload chambering requirement");
    }
    for(bool differentModel:{false,true}){
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();
        const auto alienItem=differentModel?std::uint64_t(2):(std::uint64_t(7)<<32)|2;
        r.e.Select(1,alienItem,differentModel?4:3,0,{0,27,15,false},100);r.f.grip=0;r.Tick(3);
        Check(r.e.ammo.reserve==27&&r.e.WorldRounds()==15,"same item address with different model or generation cannot consume source return credit");
        r.e.Select(1,2,3,0,{0,27,15,false},100);
        const auto before=r.e;
        ++r.f.tick;r.f.sample=r.f.tick;Check((r.e.Tick(r.f)&Stow)&&r.e.ammo.reserve==42&&r.e.WorldRounds()==0,"matching source returns escrow inside a tentative Tick transaction");
        r.e=before;Check(r.e.ammo.reserve==27&&r.e.WorldRounds()==15,"failed native Apply can restore the same pending source return without losing rounds");
        Check((r.Tick()&Stow)&&r.e.ammo.reserve==42&&r.e.WorldRounds()==0,"retrying after native rollback returns the source rounds exactly once");
        Check(!(r.Tick(5)&Stow)&&r.e.ammo.reserve==42,"committed return is not repeated on later frames");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();
        for(unsigned i=0;i<64;++i){auto& j=r.e.journals[i];j.player=1;j.item=100+i;j.model=3;j.returnRounds=0;}
        for(auto& world:r.e.world){world={};world.valid=world.grounded=true;world.model=3;world.rounds=1;world.pose=r.f.receiver;}r.total=66;
        r.e.Select(1,8,9,0,{0,20,15,false},100);
        Check(!r.e.held&&r.e.deferredMagazine.valid&&r.e.deferredMagazine.rounds==15&&r.e.WorldRounds()==39,"exhausted source journals and full world pool preserve held rounds through the bounded native-visual fallback");
    }
    {
        Replay r(0,100,1.2f,0,false);r.EjectMag();r.SupplyMag();r.SeatMag();
        r.f.palm=r.f.slide;r.f.grip=1;r.Tick();const auto grasp=r.f.palm;
        r.f.palm.position=Subtract(grasp.position,Scale(r.f.gun.basis.forward,4.8f));Check(r.Tick()&Back,"closed slide grasp reaches rear detent");
        r.f.palm=grasp;Check(r.Tick()&Forward,"returning slide while gripping chambers the round");
        Check(r.e.phase==Phase::Slide&&r.e.slideCycled&&r.e.chamber&&r.e.slideTravel==0,"completed slide cycle retains closed-finger capture while grip stays down");
        r.f.palm.position=Subtract(grasp.position,Scale(r.f.gun.basis.forward,4.8f));const auto secondBack=r.Tick();
        Check((secondBack&Back)&&(secondBack&RoundEject)&&r.e.ammo.count==14,"new full stroke in the same closed grasp extracts one chambered round");
        Check(!(r.Tick(3)&(Back|RoundEject))&&r.e.ammo.count==14,"holding the second rear detent does not repeatedly extract rounds");
        r.f.palm=grasp;Check((r.Tick()&Forward)&&r.e.phase==Phase::Slide&&r.e.chamber,"second return chambers next round while hand remains captured");
        Check(!(r.Tick(3)&(Back|Forward)),"resting closed grasp does not replay either mechanical detent");
        r.f.palm.position=Subtract(grasp.position,Scale(r.f.gun.basis.forward,8.f));
        r.f.grip=0;Check(!(r.Tick()&(Back|Forward|RoundEject))&&r.e.phase==Phase::Ready&&r.e.ammo.count==14,
            "freeing a completed grasp ignores movement in the release sample");
    }
    {
        Replay r(0,100,1.2f,7,true);r.f.seatedReceiverValid=true;r.f.seatedReceiver=r.f.receiver;
        r.f.seatedReceiver.position=Add(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6));r.EjectMag();
        const auto delta=Subtract(r.e.world[0].pose.position,r.f.seatedReceiver.position);
        Check(Dot(delta,delta)<.0001f,"ejected native magazine starts at its fully seated tip instead of teleporting to mouth");
        Check(r.e.world[0].rounds==6&&r.e.chamber,"true seated ejection preserves partial magazine and chamber");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(0,units,yaw,7,true);r.EjectMag();const auto old=r.e.world[0];
        r.f.pickupPointValid=true;r.f.pickupPoint=Add(old.pose.position,Scale(r.f.gun.basis.right,.6f*units));r.f.actionPickup=true;
        Check(r.e.ActionTarget(r.f)==0,"HUD candidate identifies the same reachable world magazine before action pickup");
        const auto events=r.Tick();Check((events&ActionTake)&&(events&Take)&&(events&Recovered),"action button recovers nearby magazine into inventory without a grip edge");
        Check(!r.e.held&&!r.e.actionCarry&&!r.e.pickupAnimating&&r.e.heldRounds==0&&r.e.ammo.reserve==48&&!r.e.world[0].valid,"action pickup adds exactly the remaining rounds to reserve without a hand prop or animation");
        Check(r.e.lastRecoveredRounds==6&&r.e.ammo.count==1&&r.e.phase==Phase::Supply,"recovery exposes notification amount and preserves chamber and reload phase");
        Check(r.e.ActionTarget(r.f)==-1,"consumed world magazine immediately disappears from ordinary pickup targeting");
        Check(!(r.Tick(20)&(Recovered|Drop))&&r.e.ammo.reserve==48,"holding action cannot recover the same magazine twice or drop a phantom hand prop");
        r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;Check(!(r.Tick()&Recovered)&&r.e.ammo.reserve==48,"new action edge cannot recreate a consumed world magazine");
    }
    {
        Replay r(0,100,1.2f,7,true);r.EjectMag();const auto old=r.e.world[0];
        const auto centre=Subtract(old.pose.position,Scale(old.pose.basis.forward,ForCategory(0).length*r.f.units*.5f));
        r.f.pickupPointValid=true;r.f.pickupPoint=Add(centre,Scale(r.f.gun.basis.right,150));r.f.pickupRayValid=true;r.f.pickupRay=r.f.gun;
        r.f.pickupRay.position=Subtract(centre,Scale(r.f.pickupRay.basis.forward,150));
        r.e.world[0].model=4;r.f.actionPickup=true;Check(!(r.Tick()&ActionTake)&&r.e.world[0].valid,"action pickup cannot mix incompatible magazine templates");r.e.world[0].model=3;
        r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;r.f.pickupRay.position=Subtract(centre,Scale(r.f.pickupRay.basis.forward,250));
        Check(!(r.Tick()&ActionTake)&&r.e.world[0].valid,"action ray cannot pick a magazine beyond bounded reach");
        r.f.actionPickup=false;r.Tick();r.f.pickupRay.position=Subtract(centre,Scale(r.f.pickupRay.basis.forward,150));r.f.actionPickup=true;
        Check(r.Tick()&ActionTake,"looking at an in-range ground magazine permits action pickup when the hand is farther away");
        Check(r.e.ammo.reserve==48&&!r.e.held&&r.e.lastRecoveredRounds==6,"aimed action pickup transfers the actual partial magazine rounds into reserve");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;
        r.f.blocked=true;r.f.actionPickup=true;Check(!(r.Tick()&ActionTake),"blocked gameplay cannot pick up a magazine by action");r.f.blocked=false;
        Check(!(r.Tick(4)&ActionTake)&&!r.e.held,"held action after a menu/tracking interruption needs a new intentional edge");
        r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;Check(r.Tick()&ActionTake,"released then pressed action resumes pickup after interruption");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;
        r.f.allowActionPickup=false;r.f.actionPickup=true;Check(r.e.ActionTarget(r.f)==-1,"native action ownership also suppresses ordinary pickup HUD target");Check(!(r.Tick()&ActionTake)&&r.e.world[0].valid,"native authored action has priority over custom magazine action pickup");
        r.f.allowActionPickup=true;Check(!(r.Tick(4)&ActionTake)&&!r.e.held,"stock-owned action edge cannot become a delayed custom pickup when its animation ends");
        r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;Check(r.Tick()&ActionTake,"new intentional action edge can pick magazine after native authored action finishes");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();auto second=r.e.world[0].pose;second.position=Add(second.position,Scale(r.f.gun.basis.right,60));
        Check(r.e.Spawn(second,4,0,3,100),"second compatible ground magazine prepared for action targeting");r.total+=4;
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;
        Check(r.e.ActionTarget(r.f)==0,"ordinary pickup target first chooses nearest hand-reachable magazine");
        const auto centre=Subtract(second.position,Scale(second.basis.forward,ForCategory(0).length*50));r.f.pickupRayValid=true;r.f.pickupRay=r.f.gun;
        r.f.pickupRay.position=Subtract(centre,Scale(r.f.pickupRay.basis.forward,150));
        Check(r.e.ActionTarget(r.f)==1,"aimed action candidate takes priority over another magazine nearer to the hand");
        r.f.actionPickup=true;Check((r.Tick()&Recovered)&&r.e.lastRecoveredRounds==4&&r.e.world[0].valid&&!r.e.world[1].valid,"action pickup consumes the exact candidate exposed for HUD text");
        r.e.phase=Phase::Finishing;Check(r.e.ActionTarget(r.f)==-1,"guided magazine insertion suppresses ordinary action target");
        r.e.phase=Phase::Slide;Check(r.e.ActionTarget(r.f)==-1,"slide grasp suppresses ordinary action target");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.Tick();auto second=r.e.world[0].pose;second.position=Add(second.position,Scale(r.f.gun.basis.right,60));
        Check(r.e.Spawn(second,4,0,3,100),"second compatible magazine prepared for between-pose action edges");r.total+=4;
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;const auto sample=r.f.sample;
        const auto buttonTick=[&](bool pressed){r.f.actionPickup=pressed;r.f.tick+=10;r.f.sample=sample;const auto events=r.e.Tick(r.f);
            Check(Valid(r.e.ammo)&&r.e.ammo.count+r.e.ammo.reserve+r.e.WorldRounds()+r.e.ejectedRounds==unsigned(r.total),"between-pose button edges preserve ammunition");return events;};
        Check((buttonTick(true)&Recovered)&&r.e.lastRecoveredRounds==6&&r.e.ammo.reserve==48,"brief action press between pose samples recovers first magazine immediately");
        Check(!(buttonTick(true)&Recovered)&&r.e.world[1].valid&&r.e.ammo.reserve==48,"held action on identical pose cannot consume a second magazine");
        Check(!(buttonTick(false)&Recovered)&&!r.e.actionDown,"action release is consumed even before a new controller pose arrives");
        Check((buttonTick(true)&Recovered)&&r.e.lastRecoveredRounds==4&&r.e.ammo.reserve==52&&!r.e.world[1].valid,"second intentional action press on same fresh pose recovers next magazine once");
        Check(!(buttonTick(true)&Recovered)&&r.e.ammo.reserve==52,"held second action edge cannot duplicate recovered rounds");
    }
    {
        Replay r(0,100,0,7,true);const auto sample=r.f.sample;r.f.eject=true;r.f.tick+=10;
        Check((r.e.Tick(r.f)&Eject)&&!r.e.inserted,"brief eject request between pose samples is processed against fresh pose");
        r.f.eject=false;r.f.tick+=10;Check(!(r.e.Tick(r.f)&Eject)&&!r.e.ejectDown&&r.f.sample==sample,"eject release between pose samples rearms edge without replaying ejection");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.SupplyMag();const int held=r.e.heldRounds,reserve=r.e.ammo.reserve;
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.actionPickup=true;
        Check(r.Tick()&Recovered,"ordinary action recovery is available while the free hand already holds a fresh magazine");
        Check(r.e.held&&r.e.heldRounds==held&&r.e.ammo.reserve==reserve+6&&!r.e.world[0].valid&&!r.e.pickupAnimating,"inventory recovery preserves the existing physical magazine and adds only recovered rounds");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.actionPickup=true;
        Check(!(r.Tick()&Recovered),"empty magazine offers no ordinary ammo recovery action");
        Check(r.e.ammo.reserve==42&&!r.e.held&&r.e.world[0].valid,"empty magazine stays on ground without manufacturing ammunition or a held prop");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.e.ammo.reserve=999999;r.total=1000006;
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.actionPickup=true;
        Check(!(r.Tick()&Recovered)&&r.e.world[0].valid&&r.e.ammo.reserve==999999,"inventory bound refuses recovery without destroying excess rounds");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(0,units,yaw,7,true);
        for(int left=7;left>0;--left){
            r.f.palm=r.f.slide;r.f.grip=0;r.Tick();r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Slide,"loaded pistol slide remains grabbable");
            const auto start=r.f.palm;r.f.palm.position=Subtract(start.position,Scale(r.f.gun.basis.forward,.048f*units));
            const auto rear=r.Tick();Check((rear&Back)&&(rear&RoundEject)&&r.e.ammo.count==left-1&&!r.e.chamber&&!r.e.ammo.ready,"rear detent extracts exactly one live chamber round");
            Check(!(r.Tick(3)&(Back|RoundEject))&&r.e.ammo.count==left-1,"holding the slide open cannot extract a second round");
            r.f.palm=start;Check(r.Tick()&Forward,"returning loaded slide closes mechanism");
            Check(r.e.ammo.count==left-1&&r.e.chamber==(left>1)&&r.e.ammo.ready==(left>1),"slide return feeds the next round without manufacturing an extra cartridge");
            r.f.grip=0;r.Tick();
        }
        Check(r.e.ejectedRounds==7&&r.e.ammo.count==0,"successive intentional slide grasps unload one round at a time");
        r.f.palm=r.f.slide;r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Slide,"empty inserted pistol also permits a dry slide grasp");
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,.048f*units));
        Check((r.Tick()&Back)&&r.e.ejectedRounds==7,"dry rack reaches rear stop without extracting a nonexistent round");
        r.f.grip=0;Check((r.Tick()&Forward)&&!r.e.chamber&&r.e.ammo.count==0,"dry rack closes without loading a nonexistent round");
    }
    {
        Replay r(0,100,1.2f,1,true);r.EjectMag();r.f.palm=r.f.slide;r.f.grip=1;r.Tick();Check(r.e.phase==Phase::Slide,"last chambered round can be extracted with magazine removed");
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,4.8f));Check((r.Tick()&RoundEject)&&r.e.ammo.count==0,"rack removes last chambered round independently of magazine");
        r.f.grip=0;Check((r.Tick()&Forward)&&!r.e.chamber&&r.e.phase==Phase::Supply,"empty pistol without magazine returns to supply state after slide closes");
    }
    {
        Replay r(0,100,0,7,true);r.f.palm=r.f.slide;r.f.grip=1;r.Tick();
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,2));Check(!(r.Tick()&(Back|RoundEject)),"short slide pull does not reach extraction detent");
        r.f.grip=0;Check(!(r.Tick()&Forward)&&r.e.chamber&&r.e.ammo.count==7&&r.e.ejectedRounds==0,"releasing a short pull preserves the loaded chamber without false rack events");
    }
    {
        Replay r(0,100,1.2f,7,true);r.f.palm=r.f.slide;r.f.slideDriverValid=true;r.f.slideDriverPoint=r.f.palm.position;r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"raw wrist driver captures slide through anatomical palm contact");
        const Quat rotation{0,std::sin(.7f),0,std::cos(.7f)};
        r.f.palm.basis={Rotate(rotation,r.f.palm.basis.forward),Rotate(rotation,r.f.palm.basis.up),Rotate(rotation,r.f.palm.basis.right)};
        r.f.palm.position=Subtract(r.f.palm.position,Scale(r.f.gun.basis.forward,6));
        Check(!(r.Tick()&(Back|RoundEject))&&r.e.slideTravel<.001f&&r.e.ammo.count==7,"controller rotation and anatomical palm offset cannot pull slide while raw wrist position stays fixed");
        r.f.slideDriverPoint=Subtract(r.f.slideDriverPoint,Scale(r.f.gun.basis.forward,4.8f));
        const auto events=r.Tick();Check((events&Back)&&(events&RoundEject)&&r.e.ammo.count==6,"translating raw wrist after capture pulls slide and extracts one chambered round");
        r.f.grip=0;Check((r.Tick()&Forward)&&r.e.chamber&&r.e.ammo.count==6,"raw wrist driven slide closes and chambers the remaining ammunition on release");
    }
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f}){
        Replay r(0,units,yaw,7,true);r.f.slideRadius=.045f;
        r.f.slideSegmentValid=true;r.f.slideA=Add(r.f.gun.position,Scale(r.f.gun.basis.forward,-.1f*units));r.f.slideB=Add(r.f.gun.position,Scale(r.f.gun.basis.forward,.14f*units));
        r.f.palm=r.f.gun;r.f.palm.position=Add(r.f.gun.position,Scale(r.f.gun.basis.up,-.10f*units));r.f.grip=1;r.Tick();
        Check(r.e.phase!=Phase::Slide,"hand near pistol grip ten centimetres below slide cannot grab the slide");
        r.f.grip=0;r.Tick();r.f.palm.position=Add(r.f.slideA,Scale(r.f.gun.basis.up,.03f*units));r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"contact within three centimetres of real slide rear endpoint captures it");
        r.f.grip=0;r.Tick();r.f.palm.position=Add(r.f.slideB,Scale(r.f.gun.basis.up,.03f*units));r.f.grip=1;r.Tick();
        Check(r.e.phase==Phase::Slide,"contact within three centimetres of real slide front endpoint captures it");
    }
    {
        Replay r(0,100,0,0,false);
        const auto holdReload=[&](unsigned n){unsigned events=0;for(unsigned i=0;i<n;++i){r.f.eject=true;events|=r.Tick();}return events;};
        Check(holdReload(1)&Eject,"fresh held reload request ejects the original clip once");
        r.f.grip=1;holdReload(22);Check(r.e.held,"held reload request still permits fresh magazine supply");
        r.f.palm=r.f.receiver;r.f.palm.position=Subtract(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6.5f));
        Check(holdReload(6)&Latch,"magazine latches while original reload request remains held");r.f.palm.position.x+=100;r.f.allowSupply=false;
        r.f.grip=0;Check(!(holdReload(1)&(Eject|Drop)),"release of guided magazine cannot drop it");
        for(unsigned i=0;i<12;++i){r.f.grip=i%2?.72f:.38f;Check(!(holdReload(1)&(Eject|Drop)),"grip threshold noise cannot release a captured magazine or repeat held eject");}
        Check(r.e.inserted&&!r.e.held&&r.e.ammo.count==15,"latched magazine commits reliably despite grip noise and held reload input");
        Check(!(holdReload(10)&(Eject|Drop))&&r.e.inserted,"continued reload hold cannot eject the newly inserted magazine");
        r.f.eject=false;r.f.grip=0;r.Tick();Check(holdReload(1)&Eject,"only releasing then pressing reload rearms magazine ejection");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();
        r.f.palm=r.f.receiver;r.f.palm.position=Subtract(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6.5f));r.Tick(6);Check(r.e.phase==Phase::Finishing,"new request rejection test starts after magazine latch");
        r.f.palm.position.x+=100;r.f.grip=0;r.f.eject=true;Check(!(r.Tick()&Eject),"fresh reload request during guided insertion is ignored and consumed");
        for(unsigned i=0;i<12;++i){r.f.eject=true;Check(!(r.Tick()&(Eject|Drop)),"ignored seating-time request cannot become delayed post-insert ejection");}
        Check(r.e.inserted&&r.e.ammo.count==15&&!r.e.held,"pending seating request leaves successfully inserted magazine intact");
    }
    {
        Replay r(0,100,0,7,false);Check(r.e.chamber&&r.e.ammo.ready,"loaded native weapon initializes manual chamber independently of released native trigger");
        for(unsigned i=0;i<80;++i){auto native=r.e.ammo;native.ready=i%2;r.e.Observe(native);Check(r.e.chamber&&r.e.ammo.ready&&r.e.ammo.count==7&&r.e.ammo.reserve==42,"native trigger bit toggling cannot unload manual chamber or change ammunition");r.Tick();}
        r.EjectMag();Check(r.e.ammo.count==1&&r.e.world[0].rounds==6,"released native trigger does not eject the chambered round with magazine");
        r.SupplyMag();r.SeatMag();auto native=r.e.ammo;native.ready=false;r.e.Observe(native);Check(r.e.chamber&&r.e.ammo.ready&&r.e.ejectedRounds==0,"tactical insertion remains chambered without a redundant live-round rack");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();r.SeatMag();auto native=r.e.ammo;native.ready=true;r.e.Observe(native);
        Check(!r.e.chamber&&!r.e.ammo.ready,"native trigger press cannot chamber a newly inserted magazine");r.RackSlide();native=r.e.ammo;native.ready=false;r.e.Observe(native);
        Check(r.e.chamber&&r.e.ammo.ready&&r.e.ammo.count==15&&r.e.ejectedRounds==0,"native trigger release after rack preserves manual chamber and all rounds");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.f.allowSupply=false;r.f.grip=1;r.Tick(30);
        Check(!r.e.held&&r.e.ammo.reserve==42,"holding grip outside pocket leaves the reserve intact");
        r.f.allowSupply=true;r.Tick(10);Check(!r.e.held&&r.e.ammo.reserve==42,"crossing the pocket with a closed grip leaves the reserve intact");
        r.f.grip=0;r.Tick();r.f.grip=1;
        Check((r.Tick()&Take)&&r.e.held&&r.e.heldRounds==15&&r.e.ammo.reserve==27,"a fresh pocket squeeze supplies exactly one magazine");
        Check(!(r.Tick(30)&Take)&&r.e.ammo.reserve==27&&r.e.heldRounds==15,"continued inventory squeeze never buys a second magazine");
    }
    {
        Replay r(0,100,0,7,true);r.f.grip=1;Check(r.Tick()&Take,"inventory squeeze can stage a spare while the old magazine remains inserted");r.Tick(10);r.EjectMag();
        Check(!(r.Tick(30)&Take)&&r.e.held&&r.e.heldRounds==15&&r.e.ammo.reserve==27,"held spare survives eject without a second purchase or forced new squeeze");
        r.SeatMag();Check(r.e.ammo.count==16&&r.e.chamber,"pre-staged magazine seats as soon as the old magazine is removed");
    }
    for(unsigned category:{0u,2u})for(int count:{0,1,7,15}){
        Replay r(category,100,1.2f,count,count>0);r.f.grip=1;
        Check((r.Tick()&Take)&&r.e.inserted&&r.e.held&&r.e.ammo.count==count,"pre-grab never ejects or changes the current loaded weapon");
        const int held=15;Check(r.e.heldRounds==held&&r.e.ammo.reserve==42-held,"pre-grab debits one correctly sized spare including a completely full gun");
        r.f.grip=0;Check((r.Tick()&Stow)&&!r.e.held&&r.e.ammo.reserve==42&&r.e.ammo.count==count,"changing one's mind returns every pre-grabbed round without altering loaded ammo");
        Check(!(r.Tick(4)&Stow)&&r.e.ammo.reserve==42,"an open hand cannot return the same staged magazine twice");
    }
    {
        Engine e;e.Select(1,2,3,2,{0,1,30,false},100);e.inserted=false;e.chamber=false;e.Remember();e.Select(1,0,0,0,{},100);
        e.NativeReloadCompleted(1,2,3,{1,0,30,true});e.Select(1,2,3,2,{1,0,30,true},100);
        Check(e.inserted&&e.chamber&&e.ammo.count==1&&e.ammo.ready,"completed native fallback acknowledges a single loaded round before returning to the same manual journal");
        e.Select(1,8,9,0,{5,20,15,true},100);e.NativeReloadCompleted(1,2,3,{2,0,30,true});
        Check(e.item==8&&e.ammo.count==5&&e.ammo.reserve==20,"source-qualified native completion cannot modify another selected weapon");
        e.Select(1,2,3,2,{2,0,30,true},100);Check(e.inserted&&e.chamber&&e.ammo.count==2,"native completion updates only its source journal while source is not selected");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.f.grip=1;
        Check((r.Tick()&Take)&&r.e.held&&r.e.ammo.reserve==27,"new inventory squeeze does not require a dwell frame");
        r.f.returnToPocket=true;r.f.grip=0;Check((r.Tick()&Stow)&&!r.e.held&&r.e.ammo.reserve==42,"immediately supplied magazine can be returned with exact accounting");
        r.f.grip=1;Check((r.Tick()&Take)&&r.e.held&&r.e.ammo.reserve==27,"release rearms the next deliberate inventory squeeze");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.SupplyMag();const int reserve=r.e.ammo.reserve,rounds=r.e.heldRounds;
        r.f.returnToPocket=true;r.f.grip=0;const auto events=r.Tick();Check((events&Stow)&&!(events&Drop)&&!r.e.held&&r.e.ammo.reserve==reserve+rounds,"release inside pocket returns every held round directly to reserve without ground prop");
        Check(!(r.Tick(10)&Stow),"held-open grip cannot return a magazine to pocket twice");
        r.SupplyMag();r.f.palm=r.f.receiver;r.f.palm.position=Subtract(r.f.receiver.position,Scale(r.f.receiver.basis.forward,6.5f));r.f.grip=0;
        const auto seat=r.Tick();Check((seat&Latch)&&!(seat&Stow)&&r.e.phase==Phase::Finishing,"accepted magwell contact takes priority over overlapping pocket-return gate");r.Tick(9);Check(r.e.inserted,"pocket gate cannot cancel captured magazine insertion");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();r.e.ammo.reserve=999999;r.total=1000014;
        r.f.returnToPocket=true;r.f.grip=0;Check(!(r.Tick()&(Stow|Drop))&&r.e.held&&r.e.heldRounds==15&&r.e.ammo.reserve==999999,"full reserve preserves held magazine instead of destroying overflow rounds");
        r.f.returnToPocket=false;r.f.grip=1;r.Tick();r.f.grip=0;Check(!(r.Tick()&(Stow|Drop))&&r.e.held,"overflow cannot bypass conservation by releasing away from the pocket");
        r.e.ammo.reserve=999980;r.total-=19;r.f.grip=1;r.Tick();r.f.grip=0;Check((r.Tick()&Stow)&&!r.e.held&&r.e.ammo.reserve==999995,"after space becomes available the preserved magazine returns exactly once");
    }
    {
        Replay r(0,100,0,0,false);r.EjectMag();
        for(auto& world:r.e.world){world={};world.valid=world.grounded=true;world.model=3;world.rounds=1;world.pose=r.f.receiver;}r.total=66;
        r.f.grip=1;r.Tick(30);Check(!r.e.held&&r.e.ammo.reserve==42,"full world pool cannot debit a new magazine that has no future drop slot");
    }
    {
        Replay r(0,100,0,7,true);r.EjectMag();r.SupplyMag();r.SeatMag();r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.grip=1;
        Check(r.Tick()&Take,"old partial magazine is held before world capacity stress");r.f.pickupPointValid=false;
        for(unsigned i=0;i<23;++i){auto& world=r.e.world[i];world={};world.valid=world.grounded=true;world.model=3;world.rounds=1;world.pose=r.f.receiver;}r.total+=23;
        Check(r.e.FreeSlots()==1,"held magazine has one reserved release slot");r.f.eject=true;Check(!(r.Tick()&Eject)&&r.e.inserted&&r.e.held,"eject cannot consume the last slot reserved for a held magazine");
        r.f.palm.position.x+=100;r.f.grip=0;Check((r.Tick()&Stow)&&!r.e.held&&r.e.FreeSlots()==1,"held magazine returns to reserve even under world capacity stress");
    }
    for(bool temporaryEmpty:{false,true}){
        Replay r(0,100,0,0,false);r.EjectMag();r.SupplyMag();
        for(auto& world:r.e.world){world={};world.valid=world.grounded=true;world.model=3;world.rounds=1;world.pose=r.f.receiver;}r.total=66;
        const int before=r.e.WorldRounds();r.e.Select(1,temporaryEmpty?0:8,temporaryEmpty?0:9,0,temporaryEmpty?Ammo{}:Ammo{5,20,15,true},100);
        Check(r.e.WorldRounds()==before&&!r.e.deferredMagazine.valid&&!r.e.held,"switch or temporary missing weapon escrows held rounds even with the whole world pool full");
        r.e.Advance(.02f,100);Check(r.e.WorldRounds()==before,"source-qualified return survives full pool and unavailable original selection");
        r.e.Select(1,2,3,0,{0,27,15,false},100);r.f.grip=0;Check((r.Tick(3)&Stow)&&r.e.ammo.reserve==42,"return to the source commits pending reserve without needing a free world slot");
        r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.actionPickup=true;
        Check((r.Tick()&Recovered)&&r.e.ammo.reserve==43,"ordinary Take ammo remains usable after full-pool source return");r.e.Advance(0,100);
        Check(!r.e.deferredMagazine.valid&&!r.e.world[0].valid&&r.e.ammo.count+r.e.ammo.reserve+r.e.WorldRounds()==66,"pending source return and ordinary pickup neither lose nor duplicate rounds");
    }
    {
        Replay r(0,100,0,7,false);
        for(unsigned cycle=0;cycle<12;++cycle){
            r.f.returnToPocket=false;r.f.pickupPointValid=false;r.f.palm=r.f.gun;r.f.palm.position.x-=80;r.f.grip=0;r.Tick();r.EjectMag();
            r.f.pickupPointValid=true;r.f.pickupPoint=r.e.world[0].pose.position;r.f.actionPickup=false;r.Tick();r.f.actionPickup=true;Check(r.Tick()&Recovered,"mixed no-fire trace recovers removed partial magazine into real reserve");
            r.f.actionPickup=false;r.f.pickupPointValid=false;r.SupplyMag();r.f.returnToPocket=true;r.f.grip=0;Check(r.Tick()&Stow,"mixed no-fire trace returns unused new magazine to pocket exactly once");
            r.f.returnToPocket=false;r.SupplyMag();r.SeatMag();r.f.blocked=true;r.Tick(3);r.f.blocked=false;r.f.grip=0;r.Tick(3);
            auto native=r.e.ammo;native.ready=cycle%2;r.e.Observe(native);Check(r.e.chamber&&r.e.ammo.count+r.e.ammo.reserve+r.e.WorldRounds()==49&&r.e.ejectedRounds==0,"repeated eject/recover/pocket/insert/pause with no shots or racks conserves all forty-nine rounds");
        }
    }
    {
        DryTrigger click;
        const auto input=[&](unsigned tick,float value,bool empty=true,bool valid=true,std::uint64_t item=7,int remaining=0){
            return click.Step(item,3,2,tick,value,valid,empty,remaining);
        };
        Check(!input(1000,1),"held trigger at activation cannot click until released");
        Check(!input(1016,0)&&input(1032,1),"fresh empty trigger edge produces a single click");
        Check(!input(1048,1)&&!input(1064,.4f)&&!input(1080,1),"held trigger and hysteresis noise do not repeat empty click");
        Check(!input(1096,0)&&!input(1152,0)&&!input(1168,1,false),"loaded trigger press does not produce an empty click");
        Check(!input(1184,1)&&!input(1200,0)&&!input(1256,0)&&input(1272,1),
              "emptying while held is silent until next deliberate trigger edge");
        Check(!input(1288,0,true,false)&&!input(1304,1),"tracking loss requires trigger release before another click");
        Check(!input(1320,0)&&!input(1500,1),"stale input interval cannot produce a surprise click");
        Check(!input(1516,0)&&!input(1532,1,true,true,8),"weapon change cannot inherit an armed trigger");
        click={};
        Check(!input(2000,0,false,true,7,1)&&!input(2016,1,true,true,7,0),
              "the first observed trigger edge can arrive with the native last-round debit, without a dry click");
        Check(!input(2032,1)&&!input(2048,0)&&!input(2112,0)&&!input(2128,1),
              "an extra trigger edge during the last shot's mechanical report stays quiet");
        Check(!input(2144,0)&&!input(2200,0)&&input(2216,1),
              "a later deliberate press on the empty gun still clicks");
        click={};
        Check(!input(3000,0,false,true,7,1)&&!input(3016,1,false,true,7,1)&&!input(3032,1)&&
              !input(3141,0)&&!input(3141,0)&&!input(3157,1),
              "one transient neutral report after a real shot cannot rearm the dry click even when the game reads it twice");
        Check(!input(3173,0)&&!input(3237,0)&&input(3253,1)&&!input(3269,1),
              "a stable release after a transient input issue restores exactly one dry click");
    }
    std::printf("KL manual reload runtime: %u checks PASS; ammo, all detachable categories, yaw/scale, pause, drops, chamber.\n",checks);
}
