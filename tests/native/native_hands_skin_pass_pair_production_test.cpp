// Native simulation input changes between colour and mask, while the render pair stays current.
// Private memory/IPC only; no positional arguments, evidence output, game or GPU starts.
// Renderer epoch and native body-root helper are controlled fixtures; no GPU/game starts.
// Actual SetFrame/PrepareDraw/CommitDraw through private IPC and native memory.
// Body root adjustment is a controlled fixture; full controller/foregrip IK is production.
// Exercise the actual GPU rig path. Only the native held-item read and VR UI
// flags are fixtures; controller mapping, acquisition, steering, reach fitting
// and the complete production arm solve are unchanged.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace Fixture {inline DWORD tick=1000;inline unsigned cameraCalls=0;inline bool cameraAllowed=true;inline DWORD Clock(){return tick;}}
#define GetTickCount Fixture::Clock
#include "native_hands_kl.cpp"
#undef GetTickCount
#include <cstdio>
#include <cstdlib>

namespace {
NativeWeaponKL::HeldSupport heldFixture;
bool teamOpen=false,weaponOpen=false;
NativeActionKL::State actionFixture{};
unsigned checks=0,failures=0;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",text);}}
NativeCameraMath::CameraPose Frame(){NativeCameraMath::CameraPose p;p.basis={{0,0,1},{0,1,0},{1,0,0}};return p;}
[[noreturn]] void Unexpected(){std::puts("FAIL: fixture attempted an unrelated game/runtime/hook entry");std::abort();}
}
namespace NativeWeaponKL {
bool ReloadHandTarget(std::uintptr_t,unsigned,const NativeCameraMath::CameraPose&,NativeCameraMath::CameraPose&){return false;}
bool ReloadHandBusy(std::uintptr_t){return false;}
bool PreferReloadSlide(const HeldSupport&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose*,std::uint32_t){return false;}
bool CaptureHeldSupport(std::uintptr_t player,float units,HeldSupport& output,bool allowInactive) {
    if(player!=heldFixture.player||units!=100.f)return false;
    if(!heldFixture.active&&!allowInactive)return false;
    output=heldFixture;return true;
}
}
namespace VRBridge {
bool TeamHudVisible(){return teamOpen;}
bool WeaponMenuVisible(){return weaponOpen;}
bool Ready(){return true;}
}
namespace NativeActionKL {
bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}
State Current(){return actionFixture;}
bool PreserveRig(){Unexpected();}
}
namespace NativeCameraKL {
bool CinematicActive(){return false;}
bool DrawStateActive(std::uintptr_t,bool*,bool){++Fixture::cameraCalls;return Fixture::cameraAllowed;}
}
namespace NativeBodyKL {
void ResetFollow(){Unexpected();}
bool AdjustAction(std::uintptr_t,const NativeActionKL::State&,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,
                  NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){Unexpected();}
bool Adjust(std::uintptr_t,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,
            NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){return true;}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unexpected();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_ApplyQueued(){Unexpected();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unexpected();}
}

namespace PairPoseFixture {template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}}

