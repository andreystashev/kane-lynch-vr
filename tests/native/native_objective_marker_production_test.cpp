// Actual native position reader plus production HUD capture. Private CPU
// arenas only: no D3D device, game, runtime, input or live hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_hud_kl.cpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
}
namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return true;}}
namespace Fixture {
unsigned checks=0,entered=0,left=0,resets=0;std::uintptr_t lastPlayer=0;
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}}
void Observe(NativeHudKL::NativeMarkerEvent event,std::uintptr_t player){if(event==NativeHudKL::NativeMarkerEvent::Begin)++entered;else if(event==NativeHudKL::NativeMarkerEvent::End)++left;else ++resets;lastPlayer=player;}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
}
int main(int argc,char** argv){using namespace Fixture;using namespace NativeHudKL;using NativeCameraMath::Vec3;
    if(argc<2){std::puts("SKIP provide pinned EXE read-only.");return 0;}NativeHudFixture::Image exe;
    Check(exe.Load(argv[1]),"load supported EXE without starting the game");
    std::uint32_t rappelTexture=0,rappelName=0;char rappelResource[80]{};
    Check(exe.Read(0xA47B08+24*4,&rappelTexture,4)&&rappelTexture==0x26&&exe.Read(0xA47FF4,&rappelName,4)&&rappelName==0x974D08&&
        exe.Read(rappelName,rappelResource,sizeof("tex:UserInterface/Icons/UI_KL_Communications_01#Rappel"))&&
        !std::strcmp(rappelResource,"tex:UserInterface/Icons/UI_KL_Communications_01#Rappel"),"pinned native ActionMarker type24 resolves to the exact Rappel icon resource rather than a reticle");
    const unsigned char prefix[]={0x8B,0x44,0x24,4,0x83,0xEC,8,0x80,0x79,0x4D,0,0x74,0x2B};
    std::array<unsigned char,sizeof(prefix)> actual{};Check(exe.Read(0x653480,actual.data(),actual.size())&&!std::memcmp(actual.data(),prefix,sizeof(prefix)),"native position-reader entry and fixed/bob branches are exact");
    const auto code=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(code!=0,"allocate selected native CPU reader");
    Check(exe.Read(0x653480,reinterpret_cast<void*>(code),0x71),"copy the complete native position reader including both return paths");
    const double height=30.;Put(code+0x100,height);Put(code+0x12,std::uint32_t(code+0x100));
    originalMarkerPosition=reinterpret_cast<MarkerPosition>(code);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(code),0x1000);
    std::array<unsigned char,0x64> record{};const auto at=reinterpret_cast<std::uintptr_t>(record.data());
    Put(at+4,Vec3{100,20,300});Put(at+0x1C,std::uint32_t(26));Put(at+0x4F,std::uint8_t(1));Put(at+0x40,5.f);
    SetNativeMarkerObserver(Observe);BeginNativeMarkers(0x11000000,reinterpret_cast<void*>(0x12000000),reinterpret_cast<void*>(0x14000000));
    Check(entered==1&&lastPlayer==0x11000000&&nativeMarkerScope.player==lastPlayer,"observer receives the actual owned native marker producer scope");
    const auto before=record;Vec3 point{};Check(MarkerPositionHook(record.data(),nullptr,&point)==&point&&point.y==25,"real native position result retains its original animated Y for the ordinary draw");
    Check(nativeMarkerScope.valid&&nativeMarkerScope.target.y==20&&nativeMarkerScope.record==at&&nativeObjectiveBobSuppressed==1,"production VR source captures stable base ONLY for actual Objective26");
    Put(at+0x40,12.f);const auto moved=record;MarkerPositionHook(record.data(),nullptr,&point);
    Check(point.y==32&&nativeMarkerScope.target.y==20&&nativeObjectiveBobSuppressed==2,"native animation continues while both VR captures keep the same authoritative objective point");
    Check(record==moved,"position capture performs no writes to the native marker record");
    for(unsigned type:{18u,19u}){Put(at+0x1C,type);MarkerPositionHook(record.data(),nullptr,&point);Check(point.y==32&&nativeMarkerScope.target.y==32,"Loot and Money retain their original position semantics instead of masquerading as goals");}
    Put(at+0x1C,std::uint32_t(7));Put(at+0x4D,std::uint8_t(1));MarkerPositionHook(record.data(),nullptr,&point);
    Check(point.y==50&&nativeMarkerScope.target.y==50,"fixed shield/command producer keeps native 30-unit world offset");
    BeginNativeMarkers(0x15000000,reinterpret_cast<void*>(0x16000000),reinterpret_cast<void*>(0x17000000));
    Check(entered==2&&nativeMarkerScope.player==0x15000000,"nested native producer has separate ownership");EndNativeMarkers();
    Check(left==1&&nativeMarkerScope.player==0x11000000,"ending a nested producer restores the outer source scope");EndNativeMarkers();
    Check(left==2&&!nativeMarkerScope.player,"outer source scope ends without lingering capture authority");
    const auto emitted=nativeObjectiveBobSuppressed.load();record=before;MarkerPositionHook(record.data(),nullptr,&point);
    Check(!nativeMarkerScope.valid&&nativeObjectiveBobSuppressed==emitted&&point.y==25,"unowned position calls use the complete original native result and cannot capture a VR target");
    BeginNativeMarkers(0x11000000,nullptr,nullptr);InvalidateDevice();Check(resets==1&&!nativeMarkerDepth&&!nativeMarkerScope.player,"device invalidation explicitly clears observer and producer authority without a GPU call");
    // Exercise the same CPU consumer called by DrawReticle, including native
    // aspect conversion and type26 policy, without constructing a D3D device.
    using namespace NativeCameraMath;
    const float nativeRows[]={1,0,0,0,0,1,0,0,0,0,1,-5,0,0,1,0};
    const std::vector<NativeHudMath::Vertex> vertices={{-1,-1,6,0xff70e5ff,0,1},{1,-1,6,0xff70e5ff,1,1},{1,1,6,0xff70e5ff,1,0},{-1,1,6,0xff70e5ff,0,0}};
    MarkerPacket packet;packet.player=0x11000000;packet.record=at;packet.recordType=26;packet.texture=0x27;packet.caller=0x253F9A;
    packet.layout={true,.018f,.032f,.006f,.009f,1500};
    ReticleFrame frame;frame.player=packet.player;frame.width=3840;frame.height=2160;frame.pairId=1;
    const float fov[]={1.1f,1.1f,1.f,1.f};Check(ApplyEyeProjection(frame.referenceProjection,fov,1,30000,false),"actual marker consumer receives valid shared SDK focal");
    const auto worldCorner=[&](const ReticleFrame& current,const float* rows,const Basis& plane,unsigned index=0){
        const auto p=vertices[index];float clip[4]{};for(unsigned i=0;i<4;++i)clip[i]=p.x*rows[i*4]+p.y*rows[i*4+1]+p.z*rows[i*4+2]+rows[i*4+3];
        const float x=(clip[0]/clip[3]-current.projection[8])/current.projection[0],y=(clip[1]/clip[3]-current.projection[9])/current.projection[5];
        const auto ray=Add(current.actualEye.basis.forward,Add(Scale(current.actualEye.basis.right,x),Scale(current.actualEye.basis.up,y)));
        return Add(current.actualEye.position,Scale(ray,Dot(Subtract(current.target,current.actualEye.position),plane.forward)/Dot(ray,plane.forward)));
    };
    for(float distance:{100.f,10000.f}){
        frame.target={distance,distance*.1f,distance*.15f};Basis plane;float radial=0;
        Check(NativeWorldMarkerMath::AnchoredBillboard(frame.target,{},plane,radial),"production fixture uses fixed WORLD target plane");
        Vec3 initial{};float otherMaximumStep=0;
        const Quat yaw{0,std::sin(.2f),0,std::cos(.2f)},pitch{0,0,std::sin(.125f),std::cos(.125f)},roll{std::sin(.15f),0,0,std::cos(.15f)};
        const Quat turns[]={{},yaw,pitch,roll,Multiply(yaw,Multiply(pitch,roll))};
        for(unsigned turn=0;turn<5;++turn)for(unsigned eye=0;eye<2;++eye){
            frame.pairId=1+turn+(distance>100?10:0);frame.eye=eye;frame.centre={};
            frame.centre.basis={Rotate(turns[turn],Basis{}.forward),Rotate(turns[turn],Basis{}.up),Rotate(turns[turn],Basis{}.right)};
            frame.actualEye=frame.centre;frame.actualEye.position=Scale(frame.centre.basis.right,eye?3.2f:-3.2f);
            const float actualFov[]={eye?.9f:1.3f,eye?1.25f:.85f,1.1f,.95f};
            Check(ApplyEyeProjection(frame.projection,actualFov,1,30000,false),"production asymmetric eye projection built");
            float rows[16]{};unsigned stage=0;packet.recordType=26;
            Check(TranslateNativeMarker(packet,frame,nativeRows,vertices,rows,stage),"actual marker consumer accepts Objective26 cropped native sprite");
            const auto corner=worldCorner(frame,rows,plane);if(!turn&&!eye)initial=corner;
            const auto step=Subtract(corner,initial);
            Check(Dot(step,step)<distance*distance*1e-10f,"production Objective26 crop remains at fixed WORLD corner across HMD rotations and stereo eyes");
            packet.recordType=7;
            Check(TranslateNativeMarker(packet,frame,nativeRows,vertices,rows,stage),"other native marker type retains its existing presentation policy");
            const auto otherStep=Subtract(worldCorner(frame,rows,plane),initial);otherMaximumStep=std::max(otherMaximumStep,std::sqrt(Dot(otherStep,otherStep)));
        }
        Check(otherMaximumStep>distance*.003f,"type26 orientation correction is actually selected and does not alter unrelated native marker types");
    }
    // Unlike the preceding fixed-layout tests, run the actual native-source
    // capture and actual consumer together while the native camera and HMD
    // have different distances. This reproduces walking away from a marker,
    // including the monitor-size cap that hid the raw physical dimensions.
    NativeHudFixture::Memory memory;
    constexpr std::uint32_t sourceCamera=0x15000000,ui=0x16000000,geometry=0x17000000,payload=0x18000000,position=0x19000000;
    memory.Set(sourceCamera+0x14C,ui);memory.Set(ui,std::uint32_t(NativeHudFixture::Base+0x545A3C));
    memory.Set(NativeHudFixture::Base+0x545A3C+0x244,std::uint32_t(NativeHudFixture::Base+0xF5BD0));
    memory.Set(ui+0x38,1.f);memory.Set(ui+0x3C,16.f/9.f);memory.Set(ui+0x44,std::uint32_t(0));memory.Set(ui+0x54,.5f);
    memory.Set(geometry,std::array<std::uint32_t,3>{1,0,payload});
    const Vec3 fixedGoal{1000,50,150};Basis fixedPlane;float unusedDistance=0;
    Check(NativeWorldMarkerMath::AnchoredBillboard(fixedGoal,{},fixedPlane,unusedDistance),"source-to-consumer fixture has a fixed WORLD reference plane");
    for(bool cropped:{false,true}){
        std::array<float,29> authored{};authored[23]=100;authored[24]=100;
        const float corners[4][2]={{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
        for(unsigned i=0;i<4;++i){authored[11+3*i]=corners[i][0]+(cropped?.2f:0);authored[12+3*i]=corners[i][1]+(cropped?.3f:0);}
        memory.Set(payload,authored);Vec3 initialCorners[4]{};unsigned iteration=0;float oldMaximumStep=0;
        for(float headDistance:{100.f,1000.f,5000.f})for(float nativeDepth:{25.f,1000.f,8000.f,-25.f,-8000.f})for(unsigned type:{26u,24u,7u})for(unsigned eye=0;eye<2;++eye){
            ++iteration;memory.Set(position,Vec3{0,0,nativeDepth});packet.layout=NativeMarkerSourceKL::Capture(NativeHudFixture::Base,sourceCamera,geometry,position,6,memory.Reader());
            Check(packet.layout.valid&&packet.layout.sourceExtentValid,"actual Capture certifies perspective source extent before near cap and native source-depth sign");
            frame.target=fixedGoal;frame.centre={};frame.centre.position=Subtract(fixedGoal,Scale(fixedPlane.forward,headDistance));
            frame.actualEye=frame.centre;frame.actualEye.position=Add(frame.centre.position,Scale(frame.centre.basis.right,eye?3.2f:-3.2f));
            frame.pairId=100+iteration;frame.eye=eye;packet.recordType=type;
            const float actualFov[]={eye?.9f:1.3f,eye?1.25f:.85f,1.1f,.95f};ApplyEyeProjection(frame.projection,actualFov,1,30000,false);
            float rows[16]{};unsigned stage=0;
            Check(TranslateNativeMarker(packet,frame,nativeRows,vertices,rows,stage),"actual Capture-to-consumer retains world-owned goal and white/native marker parts without entering reticle path");
            Vec3 reconstructed[4]{};
            for(unsigned corner=0;corner<4;++corner){reconstructed[corner]=worldCorner(frame,rows,fixedPlane,corner);
                if(iteration==1)initialCorners[corner]=reconstructed[corner];const auto step=Subtract(reconstructed[corner],initialCorners[corner]);
                Check(Dot(step,step)<.0001f,"WORLD marker corner and authored asymmetric pivot stay fixed when walking away or changing native source depth");}
            if(!cropped){const auto middle=Scale(Add(reconstructed[0],reconstructed[2]),.5f);const auto centreError=Subtract(middle,fixedGoal);
                Check(Dot(centreError,centreError)<.0001f,"zero-offset marker geometric centre stays at the true WORLD target across the complete producer-consumer path");}
            auto oldPacket=packet;oldPacket.layout.sourceExtentValid=false;oldPacket.recordType=26;++oldPacket.record;
            Check(TranslateNativeMarker(oldPacket,frame,nativeRows,vertices,rows,stage),"old projected-size negative control can still be evaluated");
            const auto oldStep=Subtract(worldCorner(frame,rows,fixedPlane),initialCorners[0]);oldMaximumStep=std::max(oldMaximumStep,std::sqrt(Dot(oldStep,oldStep)));
        }
        Check(oldMaximumStep>100,"old Capture NDC times HMD distance reproduces moving WORLD corners and crop pivot during walking");
        frame.target=fixedGoal;frame.centre={fixedPlane,Subtract(fixedGoal,Scale(fixedPlane.forward,100))};frame.actualEye=frame.centre;
        std::memcpy(frame.projection,frame.referenceProjection,sizeof(frame.projection));frame.pairId=cropped?10001:10000;packet.recordType=26;
        float nearRows[16]{};unsigned nearStage=0;
        Check(TranslateNativeMarker(packet,frame,nativeRows,vertices,nearRows,nearStage),"close source-owned marker retains the existing angular safety cap using current HMD distance");
        float clip[4][4]{};for(unsigned corner=0;corner<4;++corner)for(unsigned row=0;row<4;++row){const auto p=vertices[corner];clip[corner][row]=p.x*nearRows[row*4]+p.y*nearRows[row*4+1]+p.z*nearRows[row*4+2]+nearRows[row*4+3];}
        const float nearWidth=std::fabs(clip[1][0]/clip[1][3]-clip[0][0]/clip[0][3]);
        const float nearHeight=std::fabs(clip[2][1]/clip[2][3]-clip[1][1]/clip[1][3]);
        Check(std::max(nearWidth,nearHeight)<=.06001f,"physical extent correction does not introduce a giant close icon");
    }
    const auto extentAt=[&](Vec3 goal,float distance,std::uint64_t pair){
        frame.target=goal;packet.target=goal;frame.pairId=pair;frame.centre={fixedPlane,Subtract(goal,Scale(fixedPlane.forward,distance))};frame.actualEye=frame.centre;
        float rows[16]{};unsigned stage=0;Check(TranslateNativeMarker(packet,frame,nativeRows,vertices,rows,stage),"actual consumer accepts current fixed or attached rappel marker identity");
        const auto edge=Subtract(worldCorner(frame,rows,fixedPlane,1),worldCorner(frame,rows,fixedPlane,0));return std::sqrt(Dot(edge,edge));
    };
    packet.recordType=24;packet.fixed=true;const float oldFixedExtent=extentAt(fixedGoal,100,20000);
    const float newFixedExtent=extentAt(Add(fixedGoal,{500,0,0}),1000,20001);
    Check(newFixedExtent>oldFixedExtent*5,"actual same-pointer fixed white/rappel record at new WORLD location gets a new presentation scale");
    packet.fixed=false;packet.entity=0x12340000;packet.entityGeometry=0x12350000;
    const float attachedExtent=extentAt(fixedGoal,100,20002),movedAttachedExtent=extentAt(Add(fixedGoal,{500,0,0}),1000,20003);
    Check(std::fabs(attachedExtent-movedAttachedExtent)<.01f,"actual attached white/rappel marker keeps source scale while its living target moves");
    std::array<float,29> simple{};simple[7]=simple[8]=100;memory.Set(payload,simple);
    for(unsigned type:{26u,24u}){
        packet.recordType=type;memory.Set(position,Vec3{0,0,25});packet.layout=NativeMarkerSourceKL::Capture(NativeHudFixture::Base,sourceCamera,geometry,position,5,memory.Reader());
        Check(packet.layout.sourceExtentValid,"actual symmetric type5 source has the same physical extent certificate as the native cropped type6 source");
        const float nearExtent=extentAt(fixedGoal,100,21000+type*2);
        memory.Set(position,Vec3{0,0,-8000});packet.layout=NativeMarkerSourceKL::Capture(NativeHudFixture::Base,sourceCamera,geometry,position,5,memory.Reader());
        const float farExtent=extentAt(fixedGoal,1000,21001+type*2);
        Check(std::fabs(nearExtent-farExtent)<.01f,"actual type5 green goal and white Rappel pointer remain physically stable across retreat and native source-depth sign change");
    }
    SetNativeMarkerObserver(nullptr);VirtualFree(reinterpret_cast<void*>(code),0,MEM_RELEASE);
    std::printf("Native objective position/observer CPU producer: %u checks PASS.\n",checks);
    return 0;
}
