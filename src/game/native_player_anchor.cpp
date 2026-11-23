#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_player_anchor.hpp"
#include "native_player_pose.hpp"
#include <cstring>

namespace NativePlayerAnchor {
namespace {
std::uintptr_t image=0;
bool Read(std::uintptr_t address,void* destination,std::size_t size) {
    if(address<0x10000||address>0xffffffffu-size)return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),destination,size,&copied)&&copied==size;
}
template<class T> bool At(std::uintptr_t address,T& value){return Read(address,&value,sizeof(value));}
}
bool InitializeVerifiedImage(std::uintptr_t verifiedImage) {
    image=0;
    const unsigned char playerGetter[]={0x8b,0x44,0x24,0x04,0x85,0xc0,0x7c,0x05,0x83,0xf8,0x10,0x7c,0x01,0xcc,0x8b,0x84,0x81,0x2c,0x0a,0,0,0xc2,4,0};
    unsigned char actual[sizeof(playerGetter)]{};
    if(!Read(verifiedImage+0x26fbd0,actual,sizeof(actual))||std::memcmp(actual,playerGetter,sizeof(actual)))return false;
    image=verifiedImage;return true;
}
Anchor Current(float units,unsigned heightMm,bool animatedHead) {
    using namespace NativeCameraMath;
    Anchor anchor{};
    if(!image||!std::isfinite(units)||units<1||units>1000||heightMm<500||heightMm>2500)return anchor;
    std::uint32_t data=0,player=0,vtable=0,geometry=0,camera=0;
    if(!At(image+0x68760c,data)||!At(data+0xa2c,player)||!At(player,vtable)||
       vtable!=image+0x56582c||!At(player+4,geometry)||!At(player+0x9a0,camera))return anchor;
    std::uint32_t cameraVtable=0;
    if(!At(camera,cameraVtable)||cameraVtable!=image+0x545a3c)return anchor;
    CameraPose world;
    if(!NativePlayerPose::World(geometry,Read,world))return anchor;
    Vec3 local{0,heightMm/1000.f*units,0};
    // The model's standard head attachment (64) is mapped to the current
    // costume's pose index by its own lookup; no runtime actor addresses or
    // raw bone indices are retained between calls.
    std::uint32_t model=0,lookup=0,poses=0,count=0;
    std::uint8_t index=255;
    if(At(player+0x70,model)&&At(model+0xc,lookup)&&At(model+0x4a0,count)&&
       count>0&&count<=255&&At(lookup+64,index)&&index<count&&At(model+0x4d0,poses)) {
        CameraPose head{};
        Vec3 selected;
        if(At(poses+std::uintptr_t(index)*48,head)&&NativePlayerPose::EyeLocal(head.position,units,animatedHead,selected)) {
            // Ordinary movement filters gait X/Z; a checked body action
            // instead follows the entire native attachment so the actor is
            // never translated away from its rope or interaction equipment.
            local=selected;
            anchor.headBone=true;anchor.animatedHead=animatedHead;
        }
    }
    anchor.position=Add(world.position,NativePlayerPose::Transform(world.basis,local));
    anchor.root=world;
    anchor.player=player;anchor.sourceCamera=camera;anchor.valid=Finite(anchor.position);
    return anchor;
}
}
