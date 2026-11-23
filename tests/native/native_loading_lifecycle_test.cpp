#include "native_loading_lifecycle.hpp"
#include "controller_input.hpp"
#include "mono_frame_policy.hpp"
#include "gpu_frame_ipc.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void Lifecycle() {
    NativeLoadingLifecycle::State state;
    Check(!state.Active(false)&&!state.AwaitingWorld(),"ordinary untracked or failed gameplay never creates loading eligibility");
    Check(!state.ObserveOwnedCamera(false)&&!state.Active(false),"normal owned world packets do not create a loading transition");
    state.ObserveMenu(true);
    Check(state.AwaitingWorld()&&!state.Active(true),"verified native menu arms a future loading transition while retaining the menu itself");
    state.ObserveMenu(false);
    Check(state.Active(false),"a registered standing player does not prematurely finish native loading");
    for(unsigned i=0;i<1000;++i)state.ObserveMenu(false);
    Check(state.Active(false)&&state.Entries()==1,"loading remains eligible without any world-camera packets, independent of time or tracked frame failures");
    Check(!state.Active(false,true)&&state.AwaitingWorld(),"explicit native cinematics retain their configured presentation rather than inheriting loading");
    Check(!state.ObserveOwnedCamera(true)&&state.AwaitingWorld(),"a queued menu background camera cannot retire a still-active menu transition");
    Check(state.ObserveOwnedCamera(false)&&!state.Active(false)&&state.Resumes()==1,
          "the first exact owned world camera retires loading before the provider's VR/panel gate");
    state.ObserveMenu(false);
    Check(!state.Active(false),"later broken gameplay frames cannot reopen loading without another real native UI transition");
    state.ObserveMenu(true);state.Reset();
    Check(!state.Active(false)&&state.Entries()==2,"explicit session reset clears pending loading without erasing diagnostic history");
}
void Presentation() {
    Check(VRIPC::kInputLoading==7&&VRIPC::kContextFrame==4&&VRIPC::kVersion==16,"loading consumes existing enum space without moving IPC fields or pixel layout");
    Check(MonoFrame::LoadingProof(VRIPC::kInputLoading,VRIPC::kContextFrame),"only loading context plus native loading image authorizes its menu-style panel");
    for(const LONG context:{VRIPC::kInputUnknown,VRIPC::kInputMenu,VRIPC::kInputGame,VRIPC::kInputSuspended,VRIPC::kInputCinematic})
        Check(!MonoFrame::LoadingProof(context,VRIPC::kContextFrame),"old context images never become full-view gameplay or cinematics");
    for(const auto kind:{VRIPC::kSceneFrame,VRIPC::kUiFrame,VRIPC::kScopeFrame})
        Check(!MonoFrame::LoadingProof(VRIPC::kInputLoading,kind),"loading never interprets stale scene, ordinary panel or sniper frames as its image");
    for(const LONG context:{VRIPC::kInputLoading,VRIPC::kInputUnknown,VRIPC::kInputGame,VRIPC::kInputMenu,VRIPC::kInputCinematic})
        Check(MonoFrame::HoldLoading(true,context,true),"the committed loading panel stays visible until a successor uploads");
    Check(!MonoFrame::HoldLoading(false,VRIPC::kInputLoading,true)&&
          !MonoFrame::HoldLoading(true,VRIPC::kInputSuspended,true)&&
          !MonoFrame::HoldLoading(true,VRIPC::kInputLoading,false),
          "startup without a panel, suspension and stopped capture cannot show an old loading image");
    Check(MonoFrame::HoldScene(true,VRIPC::kInputLoading,true)&&
          !MonoFrame::HoldScene(false,VRIPC::kInputLoading,true),
          "a previously uploaded stereo layer bridges loading until the first loading image");
    Check(!MonoFrame::HoldScene(true,VRIPC::kInputMenu,true)&&
          !MonoFrame::HoldScene(true,VRIPC::kInputSuspended,true)&&
          !MonoFrame::HoldScene(true,VRIPC::kInputLoading,false),
          "native menu, suspension and stopped capture retire the stereo hold");
    using MonoFrame::ScopeExitProof;
    Check(ScopeExitProof(VRIPC::kInputGame,true,0,false,VRIPC::kScopeFrame)&&
          ScopeExitProof(VRIPC::kInputGame,true,0,false,VRIPC::kSceneFrame),
          "only a fresh native scope exit into gameplay can bridge the presentation gap");
    for(const LONG context:{VRIPC::kInputUnknown,VRIPC::kInputMenu,VRIPC::kInputLoading,VRIPC::kInputSuspended,VRIPC::kInputCinematic})
        Check(!ScopeExitProof(context,true,0,false,VRIPC::kScopeFrame),"other contexts clear the old optic immediately");
    Check(!ScopeExitProof(VRIPC::kInputGame,false,0,false,VRIPC::kScopeFrame)&&
          !ScopeExitProof(VRIPC::kInputGame,true,0,true,VRIPC::kScopeFrame)&&
          !ScopeExitProof(VRIPC::kInputGame,true,16,false,VRIPC::kScopeFrame)&&
          !ScopeExitProof(VRIPC::kInputGame,true,8,false,VRIPC::kScopeFrame)&&
          !ScopeExitProof(VRIPC::kInputGame,true,0,false,VRIPC::kUiFrame),
          "re-entry, invalid proof and foreign images cannot open a scope exit hold");
    MonoFrame::ScopeExitGrace grace;
    Check(!grace.Hold(true,0,1000)&&!grace.Hold(true,800,1000+51),"missing and expired scope images are unavailable");
    Check(grace.Hold(true,980,1000)&&grace.Hold(true,980,1024)&&grace.Hold(true,980,1047),
          "a committed scope bridges the two pending stereo eyes without changing its projection");
    Check(!grace.Hold(true,980,1048)&&!grace.Hold(true,980,1050),"the same exit cannot accumulate repeated time allowances");
    grace.Reset();
    Check(grace.Hold(true,1050,1051)&&!grace.Hold(false,1050,1052),"replacement and suspension retire the hold state");
    grace.Reset();
    Check(grace.Hold(true,0xffffffe0u,0xfffffff0u)&&!grace.Hold(true,0xffffffe0u,32),"scope grace handles system tick wraparound");
    VRIPC::EyeMetadata frame{};frame.width=3840;frame.height=2160;frame.stride=15360;frame.frameId=2;frame.poseValid=VRIPC::kContextFrame;
    Check(MonoFrame::ValidMetadata(frame,false,VRIPC::kCPU)&&!MonoFrame::ValidMetadata(frame,true,VRIPC::kCPU),
          "native loading retains full source pixels and can never masquerade as a tracked stereo pair");
    VRIPC::SharedFrames shared{};
    shared.magic=VRIPC::kMagic;shared.version=VRIPC::kVersion;shared.bytes=sizeof(shared);shared.gamePid=71;
    frame.frameId=4;frame.tick=110;
    for(auto& eye:shared.eyes){
        eye.sequence=2;eye.transport=VRIPC::kGPU;eye.transportEpoch=3;eye.gpuSlot=1;eye.metadata=frame;
    }
    VRIPC::EyeMetadata announced{};
    Check(GPUFrame::ReadPanelAnnouncement(shared,71,3,announced),
          "a native loading frame shared by both eyes enters the GPU panel queue");
    GPUFrame::PairCandidate slots[GPUFrame::SlotCount]{};
    slots[0]={true,3,frame};slots[0].metadata.frameId=3;slots[0].metadata.tick=106;
    slots[1]={false,3,frame};
    unsigned chosen=99;
    Check(GPUFrame::LatestPanel(slots,2,100,3,announced,chosen)&&chosen==0,
          "the compositor can read a completed loading frame while the newest GPU copy is still pending");
    slots[0].metadata.poseValid=VRIPC::kUiFrame;
    Check(!GPUFrame::LatestPanel(slots,2,100,3,announced,chosen),
          "the loading queue cannot substitute a previous menu image");
    shared.eyes[1].metadata.poseValid=VRIPC::kSceneFrame;
    Check(!GPUFrame::ReadPanelAnnouncement(shared,71,3,announced),
          "one eye entering a new mission withdraws the old loading announcement");
    const float fov[2][4]={{1.1f,.9f,1.2f,1},{.9f,1.1f,1.1f,1.2f}};MonoFrame::Extent full;
    Check(MonoFrame::ScopeExtent(fov,3840,2160,2,full)&&full.width>=4.4f&&full.height>=4.8f,
          "sniper scope still deliberately covers both asymmetric fields");
    MonoFrame::Extent panel;
    Check(MonoFrame::PanelExtent(3840,2160,3.6f,2,panel)&&std::fabs(panel.width-3.6f)<1e-5f&&
          std::fabs(panel.height-2.025f)<1e-5f&&panel.height<full.height,
          "loading captions retain the complete 16:9 image inside the menu panel rather than cropped scope overscan");
    Check(MonoFrame::PanelExtent(1280,720,3.6f,2,panel)&&std::fabs(panel.height-2.025f)<1e-5f,
          "Bink and native 4K cinema retain the same aspect without vertical compression");
    Check(MonoFrame::PanelExtent(1600,1200,3.6f,2,panel)&&std::fabs(panel.height-2.7f)<1e-5f,
          "a native 4:3 panel is not stretched to 16:9");
    Check(!MonoFrame::PanelExtent(0,2160,3.6f,2,panel)&&!MonoFrame::PanelExtent(3840,0,3.6f,2,panel),
          "invalid source dimensions cannot authorize a panel");
}
void InputRouting() {
    using namespace ControllerInput;
    Check(MonoFrame::InputContext(true,true,false,true,true)==VRIPC::kInputMenu,
          "a paused authored VR cinematic selects menu navigation even while its native camera/action remains active");
    Check(MonoFrame::InputContext(true,false,false,true,true)==VRIPC::kInputCinematic,
          "resuming an authored shot restores isolated cinematic bindings");
    Check(MonoFrame::InputContext(false,true,true,true,true)==VRIPC::kInputSuspended&&
          MonoFrame::InputContext(true,false,true,false,false)==VRIPC::kInputLoading,
          "host suspension and accepted loading routing remain distinct from pause");
    Settings settings;settings.immersiveLayout=true;settings.movementHeadRelative=false;settings.turnMode=TurnMode::Smooth;
    {
        Settings menuSettings=settings;menuSettings.menuWasd=false;menuSettings.menuBothSticks=true;menuSettings.menuMouse=false;
        Mapper paused;Input input;paused.Step(input,menuSettings,Context::Cinematic,100);
        input.buttons=Menu;auto result=paused.Step(input,menuSettings,Context::Cinematic,117);
        Check(result.keys[ControllerInput::Escape],"cinematic pause opens the native menu");
        input={};paused.Step(input,menuSettings,Context::Menu,150);input.rightY=-1;
        result=paused.Step(input,menuSettings,Context::Menu,167);
        Check(result.keys[Down]&&!result.leftMouse&&!result.turnYawDelta,"paused cinematic now uses real right-stick menu navigation without fire or turn");
        input={};paused.Step(input,menuSettings,Context::Menu,200);input.rightTrigger=1;
        result=paused.Step(input,menuSettings,Context::Menu,217);
        Check(result.keys[Enter]&&!result.leftMouse,"paused cinematic trigger confirms the menu choice");
    }
    Mapper mapper;Input neutral;mapper.Step(neutral,settings,Context::Loading,0);
    Input pressed;pressed.buttons=A;auto out=mapper.Step(pressed,settings,Context::Loading,100);
    Check(out.keys[Enter]&&!out.keys[Space],"loading A sends native Enter and never also Space");
    out=mapper.Step(pressed,settings,Context::Loading,450);
    Check(out.keys[Enter],"a held loading confirmation repeats native input edges if the reader was busy during its first pulse");
    mapper.Step(neutral,settings,Context::Loading,500);pressed={};pressed.buttons=X;
    out=mapper.Step(pressed,settings,Context::Loading,600);
    Check(out.keys[Space]&&!out.keys[Enter],"loading X independently sends native Space");
    mapper.Step(neutral,settings,Context::Loading,700);pressed={};pressed.rightTrigger=1;pressed.leftY=1;pressed.rightX=1;
    out=mapper.Step(pressed,settings,Context::Loading,100000);
    Check(out.keys[Enter]&&!out.leftMouse&&!out.rightMouse&&!out.middleMouse&&!out.keys['W']&&!out.keys['G']&&
          !out.keys['E']&&!out.keys[Space]&&!out.turnYawDelta&&!out.mouseX&&!out.mouseY,
          "late loading recovery accepts right-trigger Enter without tracking freshness, movement, fire, throw or camera turning");
    mapper.Reset();mapper.Step(neutral,settings,Context::Game,0);
    pressed={};pressed.rightTrigger=1;pressed.leftY=1;
    out=mapper.Step(pressed,settings,Context::Game,100);
    Check(out.leftMouse&&out.keys['W'],"input fixture establishes actual gameplay fire and movement");
    out=mapper.Step(pressed,settings,Context::Loading,116);
    Check(!out.leftMouse&&!out.keys['W']&&!out.keys[Enter],"entering loading releases gameplay and consumes already held confirmation");
    mapper.Step(neutral,settings,Context::Loading,132);out=mapper.Step(pressed,settings,Context::Loading,148);
    Check(out.keys[Enter]&&!out.leftMouse,"release then squeeze confirms loading without shooting");
    out=mapper.Step(pressed,settings,Context::Game,164);
    Check(!out.leftMouse&&!out.keys['W']&&!out.keys[Enter],"return to real stereo gameplay requires fresh release and cannot inherit loading confirm as fire");
    mapper.Step(neutral,settings,Context::Game,180);out=mapper.Step(pressed,settings,Context::Game,196);
    Check(out.leftMouse&&out.keys['W'],"fresh post-loading input restores normal gameplay");
}
}
int main(){Lifecycle();Presentation();InputRouting();std::printf("Native loading lifecycle/input/full-view policy: %u checks PASS.\n",checks);}
