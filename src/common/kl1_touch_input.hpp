#pragma once
#include "team_command_ipc.hpp"
#include "vehicle_control_ipc.hpp"
// ---- Touch controller to keyboard/mouse translation (SendInput) ----
struct Controls : ControllerInput::Settings {
    bool enabled=true;
    // A controller action may restore the game window's focus; K&L stops
    // rendering entirely while unfocused, so this matters more than in most.
    bool focusOnAction=true;
};

bool SendKey(WORD vk,bool down) {
    INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=vk;
    // Old DirectInput titles read scancodes and ignore virtual-key events
    // (Kane & Lynch imports DINPUT8.dll like the Hitman titles).
    const UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC_EX);
    if(scan) {
        input.ki.wVk=0;input.ki.wScan=static_cast<WORD>(scan&0xff);
        input.ki.dwFlags|=KEYEVENTF_SCANCODE;
        if(ControllerInput::ExtendedScan(vk,scan))input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
    }
    if(!down)input.ki.dwFlags|=KEYEVENTF_KEYUP;
    return SendInput(1,&input,sizeof(INPUT))==1;
}
bool SendMouseButton(DWORD downFlag,DWORD upFlag,bool down) {
    INPUT input{};input.type=INPUT_MOUSE;
    input.mi.dwFlags=down?downFlag:upFlag;
    return SendInput(1,&input,sizeof(INPUT))==1;
}

bool ReadMovementYaw(VRIPC::SharedFrames* shared,DWORD now,float& yaw,bool* lookLocked=nullptr,bool* nativeMovementAxes=nullptr,bool* nativeCarried=nullptr,bool* nativeMounted=nullptr) {
    if(lookLocked)*lookLocked=false;
    if(nativeMovementAxes)*nativeMovementAxes=false;
    if(nativeCarried)*nativeCarried=false;
    if(nativeMounted)*nativeMounted=false;
    const LONG before=InterlockedCompareExchange(&shared->movementSequence,0,0);
    if(before==0||(before&1)!=0)return false;
    const DWORD tick=static_cast<DWORD>(InterlockedCompareExchange(&shared->movementTick,0,0));
    const float value=shared->movementYaw;
    const auto flags=shared->reservedV6[0];
    const bool locked=(flags&1u)!=0,nativeAxes=(flags&2u)!=0;
    MemoryBarrier();
    const LONG after=InterlockedCompareExchange(&shared->movementSequence,0,0);
    if(before!=after||(after&1)!=0||tick==0||now-tick>250||!std::isfinite(value))return false;
    yaw=value;if(lookLocked)*lookLocked=locked;if(nativeMovementAxes)*nativeMovementAxes=nativeAxes;
    if(nativeCarried)*nativeCarried=(flags&4u)!=0;if(nativeMounted)*nativeMounted=(flags&12u)==12u;return true;
}

struct GameWindowSearch {
    DWORD pid=0;
    HWND window=nullptr;
    std::uint64_t area=0;
};
BOOL CALLBACK FindGameWindow(HWND window,LPARAM argument) {
    auto& search=*reinterpret_cast<GameWindowSearch*>(argument);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=search.pid||!IsWindowVisible(window)||GetWindow(window,GW_OWNER)!=nullptr||
       (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0)return TRUE;
    RECT client{};
    if(!GetClientRect(window,&client))return TRUE;
    const auto width=std::max<LONG>(0,client.right-client.left),height=std::max<LONG>(0,client.bottom-client.top);
    const auto area=static_cast<std::uint64_t>(width)*height;
    if(area>search.area){search.window=window;search.area=area;}
    return TRUE;
}
bool FocusGameWindow(DWORD gamePid) {
    GameWindowSearch search;search.pid=gamePid;
    EnumWindows(&FindGameWindow,reinterpret_cast<LPARAM>(&search));
    if(!search.window)return false;
    // Create the host thread's message queue before attaching input queues.
    // No keyboard or mouse events are synthesized to bypass foreground rules.
    MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
    const DWORD thread=GetCurrentThreadId();
    const DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    const DWORD gameThread=GetWindowThreadProcessId(search.window,nullptr);
    const bool attachedForeground=foregroundThread&&foregroundThread!=thread&&
        AttachThreadInput(thread,foregroundThread,TRUE)!=FALSE;
    const bool attachedGame=gameThread&&gameThread!=thread&&gameThread!=foregroundThread&&
        AttachThreadInput(thread,gameThread,TRUE)!=FALSE;
    if(IsIconic(search.window))ShowWindow(search.window,SW_RESTORE);
    BringWindowToTop(search.window);
    SetForegroundWindow(search.window);
    SetActiveWindow(search.window);
    SetFocus(search.window);
    if(attachedGame)AttachThreadInput(thread,gameThread,FALSE);
    if(attachedForeground)AttachThreadInput(thread,foregroundThread,FALSE);
    DWORD focused=0;GetWindowThreadProcessId(GetForegroundWindow(),&focused);
    return focused==gamePid;
}

