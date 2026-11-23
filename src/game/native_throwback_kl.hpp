#pragma once
#include "native_grenade_kl_math.hpp"
#include "native_action_kl.hpp"
#include <cstring>

namespace NativeThrowbackKL {
using namespace NativeCameraMath;
constexpr std::uintptr_t Vtable=0x5668DC,Update=0x201DE0,Launch=0x151F70,LaunchReturn=0x2020C6;
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t rva,const auto& expected) {
        unsigned char actual[64]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    std::uint32_t method=0;
    const unsigned char update[]={0x81,0xEC,0x80,0,0,0,0x56,0x8B,0xF1,0x8B,0x4E,0x68,0xE8,0x5F,0x34,0xF2,0xFF};
    const unsigned char ctor[]={0x89,0x50,0x68,0x66,0x8B,0x54,0x24,0x10,0x89,0x48,0x6C};
    const unsigned char launch[]={0x8D,0x44,0x24,0x30,0x50,0x8D,0x4C,0x24,0x10,0x51,0x8B,0x4E,0x6C,0xE8,0xAA,0xFE,0xF4,0xFF};
    const unsigned char linear[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x28,0x56,0x57,0x8D,0x44,0x24,0x10,
        0x8B,0xF1,0x8B,0x4D,8,0x50,0x51,0xE8,0xA5,0x48,4,0};
    const unsigned char end[]={0x5F,0x5E,0x8B,0xE5,0x5D,0xC2,8,0};
    return image&&read(image+Vtable+0x10,&method,4)&&method==image+Update&&
        bytes(Update,update)&&bytes(0x1FE701,ctor)&&bytes(0x2020B4,launch)&&bytes(Launch,linear)&&bytes(0x15207D,end);
}
// Incoming grenades have no held-item inventory certificate. Their owner is
// instead the exact active native throw-back action, whose ctor stores the
// player at +68 and selected projectile at +6C (not the ordinary action +8).
struct Owner {std::uintptr_t action=0,player=0,projectile=0;};
template<class Reader> bool Capture(std::uintptr_t image,const NativeActionKL::State& current,
                                    std::uintptr_t action,Reader read,Owner& output) {
    if(!current.valid||current.action!=action||current.actionVtable!=image+Vtable||
       current.actionKind!=NativeActionKL::Action::Throw||!NativeActionKL::Address(action,0x8A))return false;
    std::uint32_t vtable=0,player=0,projectile=0,afterPlayer=0,afterProjectile=0;
    if(!read(action,&vtable,4)||vtable!=image+Vtable||!read(action+0x68,&player,4)||player!=current.player||
       !read(player,&vtable,4)||vtable!=image+0x56582C||!read(action+0x6C,&projectile,4)||
       !NativeActionKL::Address(projectile,0x134)||!read(action+0x68,&afterPlayer,4)||afterPlayer!=player||
       !read(action+0x6C,&afterProjectile,4)||afterProjectile!=projectile)return false;
    output={action,player,projectile};return true;
}
inline bool Same(const Owner& a,const Owner& b) {return a.action==b.action&&a.player==b.player&&a.projectile==b.projectile;}
// The native release supplies LINEAR and ANGULAR velocity to 551F70. Replace
// only linear heading/pitch, retaining authored speed and ballistic loft in
// the gravity-aligned native horizontal frame. Spin, fuse, origin and native
// animation/release time are untouched. Vertical gaze uses the last proved
// horizontal HMD heading; no body-heading or arbitrary right-vector guess.
inline bool Velocity(Vec3 native,const NativeWeaponKL::HeadGaze& gaze,Vec3 horizontal,Vec3& output) {
    if(!Finite(native)||!Finite(gaze.direction)||!std::isfinite(gaze.unitsPerMetre)||
       gaze.unitsPerMetre<1||gaze.unitsPerMetre>1000)return false;
    const float speed2=Dot(native,native),units2=gaze.unitsPerMetre*gaze.unitsPerMetre;
    Vec3 nativeHorizontal{native.x,0,native.z};
    if(!std::isfinite(speed2)||speed2<.01f*units2||speed2>10000.f*units2||
       Dot(nativeHorizontal,nativeHorizontal)<.0001f*units2)return false;
    Basis before,after;
    if(!NativeGrenadeKLMath::AimBasis(nativeHorizontal,nativeHorizontal,before)||
       !NativeGrenadeKLMath::AimBasis(gaze.direction,horizontal,after))return false;
    const Vec3 local{Dot(native,before.right),Dot(native,before.up),Dot(native,before.forward)};
    const Vec3 mapped=Add(Add(Scale(after.right,local.x),Scale(after.up,local.y)),Scale(after.forward,local.z));
    if(!Finite(mapped)||std::fabs(Dot(mapped,mapped)-speed2)>std::max(.001f,speed2*.0001f))return false;
    output=mapped;return true;
}
}
