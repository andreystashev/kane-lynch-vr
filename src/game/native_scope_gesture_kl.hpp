#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <cstdint>

namespace NativeScopeGestureKL {
using namespace NativeCameraMath;
struct Proximity {bool valid=false,enter=false,stay=false;float distance=0,angle=0;};
inline Proximity Evaluate(Vec3 headPosition,Quat head,Vec3 rightPosition,Quat right) {
    Proximity out;Quat h,r;
    if(!Finite(headPosition)||!Finite(rightPosition)||!NormalizeQuaternion(head,h)||!NormalizeQuaternion(right,r))return out;
    const auto delta=Subtract(rightPosition,headPosition),forward=Rotate(h,{0,0,-1}),up=Rotate(h,{0,1,0});
    const auto gun=Rotate(r,{0,0,-1});
    out.distance=std::sqrt(Dot(delta,delta));
    out.angle=std::acos(std::clamp(Dot(forward,gun),-1.f,1.f))*57.2957795f;
    const float vertical=std::fabs(Dot(delta,up)),front=Dot(delta,forward);
    out.valid=true;
    // Match the established Contracts / Silent Assassin physical-controller
    // gesture. A held left foregrip is independent of this right-hand input.
    out.enter=out.distance<=.32f&&vertical<=.18f&&front>=-.08f&&out.angle<=25;
    out.stay=out.distance<=.45f&&vertical<=.25f&&front>=-.10f&&out.angle<=40;
    return out;
}
enum class Command {None,Enter,Exit};
struct Gesture {
    bool pending=false,entering=false,requested=false;
    std::uint32_t since=0;
    void Reset(){*this={};}
    Command Tick(bool allowed,bool fresh,const Proximity& proximity,std::uint32_t now) {
        if(!allowed||!fresh||!proximity.valid) {
            pending=false;
            if(requested){requested=false;return Command::Exit;}
            return Command::None;
        }
        const bool want=requested?!proximity.stay:proximity.enter;
        if(!want){pending=false;return Command::None;}
        if(!pending||entering==requested){pending=true;entering=!requested;since=now;return Command::None;}
        if(now-since<(requested?180u:120u))return Command::None;
        pending=false;requested=!requested;
        return requested?Command::Enter:Command::Exit;
    }
};
}