class TouchInput {
public:
    ~TouchInput(){ReleaseAll(true);CloseVehicleControls();}
    bool GameplayHudHeld()const{return gameplayHudHeld_;}
    bool TeamHudHeld()const{return teamHudHeld_;}
    bool GrenadeVisualHeld(DWORD now)const{return grenadeVisualUntil_&&static_cast<LONG>(grenadeVisualUntil_-now)>0;}
    bool MountedStickActive()const{return mountedStick_;}
    DWORD ManualReloadTick(DWORD now)const{return manualReloadTick_&&static_cast<LONG>(now-manualReloadTick_)>=0&&now-manualReloadTick_<=VRIPC::ManualReloadLifetimeMs?manualReloadTick_:0;}
    bool WeaponMenuHeld()const{return weaponMenuHeld_;}
    bool ClimbHeld()const{return climbHeld_;}
    DWORD TeamNextTick()const{return teamNavigationAfter_;}
    void Poll(const Renderer& renderer,const Controls& controls,DWORD now,
              Source& source,ControllerInput::Context context,const ovrSessionStatus& status) {
        gamePid_=source.pid;
        DWORD foregroundPid=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        const bool active=controls.enabled&&renderer.getInput&&
            status.IsVisible&&status.HmdPresent&&status.HmdMounted;
        if(!active){Suspend();focusActivity_=false;lastPoll=now;return;}
        if(lastPoll&&now-lastPoll<3)return;
        lastPoll=now;
        // The installed runtime may append fields beyond SDK 1.10.
        struct Storage{ovrInputState value;unsigned char extra[64];} storage{};
        if(OVR_FAILURE(renderer.getInput(renderer.session,ovrControllerType_Touch,&storage.value))){Suspend();focusActivity_=false;return;}
        const ovrInputState& input=storage.value;
        const unsigned connected=input.ControllerType&ovrControllerType_Touch;
        if(!connected){Suspend();focusActivity_=false;return;}
        ControllerInput::Input sample{};
        for(unsigned hand=0;hand<2;++hand) {
            if((connected&(1u<<hand))==0)continue;
            const float x=input.Thumbstick[hand].x,y=input.Thumbstick[hand].y;
            const float trigger=input.IndexTrigger[hand],grip=input.HandTrigger[hand];
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(trigger)||!std::isfinite(grip)||
                std::fabs(x)>1.01f||std::fabs(y)>1.01f||trigger<-.01f||trigger>1.01f||grip<-.01f||grip>1.01f) {
                Suspend();focusActivity_=false;return;
            }
            if(hand==ovrHand_Left) {
                sample.leftX=x;sample.leftY=y;sample.leftTrigger=trigger;sample.leftGrip=grip;
                if(input.Buttons&ovrButton_X)sample.buttons|=ControllerInput::X;
                if(input.Buttons&ovrButton_Y)sample.buttons|=ControllerInput::Y;
                if(input.Buttons&ovrButton_LThumb)sample.buttons|=ControllerInput::LThumb;
                if(input.Buttons&ovrButton_Enter)sample.buttons|=ControllerInput::Menu;
            } else {
                sample.rightX=x;sample.rightY=y;sample.rightTrigger=trigger;sample.rightGrip=grip;
                if(input.Buttons&ovrButton_A)sample.buttons|=ControllerInput::A;
                if(input.Buttons&ovrButton_B)sample.buttons|=ControllerInput::B;
                if(input.Buttons&ovrButton_RThumb)sample.buttons|=ControllerInput::RThumb;
            }
        }
        const bool activity=sample.buttons||std::hypot(sample.leftX,sample.leftY)>controls.deadzone||
            std::hypot(sample.rightX,sample.rightY)>controls.deadzone||
            sample.leftTrigger>controls.trigger||sample.rightTrigger>controls.trigger||
            sample.leftGrip>controls.trigger||sample.rightGrip>controls.trigger;
        const bool focusAction=activity&&!focusActivity_;
        focusActivity_=activity;
        if(foregroundPid!=source.pid) {
            // A controller action may recover the game's window after Dash or
            // the desktop took focus. Consume the whole first action, so it
            // cannot also become fire/confirm when the window becomes active.
            Suspend();
            if(controls.focusOnAction&&focusAction&&(!lastFocusAttempt_||now-lastFocusAttempt_>=1000)) {
                lastFocusAttempt_=now;
                if(FocusGameWindow(source.pid))Log("Game focus restored by controller activity; release controls before playing.");
            }
            return;
        }
        if(context==ControllerInput::Context::Suspended){Suspend();return;}
        if(connected!=controllerMask||context!=previousContext) {
            ReleaseAll();mapper.Reset();controllerMask=connected;previousContext=context;
        }
        bool lookLocked=false;
        LONG gameplayFlags=0;const bool gameplayFresh=MonoFrame::ReadGameplay(*source.shared,now,gameplayFlags);
        sample.firstPersonSkip=gameplayFresh&&(gameplayFlags&512);
        sample.movementYawValid=ReadMovementYaw(source.shared,now,sample.movementYaw,&lookLocked,&sample.nativeMovementAxes,&sample.nativeCarried,&sample.nativeMounted);
        auto desired=mapper.Step(sample,controls,context,now);
        const bool reloadAllowed=context==ControllerInput::Context::Game&&!sample.firstPersonSkip&&
            !desired.teamHudHeld&&!desired.weaponMenuHeld&&!desired.keys['F']&&!desired.keys['G'];
        if(controls.immersiveLayout){
            const auto reload=reloadIntent_.Step(desired.keys['R'],reloadAllowed,
                gameplayFresh&&(gameplayFlags&1024),now);
            manualReloadTick_=reload.tick;
            // A pulse uses one path for its whole lifetime even if readiness
            // changes in the middle: direct IPC or the original keyboard R.
            desired.keys['R']=reload.keyboard;
        }else{
            reloadIntent_.Reset();
            // Legacy layout keeps its native keyboard R path exclusively.
            manualReloadTick_=0;
        }
        mountedStick_=sample.nativeMounted&&context==ControllerInput::Context::Game&&
            (std::fabs(desired.mouseX)>.01f||std::fabs(desired.mouseY)>.01f);
        if(lookLocked&&context==ControllerInput::Context::Game){desired.turnYawDelta=0;desired.mouseX=desired.mouseY=0;}
        HandleVehicleControls(source,desired,now,context,lookLocked);
        const bool wasCancelling=grenadeCancel_.Active();
        desired=grenadeCancel_.Step(desired,now,keys['G']);
        if(desired.keys['G']||desired.grenadeRelease||wasCancelling||grenadeCancel_.Active())grenadeVisualUntil_=now+250;
        if(wasCancelling||grenadeCancel_.Active())mapper.Reset();
        gameplayHudHeld_=desired.gameplayHudHeld;
        teamHudHeld_=desired.teamHudHeld;
        weaponMenuHeld_=desired.weaponMenuHeld;
        climbHeld_=desired.climbHeld;
        if(sample.movementYawValid&&std::isfinite(desired.turnYawDelta)&&desired.turnYawDelta!=0)
            source.turnYaw=std::remainder(source.turnYaw+desired.turnYawDelta,6.2831853072f);
        if(desired.recenter) {
            InterlockedExchange(&source.shared->recenterRequest,1);
            Log(controls.immersiveLayout?"Tracking recenter requested by pressing the right stick click.":
                                        "Tracking recenter requested with both controller stick clicks.");
        }
        mouseX+=desired.mouseX;mouseY+=desired.mouseY;
        if(std::fabs(mouseX)>=1.f||std::fabs(mouseY)>=1.f) {
            INPUT move{};move.type=INPUT_MOUSE;move.mi.dwFlags=MOUSEEVENTF_MOVE;
            move.mi.dx=static_cast<LONG>(std::lround(mouseX));
            move.mi.dy=static_cast<LONG>(std::lround(mouseY));
            if(SendInput(1,&move,sizeof(INPUT))==1) {
                mouseX-=std::lround(mouseX);mouseY-=std::lround(mouseY);
            } else mouseX=mouseY=0;
        }
        // Retire wheel/map/navigation inputs before pressing Escape or any
        // new-context action. In particular MMB-up and WASD-up must precede
        // pause, and no held wheel direction may become gameplay movement.
        for(unsigned vk=0;vk<keys.size();++vk)if(!desired.keys[vk])Key(static_cast<WORD>(vk),false);
        if(!desired.leftMouse)Mouse(0,false);
        if(!desired.rightMouse)Mouse(1,false);
        if(!desired.middleMouse)Mouse(2,false);
        for(unsigned vk=0;vk<keys.size();++vk)if(desired.keys[vk])Key(static_cast<WORD>(vk),true);
        if(desired.middleMouse)Mouse(2,true);
        if(desired.leftMouse)Mouse(0,true);
        if(desired.rightMouse)Mouse(1,true);
        HandleTeamCommands(source,desired,now,context);
    }
