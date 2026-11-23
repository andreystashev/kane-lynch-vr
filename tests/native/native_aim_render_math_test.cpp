#include "native_aim_render_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace M=NativeAimRenderKL;
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
M::Frame Frame(float units){M::Frame f;f.eye.basis={{0,0,1},{0,1,0},{1,0,0}};f.units=units;
    f.projection[0]=f.projection[5]=f.projection[10]=f.projection[11]=1;
    f.origin={.2f*units,-.2f*units,.5f*units};f.contact={0,0,10*units};return f;}
int main(){
    for(float units:{1.f,100.f,1000.f}){
        auto f=Frame(units);M::Geometry g;
        Check(M::Build(f,1920,1080,g)&&g.count==54,"transparent beam and collision dot generated");
        Check((g.vertices[0].color>>24)<0x70,"beam remains subtle and translucent");
        Check(std::fabs(g.vertices[6].x-959.5f)<.01f&&std::fabs(g.vertices[6].y-539.5f)<.01f,"dot projects actual contact");
        const auto centre=g.vertices[6];f.eye.position.x=-.032f*units;M::Geometry left,right;
        Check(M::Build(f,1920,1080,left),"left eye projects contact");f.eye.position.x=.032f*units;
        Check(M::Build(f,1920,1080,right)&&left.vertices[6].x>centre.x&&right.vertices[6].x<centre.x,"stereo target uses actual eye disparity");
        f.contactValid=false;f.eye.position.x=-.032f*units;
        Check(M::Build(f,1920,1080,left)&&left.count==6,"left eye draws the current beam without a collision certificate or invented dot");
        f.eye.position.x=.032f*units;
        Check(M::Build(f,1920,1080,right)&&right.count==6,"right eye draws the same current beam while collision data is pending");
        f=Frame(units);f.origin.z=-units;Check(M::Build(f,1920,1080,g),"beam clips when muzzle behind eye plane");
        f.contact.z=-units;Check(!M::Build(f,1920,1080,g),"behind-eye contact rejected");
        f=Frame(units);f.contact=f.origin;Check(!M::Build(f,1920,1080,g),"degenerate contact rejected");
        f=Frame(units);f.projection[0]=std::numeric_limits<float>::quiet_NaN();Check(!M::Build(f,1920,1080,g),"invalid projection rejected");
    }
    auto f=Frame(100);M::Geometry g;Check(!M::Build(f,0,1080,g),"invalid dimensions rejected");
    Check(!M::Build(f,20000,1080,g),"oversized dimensions rejected");
    std::printf("KL1 laser geometry: %u checks PASS (%u-bit)\n",checks,unsigned(sizeof(void*)*8));
}
