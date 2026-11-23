#include "native_world_marker_math.hpp"
#include "native_reticle_kl_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}
bool Near(float a,float b,float epsilon=.00003f){return std::fabs(a-b)<epsilon;}
void Clip(const float* rows,const NativeHudMath::Vertex& p,float* out){for(unsigned i=0;i<4;++i)out[i]=p.x*rows[i*4]+p.y*rows[i*4+1]+p.z*rows[i*4+2]+rows[i*4+3];}
}
int main(){using namespace NativeCameraMath;using namespace NativeWorldMarkerMath;
    // Actual .34 PID33584 rows/depth, retained by process creation identity.
    // Source x/y corner spans follow 4996BF; those spans were not logged live.
    const float native[]={.7881976f,0,.0699268f,0,0,.7361624f,.03524249f,0,0,0,1,-5,0,0,1,0};
    const float fov[]={1.1f,.9f,1.05f,.95f};float projection[16]{};
    Check(ApplyEyeProjection(projection,fov,1,10000,false),"actual asymmetric eye projection");
    CameraPose eye;const Vec3 target{250,20,15};
    for(float depth:{-2140.42f,1322.21f,6.f}){
        NativeHudMath::Vertex vertices[]={{-80,-80,depth,0xff70e5ff,0,1},{80,-80,depth,0xff70e5ff,1,1},{80,80,depth,0xff70e5ff,1,0},{-80,80,depth,0xff70e5ff,0,0}};
        const auto original=vertices[0];float saved[16];std::memcpy(saved,native,sizeof(saved));
        for(float eyeZ:{-3.2f,3.2f}){eye.position={0,0,eyeZ};float rows[16]{};Observation observed;
            Check(Translate(native,vertices,4,eye,projection,target,rows,observed),"variable native depth and sheared rows translate");
            Check(observed.normalizedNegativeW==(depth<0)&&observed.minZ==depth&&observed.maxZ==depth&&observed.minW==depth&&observed.maxW==depth,
                  "signed native W and source depth are diagnosed");
            float sumX=0,sumY=0;
            for(const auto& vertex:vertices){float clip[4],before[4];Clip(rows,vertex,clip);Clip(native,vertex,before);
                Check(clip[3]>0&&Near(clip[2]/clip[3],.5f),"native negative W becomes finite visible overlay clip depth");
                sumX+=clip[0]/clip[3];sumY+=clip[1]/clip[3];}
            const auto delta=Subtract(target,eye.position);const float px=Dot(delta,eye.basis.right),py=Dot(delta,eye.basis.up),pz=Dot(delta,eye.basis.forward);
            Check(Near(sumX/4,(px*projection[0]+pz*projection[8])/pz)&&Near(sumY/4,(py*projection[5]+pz*projection[9])/pz),
                  "quad centre uses actual eye/world target including asymmetric frustum");
            float a[4],b[4],c[4],d[4];Clip(native,vertices[0],a);Clip(native,vertices[1],b);Clip(rows,vertices[0],c);Clip(rows,vertices[1],d);
            Check(Near(d[0]/d[3]-c[0]/c[3],b[0]/std::fabs(b[3])-a[0]/std::fabs(a[3])),"native horizontal size and corner orientation survive signed-W normalization");
            Check(!std::memcmp(saved,native,sizeof(saved))&&!std::memcmp(&original,&vertices[0],sizeof(original)),"native vertex colour UV and constants remain byte exact");
        }
        float rows[16]{};Check(!NativeReticleKLMath::Translate(native,vertices,4,eye,projection,target,rows),"ordinary reticle path retains strict z6 and unsheared UI proof");
    }
    NativeHudMath::Vertex vertices[]={{-80,-80,-2140,0,0,1},{80,-80,-2140,0,1,1},{80,80,-2140,0,1,0},{-80,80,-2140,0,0,0}};
    float sentinel[16];for(auto& value:sentinel)value=42;float original[16];std::memcpy(original,sentinel,sizeof(original));Observation observed;
    auto reject=[&](const char* label){Check(!Translate(native,vertices,4,eye,projection,target,sentinel,observed)&&!std::memcmp(sentinel,original,sizeof(original)),label);};
    vertices[1].z=2140;reject("mixed clip-W signs cannot warp a crossed camera-plane batch");vertices[1].z=0;reject("zero clip-W rejected transactionally");
    vertices[1].z=-2140;vertices[1].u=std::numeric_limits<float>::quiet_NaN();reject("nonfinite native UV cannot enter marker path");vertices[1].u=1;
    float invalid[16];std::memcpy(invalid,native,sizeof(invalid));invalid[4]=std::numeric_limits<float>::infinity();
    Check(!Translate(invalid,vertices,4,eye,projection,target,sentinel,observed),"nonfinite native shader row rejected");
    Check(!Translate(native,vertices,3,eye,projection,target,sentinel,observed),"incomplete native quad rejected");
    Check(!Translate(native,vertices,4,eye,projection,{-250,0,0},sentinel,observed),"target behind actual tracked eye rejected");
    auto invalidEye=eye;invalidEye.basis.forward={};Check(!Translate(native,vertices,4,invalidEye,projection,target,sentinel,observed),"invalid eye basis rejected");
    const Layout calibrated{true,.04f,.06f,.015f,-.02f,1500};
    for(float depth:{-2140.42f,1322.21f,.001f,0.f})for(float eyeZ:{-3.2f,3.2f}){
        for(auto& vertex:vertices)vertex.z=depth;eye.position={0,0,eyeZ};float rows[16]{};
        Check(Translate(native,vertices,4,eye,projection,target,rows,observed,&calibrated),"producer-calibrated native size survives either eye and near/zero deferred view W");
        float a[4],b[4],c[4];Clip(rows,vertices[0],a);Clip(rows,vertices[1],b);Clip(rows,vertices[2],c);
        Check(Near(b[0]/b[3]-a[0]/a[3],calibrated.width)&&Near(c[1]/c[3]-b[1]/b[3],calibrated.height)&&a[3]>0&&a[2]/a[3]==.5f,
              "native source size is constant, rather than enlarged by deferred VR projection or inverse W");
        const auto delta=Subtract(target,eye.position);const float px=Dot(delta,eye.basis.right),py=Dot(delta,eye.basis.up),pz=Dot(delta,eye.basis.forward);
        Check(Near((a[0]/a[3]+b[0]/b[3])*.5f,(px*projection[0]+pz*projection[8])/pz+calibrated.offsetX)&&
              Near((b[1]/b[3]+c[1]/c[3])*.5f,(py*projection[5]+pz*projection[9])/pz+calibrated.offsetY),"cropped native objective component keeps its authored offset around actual stereo target");
    }
    CameraPose centre;centre.position={10,20,30};const Vec3 worldTarget{510,100,-15};
    const float frusta[2][4]={{1.3f,.9f,1.1f,.95f},{.85f,1.25f,1.05f,1.1f}},average[]={1.075f,1.075f,1.075f,1.025f};
    float reference[16]{};Check(ApplyEyeProjection(reference,average,1,10000,false),"shared reference focal for unequal eye FOV");
    Vec3 intersections[2][4];
    for(unsigned side=0;side<2;++side){CameraPose actual=centre;const float yaw=side?.12f:-.12f;
        actual.basis.forward={std::cos(yaw),0,std::sin(yaw)};actual.basis.right=Cross(actual.basis.up,actual.basis.forward);
        actual.position=Add(centre.position,{.3f,side?.1f:-.1f,side?-3.2f:3.2f});float sdk[16]{},rows[16]{};
        Check(ApplyEyeProjection(sdk,frusta[side],1,10000,false)&&Translate(native,vertices,4,actual,sdk,worldTarget,rows,observed,&calibrated,&centre,reference),
              "one native billboard projects into unequal and canted actual eye");
        for(unsigned corner=0;corner<4;++corner){float clip[4];Clip(rows,vertices[corner],clip);const float tx=(clip[0]/clip[3]-sdk[8])/sdk[0],ty=(clip[1]/clip[3]-sdk[9])/sdk[5];
            const auto ray=Add(actual.basis.forward,Add(Scale(actual.basis.right,tx),Scale(actual.basis.up,ty)));
            const float distance=Dot(Subtract(worldTarget,actual.position),centre.basis.forward)/Dot(ray,centre.basis.forward);
            intersections[side][corner]=Add(actual.position,Scale(ray,distance));}
    }
    for(unsigned i=0;i<4;++i){const auto difference=Subtract(intersections[0][i],intersections[1][i]);Check(Dot(difference,difference)<1e-6f,"both eye rays reconstruct same WORLD corner instead of duplicated screen rectangles");}
    const auto width=Subtract(intersections[0][1],intersections[0][0]),height=Subtract(intersections[0][2],intersections[0][1]);
    Check(Near(std::sqrt(Dot(width,width)),calibrated.width*500/reference[0],.001f)&&Near(std::sqrt(Dot(height,height)),calibrated.height*500/reference[5],.001f),
          "shared billboard physical extent preserves producer native angular size at target depth");
    // Mission objectives carry a nonzero native atlas crop. That crop must
    // stay at the same WORLD location when HMD yaw/pitch/roll changes. The old
    // headset-oriented plane visibly orbited it farther at longer distances.
    for(float distance:{100.f,10000.f}){
        const Vec3 head{10,20,30},goal=Add(head,{distance,distance*.1f,distance*.15f});
        Basis plane;float radialDepth=0;Check(AnchoredBillboard(goal,head,plane,radialDepth),"objective plane has a WORLD frame independent of headset orientation");
        Vec3 baseline[4]{};float oldMaximumStep=0;
        const auto reconstruct=[&](const CameraPose& camera,const float* sdk,const float* rows,unsigned corner){
            float clip[4];Clip(rows,vertices[corner],clip);
            const float tx=(clip[0]/clip[3]-sdk[8])/sdk[0],ty=(clip[1]/clip[3]-sdk[9])/sdk[5];
            const auto ray=Add(camera.basis.forward,Add(Scale(camera.basis.right,tx),Scale(camera.basis.up,ty)));
            return Add(camera.position,Scale(ray,Dot(Subtract(goal,camera.position),plane.forward)/Dot(ray,plane.forward)));
        };
        const Quat yaw{0,std::sin(.2f),0,std::cos(.2f)},pitch{0,0,std::sin(.125f),std::cos(.125f)},roll{std::sin(.15f),0,0,std::cos(.15f)};
        const Quat turns[]={{},yaw,pitch,roll,Multiply(yaw,Multiply(pitch,roll))};
        for(unsigned turn=0;turn<5;++turn)for(unsigned side=0;side<2;++side){
            CameraPose headPose;headPose.position=head;
            headPose.basis={Rotate(turns[turn],Basis{}.forward),Rotate(turns[turn],Basis{}.up),Rotate(turns[turn],Basis{}.right)};
            auto actual=headPose;actual.position=Add(head,Scale(headPose.basis.right,side?3.2f:-3.2f));float sdk[16]{},rows[16]{},oldRows[16]{};
            Check(ApplyEyeProjection(sdk,frusta[side],1,20000,false)&&
                Translate(native,vertices,4,actual,sdk,goal,rows,observed,&calibrated,&headPose,reference,true),
                "actual asymmetric eye projects anchored objective with HMD yaw pitch roll and either IPD offset");
            Check(Translate(native,vertices,4,actual,sdk,goal,oldRows,observed,&calibrated,&headPose,reference),"old moving-plane negative control remains observable");
            for(unsigned corner=0;corner<4;++corner){const auto world=reconstruct(actual,sdk,rows,corner);
                if(!turn&&!side)baseline[corner]=world;
                const auto difference=Subtract(world,baseline[corner]);
                Check(Dot(difference,difference)<distance*distance*1e-10f,"objective WORLD corners remain fixed under pure head rotation, including the cropped offset");
                const auto oldDifference=Subtract(reconstruct(actual,sdk,oldRows,corner),baseline[corner]);
                oldMaximumStep=std::max(oldMaximumStep,std::sqrt(Dot(oldDifference,oldDifference)));
            }
        }
        Check(oldMaximumStep>distance*.005f,"old HMD-oriented billboard reproduces a distance-scaled WORLD sway rejected by the new path");
        Check(Dot(Subtract(baseline[2],baseline[1]),Vec3{0,1,0})>0,"objective vertical crop keeps WORLD up despite headset roll");
        CameraPose lookingBack;lookingBack.position=head;lookingBack.basis.forward={-1,0,0};lookingBack.basis.right={0,0,1};
        float rejectedRows[16]{};
        Check(!Translate(native,vertices,4,lookingBack,reference,goal,rejectedRows,observed,&calibrated,&lookingBack,reference,true),"anchored objective behind current eye remains rejected");
        CameraPose lookingForward;lookingForward.position=head;
        Check(!Translate(native,vertices,4,lookingForward,reference,Add(head,{distance,0,distance*10}),rejectedRows,observed,&calibrated,&lookingForward,reference,true),"anchored objective beyond the existing offscreen clip bound remains rejected");
    }
    Basis overhead;float overheadDistance=0;
    Check(AnchoredBillboard({0,100,0},{},overhead,overheadDistance)&&ValidateBasis(overhead)&&Near(overheadDistance,100),"vertical goal uses a deterministic finite WORLD pole basis");
    Check(!AnchoredBillboard({},{},overhead,overheadDistance),"goal at the head cannot create an undefined billboard plane");
    vertices[0].z=-140;vertices[3].z=80;
    float rebuilt[16]{};
    Check(Translate(native,vertices,4,eye,projection,target,rebuilt,observed,&calibrated),"owned calibrated world sprite accepts native per-corner Z while rebuilding its world billboard");
    projection[11]=0;reject("nonperspective tracked-eye certificate rejected");
    std::printf("Native world marker projection: %u checks PASS.\n",checks);return 0;
}
