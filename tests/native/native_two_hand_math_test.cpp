#include "native_two_hand_math.hpp"
#include "native_weapon_math.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

namespace {
using namespace NativeCameraMath;
using namespace NativeTwoHandMath;
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
bool Near(Vec3 a,Vec3 b,float epsilon=.001f){return NativeArmMath::Length(Subtract(a,b))<epsilon;}
bool Near(const CameraPose& a,const CameraPose& b){return Near(a.position,b.position)&&Near(a.basis.forward,b.basis.forward)&&Near(a.basis.up,b.basis.up)&&Near(a.basis.right,b.basis.right);}
CameraPose World(const CameraPose& root,const CameraPose& local){return NativeRigMath::ToWorld(root,{local.basis,local.position});}
CameraPose Frame(){CameraPose result;result.basis={{0,0,1},{0,1,0},{1,0,0}};return result;}
Key Identity(){return {1,2,3,4,5,6,7,8,9};}
struct FileImage {
    std::vector<unsigned char> data;
    struct Section {std::uint32_t virtualSize,rva,rawSize,raw;};
    std::vector<Section> sections;
    template<class T> bool Get(std::size_t offset,T& value)const {
        if(offset>data.size()||sizeof(T)>data.size()-offset)return false;
        std::memcpy(&value,data.data()+offset,sizeof(T));return true;
    }
    bool Load(const char* path) {
        std::ifstream file(path,std::ios::binary);if(!file)return false;
        file.seekg(0,std::ios::end);const auto size=file.tellg();if(size<=0||size>64*1024*1024)return false;
        data.resize(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),size);if(!file)return false;
        std::uint32_t pe=0,signature=0;std::uint16_t count=0,optional=0,magic=0;
        if(!Get(0x3C,pe)||!Get(pe,signature)||signature!=0x4550||!Get(pe+6,count)||count>96||
           !Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10B)return false;
        for(unsigned i=0;i<count;++i){Section section{};if(!Get(pe+24+optional+40*i+8,section))return false;sections.push_back(section);}
        return true;
    }
    bool Read(std::uintptr_t address,void* output,std::size_t size)const {
        if(address<0x400000)return false;const auto rva=address-0x400000;
        for(const auto& section:sections)if(rva>=section.rva&&rva-section.rva<section.rawSize) {
            const auto offset=rva-section.rva;
            if(size>section.rawSize-offset||section.raw+offset>data.size()||size>data.size()-section.raw-offset)return false;
            std::memcpy(output,data.data()+section.raw+offset,size);return true;
        }
        return false;
    }
};
void Gestures(float units) {
    const auto key=Identity();State state;
    CameraPose primary=Frame(),controller=Frame(),socket=Frame(),right,left;controller.position={0,0,.30f*units};socket.position=controller.position;
    auto tick=[&](float grip,bool eligible=true,bool candidate=true) {
        static std::uint32_t now=1000;now+=16;
        return Targets(state,key,socket,candidate,primary,controller,units,now,now,grip,eligible,right,left);
    };
    Check(!tick(1)&&!tick(.5f)&&!tick(1),"button held at context entry requires a full release");
    Check(!tick(0)&&tick(1)&&state.held,"fresh press within authored socket acquires support");
    const auto heldSocket=state.socket;socket.position={0,.5f*units,0};
    controller.position={.15f*units,0,.30f*units};
    Check(tick(1,true,false)&&Near(state.socket,heldSocket),"native recoil candidate loss preserves the frozen owned contact");
    Check(Near(right.position,primary.position)&&Near(NativeRigMath::ToModel(right,left),heldSocket),"two-hand steering fixes primary position and rigid contact");
    Check(tick(.4f)&&tick(.25f)&&tick(.19f)&&tick(.09f)&&state.held,"relaxed analog grip never crosses the stronger physical-release threshold");
    Check(tick(0)&&tick(.9f)&&state.held,"one zero-valued host sample cannot drop a physically held grip");
    bool released=false;for(unsigned i=0;i<10;++i)released=!tick(0);
    Check(released&&!state.held&&state.lowGripSamples>=3,"only sustained low-grip host publications confirm physical release");
    Check(!tick(1,false)&&!tick(1)&&!tick(.4f)&&!tick(1),"UI/action invalidation blocks re-acquisition until full release");
    socket=heldSocket;controller.position=socket.position;
    Check(!tick(0)&&tick(1),"released button can reacquire after context recovery");
    controller.position=primary.position;
    Check(tick(1)&&state.held&&state.aimRecovered,"one coincident-hand sample retains the last valid orientation");
    for(unsigned i=0;i<10;++i)tick(1);
    Check(tick(1)&&state.held&&state.aimRecovered,"held grip retains the last valid orientation through a long singular lever");
    controller.position=socket.position;Check(tick(1),"returning to contact reacquires after geometry failure without a second press");
    // A new item/source identity never inherits a held gesture.
    for(unsigned part=0;part<9;++part) {
        State next=state;auto changed=key;
        switch(part){case 0:++changed.player;break;case 1:++changed.bodyModel;break;case 2:++changed.weapon;break;
        case 3:++changed.root;break;case 4:++changed.model;break;case 5:++changed.marker;break;case 6:++changed.markerGeometry;break;
        case 7:++changed.reference;break;default:++changed.recenter;break;}
        Check(!Targets(next,changed,socket,true,primary,controller,units,state.tick+16,1,1,true,right,left)&&!next.held,
              "every ownership/source/recenter change drops held contact");
    }
    Check(Targets(state,key,socket,true,primary,controller,units,state.tick+1000,1,1,true,right,left),"renderer stall does not discard a fresh valid grip and owner");
    Check(!Targets(state,key,socket,true,primary,controller,units,state.tick+16,2,1,true,right,left,nullptr,false)&&state.held&&state.suspended,
          "temporary native reload or absent pose suspends output without dropping the physical latch");
    Check(Targets(state,key,socket,true,primary,controller,units,state.tick+16,3,1,true,right,left)&&state.held,
          "the same gun resumes after suspension without another contact-zone test");
    for(unsigned i=0;i<9;++i)Targets(state,key,socket,true,primary,controller,units,state.tick+16,4+i,0,true,right,left,nullptr,false);
    Check(!state.held&&state.releaseReason==ReleaseReason::Input,
          "confirmed physical release retires a suspended grip without requiring a valid visual pose");
    state={};controller.position={.19f*units,0,.30f*units};
    Check(!tick(0)&&!tick(1),"new grip outside enlarged fore-end zone cannot grab");
    controller.position=socket.position;
    Check(tick(1),"a squeezed hand entering the foregrip zone acquires without an awkward second press");
    state={};controller.position=socket.position;
    Check(!tick(0)&&!tick(std::numeric_limits<float>::quiet_NaN()),"nonfinite grip fails closed");
    // One predicted stereo pair publishes one result, including weapon-before-body.
    state={};Targets(state,key,socket,true,primary,controller,units,1000,10,0,true,right,left);
    Check(Targets(state,key,socket,true,primary,controller,units,1016,11,1,true,right,left),"pair starts with a valid acquisition");
    const auto pairRight=right,pairLeft=left;controller.position.x+=.1f*units;
    Check(Targets(state,key,socket,true,primary,controller,units,1017,11,1,true,right,left)&&Near(pairRight,right)&&Near(pairLeft,left),
          "both eyes and lazy/body draw reuse exactly the same coupled targets");
    Check(!state.acquired&&!state.aimRecovered&&!state.released,"stereo cache reuse does not duplicate gesture events");
    // Many body/material draws of one zero-valued host sample must not turn
    // wall time into evidence of a sustained physical release.
    for(unsigned i=0;i<20;++i)Targets(state,key,socket,true,primary,controller,units,1030+i*16,20+i,0,true,right,left,nullptr,true,1030);
    Check(state.held&&state.lowGripSamples==1,"repeated same-input-tick zero keeps the grip through arbitrarily many GPU draws");
    Targets(state,key,socket,true,primary,controller,units,1500,50,0,true,right,left,nullptr,true,1500);
    Check(state.held&&state.lowGripSamples==1,"a stall gap cannot complete a pending low-grip release");
    Targets(state,key,socket,true,primary,controller,units,1516,51,0,true,right,left,nullptr,true,1516,false);
    Check(state.held&&state.suspended&&state.lowGripSamples==0,"unknown grip input breaks confirmation and only suspends logical contact");
}
void ContactAndSteering(float units) {
    auto primary=Frame(),socket=Frame(),controller=Frame();socket.position={0,0,.30f*units};
    controller.position={.17f*units,0,.35f*units};
    Check(ContactNear(primary,socket,controller,units),"fore-end capsule permits a natural imperfect hand placement");
    controller.position={0,0,.53f*units};
    Check(ContactNear(primary,socket,controller,units),"fore-end capsule permits a different grip along the gun");
    controller.position={0,0,.55f*units};
    Check(!ContactNear(primary,socket,controller,units),"fore-end contact stays bounded along the barrel");
    auto rawRight=primary,rawLeft=primary,displayed=primary;
    rawRight.position={.2f*units,.2f*units,.8f*units};
    rawLeft.position=Add(rawRight.position,{-.12f*units,.08f*units,.36f*units});
    displayed.position={.15f*units,.1f*units,.4f*units};
    CameraPose lever,right,left;
    Check(SteeringController(displayed,rawRight,rawLeft,lever)&&
          Near(Subtract(lever.position,displayed.position),Subtract(rawLeft.position,rawRight.position)),
          "steering retains the actual physical hand lever when the right wrist is IK-clamped");
    Check(Aim(displayed,socket,lever,units,right,left),"physical lever supplies a valid coupled pose");
    Vec3 actual,expected;
    Check(NativeArmMath::Unit(Subtract(left.position,right.position),actual)&&
          NativeArmMath::Unit(Subtract(rawLeft.position,rawRight.position),expected)&&Near(actual,expected),
          "coupled grip aims exactly along physical hands rather than two unrelated shoulder projections");
    auto reversed=primary;reversed.position={.05f*units,0,-.30f*units};
    Check(Aim(primary,socket,reversed,units,right,left),"offhand remains the lever beyond ninety degrees of wrist bend");
    Check(NativeArmMath::Unit(Subtract(left.position,right.position),actual)&&
          NativeArmMath::Unit(reversed.position,expected)&&Near(actual,expected),
          "large primary wrist bend does not silently detach the support hand");
    auto far=primary;far.position={0,0,.9f*units};
    Check(Aim(primary,socket,far,units,right,left)&&
          std::fabs(NativeArmMath::Length(Subtract(left.position,right.position))-.30f*units)<.0001f*units,
          "moving support farther away changes direction without stretching the fixed weapon contact");
    State state;controller=World(displayed,socket);
    Targets(state,Identity(),socket,true,displayed,controller,units,100,1,0,true,right,left,&lever);
    Check(Targets(state,Identity(),socket,true,displayed,controller,units,116,2,.60f,true,right,left,&lever),
          "moderate squeeze in the rendered contact zone acquires with the physical steering lever");
    const auto expectedRight=right;controller.position=displayed.position;auto singular=displayed;
    Check(Targets(state,Identity(),socket,true,displayed,controller,units,132,3,.25f,true,right,left,&singular)&&
          state.aimRecovered&&Near(right.basis.forward,expectedRight.basis.forward),
          "relaxed grip and one singular sample retain the acquired aiming rotation");
    auto moved=displayed;moved.position=Add(moved.position,{.05f*units,0,0});singular=moved;
    Check(Targets(state,Identity(),socket,true,moved,controller,units,148,4,.25f,true,right,left,&singular)&&
          Near(right.position,moved.position)&&Near(NativeRigMath::ToModel(right,left),socket),
          "grace orientation follows today's primary position and exact authored contact");
    for(unsigned i=0;i<9;++i)Targets(state,Identity(),socket,true,moved,controller,units,164+i*16,5+i,0,true,right,left,&singular);
    Check(!state.held&&state.releaseReason==ReleaseReason::Input,"confirmed button release overrides aiming recovery grace");
}
void AnatomicalReach(float units) {
    const NativeArmMath::Joints arms[]={{{-.18f*units,0,0},{-.18f*units,-.10f*units,.25f*units},{-.18f*units,0,.50f*units}},
                                      {{ .18f*units,0,0},{ .18f*units,-.10f*units,.25f*units},{ .18f*units,0,.50f*units}}};
    Reach reach[2];Check(ArmReach(arms[0],reach[0])&&ArmReach(arms[1],reach[1]),"anatomical bounds use actual fixed upper/lower lengths");
    for(int step=-8;step<=8;++step) {
        CameraPose right=Frame(),left=Frame();right.position={.10f*units,.0125f*step*units,.40f*units};
        left.position={-.10f*units,.0125f*step*units,.60f*units};
        const auto originalRight=right,originalLeft=left,relative=NativeRigMath::ToModel(right,left);
        Check(FitReach(right,left,reach[1],reach[0],units),"slightly overextended support hand keeps its owned foregrip");
        Check(Near(NativeRigMath::ToModel(right,left),relative)&&Near(right.basis.forward,originalRight.basis.forward)&&
              NativeArmMath::Length(Subtract(right.position,originalRight.position))<=.18f*units,
              "reach correction translates the rigid weapon contact without scale or rotation");
        for(unsigned side=0;side<2;++side) {
            NativeArmMath::Joints solved;bool clamped=true;const auto target=side?right.position:left.position;
            Check(NativeArmMath::Solve(arms[side],target,{0,-1,0},solved,&clamped)&&!clamped,
                  "both final coupled targets pass the real arm solver without reach clamping");
            Check(std::fabs(NativeArmMath::Length(Subtract(solved.elbow,solved.shoulder))-
                            NativeArmMath::Length(Subtract(arms[side].elbow,arms[side].shoulder)))<.00001f*units&&
                  std::fabs(NativeArmMath::Length(Subtract(solved.wrist,solved.elbow))-
                            NativeArmMath::Length(Subtract(arms[side].wrist,arms[side].elbow)))<.00001f*units,
                  "fixed anatomical upper and lower segments never lengthen");
        }
        CameraPose world=Frame();const Quat q{.2f,.1f,.3f,std::sqrt(.86f)};
        world.basis={Rotate(q,world.basis.forward),Rotate(q,world.basis.up),Rotate(q,world.basis.right)};
        world.position={-2*units,3*units,4*units};
        Reach movedReach[2];for(unsigned side=0;side<2;++side){movedReach[side]=reach[side];movedReach[side].shoulder=World(world,{Frame().basis,reach[side].shoulder}).position;}
        auto movedRight=World(world,originalRight),movedLeft=World(world,originalLeft);
        Check(FitReach(movedRight,movedLeft,movedReach[1],movedReach[0],units)&&
              NativeArmMath::Length(Subtract(movedRight.position,World(world,right).position))<.00001f*units&&
              NativeArmMath::Length(Subtract(movedLeft.position,World(world,left).position))<.00001f*units,
              "reach solution is invariant under authored world rotation and translation");
    }
    CameraPose right=Frame(),left=Frame();right.position={0,0,1.5f*units};left.position={0,0,1.8f*units};
    const auto untouchedRight=right,untouchedLeft=left;
    Check(!FitReach(right,left,reach[1],reach[0],units)&&Near(right,untouchedRight)&&Near(left,untouchedLeft),
          "large impossible controller offsets reject atomically instead of dragging weapon or stretching arms");
    Reach invalid=reach[1];invalid.maximum=invalid.minimum;
    Check(!FitReach(right,left,invalid,reach[0],units),"invalid anatomical intervals cannot authorize a coupled pose");
}
void Geometry(float units) {
    CameraPose right=Frame(),left=Frame(),socket;left.position={0,0,.30f*units};const Vec3 muzzle{0,0,.60f*units};
    Check(AuthoredSocket(right,left,muzzle,units,socket),"separated original wrist before real muzzle supplies authored contact");
    CameraPose bad=left;bad.position={0,-.4f*units,0};
    Check(!AuthoredSocket(right,bad,muzzle,units,socket),"hanging native offhand does not masquerade as a foregrip");
    bad.position={0,0,.70f*units};Check(!AuthoredSocket(right,bad,muzzle,units,socket),"contact beyond muzzle rejected");
    bad.position={0,0,.03f*units};Check(!AuthoredSocket(right,bad,muzzle,units,socket),"pistol cup is not a long-gun foregrip");
    Check(AuthoredSocket(right,left,muzzle,units,socket),"valid authored contact restored");
    for(const float invalid:{0.f,-1.f,1001.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        CameraPose untouchedRight=right,untouchedLeft=left;untouchedRight.position.x=123.f;untouchedLeft.position.y=456.f;
        const auto originalRight=untouchedRight,originalLeft=untouchedLeft;
        Check(!Aim(right,socket,left,invalid,untouchedRight,untouchedLeft)&&
              Near(untouchedRight,originalRight)&&Near(untouchedLeft,originalLeft),
              "invalid direct-call world scale rejects atomically before steering");
    }
    for(int step=-9;step<=9;++step) {
        const float angle=float(step)*.12f;
        const Quat q{0,std::sin(angle/2),0,std::cos(angle/2)};
        CameraPose primary=right;primary.position={2.f*units,.5f*units,-units};
        CameraPose physical=left;physical.position=Add(primary.position,Rotate(q,socket.position));
        CameraPose aimed,support;
        Check(Aim(primary,socket,physical,units,aimed,support),"two-controller line steers authored lever through yaw sweep");
        Check(Near(aimed.position,primary.position)&&Near(Subtract(support.position,aimed.position),Subtract(physical.position,primary.position)),
              "physical line rotates support while exact primary translation is retained");
        const CameraPose attachment=right,localMuzzle{right.basis,muzzle};CameraPose rendered;Vec3 origin,target;
        Check(NativeWeaponMath::Shot(right,aimed,attachment,localMuzzle,units,rendered,origin,target)&&
              Near(origin,World(rendered,localMuzzle).position)&&
              Near(Scale(Subtract(target,origin),1.f/(100.f*units)),aimed.basis.right),
              "rendered attachment and native shot consume the same coupled primary wrist");
        CameraPose world;const Quat turn{.2f,0,0,std::sqrt(.96f)};
        world.basis={Rotate(turn,world.basis.forward),Rotate(turn,world.basis.up),Rotate(turn,world.basis.right)};
        world.position={-3.f*units,2.f*units,units};CameraPose movedRight,movedLeft;
        Check(Aim(World(world,primary),socket,World(world,physical),units,movedRight,movedLeft)&&
              Near(movedRight,World(world,aimed))&&Near(movedLeft,World(world,support)),"world translation/rotation preserves contact and steering");
    }
}
}
void RenderedContact(float units) {
    using namespace NativeTwoHandMath;
    auto root=Frame();root.position={3*units,-2*units,units};
    const Quat turn{0,std::sin(.31f),0,std::cos(.31f)};
    root.basis={Rotate(turn,root.basis.forward),Rotate(turn,root.basis.up),Rotate(turn,root.basis.right)};
    NativeRigMath::Bone input[7]{};const std::uint32_t parents[7]={0,0,1,2,0,4,5};
    for(auto& bone:input)bone.basis=Frame().basis;
    input[1].position={.2f*units,1.5f*units,0};input[2].position={.2f*units,1.5f*units,.3f*units};input[3].position={.2f*units,1.5f*units,.6f*units};
    input[4].position={-.2f*units,1.5f*units,.4f*units};input[5].position={0,1.3f*units,.55f*units};input[6].position={.2f*units,1.5f*units,.9f*units};
    auto rawRight=Frame();rawRight.position={.2f*units,1.5f*units,units};rawRight=World(root,rawRight);
    auto socket=Frame();socket.position={0,0,.3f*units};CameraPose displayedRight,displayedLeft;
    const NativeArmMath::Joints rightArm{input[1].position,input[2].position,input[3].position};
    const NativeArmMath::Joints leftArm{input[4].position,input[5].position,input[6].position};
    const Vec3 pole{1,-.5f,0};
    Check(RenderedWrist(root,rightArm,rawRight,pole,displayedRight),"raw controller beyond reach projects to the actual displayed primary wrist");
    const auto rawLeft=World(displayedRight,socket);
    Check(RenderedWrist(root,leftArm,rawLeft,pole,displayedLeft),"support controller at the displayed gun projects through the same native arm solver");
    NativeRigMath::Bone actual[7];NativeRigMath::Result result;
    Check(NativeRigMath::SolveArm(input,parents,7,1,2,3,NativeRigMath::ToModel(root,rawRight),pole,actual,result)&&result.clamped&&
          Near(displayedRight.position,NativeRigMath::ToWorld(root,actual[3]).position)&&Near(displayedRight.basis.up,rawRight.basis.up),
          "lightweight contact projection matches full production SolveArm wrist and preserves controller roll");
    State oldState,newState;CameraPose right,left;
    Targets(oldState,Identity(),socket,true,rawRight,rawLeft,units,100,1,0,true,right,left);
    Check(!Targets(oldState,Identity(),socket,true,rawRight,rawLeft,units,116,2,1,true,right,left),
          "old raw-controller contact misses the displayed foregrip despite the physical support hand touching it");
    Targets(newState,Identity(),socket,true,displayedRight,displayedLeft,units,100,1,0,true,right,left);
    Check(Targets(newState,Identity(),socket,true,displayedRight,displayedLeft,units,116,2,1,true,right,left),
          "actual rendered-wrist contact acquires the visible foregrip zone");
    const auto before=displayedRight;auto bad=root;bad.basis.up={0,0,0};
    Check(!RenderedWrist(bad,rightArm,rawRight,pole,displayedRight)&&Near(before,displayedRight),
          "invalid native root cannot alter the existing contact pose");
}
int main(int argc,char** argv) {
    {
        auto socket=Frame();socket.position={4,-3,29};
        SeparateRifleSocket(socket,{4,-3,70},{4,-3,25},{0,0,1},100);
        Check(Near(socket.position,{4,-3,39}),"close rifle contact moves along the barrel and clears the bolt");
        const auto aligned=socket;
        SeparateRifleSocket(socket,{4,-3,70},{4,-3,25},{0,0,1},100);
        Check(Near(socket,aligned),"a separated foregrip does not move again");
        socket.position={4,-3,28};
        SeparateRifleSocket(socket,{4,-3,41},{4,-3,25},{0,0,1},100);
        Check(Near(socket.position,{4,-3,31}),"the foregrip stops short of the muzzle");
    }
    {
        State state;const auto primary=Frame();auto socket=Frame();socket.position={35,0,0};auto off=World(primary,socket);CameraPose right,left;
        Targets(state,Identity(),socket,true,primary,off,100,100,1,0,true,right,left);
        Check(Targets(state,Identity(),socket,true,primary,off,100,116,2,1,true,right,left),"foregrip acquired before support optical loss");
        auto moved=primary;moved.position={4,3,2};
        Check(Targets(state,Identity(),socket,false,moved,off,100,132,3,1,true,right,left,nullptr,false,132,true,true)&&
            state.held&&state.aimRecovered&&Near(NativeRigMath::ToModel(right,left).position,socket.position),
            "valid held grip and current primary preserve rigid visual foregrip through offhand pose loss");
        Check(!Targets(state,Identity(),socket,false,moved,off,100,148,4,1,true,right,left,nullptr,false,148,true,false)&&state.held,
            "loss of both controller poses suspends visualization without inventing a new primary pose");
        state={};auto carrier=Frame();
        Targets(state,Identity(),socket,true,primary,off,100,200,10,0,true,right,left,nullptr,true,200,true,true,&carrier);
        Check(Targets(state,Identity(),socket,true,primary,off,100,216,11,1,true,right,left,nullptr,true,216,true,true,&carrier),"moving-carrier foregrip acquired");
        const auto previousRight=right,previousLeft=left;carrier.position={50,0,0};auto primaryMoved=primary,offMoved=off;primaryMoved.position.x+=50;offMoved.position.x+=50;
        Check(Targets(state,Identity(),socket,true,primaryMoved,offMoved,100,232,11,1,true,right,left,nullptr,true,232,true,true,&carrier)&&
            Near(right.position,Add(previousRight.position,{50,0,0}))&&Near(left.position,Add(previousLeft.position,{50,0,0})),
            "second eye of same pair rebases BOTH grips with current vehicle instead of stale world positions");
    }
    for(unsigned category=0;category<32;++category)
        Check(LongGun(category)==((category>=2&&category<=6)||category==15),"only verified native long firearm categories eligible");
    for(float units:{1.f,100.f,1000.f}){Gestures(units);Geometry(units);AnatomicalReach(units);RenderedContact(units);ContactAndSteering(units);}
    if(argc>1) {
        FileImage image;Check(image.Load(argv[1]),"pinned PE32 loads without running game");
        auto read=[&](auto address,void* output,auto size){return image.Read(address,output,size);};
        Check(VerifyTypeImage(0x400000,read),"actual EXE proves template field getter and all long-gun enum metadata");
        auto changed=[&](auto address,void* output,auto size){if(!read(address,output,size))return false;
            if(address==0x400000+0x279A50)static_cast<unsigned char*>(output)[0]^=1;return true;};
        Check(!VerifyTypeImage(0x400000,changed),"changed real template category layout disables grip eligibility");
    }
    std::printf("KL1 native two-hand grip: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
