#include "native_camera_math.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace NativeCameraMath;

namespace {
int failures=0;
constexpr float pi=3.14159265358979323846f;
void Check(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}
}
bool Near(float a,float b,float tolerance=0.0002f) { return std::fabs(a-b)<=tolerance; }
bool Near(Vec3 a,Vec3 b,float tolerance=0.0002f) {
    return Near(a.x,b.x,tolerance)&&Near(a.y,b.y,tolerance)&&Near(a.z,b.z,tolerance);
}
bool Near(const Basis& a,const Basis& b,float tolerance=0.0002f) {
    return Near(a.forward,b.forward,tolerance)&&Near(a.up,b.up,tolerance)&&Near(a.right,b.right,tolerance);
}
Quat AxisAngle(Vec3 axis,float degrees) {
    const float half=degrees*pi/360.f;
    return {axis.x*std::sin(half),axis.y*std::sin(half),axis.z*std::sin(half),std::cos(half)};
}
void CheckRotation(const CameraPose& base,float degrees) {
    const float sine=std::sin(degrees*pi/180.f),cosine=std::cos(degrees*pi/180.f);
    CameraPose pose;
    Check(Compose(base,AxisAngle({0,1,0},degrees),{},{},100,pose),"yaw composes");
    Check(Near(pose.basis.forward,Subtract(Scale(base.basis.forward,cosine),Scale(base.basis.right,sine))),
          "positive RH yaw turns gaze toward tracking left (not inverse yaw)");
    Check(Near(pose.basis.right,Add(Scale(base.basis.right,cosine),Scale(base.basis.forward,sine))),"yaw right axis");
    Check(Near(pose.basis.up,base.basis.up),"yaw preserves up");
    Check(Compose(base,AxisAngle({1,0,0},degrees),{},{},100,pose),"pitch composes");
    Check(Near(pose.basis.forward,Add(Scale(base.basis.forward,cosine),Scale(base.basis.up,sine))),
          "positive RH pitch looks up (not inverse pitch)");
    Check(Near(pose.basis.up,Subtract(Scale(base.basis.up,cosine),Scale(base.basis.forward,sine))),"pitch up axis");
    Check(Near(pose.basis.right,base.basis.right),"pitch preserves right");
    Check(Compose(base,AxisAngle({0,0,1},degrees),{},{},100,pose),"roll composes");
    Check(Near(pose.basis.up,Subtract(Scale(base.basis.up,cosine),Scale(base.basis.right,sine))),
          "positive RH roll tilts up toward tracking left (not inverse roll)");
    Check(Near(pose.basis.right,Add(Scale(base.basis.right,cosine),Scale(base.basis.up,sine))),"roll right axis");
    Check(Near(pose.basis.forward,base.basis.forward),"roll preserves forward");
}
void TestKnownAngles() {
    const CameraPose bases[]={
        {},
        {{{0.6f,0.f,0.8f},{0.f,1.f,0.f},{0.8f,0.f,-0.6f}},{123.f,-10.f,400.f}},
        {{{0.f,0.f,-1.f},{0.f,1.f,0.f},{-1.f,0.f,0.f}},{-15.f,180.f,30.f}}
    };
    for(const auto& base:bases) {
        Check(ValidatePose(base),"native negative-determinant F/U/R basis is valid");
        CameraPose pose;
        Check(Compose(base,{},{},{},100,pose),"identity composes");
        Check(Near(pose.basis,base.basis)&&Near(pose.position,base.position),"identity preserves base");
        CheckRotation(base,30.f);
        CheckRotation(base,-30.f);
    }
}
void TestTranslationAndEyes() {
    CameraPose base{{{0.6f,0,0.8f},{0,1,0},{0.8f,0,-0.6f}},{10,20,30}},pose;
    // Expected native translations are written explicitly for this heading.
    Check(Compose(base,{}, {1,0,0},{},100,pose)&&Near(pose.position,{90,20,-30}),"tracking +X moves right");
    Check(Compose(base,{}, {0,1,0},{},100,pose)&&Near(pose.position,{10,120,30}),"tracking +Y moves up");
    Check(Compose(base,{}, {0,0,-1},{},100,pose)&&Near(pose.position,{70,20,110}),"tracking -Z moves forward");
    Check(Compose(base,AxisAngle({0,1,0},90), {1,0,0},{},100,pose)&&Near(pose.position,{90,20,-30}),
          "room-scale displacement does not rotate again with the current head");
    const Quat head=Multiply(AxisAngle({0,1,0},30),Multiply(AxisAngle({1,0,0},-20),AxisAngle({0,0,1},10)));
    CameraPose left,right,center;
    Check(Compose(base,head,{0.2f,0.1f,-0.3f},{-0.032f,0,0},100,left),"left eye composes");
    Check(Compose(base,head,{0.2f,0.1f,-0.3f},{0.032f,0,0},100,right),"right eye composes");
    Check(Compose(base,head,{0.2f,0.1f,-0.3f},{},100,center),"center eye composes");
    const Vec3 separation=Subtract(right.position,left.position);
    Check(Near(std::sqrt(Dot(separation,separation)),6.4f),"64 mm IPD becomes 6.4 game units");
    Check(Near(separation,Scale(center.basis.right,6.4f)),"eye separation follows full head rotation");
    Check(Near(Scale(Add(left.position,right.position),0.5f),center.position),"eye midpoint equals tracked head");
    Check(Near(left.basis,right.basis),"parallel eye orientation avoids toe-in");
    // Pitch 90 degrees rotates a head-local -Z eye depth upward.
    Check(Compose({},AxisAngle({1,0,0},90),{},{0,0,-0.1f},100,pose)&&Near(pose.position,{0,10,0}),
          "head-local eye depth rotates with pitch");
}
void TestRecenter() {
    const Quat yaw=AxisAngle({0,1,0},70);
    const Quat tilt=Multiply(AxisAngle({1,0,0},20),AxisAngle({0,0,1},-15));
    const Quat head=Multiply(yaw,tilt);
    Quat reference,relative;
    Vec3 displacement;
    Check(MakeYawReference(head,reference),"yaw recenter extracts physical heading");
    Check(RelativeTrackingPose(head,{2,1,3},reference,{2,1,3},relative,displacement),"yaw reference produces relative pose");
    CameraPose result,expected;
    Check(Compose({},relative,displacement,{},100,result),"recentered head composes");
    Check(Compose({},tilt,{},{},100,expected),"expected retained pitch/roll composes");
    Check(Near(result.basis,expected.basis),"yaw recenter retains pitch and roll");
    Check(Near(displacement,{}),"recenter zeros translation");

    // 90-degree reference yaw maps reference-forward into tracking -X.
    const Quat yaw90=AxisAngle({0,1,0},90);
    Check(RelativeTrackingPose(yaw90,{1,2,3},yaw90,{2,2,3},relative,displacement),"position uses reference rotation");
    Check(Near(displacement,{0,0,-1}),"reference-forward room-scale displacement keeps forward sign");
    Check(Compose({},relative,displacement,{},100,result)&&Near(result.position,{100,0,0}),"recentered step follows native base forward");

    Check(RelativeTrackingPose(head,{2,1,3},head,{2,1,3},relative,displacement),"full pose reference supported");
    Check(Compose({},relative,displacement,{},100,result)&&Near(result.basis,Basis{}),"full reference zeros all rotation");
    const Quat negativeHead{-head.x,-head.y,-head.z,-head.w};
    Check(RelativeTrackingPose(negativeHead,{2,1,3},head,{2,1,3},relative,displacement),"quaternion sign equivalence supported");
    Check(Compose({},relative,displacement,{},100,result)&&Near(result.basis,Basis{}),"q and -q have same pose");
    const Quat sentinel{0.1f,0.2f,0.3f,0.4f};
    reference=sentinel;
    Check(!MakeYawReference(AxisAngle({1,0,0},90),reference),"vertical gaze rejects unstable yaw reference");
    Check(reference.x==sentinel.x&&reference.w==sentinel.w,"failed yaw recenter retains prior reference");
}
void TestValidation() {
    const float nan=std::numeric_limits<float>::quiet_NaN(),infinity=std::numeric_limits<float>::infinity();
    CameraPose output;output.position={11,22,33};
    const CameraPose sentinel=output;
    const Quat badQuats[]={{0,0,0,0},{nan,0,0,1},{0,infinity,0,1},{100,0,0,1},{1e-20f,0,0,0}};
    for(const auto& q:badQuats) {
        Check(!Compose({},q,{},{},100,output),"corrupt quaternion rejected");
        Check(Near(output.position,sentinel.position)&&Near(output.basis,sentinel.basis),"failure leaves output unchanged");
    }
    const float badScales[]={0,-1,nan,infinity,100001};
    for(float s:badScales)Check(!Compose({},{},{},{},s,output),"invalid scale rejected");
    Check(!Compose({},{},{nan,0,0},{},100,output),"NaN displacement rejected");
    Check(!Compose({},{},{},{0,infinity,0},100,output),"infinite eye offset rejected");
    CameraPose bad;
    bad.basis.forward={2,0,0};
    Check(!Compose(bad,{},{},{},100,output),"scaled native basis rejected");
    bad={};bad.basis.up=bad.basis.forward;
    Check(!ValidatePose(bad),"nonorthogonal native basis rejected");
    bad={};bad.basis.right={0,0,1};
    Check(!ValidatePose(bad),"opposite native handedness rejected");
    bad={};bad.position.x=infinity;
    Check(!ValidatePose(bad),"nonfinite native position rejected");
    Check(!ValidateBasis({},nan),"nonfinite validation tolerance rejected");
    Check(!Compose({},{},{std::numeric_limits<float>::max(),0,0},{},100,output),"overflowed result rejected");
    Quat normalized;
    Check(NormalizeQuaternion({0,0,0,1.01f},normalized)&&Near(normalized.w,1),"minor quaternion norm drift normalized");
    Check(!RelativeTrackingPose({}, {},{0,0,0,0},{},normalized,output.position),"invalid recenter quaternion rejected");
}
void TestRepeatedRotations() {
    // Stress combined headings and rotations, including wraparound and 180 deg.
    // Each independent call must remain a rigid F/U/R frame and preserve IPD.
    for(int body=-180;body<=180;body+=30)for(int yaw=-180;yaw<=180;yaw+=30)
        for(int pitch=-60;pitch<=60;pitch+=30)for(int roll=-60;roll<=60;roll+=30) {
            const float angle=body*pi/180.f;
            CameraPose base{{{std::cos(angle),0,std::sin(angle)},{0,1,0},
                             {std::sin(angle),0,-std::cos(angle)}},{10,180,-30}};
            const Quat head=Multiply(AxisAngle({0,1,0},static_cast<float>(yaw)),
                                   Multiply(AxisAngle({1,0,0},static_cast<float>(pitch)),AxisAngle({0,0,1},static_cast<float>(roll))));
            CameraPose left,right;
            Check(Compose(base,head,{0.1f,0.2f,-0.3f},{-0.032f,0,0},100,left),"rotation sweep left valid");
            Check(Compose(base,head,{0.1f,0.2f,-0.3f},{0.032f,0,0},100,right),"rotation sweep right valid");
            Check(Near(Subtract(right.position,left.position),Scale(left.basis.right,6.4f),0.0003f),"rotation sweep IPD direction");
        }
}
Vec3 ProjectPoint(const float* projection,Vec3 point) {
    const float w=point.x*projection[3]+point.y*projection[7]+point.z*projection[11]+projection[15];
    return {(point.x*projection[0]+point.y*projection[4]+point.z*projection[8]+projection[12])/w,
            (point.x*projection[1]+point.y*projection[5]+point.z*projection[9]+projection[13])/w,
            (point.x*projection[2]+point.y*projection[6]+point.z*projection[10]+projection[14])/w};
}
float PlaneDistance(const float* plane,Vec3 point) {
    return plane[0]*point.x+plane[1]*point.y+plane[2]*point.z+plane[3];
}
void TestProjectionAndClip() {
    const float fov[4]={0.8f,1.3f,1.15f,0.65f}; // deliberately asymmetric
    constexpr float nearUnits=5.f,farUnits=30000.f,z=25.f;
    float projection[16];
    for(float& element:projection)element=12345.f;
    Check(ValidFov(fov),"asymmetric FOV is valid");
    Check(ApplyEyeProjection(projection,fov,nearUnits,farUnits,false),"finite projection builds");
    Check(Near(ProjectPoint(projection,{-fov[0]*z,0,z}).x,-1.f),"left edge maps to NDC -1");
    Check(Near(ProjectPoint(projection,{fov[1]*z,0,z}).x,1.f),"right edge maps to NDC +1");
    Check(Near(ProjectPoint(projection,{0,fov[2]*z,z}).y,1.f),"top edge maps to NDC +1");
    Check(Near(ProjectPoint(projection,{0,-fov[3]*z,z}).y,-1.f),"bottom edge maps to NDC -1");
    Check(ProjectPoint(projection,{-fov[0]*z-1.f,0,z}).x<-1.f,"beyond left projects outside");
    Check(ProjectPoint(projection,{fov[1]*z+1.f,0,z}).x>1.f,"beyond right projects outside");
    Check(ProjectPoint(projection,{0,fov[2]*z+1.f,z}).y>1.f,"beyond top projects outside");
    Check(ProjectPoint(projection,{0,-fov[3]*z-1.f,z}).y<-1.f,"beyond bottom projects outside");
    Check(Near(ProjectPoint(projection,{0,0,nearUnits}).z,0.f),"finite near maps to depth zero");
    Check(Near(ProjectPoint(projection,{0,0,farUnits}).z,1.f),"native finite far maps to depth one");
    Check(ProjectPoint(projection,{0,0,nearUnits/2.f}).z<0.f,"closer than near is clipped");
    Check(ProjectPoint(projection,{0,0,farUnits*2.f}).z>1.f,"beyond finite far is clipped");
    const int zeroElements[]={1,2,3,4,6,7,12,13,15};
    for(int index:zeroElements)Check(projection[index]==0.f,"projection clears unrelated source terms");
    Check(projection[11]==1.f,"projection has row-vector perspective W=Z");
    Check(ApplyEyeProjection(projection,fov,nearUnits,std::numeric_limits<float>::infinity(),true),"infinite native far builds");
    Check(projection[10]==1.f&&projection[14]==-nearUnits,"infinite far keeps native depth convention");
    Check(Near(ProjectPoint(projection,{0,0,nearUnits}).z,0.f),"infinite near maps to depth zero");
    Check(Near(ProjectPoint(projection,{0,0,1e9f}).z,1.f),"infinite far approaches depth one");
    Check(ProjectPoint(projection,{0,0,farUnits}).z<1.f,"former finite far remains inside infinite projection");

    float planes[48],original[48];
    for(int index=0;index<48;++index)planes[index]=static_cast<float>(index+1)*0.125f;
    std::memcpy(original,planes,sizeof(planes));
    Check(ApplyClipFov(planes,fov),"outward clip planes build");
    Check(std::memcmp(planes,original,8*sizeof(float))==0,"clip update preserves near and far planes exactly");
    Check(std::memcmp(planes+24,original+24,24*sizeof(float))==0,"clip update preserves optional portal planes exactly");
    const Vec3 edges[4]={{fov[1]*z,0,z},{-fov[0]*z,0,z},{0,fov[2]*z,z},{0,-fov[3]*z,z}};
    const Vec3 outside[4]={{fov[1]*z+1.f,0,z},{-fov[0]*z-1.f,0,z},{0,fov[2]*z+1.f,z},{0,-fov[3]*z-1.f,z}};
    const Vec3 inside[4]={{fov[1]*z-1.f,0,z},{-fov[0]*z+1.f,0,z},{0,fov[2]*z-1.f,z},{0,-fov[3]*z+1.f,z}};
    for(int side=0;side<4;++side) {
        const float* plane=planes+(side+2)*4;
        Check(Near(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2],1.f),"clip side normal is unit length");
        Check(plane[3]==0.f,"clip side plane contains camera origin");
        Check(Near(PlaneDistance(plane,edges[side]),0.f),"FOV edge lies on corresponding clip plane");
        Check(PlaneDistance(plane,outside[side])>0.f,"outward clip normal rejects outside");
        Check(PlaneDistance(plane,inside[side])<0.f,"outward clip normal accepts inside");
        Check(PlaneDistance(plane,{0,0,z})<0.f,"all side planes contain forward center");
    }
}
void TestProjectionValidation() {
    const float nan=std::numeric_limits<float>::quiet_NaN(),infinity=std::numeric_limits<float>::infinity();
    const float fov[4]={0.8f,1.3f,1.15f,0.65f};
    float matrix[16],sentinel[16],planes[48],original[48];
    for(int index=0;index<16;++index)matrix[index]=sentinel[index]=static_cast<float>(index+1);
    for(int index=0;index<48;++index)planes[index]=original[index]=static_cast<float>(index+1);
    const float badValues[]={nan,infinity,-infinity,0.f,-1.f,0.049f,5.001f};
    for(int side=0;side<4;++side)for(float badValue:badValues) {
        float invalid[4]={fov[0],fov[1],fov[2],fov[3]};invalid[side]=badValue;
        Check(!ValidFov(invalid),"invalid FOV tangent rejected");
        Check(!ApplyEyeProjection(matrix,invalid,5.f,30000.f,false),"projection rejects invalid FOV");
        Check(std::memcmp(matrix,sentinel,sizeof(matrix))==0,"bad FOV leaves projection unchanged");
        Check(!ApplyClipFov(planes,invalid),"clip planes reject invalid FOV");
        Check(std::memcmp(planes,original,sizeof(planes))==0,"bad FOV leaves all clip planes unchanged");
    }
    const float badNear[]={0.f,-1.f,nan,infinity};
    for(float nearUnits:badNear) {
        Check(!ApplyEyeProjection(matrix,fov,nearUnits,30000.f,false),"invalid near distance rejected");
        Check(std::memcmp(matrix,sentinel,sizeof(matrix))==0,"bad near leaves projection unchanged");
    }
    const float badFar[]={0.f,-1.f,5.f,4.f,nan,infinity};
    for(float farUnits:badFar) {
        Check(!ApplyEyeProjection(matrix,fov,5.f,farUnits,false),"invalid finite far distance rejected");
        Check(std::memcmp(matrix,sentinel,sizeof(matrix))==0,"bad far leaves projection unchanged");
    }
    Check(!ValidFov(nullptr),"null FOV rejected");
    Check(!ApplyEyeProjection(nullptr,fov,5.f,30000.f,false),"null projection output rejected");
    Check(!ApplyEyeProjection(matrix,nullptr,5.f,30000.f,false),"null projection FOV rejected");
    Check(!ApplyClipFov(nullptr,fov),"null clip output rejected");
    Check(!ApplyClipFov(planes,nullptr),"null clip FOV rejected");
    Check(std::memcmp(matrix,sentinel,sizeof(matrix))==0&&std::memcmp(planes,original,sizeof(planes))==0,
          "null FOV leaves outputs unchanged");
}
void TestVisibilityProjection() {
    // Left and right eyes are deliberately asymmetric in opposite directions.
    const float eyes[2][4]={{1.4f,.8f,1.1f,.7f},{.9f,1.3f,.95f,1.25f}};
    float unionFov[4];
    constexpr float guard=1.1f;
    for(unsigned side=0;side<4;++side)
        unionFov[side]=(eyes[0][side]>eyes[1][side]?eyes[0][side]:eyes[1][side])*guard;
    float fields[6]={99,98,97,96,95,94};
    Check(ApplyVisibilityProjection(fields+1,unionFov),"native scalar visibility projection builds from both-eye union");
    const float horizontal=1.4f*guard,vertical=1.25f*guard;
    Check(Near(fields[1],2.f*std::atan(horizontal)),"visibility FOV encloses the widest horizontal tangent");
    Check(Near(fields[2],.5f/horizontal)&&Near(fields[3],1.f)&&Near(fields[4],horizontal/vertical),
          "visibility perspective and scales describe the same enclosing FOV");
    Check(fields[0]==99&&fields[5]==94,"visibility update writes only four CameraSpace fields");
    // Match the original CPU Proj3D, including its downward screen Y. Check
    // all eight near/far corners used by 0x6C1FB0, rather than a second copy
    // of the new helper's implementation.
    const float depths[2]={5.f,30000.f};
    const float screenEdges[2]={-.5f,.5f};
    for(float z:depths)for(float x:screenEdges)for(float y:screenEdges) {
        const float factor=z/fields[2];
        const Vec3 corner{x*factor,-y*factor/fields[4],z};
        Check(Near(std::fabs(corner.x/corner.z),horizontal)&&Near(std::fabs(corner.y/corner.z),vertical),
              "native near/far visibility corners reach the guarded horizontal and vertical extents");
        Check(std::fabs(corner.x/corner.z)>=unionFov[0]-.00001f&&
              std::fabs(corner.x/corner.z)>=unionFov[1]-.00001f&&
              std::fabs(corner.y/corner.z)>=unionFov[2]-.00001f&&
              std::fabs(corner.y/corner.z)>=unionFov[3]-.00001f,
              "all reconstructed frustum corners enclose both actual eye frusta");
    }
    for(const auto& eye:eyes)for(float z:depths) {
        const Vec3 edgePoints[4]={{-eye[0]*z,0,z},{eye[1]*z,0,z},{0,eye[2]*z,z},{0,-eye[3]*z,z}};
        for(Vec3 point:edgePoints) {
            const float screenX=point.x*fields[2]/point.z;
            const float screenY=-point.y*fields[2]*fields[4]/point.z;
            Check(std::fabs(screenX)<.5f&&std::fabs(screenY)<.5f,
                  "actual asymmetric eye edges remain inside guarded native screen bounds");
        }
    }
    const float badValues[]={0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),5.01f};
    const float sentinel[4]={1,2,3,4};
    for(unsigned side=0;side<4;++side)for(float bad:badValues) {
        float invalid[4]={unionFov[0],unionFov[1],unionFov[2],unionFov[3]};invalid[side]=bad;
        float output[4];std::memcpy(output,sentinel,sizeof(output));
        Check(!ApplyVisibilityProjection(output,invalid),"visibility projection rejects invalid FOV");
        Check(std::memcmp(output,sentinel,sizeof(output))==0,"invalid visibility projection leaves all fields unchanged");
    }
    Check(!ApplyVisibilityProjection(nullptr,unionFov),"null visibility projection output rejected");
    Check(!ApplyVisibilityProjection(fields,nullptr),"null visibility projection FOV rejected");
}
} // namespace

int main() {
    TestKnownAngles();
    TestTranslationAndEyes();
    TestRecenter();
    TestValidation();
    TestRepeatedRotations();
    TestProjectionAndClip();
    TestProjectionValidation();
    TestVisibilityProjection();
    if(failures){std::fprintf(stderr,"native camera math: %d failures\n",failures);return EXIT_FAILURE;}
    std::puts("native camera math: all checks passed (axis signs, recenter, 64 mm stereo, 4225 rotations, projection, clip planes and guarded visibility corners)");
    return EXIT_SUCCESS;
}
