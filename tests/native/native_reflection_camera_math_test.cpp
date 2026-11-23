#include "native_reflection_camera_math.hpp"
#include "native_camera_kl_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace N=NativeReflectionCameraMath;
using namespace NativeCameraMath;
namespace {
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool Near(double a,double b,double tolerance=2.e-4){return std::fabs(a-b)<=tolerance;}
using M=N::Detail::Matrix;
M Mirror(Vec3 normal,float distance) {
    M m=N::Detail::Identity();const double n[]={normal.x,normal.y,normal.z};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m.a[r*4+c]-=2.*n[r]*n[c];
    for(unsigned c=0;c<3;++c)m.a[12+c]=-2.*distance*n[c];return m;
}
CameraPose NativeView(const CameraPose& world){CameraPose view;Check(NativeCameraKLMath::ViewFromWorld(world,view),"ordinary view fixture has native packed convention");return view;}
void Compare(const CameraPose& a,const M& b,const char* message) {
    const auto m=N::Detail::View(a);bool same=true;
    for(unsigned i=0;i<16;++i)same=same&&Near(m.a[i],b.a[i],i>=12?.015:2.e-4);Check(same,message);
}
void Lens(float* p,float l,float r,float d,float u) {
    const float fov[]={l,r,d,u};Check(ApplyEyeProjection(p,fov,2.f,40000.f,false),"perspective fixture");
}
// Input generator uses the native algorithm's inverse VP, then the assertion
// checks independent world-plane geometric invariants after all eye changes.
void NativeOblique(const CameraPose& view,const float* lens,const double plane[4],float* out) {
    M p;for(unsigned i=0;i<16;++i)p.a[i]=lens[i];
    M inverse;Check(N::Detail::Inverse(N::Detail::Multiply(N::Detail::View(view),p),inverse),"native clip input inverse");
    double clip[4]{},squared=0;
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)clip[r]+=inverse.a[r*4+c]*plane[c];squared+=clip[r]*clip[r];}
    for(double& value:clip)value/=std::sqrt(squared);
    std::memcpy(out,lens,16*sizeof(float));
    for(unsigned r=0;r<4;++r){double value=0;for(unsigned k=0;k<4;++k)value+=p.a[r*4+k]*clip[k];out[r*4+2]=float(value);}
}
void PlaneInvariant(const CameraPose& view,const float* p,const double plane[4]) {
    M projection;for(unsigned i=0;i<16;++i)projection.a[i]=p[i];
    const auto vp=N::Detail::Multiply(N::Detail::View(view),projection);
    double dot=0,norm=0;for(unsigned r=0;r<3;++r){dot+=vp.a[r*4+2]*plane[r];norm+=plane[r]*plane[r];}
    const double scale=dot/norm;Check(scale>0,"oblique world half-space sign is preserved");
    for(unsigned r=0;r<4;++r)
        Check(Near(vp.a[r*4+2]/scale,plane[r],r==3?.08:.0003),"reflected eye keeps the original physical world clipping plane");
}
}
int main() {
    CameraPose original;original.position={125,25000,-80};
    const auto native=NativeView(original);const auto mirror=Mirror({0,1,0},-24950);
    const auto reflected=N::Detail::Packed(N::Detail::Multiply(mirror,N::Detail::View(native)));
    Check(ValidatePose(native)&&!ValidatePose(reflected),"ordinary native validator retains strict handedness; mirror is separately owned");
    Check(N::Detail::Determinant3(N::Detail::View(native))>0&&N::Detail::Determinant3(N::Detail::View(reflected))<0,
        "packed FUR parity is distinct from the standard D3D affine determinant");
    CameraPose eyeWorld;const Quat turn{.1f,.23f,-.04f,.967f};
    Check(Compose(original,turn,{.032f,.025f,-.07f},{},100,eyeWorld),"tracked translation plus yaw pitch roll fixture");
    const auto eye=NativeView(eyeWorld);CameraPose output;
    Check(N::BuildView(native,reflected,eye,output),"certified plane mirror accepts current translated rotated eye");
    Compare(output,N::Detail::Multiply(mirror,N::Detail::View(eye)),"reflection follows the current eye exactly once");
    auto otherWorld=eyeWorld;otherWorld.position=Add(otherWorld.position,Scale(eyeWorld.basis.right,-6.4f));
    const auto other=NativeView(otherWorld);CameraPose otherOutput;
    Check(N::BuildView(native,reflected,other,otherOutput),"second stereo eye reflection builds");
    Compare(otherOutput,N::Detail::Multiply(mirror,N::Detail::View(other)),"second eye retains its own physical eye offset");
    CameraPose unchanged=output;
    auto invalid=reflected;invalid.basis.right=Scale(invalid.basis.right,2);
    Check(!N::BuildView(native,invalid,eye,output)&&!std::memcmp(&output,&unchanged,sizeof(output)),"scale corruption fails closed without changing the output");
    float floorHeight=0;
    Check(N::HorizontalPlane(native,reflected,floorHeight)&&Near(floorHeight,24950,.015),
          "horizontal floor height comes from the actual paired views");
    const auto wall=N::Detail::Packed(N::Detail::Multiply(Mirror({0,0,1},-80),N::Detail::View(native)));
    Check(!N::HorizontalPlane(native,wall,floorHeight)&&Near(floorHeight,24950,.015),
          "a wall reflected view cannot qualify as floor history");
    const auto inversion=Mirror({1,0,0},0);auto central=N::Detail::Identity();central.a[0]=central.a[5]=central.a[10]=-1;
    invalid=N::Detail::Packed(N::Detail::Multiply(central,N::Detail::View(native)));
    Check(!N::BuildView(native,invalid,eye,output),"point inversion is not accepted as a planar reflection");
    auto glide=inversion;glide.a[13]=5;
    invalid=N::Detail::Packed(N::Detail::Multiply(glide,N::Detail::View(native)));
    Check(!N::BuildView(native,invalid,eye,output),"glide-reflection translation fails the involution proof");
    invalid=native;invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    Check(!N::BuildView(invalid,reflected,eye,output),"nonfinite source camera fails closed");
    float nativeP[16],oblique[16],vrP[16],outP[16];
    Lens(nativeP,1.2f,1.2f,.8f,.8f);Lens(vrP,.83f,1.13f,1.08f,.91f);
    const double floorPlane[]={0,1,0,-24950};NativeOblique(reflected,nativeP,floorPlane,oblique);
    Check(N::BuildProjection(reflected,oblique,output,vrP,outP),"asymmetric VR lens gets the reflected world clip plane");
    PlaneInvariant(output,outP,floorPlane);
    for(unsigned i=0;i<16;++i)if(i%4!=2)Check(outP[i]==vrP[i],"oblique transfer preserves the VR lens side and homogeneous columns exactly");
    float unchangedP[16];
    Check(N::BuildProjection(reflected,oblique,reflected,nativeP,unchangedP),"zero eye/lens delta oblique roundtrip");
    for(unsigned i=0;i<16;++i)Check(Near(unchangedP[i],oblique[i],2.e-4),"zero delta retains the native normalized oblique projection");
    Check(N::BuildProjection(reflected,oblique,otherOutput,vrP,outP),"second eye oblique transfer");
    PlaneInvariant(otherOutput,outP,floorPlane);
    // Oblique clipping plane need not coincide with the mirror: tilted world
    // clipping is preserved rather than guessed from the current eye plane.
    const double tiltedPlane[]={.3,.8,.5196152422706632,-20020};
    NativeOblique(reflected,nativeP,tiltedPlane,oblique);
    Check(N::BuildProjection(reflected,oblique,otherOutput,vrP,outP),"non-axis aligned world clip transfer");PlaneInvariant(otherOutput,outP,tiltedPlane);
    float sentinel[16];for(unsigned i=0;i<16;++i)sentinel[i]=42.f;std::memcpy(outP,sentinel,sizeof(outP));
    float badP[16];std::memcpy(badP,vrP,sizeof(badP));badP[0]=0;
    Check(!N::BuildProjection(reflected,oblique,otherOutput,badP,outP)&&!std::memcmp(outP,sentinel,sizeof(outP)),"singular lens fails closed without output writes");
    auto badOblique=oblique[2];oblique[2]=std::numeric_limits<float>::infinity();
    Check(!N::BuildProjection(reflected,oblique,otherOutput,vrP,outP),"nonfinite native clip fails closed");oblique[2]=badOblique;
    CameraPose finalView;
    Check(!N::Build(native,reflected,nativeP,oblique,other,vrP,finalView,outP),"combined certificate rejects a clip plane unrelated to the reflector plane");
    NativeOblique(reflected,nativeP,floorPlane,oblique);
    Check(N::Build(native,reflected,nativeP,oblique,other,vrP,finalView,outP),"combined view/projection transaction succeeds");
    Compare(finalView,N::Detail::View(otherOutput),"combined API keeps reflected second-eye pose");PlaneInvariant(finalView,outP,floorPlane);
    Check(N::Build(native,reflected,nativeP,nativeP,other,vrP,finalView,outP)&&!std::memcmp(outP,vrP,sizeof(outP)),
        "native clip no-op retains the exact VR projection without a fabricated world near plane");
    oblique[0]*=1.2f;
    Check(!N::Build(native,reflected,nativeP,oblique,other,vrP,finalView,outP),"non-clip lens changes are outside the native oblique certificate");
    const auto tiltedMirror=Mirror({.6f,0,.8f},-70.f);
    const auto tiltedReflected=N::Detail::Packed(N::Detail::Multiply(tiltedMirror,N::Detail::View(native)));
    const double wallPlane[]={.6,0,.8,-70};
    NativeOblique(tiltedReflected,nativeP,wallPlane,oblique);
    Check(N::Build(native,tiltedReflected,nativeP,oblique,other,vrP,finalView,outP),"world mirror proof and oblique transfer also accept a slanted wall");
    Compare(finalView,N::Detail::Multiply(tiltedMirror,N::Detail::View(other)),"non-axis mirror composes translated rotated stereo view in world space");
    PlaneInvariant(finalView,outP,wallPlane);
    std::printf("Native reflection camera math: %u checks PASS; CPU only.\n",checks);
}
