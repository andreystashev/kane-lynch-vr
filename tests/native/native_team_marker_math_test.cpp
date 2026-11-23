#include "native_team_marker_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
float MeanX(const NativeTeamMarkerMath::Geometry& g){float sum=0;for(unsigned i=0;i<g.count;++i)sum+=g.vertices[i].x;return sum/float(g.count);}
}
int main(){
    using namespace NativeTeamMarkerMath;using namespace NativeCameraMath;
    NativeHandHudKL::Frame frame;frame.player=1;frame.unitsPerMetre=100;
    Geometry native;Check(Build(frame,3840,2160,1000,native)&&!native.count,"retained native world markers do not acquire duplicate vector glyphs");
    frame.nativeCommandMarkers=false;
    frame.actualEye.basis={{0,0,1},{0,1,0},{1,0,0}};
    const float fov[]={1,1,1,1};Check(ApplyEyeProjection(frame.projection,fov,1,100000,false),"native eye projection fixture valid");
    frame.commandMarkers.valid=true;frame.commandMarkers.player=1;frame.commandMarkers.tick=1000;frame.commandMarkers.count=3;
    frame.commandMarkers.markers[0]={Icon::Circle,{-100,50,1000}};
    frame.commandMarkers.markers[1]={Icon::Shield,{0,0,1000}};
    frame.commandMarkers.markers[2]={Icon::Lightning,{100,50,1000}};
    Geometry g;Check(Build(frame,3840,2160,1000,g)&&g.markers==3&&g.count==324,"circle shield and lightning have distinct bounded vector outlines");
    for(unsigned i=0;i<g.count;++i)Check(std::isfinite(g.vertices[i].x)&&std::isfinite(g.vertices[i].y)&&g.vertices[i].rhw>0,"world icon vertices use finite actual-eye projection");
    Check(!frame.teamHeld&&!frame.held,"world marker fixture does not require the wrist menu to remain visible");
    frame.commandMarkers.count=1;frame.commandMarkers.markers[0]={Icon::Circle,{0,0,1000}};
    frame.actualEye.position.x=-3.2f;Check(Build(frame,3840,2160,1000,g)&&g.count,"left eye world marker projects");const float left=MeanX(g);
    frame.actualEye.position.x=3.2f;Check(Build(frame,3840,2160,1000,g)&&g.count,"right eye world marker projects");const float right=MeanX(g);
    Check(left>right&&left-right>10,"actual eye positions create correct stereo disparity instead of a duplicate screen HUD");
    frame.actualEye.position={};frame.actualEye.basis={{1,0,0},{0,1,0},{0,0,-1}};
    Check(Build(frame,3840,2160,1000,g)&&!g.count,"world marker does not turn with the headset when the target leaves view");
    frame.actualEye.basis={{0,0,1},{0,1,0},{1,0,0}};frame.commandMarkers.markers[0].position.z=-100;
    Check(Build(frame,3840,2160,1000,g)&&!g.count,"behind-eye marker cannot reflect into the image");
    frame.commandMarkers.markers[0].position={0,0,1000};Check(!Build(frame,3840,2160,1151,g),"old native marker snapshot is not retained");
    frame.commandMarkers.markers[0].position.x=std::numeric_limits<float>::quiet_NaN();Check(!Build(frame,3840,2160,1000,g),"nonfinite target cannot produce geometry");
    frame.commandMarkers.markers[0].position={0,0,1000};frame.commandMarkers.markers[0].icon=static_cast<Icon>(99);
    Check(!Build(frame,3840,2160,1000,g),"unknown icon state cannot fabricate an order marker");
    for(auto icon:{Icon::Circle,Icon::Shield,Icon::Lightning}){const auto s=Shape(icon);Check(s.count>=6&&s.count<=17&&s.points[0].x==s.points[s.count-1].x&&std::fabs(s.points[0].y-s.points[s.count-1].y)<.00001f,"all shapes close consistently for card and world rendering");}
    std::printf("Native command marker projection: %u checks PASS.\n",checks);return 0;
}
