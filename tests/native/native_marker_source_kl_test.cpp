#include "native_marker_source_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {unsigned checks=0;void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<1e-6f;}}
int main(){using namespace NativeHudFixture;Memory memory;constexpr std::uint32_t camera=0x15000000,ui=0x16000000,geometry=0x17000000,payload=0x18000000,position=0x19000000;
    memory.Set(camera+0x14C,ui);memory.Set(ui,std::uint32_t(Base+0x545A3C));memory.Set(Base+0x545A3C+0x244,std::uint32_t(Base+0xF5BD0));
    memory.Set(ui+0x38,1.f);memory.Set(ui+0x3C,16.f/9.f);memory.Set(ui+0x44,std::uint32_t(0));memory.Set(ui+0x54,.5f);
    memory.Set(geometry,std::array<std::uint32_t,3>{1,0,payload});std::array<float,29> authored{};authored[7]=32;authored[8]=16;
    memory.Set(payload,authored);NativeCameraMath::Vec3 centre{50,-20,1500};memory.Set(position,centre);
    const auto capture=[&](unsigned kind){return NativeMarkerSourceKL::Capture(Base,camera,geometry,position,kind,memory.Reader());};
    auto layout=capture(5);Check(layout.valid&&Near(layout.width,32.f/1500)&&Near(layout.height,16.f/1500*16/9),"normal native image uses original UI focal aspect and authored extent");
    NativeMarkerSourceKL::CanonicalQuad canonical;
    Check(NativeMarkerSourceKL::Capture(Base,camera,geometry,position,5,memory.Reader(),&canonical).valid&&canonical.valid&&canonical.kind==5&&
        canonical.corners[0].x==-1&&canonical.corners[0].y==-1&&canonical.corners[2].x==1&&canonical.corners[2].y==1,"type5 canonical source corners use exact verified native order");
    Check(layout.sourceExtentValid&&Near(layout.sourceWidth,32)&&Near(layout.sourceHeight,16.f*16/9),"perspective source retains authored extent before depth projection");
    for(float depth:{25.f,8000.f,-25.f,-8000.f}){
        centre.z=depth;memory.Set(position,centre);const auto fixed=capture(5);
        Check(fixed.valid&&fixed.sourceExtentValid&&Near(fixed.sourceWidth,layout.sourceWidth)&&Near(fixed.sourceHeight,layout.sourceHeight),
              "near cap far depth and negative native camera depth cannot change the physical source extent");
    }
    centre.z=1500;memory.Set(position,centre);
    centre.z=-1500;memory.Set(position,centre);auto negative=capture(5);Check(negative.valid&&negative.width==layout.width,"negative source depth cannot inflate or invert original native size");
    centre.z=1500;memory.Set(position,centre);memory.Set(ui+0x44,std::uint32_t(1));memory.Set(ui+0x38,.01f);memory.Set(ui+0x3C,.02f);layout=capture(5);
    Check(layout.valid&&Near(layout.width,.06f)&&Near(layout.height,.06f),"near/orthographic native marker is capped while preserving square authored aspect");
    Check(!layout.sourceExtentValid,"orthographic screen source cannot invent a WORLD extent");
    memory.Set(ui+0x44,std::uint32_t(0));memory.Set(ui+0x38,1.f);memory.Set(ui+0x3C,1.f);
    authored={};authored[23]=32;authored[24]=16;const float corners[4][2]={{-.5f,0},{.5f,0},{.5f,.5f},{-.5f,.5f}};
    for(unsigned i=0;i<4;++i){authored[11+3*i]=corners[i][0];authored[12+3*i]=corners[i][1];}memory.Set(payload,authored);layout=capture(6);
    Check(layout.valid&&Near(layout.width,32.f/1500)&&Near(layout.height,8.f/1500)&&Near(layout.offsetY,4.f/1500),"cropped objective type6 retains its original half-height and centre offset");
    Check(layout.sourceExtentValid&&Near(layout.sourceWidth,32)&&Near(layout.sourceHeight,8)&&Near(layout.sourceOffsetY,4),"cropped source pivot is retained in physical coordinates before projection");
    centre.x=1e12f;centre.y=-1e12f;memory.Set(position,centre);
    Check(NativeMarkerSourceKL::Capture(Base,camera,geometry,position,6,memory.Reader(),&canonical).valid&&canonical.valid&&canonical.kind==6&&
        canonical.corners[0].x==-1&&canonical.corners[0].y==-1&&canonical.corners[2].x==1&&canonical.corners[2].y==1,
        "canonical cropped source corners remain modest when native UI translation would erase deferred float corner extents");
    authored[12]=std::numeric_limits<float>::quiet_NaN();memory.Set(payload,authored);Check(!capture(6).valid,"corrupt native authored corner rejected");authored[12]=0;memory.Set(payload,authored);
    memory.Set(geometry,std::array<std::uint32_t,3>{2,0,payload});Check(!capture(6).valid,"unknown native batched objective cannot be treated as single target");memory.Set(geometry,std::array<std::uint32_t,3>{1,0,payload});
    memory.Set(Base+0x545A3C+0x244,std::uint32_t(0));Check(!capture(6).valid,"unproved native UI unproject method rejects calibration");memory.Set(Base+0x545A3C+0x244,std::uint32_t(Base+0xF5BD0));
    Check(!capture(7).valid,"unrelated particle layout cannot become objective geometry");
    centre.z=0;memory.Set(position,centre);Check(!capture(6).valid,"zero producer depth does not invent native size");
    std::printf("Native marker source calibration: %u checks PASS.\n",checks);return 0;
}