namespace RenderPairFixture {std::uint64_t epoch=1;unsigned eye=0;bool context=true;
bool Context(std::uint64_t& out,unsigned& index){if(!context)return false;out=epoch;index=eye;return true;}}
int main(){
    using namespace NativeHandsKL;using namespace NativeCameraMath;using PairPoseFixture::Put;
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private lazy visual memory allocated");if(!allocation)return 1;
    image=reinterpret_cast<std::uintptr_t>(allocation);
    const auto data=image+0x10000,player=image+0x20000,geometry=image+0x30000,model=image+0x40000,
        cache=image+0x50000,entry=image+0x60000,lookup=image+0x70000,parentsAt=image+0x71000,rest=image+0x72000;
    Put(image+kGameData,data);Put(data+0xA2C,player);Put(player,image+kPlayerVtable);Put(player+0x70,model);
    Put(entry,image+kBonesVtable);Put(entry+0xF4,geometry);Put(geometry+0x60,player);Put(entry+0x108,cache);Put(entry+0x114,10u);
    Put(model+0x4A0,10u);Put(model+0xC,lookup);Put(model+4,rest);Put(model+0x10,parentsAt);
    const std::uint8_t parents[]={0,0,1,1,3,4,1,6,7,2};std::memcpy(reinterpret_cast<void*>(parentsAt),parents,sizeof(parents));
    const std::uint32_t parentIds[]={0,0,1,1,3,4,1,6,7,2};
    std::memset(reinterpret_cast<void*>(lookup),0xff,128);
    Put(lookup,std::uint8_t(1));Put(lookup+6,std::uint8_t(2));Put(lookup+64,std::uint8_t(9));
    Put(lookup+22,std::uint8_t(3));Put(lookup+23,std::uint8_t(4));Put(lookup+24,std::uint8_t(5));
    Put(lookup+16,std::uint8_t(6));Put(lookup+17,std::uint8_t(7));Put(lookup+18,std::uint8_t(8));
    NativeSkinMath::BindLocal local[10];local[1].position={0,90,0};local[2].position={0,60,0};local[3].position={18,35,0};
    local[4].position={0,0,35};local[5].position={0,0,35};local[6].position={-18,35,0};local[7].position={0,0,35};
    local[8].position={0,0,35};local[9].position={0,4,-8};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));CameraPose bind[10];
    Check(NativeSkinMath::BindPose(local,parentIds,10,bind),"full native head and both arm topology generated");
    for(unsigned i=0;i<10;++i)Check(NativeSkinMath::Palette(bind[i],{bind[i].basis,Add(bind[i].position,i?Vec3{0,-5,0}:Vec3{})},reinterpret_cast<float*>(cache+0x90+i*48)),"native palette generated");
    std::array<unsigned char,480> nativePalette{};std::memcpy(nativePalette.data(),reinterpret_cast<void*>(cache+0x90),nativePalette.size());
    CameraPose root;root.basis={{-.10086986422538757f,-.5405029058456421f,.8352736234664917f},
        {-.02102416753768921f,.8405261635780334f,.5413629412651062f},
        {.9946774244308472f,-.03704633191227913f,.09614726901054382f}};
    root.position={319.1148376464844f,179.98817443847656f,-10721.677734375f};Put(entry+0x10,root);Put(entry+0x40,root);Put(image+kGpuPoseIndex,0u);
    CameraFrame camera;camera.valid=camera.firstPerson=camera.nativeBodyAction=true;camera.units=100;camera.tick=Fixture::Clock();camera.pairId=123;
    camera.centre=root;camera.centre.position=Add(root.position,{0,150,0});camera.rawHeadOrientation[3]=1;
    camera.hands.present=camera.hands.mounted=1;camera.hands.tick=Fixture::Clock();camera.hands.head.orientation[3]=1;
    camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    for(auto& hand:camera.hands.hands){hand.orientation[3]=1;hand.flags=HandsIPC::kOrientation|HandsIPC::kPosition;}
    for(auto& mask:camera.hands.inputValid)mask=HandsIPC::kGripInput|HandsIPC::kTriggerInput;
    camera.hands.hands[1].position[0]=.15f;camera.hands.hands[1].position[1]=-.25f;camera.hands.hands[1].position[2]=-.60f;
    camera.hands.hands[0].position[0]=-.15f;camera.hands.hands[0].position[1]=-.25f;camera.hands.hands[0].position[2]=-.60f;
    camera.input=camera.hands;Check(CaptureRigMetadata(model,player,camera.rig),"actual current owner/model/rest rig captured");


    camera.nativeBodyAction=false;camera.hands.hands[0].position[0]=.10f;camera.hands.hands[0].position[2]=-1.f;
    originalRig=camera.rig;installed=true;lastOwnedEntry=entry;lastOwnedModel=model;lastOwnedEntryTick=Fixture::Clock()-500;
    actionFixture={};actionFixture.valid=true;actionFixture.player=player;actionFixture.movementKind=NativeActionKL::Movement::Stand;
    const auto item=image+0x80000,itemRoot=image+0x81000,itemModel=image+0x82000;
    Put(player+0x2D8,image+0x5508B4);Put(player+0x358,image+0x5508B4);
    NativeWeaponOwnership::HandState leftHand,rightHand;leftHand.attacher=32;rightHand.attacher=31;rightHand.item=std::uint32_t(item);rightHand.enabled=1;
    Put(player+0x2D8+0x60,leftHand);Put(player+0x358+0x60,rightHand);Put(item+4,itemRoot);Put(itemRoot+0x60,item);Put(item+0xC0,itemModel);Put(item+0x58,8u);
    heldFixture={};heldFixture.player=player;heldFixture.weapon=item;heldFixture.root=itemRoot;heldFixture.model=itemModel;heldFixture.marker=6;
    heldFixture.markerGeometry=7;heldFixture.reference=8;heldFixture.category=4;heldFixture.active=heldFixture.candidate=true;
    heldFixture.socket=Frame();heldFixture.socket.position={20,0,0};supportGripEnabled=true;supportState={};preparedValid=false;renderPair={};
    HandsIPC::Producer producer;Check(producer.Open(GetCurrentProcessId()),"private render admission IPC source belongs only to fixture PID");
    ConfigureRenderContext(RenderPairFixture::Context);const float offset[3]={0,0,0};auto sample=camera.hands;
    const auto publish=[&](std::uint64_t pair,float grip,unsigned dt){Fixture::tick+=dt;sample.tick=Fixture::Clock();sample.grip[0]=grip;producer.Publish(sample);
        SetFrame(camera.centre,camera.rawHeadOrientation,camera.rawHeadPosition,offset,100,pair,true,0,false);};
    const auto prepare=[&](PreparedDraw& draw){CameraFrame current;if(!FrameSnapshot(current)||!PrepareDraw(current,entry,draw))return false;
        CommitDraw(current,draw);visual=draw.visual;visualGeneration=current.generation;return true;};
    const auto nativeInput=image+0x83000;Put(player+0x9B8,std::uint32_t(nativeInput));Put(nativeInput+0x18,std::uint8_t(0));Put(player+0x400,0u);Put(player+0x96C,0.f);Put(player+0x970,0.f);
    PreparedDraw warm;publish(2452,0,1);Check(prepare(warm),"ordinary production prepare warms up a real physical release");
    ++RenderPairFixture::epoch;PreparedDraw colour;publish(2453,.8f,1);
    Check(prepare(colour)&&colour.visual.supportHeld&&colour.visual.tracked[0]&&colour.visual.tracked[1],"first eye freshly admits both production arm solves and foregrip");
    const auto admission=renderPair.startedTick,sourceTick=colour.visual.sourcePoseTick;const auto firstGeneration=colour.generation;
    Check(StationaryOrdinaryPlayer(player,actionFixture),"first material proves stationary native input");
    Put(player+0x96C,.5f);Check(!StationaryOrdinaryPlayer(player,actionFixture),"native input changed while exact movement/action owner remains identical");
    ClearFrame();PreparedDraw repeated;publish(2453,.8f,30);
    const bool repeatedPrepared=prepare(repeated);
    float paletteDelta=0,rootDelta=0,wristDelta=0;
    if(repeatedPrepared){
        for(unsigned i=0;i<colour.count;++i)for(unsigned j=0;j<12;++j)paletteDelta=std::max(paletteDelta,std::fabs(colour.palette[i][j]-repeated.palette[i][j]));
        const auto a=reinterpret_cast<const float*>(&colour.renderRoot),b=reinterpret_cast<const float*>(&repeated.renderRoot);
        for(unsigned i=0;i<12;++i)rootDelta=std::max(rootDelta,std::fabs(a[i]-b[i]));
        const auto c=reinterpret_cast<const float*>(colour.visual.finalWrist),d=reinterpret_cast<const float*>(repeated.visual.finalWrist);
        for(unsigned i=0;i<24;++i)wristDelta=std::max(wristDelta,std::fabs(c[i]-d[i]));
    }
    std::printf("same-image native-input policy max deltas palette=%g root=%g wrists=%g\n",paletteDelta,rootDelta,wristDelta);
    Check(repeatedPrepared&&!std::memcmp(colour.palette.data(),repeated.palette.data(),colour.count*sizeof(colour.palette[0])),"reflection ClearFrame followed by same admitted epoch preserves numerical pose");
    CameraFrame stillSame;Check(FrameSnapshot(stillSame),"same actual image has a current render context");
    using NativeSkinMath::PaletteSource;
    PaletteSource skinSource;skinSource.palette=colour.paletteSource;skinSource.root=colour.rawRoot;
    const auto colourEpoch=RenderPairFixture::epoch;
    const ColourSkin saved{colour,skinSource,1234,colourEpoch,0};
    Check(saved.Same(stillSame,entry,1234,colour.cache,colour.count,skinSource,colourEpoch,0),
          "the same image can reuse its exact colour preparation after a transient frame retirement");
    auto newSource=skinSource;newSource.root.position.z+=1;
    Check(!saved.Same(stillSame,entry,1234,colour.cache,colour.count,newSource,colourEpoch,0)&&
          !saved.Same(stillSame,entry,1234,colour.cache,colour.count,skinSource,colourEpoch,1)&&
          !saved.Same(stillSame,entry,1234,colour.cache,colour.count,skinSource,colourEpoch+1,0),
          "changing the native WORLD, eye or image cannot borrow a saved colour palette");
    ++RenderPairFixture::epoch;RenderPairFixture::eye=1;sample.hands[0].position[0]+=.5f;sample.hands[1].position[0]+=.5f;
    PreparedDraw rightColour;publish(2453,.8f,80);
    Check(prepare(rightColour)&&rightColour.visual.supportHeld&&rightColour.generation!=firstGeneration,"second eye admitted within250ms keeps source grip after original100ms pose age");
    Check(!HandsIPC::Fresh(Fixture::Clock(),sourceTick)&&renderPair.startedTick==admission&&renderPair.admittedTick==admission,"physical pair has immutable admission time and actually crosses original100ms expiry");
    PreparedDraw mask;publish(2453,.8f,15);const auto stableBefore=stableTorsoFrames.load();
    Check(prepare(mask)&&mask.visual.supportHeld&&mask.visual.tracked[0]&&mask.visual.tracked[1],"late mask has same coupled tracked result after125ms");
    Check(!std::memcmp(colour.palette.data(),mask.palette.data(),colour.count*sizeof(colour.palette[0]))&&
        !std::memcmp(colour.visual.finalWrist,mask.visual.finalWrist,sizeof(colour.visual.finalWrist)),"actual SetFrame PrepareDraw and Commit keep color/mask palettes and firearm wrists bit-identical across expiry");
    Check(stableTorsoFrames>stableBefore,"upper-body stabilization uses same admitted time as arm and grip solve");
    VisualFrame simulation;Check(!SnapshotVisual(simulation),"render-only retention cannot extend old pose into simulation SnapshotVisual");
    ControllerAim aim;Check(SnapshotControllerAim(1,aim)&&aim.tick==Fixture::Clock(),"simulation controller aiming still reads separately fresh real IPC input");
    Check(renderPair.neutralStanding,"admitted stationary policy remains immutable through newer simulation movement input");
    Check(RenderPairCaptureReady()&&renderPairSecondEye==1&&renderPairReused>=2,"normal100ms crossing remains publishable with live admission counters");
    const auto oldPair=renderPair.pair;++RenderPairFixture::epoch;RenderPairFixture::eye=0;PreparedDraw next;
    publish(oldPair+1,.8f,1);Check(prepare(next)&&next.visual.sourcePoseTick==Fixture::Clock()&&SnapshotVisual(simulation),"new physical pair freshly admits new controller data and restores fresh simulation pose");
    Check(!renderPair.neutralStanding,"next physical pair observes current moving input without extending old stance");
    InvalidateSupportGrip();publish(oldPair+1,.8f,1);
    Check(!RenderPairCaptureReady()&&!renderPair.valid,"explicit support invalidation retires already admitted pair and marks incomplete image");
    ClearFrame();Check(!RenderPairCaptureReady(),"ClearFrame before capture does not erase sticky rejected image");
    RetireRenderPairImage();Check(RenderPairCaptureReady()&&renderPair.blocked,"image retirement clears readiness only, never resurrecting pair authority");
    publish(oldPair+1,.8f,1);Check(!RenderPairCaptureReady(),"retry of tombstoned pair remains rejected until explicit pair restart");
    ++RenderPairFixture::epoch;RenderPairFixture::eye=0;publish(oldPair+2,.8f,1);
    Check(RenderPairCaptureReady()&&renderPair.valid,"fresh new pair after restart recovers without an authority deadlock");
    const auto expiredPair=renderPair.pair;++RenderPairFixture::epoch;RenderPairFixture::eye=1;publish(expiredPair,.8f,251);
    Check(!RenderPairCaptureReady()&&!renderPair.valid&&renderPairExpired==1,"second eye past250ms cannot publish native eye after tracked first eye");
    ++RenderPairFixture::epoch;RenderPairFixture::eye=0;publish(expiredPair+1,.8f,1);
    Check(RenderPairCaptureReady()&&renderPair.valid,"expired pair recovery requires and admits a new physical pair");
    Put(item+0x58,9u);publish(expiredPair+1,.8f,1);Check(!RenderPairCaptureReady(),"native held reference replacement invalidates retained pair");Put(item+0x58,8u);
    ++RenderPairFixture::epoch;publish(expiredPair+2,.8f,1);Check(RenderPairCaptureReady(),"new pair can recover current owned weapon after identity retirement");
    sample.mounted=0;publish(expiredPair+2,.8f,1);Check(!RenderPairCaptureReady(),"explicit host unmount cannot retain admitted render authority");sample.mounted=1;
    ++RenderPairFixture::epoch;publish(expiredPair+3,.8f,1);Check(RenderPairCaptureReady(),"new mounted pair freshly recovers");
    RenderPairFixture::context=false;publish(expiredPair+3,.8f,1);Check(!RenderPairCaptureReady(),"foreign camera context cannot borrow admitted hands");
    RenderPairFixture::context=true;RenderPairFixture::eye=0;++RenderPairFixture::epoch;Put(player+0x96C,0.f);
    publish(expiredPair+4,.8f,1);
    Check(RenderPairCaptureReady()&&renderPair.neutralStanding,"fresh valid image refreshes stationary policy after input returns to zero");
    actionFixture.movement=image+0x84000;publish(expiredPair+4,.8f,1);
    Check(!RenderPairCaptureReady()&&!renderPair.valid,"replacement native movement cannot retain old stance policy or render authority");
    Check(!std::memcmp(nativePalette.data(),reinterpret_cast<void*>(cache+0x90),nativePalette.size()),"all render admission paths leave native source palette byte-identical");
    // Diagnostic sampler exercises the actual CPU staging presented by the
    // owned binder. No D3D device exists and no getter/readback can be called.
    const auto state=image+0x85000;const auto staging=reinterpret_cast<std::uintptr_t>(gpuPalette.data());
    std::memcpy(gpuPalette.data(),colour.palette.data(),colour.count*48);
    NativeSkinMath::PaletteSource source;source.palette=cache+0x90;source.root=root;
    paletteLease={image,state,entry,geometry,player,model,cache,staging,source,colour.count,1,true};
    Put(state+0x3BC,staging);Put(state+0x8C,colour.renderRoot);
    boundShadow=false;boundPair=renderPair.pair;boundGeneration=frameGeneration;
    techniqueObservation.pass=1;skinPassPairPending={};skinPassColours=skinPassCompared=skinPassDifferent=skinPassSourceChanged=skinPassRejected=0;
    SetSkinPassPairDiagnostics(false);SetDrawContextDiagnostics(false);ObserveSkinPassPair();
    Check(skinPassColours==0&&Fixture::cameraCalls==0,"disabled sampler performs no camera getter or sample work");
    SetSkinPassPairDiagnostics(true);ObserveSkinPassPair();
    Check(skinPassColours==1&&Fixture::cameraCalls==1,"enabled sampler captures one exact owned normal colour lease even with visual diagnostics disabled");
    ObserveSkinPassPair();Check(skinPassColours==1&&Fixture::cameraCalls==1,"repeated colour material is not resampled");
    ++frameGeneration;boundGeneration=frameGeneration;techniqueObservation.pass=9;
    gpuPalette[3][11]+=.125f;auto movedWorld=colour.renderRoot;movedWorld.position.x+=1;Put(state+0x8C,movedWorld);
    ObserveSkinPassPair();auto evidence=SnapshotSkinPassPair();
    Check(evidence.compared==1&&evidence.different==1&&evidence.last.valid&&evidence.last.sourceEqual,
        "same epoch color/mask differences are recorded independently of equal native source bytes");
    Check(!evidence.last.paletteEqual&&!evidence.last.worldEqual&&evidence.last.maxPaletteDelta==.125f&&evidence.last.maxWorldDelta==1,
        "sampler reports exact palette and WORLD deltas from consumed staging");
    Check(evidence.last.firstGeneration+1==evidence.last.lastGeneration&&evidence.last.epoch==RenderPairFixture::epoch,
        "comparison retains two hands generations within the actual same image epoch");
    ObserveSkinPassPair();Check(skinPassCompared==1,"mask is compared only once per selected image");
    Fixture::tick+=501;++RenderPairFixture::epoch;techniqueObservation.pass=1;ObserveSkinPassPair();
    Check(skinPassColours==2,"500ms gate admits a new image colour sample");
    techniqueObservation.pass=9;Put(cache+0x90+11*4,1.f);ObserveSkinPassPair();evidence=SnapshotSkinPassPair();
    Check(evidence.compared==2&&evidence.sourceChanged==1&&!evidence.last.sourceEqual,
        "changed native palette is explicitly distinguished from a VR-only mismatch");
    Check(evidence.lastDifferent.valid&&evidence.lastDifferent.maxWorldDelta==1&&evidence.lastDifferent.epoch+1==evidence.last.epoch,"rare mismatch remains retained after later matching comparison");
    std::memcpy(reinterpret_cast<void*>(cache+0x90),nativePalette.data(),nativePalette.size());
    Fixture::tick+=501;++RenderPairFixture::epoch;techniqueObservation.pass=1;ObserveSkinPassPair();
    ++RenderPairFixture::epoch;techniqueObservation.pass=9;ObserveSkinPassPair();
    Check(skinPassCompared==2,"a different renderer image epoch cannot be mixed into the pending colour");
    Fixture::tick+=501;techniqueObservation.pass=1;Fixture::cameraAllowed=false;ObserveSkinPassPair();
    Check(skinPassRejected==1&&skinPassColours==3,"foreign camera rejects sampling without recording a colour lease");
    Fixture::cameraAllowed=true;SetSkinPassPairDiagnostics(false);const auto calls=Fixture::cameraCalls;ObserveSkinPassPair();
    Check(Fixture::cameraCalls==calls,"disabled sampler remains inert after previous valid samples");
    paletteLease={};
    ConfigureRenderContext(nullptr);producer.Close();consumer.Close();installed=false;VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("KL1 real render pair admission: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
