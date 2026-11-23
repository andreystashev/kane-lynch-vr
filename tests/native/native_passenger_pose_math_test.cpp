#include "native_passenger_pose_math.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace NativePassengerPoseMath;
unsigned checks=0;void Check(bool value,const char* name){++checks;if(!value){std::printf("FAIL %s\n",name);std::exit(1);}}
bool Near(Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.01f;}
constexpr unsigned Count=15;
const std::uint32_t parents[Count]={0,0,1,2,3,1,5,6,7,1,9,10,11,2,2};
const Rig rig{1,3,{{5,6,7,8},{9,10,11,12}}};
void Build(CameraPose* bind,Bone* pose){
    const Vec3 positions[Count]={{},{0,95,0},{0,140,0},{0,160,0},{0,164,-8},{10,90,0},{10,48,-3},{10,7,0},{10,3,-15},
        {-10,90,0},{-10,48,-3},{-10,7,0},{-10,3,-15},{20,140,0},{-20,140,0}};
    for(unsigned i=0;i<Count;++i){bind[i].position=positions[i];pose[i]={bind[i].basis,bind[i].position};}
}
}
int main(){
    std::array<CameraPose,Count> bind{};std::array<Bone,Count> source{},posed{};Build(bind.data(),source.data());
    const auto original=source;Sample sample;std::array<std::uint8_t,Count> changed{};
    Check(Topology(rig,parents,Count),"exact named pelvis/head and both thigh-calf-foot-toe chains validated");
    Check(Apply(bind.data(),source.data(),posed.data(),parents,Count,rig,sample,100,changed.data()),"native bind lengths support low cargo crouch without moving entity/floor");
    Check(Near(posed[3].position,{0,115,0})&&Near(posed[1].position,{0,50,0}),"crouch moves hips/head once inside skin pose");
    Check(Near(posed[7].position,bind[7].position)&&Near(posed[11].position,bind[11].position)&&
        Near(posed[8].position,bind[8].position)&&Near(posed[12].position,bind[12].position),"crouch IK pins native feet/toes to original floor height");
    Check(posed[6].position.z<-20&&posed[10].position.z<-20,"both crouched knees bend toward actual anatomical model minus-Z");
    Vec3 eye;Check(Eye(bind.data(),Count,3,4,sample,100,eye),"same cargo eye helper accepts exact bind head/marker");
    Vec3 actual;Check(NativeSkinMath::AnatomicalEye(bind[3],bind[4],posed[3],100,actual)&&Near(eye,actual),"camera anatomical eye exactly matches rendered crouched body head, without camera-only movement");
    Check(!std::memcmp(source.data(),original.data(),sizeof(source)),"pose operation leaves original native queued source bit-exact");
    const auto thighLength=NativeArmMath::Length(Subtract(bind[6].position,bind[5].position));
    const auto calfLength=NativeArmMath::Length(Subtract(bind[7].position,bind[6].position));
    sample.speed=.55f;sample.velocityMetres={0,0,-.55f};
    for(unsigned step=0;step<=64;++step){sample.phase=step*6.283185307179586f/64;changed={};
        Check(Apply(bind.data(),source.data(),posed.data(),parents,Count,rig,sample,100,changed.data()),"complete cargo walk phase produces finite owned crouched avatar");
        for(const auto& leg:rig.legs){
            Check(std::fabs(NativeArmMath::Length(Subtract(posed[leg.calf].position,posed[leg.thigh].position))-thighLength)<.01f&&
                  std::fabs(NativeArmMath::Length(Subtract(posed[leg.foot].position,posed[leg.calf].position))-calfLength)<.01f,
                  "crouched locomotion preserves real native thigh/calf lengths");
            Check(posed[leg.foot].position.y>=bind[leg.foot].position.y-.01f&&posed[leg.foot].position.y<=bind[leg.foot].position.y+7.01f,
                  "swing foot has bounded floor clearance and never sinks through van floor");
        }
        Check(Near(posed[3].position,{0,115,0})&&Eye(bind.data(),Count,3,4,sample,100,eye)&&
            NativeSkinMath::AnatomicalEye(bind[3],bind[4],posed[3],100,actual)&&Near(eye,actual),"walking feet never bob or desynchronize the VR eye");
    }
    const auto untouched=posed;auto badParents=std::array<std::uint32_t,Count>{};std::memcpy(badParents.data(),parents,sizeof(parents));badParents[6]=6;
    Check(!Apply(bind.data(),source.data(),posed.data(),badParents.data(),Count,rig,sample,100,changed.data())&&!std::memcmp(posed.data(),untouched.data(),sizeof(posed)),"cyclic or foreign leg topology fails before mutation");
    sample.crouchMetres=.9f;Check(!Apply(bind.data(),source.data(),posed.data(),parents,Count,rig,sample,100,changed.data()),"unbounded crouch cannot collapse or stretch the avatar");
    // Actual immutable BoneDefs resource path, with no guessed lookup IDs.
    using namespace NativeHudFixture;Memory memory;
    constexpr std::uint32_t Model=0x15000000,Geometry=0x16000000,Resource=0x17000000,Parts=0x18000000,Header=0x19000000,Defs=0x1A000000;
    memory.Set(Player+0x70,Model);memory.Set(Model+0x4A0,std::uint32_t(Count));memory.Set(Player+4,Geometry);memory.Set(Geometry+0x68,std::uint32_t(1));
    memory.Set(Base+0x71F308+4,Resource);memory.Set(Base+0x71F308+8,Parts);memory.Set(Base+0x71F308+12,Header);memory.Set(Base+0x71F308+16,Defs);
    memory.Set(Resource+2,std::uint16_t(7));memory.Set(Resource+0xC,std::uint8_t(1));memory.Set(Resource+0x10,std::uint32_t(2));memory.Set(Parts+4,std::uint32_t(3));
    memory.Set(Header,std::uint32_t(Count));memory.Set(Header+4,std::uint32_t(4));memory.Set(Defs,std::array<unsigned char,Count*64>{});
    const char* names[Count]={"ROOT","PELVIS","SPINE","HEAD","eye attacher","LEFT_THIGH","LEFT_CALF","LEFT_FOOT","LEFT_TOE","RIGHT_THIGH","RIGHT_CALF","RIGHT_FOOT","RIGHT_TOE","LEFT_ARM","RIGHT_ARM"};
    for(unsigned i=0;i<Count;++i)for(unsigned c=0;c<std::strlen(names[i])+1;++c)memory.Set(Defs+i*64+0x1C+c,names[i][c]);
    Rig captured;Check(CaptureRig(Base,Player,Model,Count,memory.Reader(),captured)&&captured.pelvis==1&&captured.head==3&&captured.legs[1].foot==11,
        "exact current model BoneDefs resolve pelvis/head/legs by resource names");
    memory.Set(Defs+7*64+0x1C,'X');Check(!CaptureRig(Base,Player,Model,Count,memory.Reader(),captured),"unknown costume foot cannot use an assumed standard numeric leg index");
    std::printf("Cargo crouch/walk pose and anatomical eye: %u checks PASS.\n",checks);
}
