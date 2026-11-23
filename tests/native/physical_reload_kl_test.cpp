// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#include "physical_reload_geometry_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace PhysicalReloadKL;
static unsigned checks=0;
static void Check(bool value,const char* text){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
struct Fixture {
    State state;Input input;
    Fixture(){state.Begin(1,7);input.valid=true;input.scene=1;input.reference=7;input.units=100;
        input.palm.position={0,0,50};input.receiver.position={30,0,50};}
    bool Tick(unsigned dt=20){input.tick+=dt;input.sample=input.tick;return state.Tick(input);}
    void Hold(){input.grip=0;Tick();input.grip=1;Tick();for(unsigned i=0;i<20;++i)Tick();}
    void Contact(){input.palm.position=Subtract(input.receiver.position,Scale(input.palm.basis.forward,.065f*input.units));}
};
int main(){
    {
        Check(LoaderForContacts(0,false,false,false)==Loader::MagazineAndSlide,"accepted pistol fallback retains full manual mechanism");
        for(unsigned category:{1u,7u,14u,18u})Check(LoaderForContacts(category,true,true,true)==Loader::Native,"unsupported cylinder/launcher cannot qualify by coincidental bone names");
        for(unsigned category:{2u,4u,5u,6u,15u,16u,17u}){
            Check(LoaderForContacts(category,false,true,true)==Loader::Native,"magazine without owned frame cannot enable manual loader");
            Check(LoaderForContacts(category,true,false,true)==Loader::Native,"frame and handle without actual detachable clip retain native loader");
            Check(LoaderForContacts(category,true,true,true)==Loader::MagazineAndSlide,"real frame clip and handle permit complete manual loader");
        }
        Check(LoaderForContacts(2,true,true,false)==Loader::MagazineOnly,"real SMG clip with monolithic receiver supports magazine handling without invented slide");
        Check(LoaderForContacts(4,true,true,false,false,true)==Loader::MagazineOnly,"measured AK magazine needs no synthetic bolt");
        Check(LoaderForContacts(4,true,true,true,false,true)==Loader::MagazineAndSlide,"a real rifle slide remains preferred");
        Check(LoaderForContacts(4,true,false,false,false,true)==Loader::Native,"AK still needs a live owned magazine");
        Check(LoaderForContacts(17,true,true,false,false,true)==Loader::MagazineOnly,"exact measured bullpup permits manual magazine handling without invented slide");
        Check(!Supported(3)&&LoaderForContacts(3,true,true,true,false,true)==Loader::Native,
              "Type 88 always uses native reload, even if a model has all named contacts");
        Check(LoaderForContacts(17,false,true,false,false,true)==Loader::Native,"bullpup proof still requires owned frame");
        Check(LoaderForContacts(17,true,false,false,false,true)==Loader::Native,"bullpup proof still requires detachable magazine");
        Check(LoaderForContacts(6,true,true,false,false,true)==Loader::Native,"bullpup exception cannot enable a different native category");
        for(unsigned category:{3u,4u,5u,6u,15u,16u,17u})Check(LoaderForContacts(category,true,true,false)==Loader::Native,"other families do not silently acquire SMG magazine-only policy");
    }
    {
        Visual contact;contact.active=contact.gunValid=true;contact.inserted=false;contact.tick=1000;
        for(auto phase:{Phase::Supply,Phase::NeedsChamber,Phase::Slide}){contact.phase=phase;
            Check(PhysicalReloadGeometryKL::Build(contact,1000).vertices.empty(),"magwell and slide contacts emit no floating outline geometry");}
    }
    {
        // Observed session1324: the published gun grip is local centimetres,
        // while the wrist is kilometres from the level origin. Never subtract
        // those two spaces, and never assume the authored F axis is a barrel.
        CameraPose socket;socket.position={.08217999f,-.05116091f,.02355258f};
        socket.basis={{.9853507f,-.170543f,.00000094f},{.1705437f,.9853528f,-.00000242f},{-.00000062f,-.00000247f,-1.000002f}};
        CameraPose wrist;wrist.position={-5702.062f,219.2249f,-10313.4238f};
        wrist.basis={{-.99089f,-.13439f,-.00834f},{-.13459f,.99042f,.03096f},{-.00410f,-.03180f,.99949f}};
        const Vec3 barrel{.01890991f,-.2002422f,.9795639f};
        for(unsigned category:{0u,4u})for(unsigned rotation=0;rotation<24;++rotation){const float half=float(rotation)*.13f;const Quat q{0,std::sin(half),0,std::cos(half)};
            auto frame=wrist;frame.basis={Rotate(q,wrist.basis.forward),Rotate(q,wrist.basis.up),Rotate(q,wrist.basis.right)};
            CameraPose local;Check(ReceiverLocal(socket,ReloadMathKL::Inverse(frame.basis,Rotate(q,barrel)),category,100,local),"recorded local socket accepts actual barrel frame");
            const auto receiver=ReloadMathKL::ToWorld(frame,local);const auto delta=Subtract(receiver.position,frame.position);
            Check(std::sqrt(Dot(delta,delta))<50&&std::sqrt(Dot(delta,delta))>5,"recorded receiver remains next to wrist at large world coordinates");
            Check(Dot(receiver.basis.up,Rotate(q,barrel))>.999f,"receiver uses the actual barrel, independently of authored mesh axes");
        }
    }
    for(unsigned type=0;type<19;++type)Check(Supported(type)==((type<=6&&type!=3)||type==15||type==16||type==17),
        "the family catalog reserves Type 88 for native reload and requires model contacts for supported families");
    Fixture f;f.input.grip=1;for(unsigned i=0;i<30;++i)f.Tick();Check(!f.state.Held(),"a preheld grip cannot summon a magazine");
    f.Hold();Check(f.state.phase==Phase::Holding,"fresh sustained grip supplies in free space");
    f.input.grip=0;Check(!f.Tick()&&f.state.phase==Phase::Supply,"release away drops magazine without requesting native refill");
    f.Hold();f.Contact();for(unsigned i=0;i<6;++i)Check(!f.Tick(),"contact while held never commits ammunition");
    Check(f.state.phase==Phase::Seated,"forgiving contact has a stable seating cue");
    f.input.grip=0;Check(f.Tick()&&f.state.phase==Phase::Finishing,"release at receiver requests exactly one native continuation");
    for(unsigned i=0;i<20;++i)Check(!f.Tick(),"repeated frames cannot repeat a commit");
    for(unsigned boundary=0;boundary<4;++boundary){Fixture p;p.Hold();p.Contact();p.Tick();
        if(boundary==0)p.input.valid=false;
        if(boundary==1)p.input.blocked=true;
        if(boundary==2)p.input.grip=std::numeric_limits<float>::quiet_NaN();
        if(boundary==3)p.input.reference=8;
        for(unsigned i=0;i<25;++i)Check(!p.Tick(),"invalid/context-blocked frames cannot complete insertion");
        p.input.valid=true;p.input.blocked=false;p.input.grip=0;p.input.reference=7;
        Check(!p.Tick()&&p.state.phase==Phase::Holding,"resume release is neutral, never an insertion edge");
    }
    Fixture blocked;blocked.input.blocked=true;blocked.Hold();Check(!blocked.state.Held(),"world pickup blocks supply");
    blocked.input.blocked=false;for(unsigned i=0;i<40;++i)blocked.Tick();Check(!blocked.state.Held(),"unblocking requires a fresh grip");
    Fixture shortHold;shortHold.Tick();shortHold.input.grip=1;shortHold.Tick();shortHold.Tick(100);shortHold.input.grip=0;shortHold.Tick();
    Check(!shortHold.state.Held(),"short presses remain available to other gestures");
    Fixture inverted;inverted.Hold();inverted.Contact();inverted.input.palm.basis.forward=Scale(inverted.input.palm.basis.forward,-1);
    inverted.input.palm.basis.right=Scale(inverted.input.palm.basis.right,-1);inverted.Contact();
    for(unsigned i=0;i<20;++i)inverted.Tick();Check(inverted.state.phase==Phase::Holding,"fully inverted feed cannot seat");
    inverted.input.grip=0;Check(!inverted.Tick(),"inverted release never authorizes refill");
    for(float units:{1.f,100.f,1000.f})for(unsigned category:{0u,2u,3u,4u,6u,15u,16u,17u})for(unsigned rotation=0;rotation<24;++rotation){
        Fixture p;p.input.units=units;const float half=float(rotation)*.13f;const Quat q{0,std::sin(half),0,std::cos(half)};
        CameraPose gun;gun.basis={Rotate(q,gun.basis.forward),Rotate(q,gun.basis.up),Rotate(q,gun.basis.right)};gun.position={300,100,-200};
        p.input.receiver=Receiver(gun,ForCategory(category),units);p.input.palm.basis=p.input.receiver.basis;
        const auto well=Scale(Subtract(p.input.receiver.position,gun.position),1.f/units);
        Check(std::fabs(Dot(well,gun.basis.right))<.0001f&&Dot(well,gun.basis.forward)>.01f&&Dot(well,gun.basis.up)<-.05f,
            "receiver sits below and forward of the grip, never sideways");
        p.input.palm.position=Add(p.input.receiver.position,{2*units,0,0});p.Hold();p.Contact();
        for(unsigned i=0;i<6;++i)p.Tick();p.input.grip=0;Check(p.Tick(),"seating invariant under body yaw, translation, weapon category and unit scale");
        Visual v;v.active=v.held=v.gunValid=true;v.inserted=false;v.units=units;v.category=category;v.phase=Phase::Holding;v.tick=1000;
        v.receiver=p.input.receiver;v.magazine=Magazine(p.input.palm,units);
        Check(PhysicalReloadGeometryKL::Build(v,1000).vertices.empty(),"held magazine is never replaced with procedural geometry");
        v.world[0].valid=true;v.world[0].pose=v.magazine;v.world[0].model=0x1111;
        v.world[1]=v.world[0];v.world[1].model=0x2222;v.world[1].rounds=7;
        v.held=false;v.gunValid=false;
        Check(PhysicalReloadGeometryKL::Build(v,1000).vertices.empty(),"culled or different-model native floor magazines never acquire fake geometry");
        Check(PhysicalReloadGeometryKL::Build(v,1200).vertices.empty(),"stale reload visuals hidden");
    }
    {
        Fixture duplicate;duplicate.Tick();duplicate.input.grip=1;duplicate.Tick();
        for(unsigned i=0;i<15;++i){duplicate.input.tick+=5;Check(!duplicate.state.Tick(duplicate.input),"duplicate pose cannot advance supply");}
        Check(!duplicate.state.Held(),"render repeats do not supply a magazine");
    }
    std::printf("KL physical reload: %u checks PASS; no game calls or ammo writes.\n",checks);
}
