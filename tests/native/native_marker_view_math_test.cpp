#include "native_marker_view_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
int main(){using namespace NativeMarkerViewMath;unsigned checks=0;
 const auto check=[&](bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}};
 const auto near=[](float a,float b){return std::fabs(a-b)<1e-5f;};
 float reference[16]{};reference[0]=.93f;reference[5]=1.12f;
 NativeWorldMarkerMath::Layout native{true,.018f,.032f,0,0,100};
 check(std::fabs((native.width/reference[0])/(native.height/reference[5])-1)>.2f,"old monitor NDC produces an oval in the SDK eye");
 for(auto raster:std::array<std::array<unsigned,2>,3>{{{3840,2160},{1920,1080},{1280,720}}}){
  const auto round=InEyeAspect(native,raster[0],raster[1],reference);
  check(round.valid&&near(round.width/reference[0],round.height/reference[5]),"native square pixel icon becomes a round angular billboard at every game resolution");
  auto rectangle=native;rectangle.width*=2;auto authored=InEyeAspect(rectangle,raster[0],raster[1],reference);
  check(authored.valid&&near((authored.width/reference[0])/(authored.height/reference[5]),2),"native rectangle/shield atlas aspect is preserved instead of forcing every resource square");
  auto crop=native;crop.height*=.5f;crop.offsetY=native.height*.25f;auto clipped=InEyeAspect(crop,raster[0],raster[1],reference);
  check(clipped.valid&&near(clipped.offsetY/clipped.height,.5f),"objective fill crop and vertical anchor remain aligned");
 }
 check(!InEyeAspect(native,0,2160,reference).valid&&!InEyeAspect(native,3840,0,reference).valid,"invalid raster dimensions reject aspect interpretation");
 PairCache cache;Key key{1,2,26,0x27,0x253EBB};Vec3 point{10,20,30};auto layout=native;bool reused=false;
 check(cache.Select(key,7,1000,point,layout,reused)&&!reused,"left eye records authoritative objective position/layout");
 point.y+=5;layout.offsetY+=.01f;check(cache.Select(key,7,1010,point,layout,reused)&&reused&&point.y==20&&layout.offsetY==0,"right eye uses the same point and crop rather than alternating vertical states");
 point.y=25;check(cache.Select(key,8,1020,point,layout,reused)&&!reused&&point.y==25,"a new pair follows actual target movement");
 auto replaced=key;replaced.entity=3;replaced.geometry=4;point.y=27;
 check(cache.Select(replaced,8,1021,point,layout,reused)&&!reused&&point.y==27,"same record slot with a new goal entity cannot reuse the old target");
 auto regenerated=replaced;regenerated.geometry=5;point.y=28;
 check(cache.Select(regenerated,8,1022,point,layout,reused)&&!reused&&point.y==28,"replacement geometry at the same goal entity cannot reuse the old target");
 auto foreground=key;foreground.caller=0x253F9A;point.y=30;check(cache.Select(foreground,8,1021,point,layout,reused)&&!reused,"foreground and background native producers retain separate crop identity");
 point.y=99;check(cache.Select(key,8,1171,point,layout,reused)&&!reused&&point.y==99,"expired pair cannot retain an old point");
 auto other=key;other.player=3;check(cache.Select(other,8,1172,point,layout,reused)&&!reused,"foreign player cannot inherit pair state");
 point.x=std::numeric_limits<float>::quiet_NaN();check(!cache.Select(key,8,1173,point,layout,reused),"nonfinite world point rejected");
 check(Objective(26)&&!Objective(18)&&!Objective(19)&&!Objective(7)&&!Objective(21),"objective stabilization never changes native recipient or Defend identities");
 WorldScaleCache worldScale;auto physical=native;physical.sourceExtentValid=true;
 physical.sourceWidth=100;physical.sourceHeight=100;physical.sourceOffsetX=20;physical.sourceOffsetY=30;
 const auto select=[&](Key identity,unsigned tick,float distance){auto value=physical;check(worldScale.Select(identity,tick,distance,value,reused),"owned source selects bounded physical presentation");return value;};
 auto first=select(key,1000,100);check(!reused&&near(first.sourceWidth,6)&&near(first.sourceOffsetX,1.2f),"initial near source chooses its safety-capped physical extent and pivot");
 for(float distance:{1000.f,5000.f}){auto retreat=select(key,1010, distance);check(reused&&near(retreat.sourceWidth,first.sourceWidth)&&near(retreat.sourceOffsetY,first.sourceOffsetY),"retreat through the old cap range cannot expand WORLD corners or crop pivot");}
 auto approach=select(key,1020,50);check(reused&&near(approach.sourceWidth,3),"approach can shrink the presentation to retain close comfort cap");
 auto retreat=select(key,1030,5000);check(reused&&near(retreat.sourceWidth,3),"retreat cannot undo the safe smaller WORLD extent");
 auto replacedEntity=key;replacedEntity.entity=3;auto replacement=select(replacedEntity,1040,1000);check(!reused&&near(replacement.sourceWidth,60),"same record address with different entity cannot inherit prior physical scale");
 auto replacedGeometry=replacedEntity;replacedGeometry.geometry=4;replacement=select(replacedGeometry,1050,2000);check(!reused&&near(replacement.sourceWidth,100),"new geometry lifetime at same entity resets physical scale");
 auto fixedKey=key;fixedKey.fixed=true;fixedKey.fixedPoint={1,2,3};auto fixedScale=select(fixedKey,1051,100);
 fixedKey.fixedPoint={4,5,6};auto fixedReplacement=select(fixedKey,1052,1000);
 check(!reused&&near(fixedScale.sourceWidth,6)&&near(fixedReplacement.sourceWidth,60),"same fixed marker record slot at a different WORLD target cannot inherit old physical scale");
 auto changed=physical;changed.sourceWidth=200;check(worldScale.Select(key,1060,1000,changed,reused)&&!reused&&near(changed.sourceWidth,60),"changed native style resets physical scale without changing its current source semantics");
 auto stale=select(key,1061+WorldScaleCache::MaximumAge,5000);check(!reused&&near(stale.sourceWidth,100),"expired source cannot retain an obsolete presentation lifetime");
 auto backwards=physical;check(!worldScale.Select(key,1060,5000,backwards,reused)&&near(backwards.sourceWidth,100),"backwards timestamp cannot mutate or reuse current source scale");
 worldScale.Reset();auto reset=select(key,40000,1000);check(!reused&&near(reset.sourceWidth,60),"device or scene invalidation discards physical source presentation authority");
 std::printf("Native marker aspect/pair: %u checks PASS.\n",checks);
}
