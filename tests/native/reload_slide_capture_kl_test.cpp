// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#include "reload_slide_capture_kl.hpp"
#include <cstdio>
#include <cstdlib>
using namespace NativeCameraMath;
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    for(float units:{10.f,100.f,250.f})for(float yaw:{0.f,1.2f,3.1f})for(unsigned category:{2u,3u,4u,6u,15u,16u,17u}){
        CameraPose box;const Quat q{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
        box.basis={Rotate(q,{0,0,1}),Rotate(q,{0,1,0}),Rotate(q,{1,0,0})};box.position={3*units,units,-2*units};
        const Vec3 half=Scale(Vec3{.012599f,.0092226f,.0075661f},units);
        const auto test=[&](Vec3 local,bool needs){return ReloadSlideCaptureKL::Contains(Add(box.position,ReloadMathKL::Transform(box.basis,Scale(local,units))),units,category,needs,box.position,box.position,.04f,box,half,true);};
        for(unsigned axis=0;axis<3;++axis)for(float side:{-1.f,1.f}){
            Vec3 p{};(&p.x)[axis]=side*(((&half.x)[axis]/units)+.04f);
            Check(test(p,false),"all faces of a small native handle admit a four-centimetre hand approach");
            (&p.x)[axis]=side*(((&half.x)[axis]/units)+.08f);
            Check(!test(p,false)&&test(p,true),"empty-chamber mechanism has additional acquisition priority clearance");
            (&p.x)[axis]=side*(((&half.x)[axis]/units)+.11f);
            Check(!test(p,true),"distant foregrip remains outside mechanism volume");
        }
        Check(!test({.07f,.07f,.07f},false),"rounded corner avoids expanding a large cube into foregrip");
        Check(!ReloadSlideCaptureKL::Contains(box.position,units,category,false,box.position,box.position,.04f,box,{},true),"invalid native bounds cannot claim a support gesture");
    }
    CameraPose box;const Vec3 a{0,0,0},b{0,0,10};
    for(float x:{0.f,1.f,3.99f,4.f,4.01f,9.f}){
        const bool old=x*x/(100.f*100.f)<.04f*.04f;
        Check(ReloadSlideCaptureKL::Contains({x,0,5},100,0,false,a,b,.04f,box,{1,1,1},true)==old,"ready pistol keeps its narrow intentional slide capsule");
    }
    Check(ReloadSlideCaptureKL::Contains({4.01f,0,5},100,0,true,a,b,.04f,box,{1,1,1},true),"empty pistol admits a whole-hand approach around the actual slide box");
    Check(!ReloadSlideCaptureKL::Contains({15,0,5},100,0,true,a,b,.04f,box,{1,1,1},true),"comfortable empty-pistol grasp does not claim a distant hand");
    Check(!ReloadSlideCaptureKL::Contains({},0,6,false,a,b,.04f,box,{1,1,1},true),"invalid scale rejects capture");
    std::printf("slide capture: %u checks PASS\n",checks);
}