private:
    void Suspend() {
        ReleaseAll();mapper.Reset();controllerMask=0;previousContext=ControllerInput::Context::Suspended;
    }
    void ReleaseAll(bool final=false) {
        if(vehicleShared_)VehicleControlIPC::Cancel(*vehicleShared_);
        gameplayHudHeld_=false;
        mountedStick_=false;
        manualReloadTick_=0;
        reloadIntent_.Reset();
        grenadeVisualUntil_=0;
        teamHudHeld_=false;
        weaponMenuHeld_=false;
        climbHeld_=false;
        CancelTeamCommand(GetTickCount());
        teamNavigationAfter_=0;
        if(final)CloseTeamCommands();
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        ControllerInput::Output neutral;
        if(!final&&gamePid_&&foregroundPid==gamePid_)
            neutral=grenadeCancel_.Step(neutral,GetTickCount(),keys['G']);
        else grenadeCancel_.Reset();
        // Never synthesize a cancellation key-down into another application.
        // While our game still owns focus, let E reach native input before
        // retiring an armed G instead of inadvertently throwing on UI entry.
        for(unsigned vk=0;vk<keys.size();++vk)if(!neutral.keys[vk])Key(static_cast<WORD>(vk),false);
        for(unsigned vk=0;vk<keys.size();++vk)if(neutral.keys[vk])Key(static_cast<WORD>(vk),true);
        for(unsigned button=0;button<3;++button)Mouse(button,false);
        mouseX=mouseY=0;
    }
    void Key(WORD vk,bool down) {
        const auto slot=static_cast<unsigned>(vk&0xff);
        if(slot<keys.size()&&keys[slot]==down)return;
        if(SendKey(vk,down)&&slot<keys.size())keys[slot]=down;
    }
    void CloseVehicleControls(){
        if(vehicleShared_){VehicleControlIPC::Cancel(*vehicleShared_);UnmapViewOfFile(vehicleShared_);vehicleShared_=nullptr;}
        if(vehicleMap_){CloseHandle(vehicleMap_);vehicleMap_=nullptr;}vehiclePid_=0;
    }
    bool OpenVehicleControls(DWORD pid,DWORD now){
        if(vehicleShared_&&vehiclePid_==pid)return VehicleControlIPC::Header(*vehicleShared_,pid);
        if(lastVehicleOpen_&&now-lastVehicleOpen_<500)return false;lastVehicleOpen_=now;CloseVehicleControls();
        wchar_t name[100];VehicleControlIPC::MappingName(name,100,pid);
        vehicleMap_=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!vehicleMap_)return false;
        vehicleShared_=static_cast<VehicleControlIPC::Shared*>(MapViewOfFile(vehicleMap_,FILE_MAP_ALL_ACCESS,0,0,sizeof(VehicleControlIPC::Shared)));
        if(!vehicleShared_||!VehicleControlIPC::Header(*vehicleShared_,pid)){CloseVehicleControls();return false;}vehiclePid_=pid;return true;
    }
    void HandleVehicleControls(Source& source,ControllerInput::Output& desired,DWORD now,ControllerInput::Context context,bool lookLocked){
        if(!OpenVehicleControls(source.pid,now))return;
        const auto ack=static_cast<std::uint32_t>(InterlockedCompareExchange(&source.shared->recenterAck,0,0));
        const VehicleControlIPC::Intent intent{context==ControllerInput::Context::Game&&!lookLocked&&!desired.teamHudHeld&&!desired.weaponMenuHeld&&!desired.recenter,
            desired.mountedControl,desired.passengerControl,desired.passengerCrouched,desired.mountedX,desired.mountedY,desired.passengerX,desired.passengerY};
        if(VehicleControlIPC::Dispatch(*vehicleShared_,source.pid,now,ack,intent,desired.mouseX,desired.mouseY)==VehicleControlIPC::Route::Passenger){
            for(unsigned key:{unsigned('W'),unsigned('A'),unsigned('S'),unsigned('D'),unsigned(ControllerInput::Control)})desired.keys[key]=false;
        }
    }
    void Mouse(unsigned button,bool active) {
        if(button>=3)return;
        bool& previous=mouseButtons[button];
        if(previous==active)return;
        constexpr DWORD down[3]={MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_MIDDLEDOWN};
        constexpr DWORD up[3]={MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP};
        if(SendMouseButton(down[button],up[button],active))previous=active;
    }
    bool OpenTeamCommands(DWORD pid,DWORD now) {
        if(teamShared_&&teamPid_==pid)return TeamCommandIPC::Header(*teamShared_,pid);
        if(teamPid_!=pid)CloseTeamCommands();
        if(lastTeamOpenAttempt_&&now-lastTeamOpenAttempt_<500)return false;
        lastTeamOpenAttempt_=now;teamPid_=pid;
        wchar_t name[100];TeamCommandIPC::MappingName(name,100,pid);
        teamMap_=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
        if(!teamMap_)return false;
        teamShared_=static_cast<TeamCommandIPC::Shared*>(MapViewOfFile(teamMap_,FILE_MAP_ALL_ACCESS,0,0,sizeof(TeamCommandIPC::Shared)));
        if(!teamShared_||!TeamCommandIPC::Header(*teamShared_,pid)){CloseTeamCommands();return false;}
        TeamCommandIPC::Request previous;
        if(TeamCommandIPC::Read(teamShared_->request,previous))teamSerial_=previous.serial;
        return true;
    }
    void CancelTeamCommand(DWORD now) {
        if(teamShared_&&teamRequestLive_) {
            TeamCommandIPC::CancelCommand(*teamShared_,teamPid_,now,teamSerial_);
        }
        teamRequestLive_=false;
    }
    void CloseTeamCommands() {
        CancelTeamCommand(GetTickCount());
        if(teamShared_)UnmapViewOfFile(teamShared_);
        if(teamMap_)CloseHandle(teamMap_);
        teamShared_=nullptr;teamMap_=nullptr;teamPid_=0;teamSerial_=0;lastTeamOpenAttempt_=0;
    }
    void HandleTeamCommands(Source& source,const ControllerInput::Output& desired,DWORD now,ControllerInput::Context context) {
        if(!desired.teamHudHeld||context!=ControllerInput::Context::Game) {
            CancelTeamCommand(now);teamNavigationAfter_=0;return;
        }
        if(desired.teamNext)teamNavigationAfter_=now;
        if(!TeamCommandIPC::Known(desired.teamCommand)||!OpenTeamCommands(source.pid,now))return;
        const auto recenter=static_cast<std::uint32_t>(InterlockedCompareExchange(&source.shared->recenterAck,0,0));
        if(!TeamCommandIPC::PublishCommand(*teamShared_,source.pid,now,recenter,desired.teamCommand,teamNavigationAfter_,teamSerial_)) {
            Log("Team command skipped: waiting for the selected receiver to appear in the VR panel.");return;
        }
        teamRequestLive_=true;
    }
    ControllerInput::Mapper mapper;
    ControllerInput::NativeGrenadeCancel grenadeCancel_;
    ControllerInput::Context previousContext=ControllerInput::Context::Suspended;
    unsigned controllerMask=0;
    bool focusActivity_=false;
    bool gameplayHudHeld_=false,teamHudHeld_=false,weaponMenuHeld_=false,climbHeld_=false,mountedStick_=false;
    DWORD manualReloadTick_=0;
    ControllerInput::ReloadIntentLatch reloadIntent_;
    DWORD grenadeVisualUntil_=0;
    DWORD lastFocusAttempt_=0;
    DWORD gamePid_=0;
    DWORD lastPoll=0;
    float mouseX=0,mouseY=0;
    bool mouseButtons[3]{};
    std::array<bool,256>keys{};
    HANDLE teamMap_=nullptr;
    TeamCommandIPC::Shared* teamShared_=nullptr;
    DWORD teamPid_=0,lastTeamOpenAttempt_=0,teamNavigationAfter_=0;
    std::uint32_t teamSerial_=0;
    bool teamRequestLive_=false;
    HANDLE vehicleMap_=nullptr;VehicleControlIPC::Shared* vehicleShared_=nullptr;
    DWORD vehiclePid_=0,lastVehicleOpen_=0;
};
