#include "native_player_pose.hpp"
#include "native_action_kl.hpp"
#include "native_camera_kl_math.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
using namespace NativeCameraMath;
int main() {
    using NativePlayerPose::Geometry;
    const Basis identity{{0,0,1},{0,1,0},{1,0,0}};
    std::map<std::uintptr_t,Geometry> geometry;
    geometry[0x10000].pose={identity,{0,0,0}};
    geometry[0x10000].parent=0x20000;
    geometry[0x20000].pose={{{1,0,0},{0,1,0},{0,0,-1}},{100,2,300}};
    const auto read=[&](std::uintptr_t address,void* output,std::size_t size){
        const auto found=geometry.find(address);if(found==geometry.end()||size!=sizeof(Geometry))return false;
        std::memcpy(output,&found->second,size);return true;
    };
    unsigned failures=0,checks=0;
    const auto check=[&](bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}};
    CameraPose world;
    check(NativePlayerPose::World(0x10000,read,world),"nested native geometry accepted");
    check(world.position.x==100&&world.position.y==2&&world.position.z==300,"root follows translated parent");
    const auto eye=Add(world.position,NativePlayerPose::Transform(world.basis,{0,170,6}));
    check(eye.x==106&&eye.y==172&&eye.z==300,"head attachment follows rotated root");
    geometry[0x20000].parent=0x10000;
    check(!NativePlayerPose::World(0x10000,read,world),"parent cycle rejected");
    geometry[0x20000].parent=0x30000;
    check(!NativePlayerPose::World(0x10000,read,world),"released parent rejected");
    geometry[0x20000].parent=0;geometry[0x10000].pose.basis.up={0,0,0};
    check(!NativePlayerPose::World(0x10000,read,world),"invalid streamed pose rejected");
    Vec3 local{99,98,97};
    check(NativePlayerPose::EyeLocal({17,170,-21},100,false,local)&&local.x==0&&local.y==170&&local.z==0,
          "ordinary locomotion removes native head gait sway while retaining stance height");
    check(NativePlayerPose::EyeLocal({64,72,-37},100,true,local)&&local.x==64&&local.y==72&&local.z==-37,
          "pickup follows the actual bent head instead of hovering upright over the actor root");
    check(NativePlayerPose::EyeLocal({0,8,165},100,true,local)&&local.y==8&&local.z==165,
          "horizontal rappel head is accepted without a standing height requirement");
    const Vec3 previous=local;
    check(!NativePlayerPose::EyeLocal({0,8,165},100,false,local)&&local.x==previous.x&&local.y==previous.y&&local.z==previous.z,
          "horizontal action data does not weaken ordinary stance validation or mutate rejected output");
    check(!NativePlayerPose::EyeLocal({0,0,0},100,true,local)&&!NativePlayerPose::EyeLocal({0,400,0},100,true,local),
          "missing or implausibly distant action eye attachment is rejected");
    check(!NativePlayerPose::EyeLocal({0,std::numeric_limits<float>::quiet_NaN(),0},100,true,local)&&
          !NativePlayerPose::EyeLocal({0,170,0},0,true,local),"nonfinite action eye or invalid world scale is rejected");
    // Reproduce the observed .30 failure: a low/offset native head must not
    // become a standing camera above the entity root during body actions.
    struct ActionEye {NativeActionKL::Movement movement;Vec3 local,world;};
    const ActionEye actions[]={
        {NativeActionKL::Movement::Climb,{25,100,-12},{125,120,288}},
        {NativeActionKL::Movement::Fall,{45,15,-80},{145,35,220}},
        {NativeActionKL::Movement::Die,{-30,20,60},{70,40,360}}};
    CameraPose actor{identity,{100,20,300}};
    const CameraPose orbit{identity,{100,190,470}};
    const auto near=[](Vec3 a,Vec3 b){const auto delta=Subtract(a,b);return Dot(delta,delta)<1e-7f;};
    for(const auto& example:actions) {
        NativeActionKL::State action;action.valid=true;action.player=0x11000000;
        action.movement=0x12000000;action.movementKind=example.movement;
        Vec3 attachment{};
        check(NativePlayerPose::EyeLocal(example.local,100,action.FollowAnimatedHead(),attachment),
              "checked climb/fall/wounded low head is accepted as a full anatomical attachment");
        const auto position=Add(actor.position,NativePlayerPose::Transform(actor.basis,attachment));
        check(near(position,example.world),"body action eyes retain their explicit horizontal and vertical model displacement");
        NativeCameraKLMath::FirstPersonHeading heading;CameraPose camera;
        check(NativeCameraKLMath::FirstPersonBase(heading,action.player,0x22000000,true,orbit,position,camera)&&
              near(camera.position,example.world)&&near(camera.basis.forward,identity.forward),
              "a remote native action camera cannot move the VR eye off the anatomical attachment or add an animation turn");
    }
    actor.basis={{1,0,0},{0,1,0},{0,0,-1}};
    const auto rotatedEye=Add(actor.position,NativePlayerPose::Transform(actor.basis,actions[0].local));
    check(near(rotatedEye,{88,120,275}),"climb anatomical eyes follow real actor yaw rather than a world-space offset");
    std::printf("Native player pose: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
