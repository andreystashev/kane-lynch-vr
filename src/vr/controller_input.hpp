#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "team_command_types.hpp"

// Platform-independent Touch mapping. The host supplies only controller data
// and applies the resulting desired key/button state through SendInput.
namespace ControllerInput {
enum class Context { Suspended, Menu, Game, Actions, Inventory, Map, Cinematic, Loading };
enum class TurnMode { Mouse, Snap, Smooth };
enum Button : std::uint32_t {
    A=1u<<0, B=1u<<1, X=1u<<2, Y=1u<<3,
    LThumb=1u<<4, RThumb=1u<<5, Menu=1u<<6
};
enum Key : unsigned {
    Tab=0x09, Enter=0x0d, Shift=0x10, Control=0x11, Escape=0x1b,
    Space=0x20, Left=0x25, Up=0x26, Right=0x27, Down=0x28
};
// Some keyboard layouts return the unprefixed keypad scan for navigation
// keys even with MAPVK_VK_TO_VSC_EX. DirectInput still needs the E0 flag;
// otherwise Up/Down become numpad 8/2 and native menu bindings never fire.
inline bool ExtendedScan(unsigned virtualKey,unsigned mappedScan) {
    return (mappedScan&0xff00u)==0xe000u||
        (virtualKey>=0x21u&&virtualKey<=0x28u)||virtualKey==0x2du||virtualKey==0x2eu;
}
struct Input {
    float leftX=0,leftY=0,rightX=0,rightY=0;
    float leftTrigger=0,rightTrigger=0,leftGrip=0,rightGrip=0;
    std::uint32_t buttons=0;
    // Game publishes the actual horizontal view relative to its input camera.
    // Positive yaw points toward the game's native right direction.
    float movementYaw=0;
    bool movementYawValid=false;
    // Verified native Rappel/Climb owns movement. Its thresholds require the
    // full WASD axis, independent of the ordinary walk/run preference.
    bool nativeMovementAxes=false;
    bool nativeCarried=false;
    bool nativeMounted=false;
    bool firstPersonSkip=false;
};
struct Settings {
    float deadzone=0.35f,trigger=0.55f,mouseSensitivity=14.f;
    float menuThreshold=0.55f,menuMouseSensitivity=10.f;
    bool rightStickPitch=false,menuWasd=true,menuMouse=false,movementHeadRelative=true;
    bool menuXSpace=false;
    bool leftTriggerAim=false,yOpensMenu=false;
    // Direct VR turns are expressed in radians rather than native mouse pixels.
    // The generic profile retains mouse turning; the K&L host selects Smooth.
    TurnMode turnMode=TurnMode::Mouse;
    float snapTurnDegrees=30.f,smoothTurnDegreesPerSecond=90.f;
    bool immersiveLayout=false;
    bool menuBothSticks=false;
    // Foregrip setting belongs to the weapon solver. Immersive menu gestures
    // reserve the right grip for command selection while Y is held.
    bool supportGrip=false;
};
struct Output {
    std::array<bool,256> keys{};
    bool leftMouse=false,rightMouse=false,middleMouse=false,recenter=false;
    bool gameplayHudHeld=false,teamHudHeld=false,weaponMenuHeld=false,climbHeld=false,teamNext=false;
    TeamCommandIPC::Command teamCommand=TeamCommandIPC::Command::None;
    // Only a deliberate release of an already charged right-grip gesture may throw.
    // Other G-up transitions first need the native E cancellation handshake.
    bool grenadeRelease=false;
    float mouseX=0,mouseY=0;
    float turnYawDelta=0;
    bool mountedControl=false,passengerControl=false,passengerCrouched=false;
    float mountedX=0,mountedY=0,passengerX=0,passengerY=0;
};

// Hold native E while G is still down for a full input pulse, then release
// G while keeping E held. Sending E-down/E-up in one poll is invisible to
// the game's state-based DirectInput reader. UI/calibration requests wait
// until cancellation has had that opportunity to run.
class NativeGrenadeCancel {
public:
    Output Step(const Output& requested,std::uint32_t now,bool nativeGDown) {
        if(!active_&&nativeGDown&&!requested.keys['G']&&!requested.grenadeRelease) {
            active_=true;start_=now;escape_=recenter_=false;
        }
        if(active_) {
            escape_=escape_||requested.keys[Escape];recenter_=recenter_||requested.recenter;
            const auto elapsed=now-start_;
            if(elapsed<120) {
                Output result;result.keys['E']=true;result.keys['G']=elapsed<60;return result;
            }
            active_=false;
            if(escape_){escapePending_=true;escapeStart_=now;}
            Output result;result.recenter=recenter_;result.keys[Escape]=escapePending_;
            escape_=recenter_=false;return result;
        }
        Output result=requested;
        if(escapePending_) {
            if(now-escapeStart_<60)result.keys[Escape]=true;
            else escapePending_=false;
        }
        return result;
    }
    bool Active()const{return active_;}
    void Reset(){active_=escape_=recenter_=escapePending_=false;start_=escapeStart_=0;}
private:
    bool active_=false,escape_=false,recenter_=false,escapePending_=false;
    std::uint32_t start_=0,escapeStart_=0;
};

class RepeatingDirection {
public:
    // Deliberate press/release pulses also work in menus using DirectInput
    // state edges, which do not receive Windows keyboard auto-repeat.
    bool Step(int direction,std::uint32_t now) {
        if(!direction){Reset();return false;}
        if(direction!=direction_) {
            direction_=direction;pulseStart_=now;nextPulse_=now+350;
        } else if(static_cast<std::int32_t>(now-nextPulse_)>=0) {
            pulseStart_=now;nextPulse_=now+160;
        }
        return now-pulseStart_<60;
    }
    void Reset(){direction_=0;pulseStart_=nextPulse_=0;}
private:
    int direction_=0;
    std::uint32_t pulseStart_=0,nextPulse_=0;
};

class MenuStick {
public:
    int Step(float lx,float ly,float rx,float ry,bool both,float deadzone,float threshold) {
        if(!both)rx=ry=0;
        const float neutral=std::max(.23f,deadzone);
        if(direction_) {
            const float x=right_?rx:lx,y=right_?ry:ly;
            if(std::isfinite(x)&&std::isfinite(y)&&std::hypot(x,y)>neutral) {
                const float signedAxis=std::abs(direction_)==2?y:x;
                if(signedAxis*direction_<=0||std::fabs(signedAxis)<threshold*.65f)cancelled_=true;
                return cancelled_?0:direction_;
            }
            Reset();
            // An already deflected second stick does not take over a gesture.
            waitNeutral_=std::hypot(lx,ly)>neutral||std::hypot(rx,ry)>neutral;
        }
        if(waitNeutral_) {
            waitNeutral_=std::hypot(lx,ly)>neutral||std::hypot(rx,ry)>neutral;
            return 0;
        }
        const int left=Choose(lx,ly,threshold),right=Choose(rx,ry,threshold);
        right_=right&&(!left||std::max(std::fabs(rx),std::fabs(ry))>std::max(std::fabs(lx),std::fabs(ly)));
        direction_=right_?right:left;
        return direction_;
    }
    void Reset(){direction_=0;right_=cancelled_=waitNeutral_=false;}
private:
    static int Choose(float x,float y,float threshold) {
        if(!std::isfinite(x)||!std::isfinite(y))return 0;
        const float ax=std::fabs(x),ay=std::fabs(y);
        if(ax>=threshold&&ax>1.4f*ay)return x>0?1:-1;
        if(ay>=threshold&&ay>1.4f*ax)return y>0?2:-2;
        return 0;
    }
    int direction_=0;
    bool right_=false,cancelled_=false,waitNeutral_=false;
};

class PressPulse {
public:
    bool Step(bool pressed,std::uint32_t now) {
        if(pressed&&!pressed_){start_=now;running_=true;}
        pressed_=pressed;
        if(running_&&now-start_>=60)running_=false;
        return running_;
    }
    void Reset(){pressed_=running_=false;start_=0;}
private:
    bool pressed_=false,running_=false;
    std::uint32_t start_=0;
};

// Native KL1's wheel acts once per nonzero direction and only clears its
// +0x50 latch after a zero direction. A second excursion in the same direction
// advances to a teammate's outer inventory ring and invokes native exchange.
// Keep each excursion deliberate, including an observable release interval;
// changing the dominant diagonal axis must not accidentally exchange an item.
class InventoryDirection {
public:
    int Step(float x,float y,float deadzone,float threshold,std::uint32_t now) {
        if(!std::isfinite(x)||!std::isfinite(y)){Reset();return 0;}
        if(std::hypot(x,y)<=deadzone) {
            if(direction_){direction_=0;releaseUntil_=now+60;releasing_=true;}
            return 0;
        }
        if(direction_)return direction_;
        if(releasing_&&static_cast<std::int32_t>(now-releaseUntil_)<0)return 0;
        releasing_=false;
        if(std::max(std::fabs(x),std::fabs(y))<threshold)return 0;
        direction_=std::fabs(y)>=std::fabs(x)?(y>0?2:-2):(x>0?1:-1);
        return direction_;
    }
    void Reset(){direction_=0;releaseUntil_=0;releasing_=false;}
private:
    int direction_=0;
    std::uint32_t releaseUntil_=0;
    bool releasing_=false;
};

// A held gesture never also completes its short action, even if the host
// missed every intermediate controller poll between press and release.
class HudButton {
public:
    struct Result {bool tap=false,show=false;};
    Result Step(bool down,std::uint32_t now) {
        if(down&&!down_){start_=now;held_=false;}
        if(down_&&now-start_>=350)held_=true;
        const Result result{down_&&!down&&!held_,down&&held_};down_=down;return result;
    }
    void Reset(){down_=held_=false;start_=0;}
private:
    bool down_=false,held_=false;std::uint32_t start_=0;
};
class ReloadSprintButton {
public:
    struct Result {bool reload=false,sprint=false;};
    Result Step(bool down,bool eligible,std::uint32_t now) {
        if(down&&!down_){start_=now;held_=false;}
        // Duration decides whether the release is a tap; movement only decides
        // whether the already-held button can currently produce sprint.
        if(down_&&now-start_>=350)held_=true;
        const Result result{down_&&!down&&!held_,down&&held_&&eligible};down_=down;return result;
    }
    void Reset(){down_=held_=false;start_=0;}
private:
    bool down_=false,held_=false;std::uint32_t start_=0;
};

// The controller's R-shaped pulse is an intent, not an injected keyboard
// press in immersive mode. One pulse must publish exactly one event even if
// host input is sampled repeatedly before the 60 ms pulse has ended.
class ReloadIntentLatch {
public:
    struct Result {bool keyboard=false;std::uint32_t tick=0;};
    Result Step(bool pulse,bool allowed,bool directReady,std::uint32_t now) {
        if(!allowed)tick_=0;
        if(pulse&&!pulse_){
            direct_=directReady;
            tick_=allowed&&direct_?now:0;
        }
        pulse_=pulse;
        return {allowed&&pulse&&!direct_,tick_};
    }
    void Reset(){pulse_=direct_=false;tick_=0;}
private:
    bool pulse_=false,direct_=false;
    std::uint32_t tick_=0;
};

class TeamCommandButtons {
public:
    void Reset(bool recall=false,bool move=false,bool attack=false){recall_=recall;move_=move;attack_=attack;}
    TeamCommandIPC::Command Step(bool recall,bool move,bool attack,bool allowed) {
        const bool nextRecall=recall&&!recall_,nextMove=move&&!move_,nextAttack=attack&&!attack_;
        recall_=recall;move_=move;attack_=attack;
        // Conflicting new buttons consume the whole chord. No command waits
        // behind a navigation gesture or reappears when its stick centres.
        if(!allowed||unsigned(nextRecall)+unsigned(nextMove)+unsigned(nextAttack)!=1)return TeamCommandIPC::Command::None;
        return nextRecall?TeamCommandIPC::Command::Recall:nextMove?TeamCommandIPC::Command::Move:TeamCommandIPC::Command::Attack;
    }
private:
    bool recall_=false,move_=false,attack_=false;
};

class Mapper {
public:
    Output Step(const Input& raw,const Settings& settings,Context context,
                std::uint32_t now,bool active=true) {
        Output result{};bool grenadeInputOwned=false,actionTap=false,weaponTap=false;
        if(!active||context==Context::Suspended){Reset();return result;}
        const float dt=initialized_?std::min(50.f,float(now-lastTick_)):0.f;
        if(!initialized_||context_!=context) {
            // Holding fire while closing a menu must not fire in the mission;
            // held sticks also need to return to centre after a mode/focus change.
            blockedButtons_=raw.buttons;
            blockedAnalog_=AnalogButtons(raw,settings.trigger);
            leftBlocked_=std::hypot(raw.leftX,raw.leftY)>settings.deadzone;
            rightBlocked_=std::hypot(raw.rightX,raw.rightY)>settings.deadzone;
            menuStick_.Reset();menuRepeat_.Reset();loadingConfirm_.Reset();loadingSkip_.Reset();movementSector_=-1;
            confirm_.Reset();back_.Reset();reload_.Reset();map_.Reset();inventory_.Reset();zoomOut_.Reset();
            hudButton_.Reset();grenadeButton_.Reset();actionTap_.Reset();reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;
            crouchClimb_.Reset();grenadeType_.Reset();grenadeWasCharging_=grenadeCancelled_=grenadeTypeGesture_=false;
            crouched_=crouchPressed_=runPressed_=false;
            recenterConsumed_=false;context_=context;initialized_=true;
            snapArmed_=true;wheelHeld_=false;wheelSelectionBlocked_=false;wheelDirection_=0;
            wheelNavigation_.Reset();
            teamGripHeld_=actionPressed_=false;
            teamCommandButtons_.Reset();
            nativeAxes_=context==Context::Game&&raw.nativeMovementAxes;
        }
        lastTick_=now;
        if(context==Context::Game&&raw.nativeMovementAxes!=nativeAxes_) {
            // A long A hold on the rope must not become ground sprint on
            // landing. Likewise a held run-toggle click belongs to its old mode.
            blockedButtons_|=raw.buttons&(A|B|LThumb);
            crouchClimb_.Reset();
            reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;runPressed_=false;
            nativeAxes_=raw.nativeMovementAxes;
        }
        blockedButtons_&=raw.buttons;
        const auto analog=AnalogButtons(raw,settings.trigger);
        blockedAnalog_&=analog;
        if(std::hypot(raw.leftX,raw.leftY)<=settings.deadzone)leftBlocked_=false;
        if(std::hypot(raw.rightX,raw.rightY)<=settings.deadzone)rightBlocked_=false;
        Input input=raw;
        input.buttons&=~blockedButtons_;
        if(leftBlocked_)input.leftX=input.leftY=0;
        if(rightBlocked_)input.rightX=input.rightY=0;
        const auto available=analog&~blockedAnalog_;
        const bool leftTrigger=(available&1)!=0,rightTrigger=(available&2)!=0;
        const bool leftGrip=(available&4)!=0,rightGrip=(available&8)!=0;
        const auto recenterMask=settings.immersiveLayout?RThumb:(LThumb|RThumb);
        const bool physicalRecenterHeld=(raw.buttons&recenterMask)!=0;
        const bool chord=(input.buttons&recenterMask)==recenterMask;
        if(!physicalRecenterHeld)recenterConsumed_=false;
        result.recenter=chord&&!recenterConsumed_;
        if(result.recenter) {
            recenterConsumed_=true;crouched_=crouchPressed_=runPressed_=false;
            // Changing the tracking origin is a mode change for both sticks
            // and held actions. Do not keep walking/turning or fire a shot
            // against the old origin while the host acknowledges the reset.
            blockedButtons_|=raw.buttons;blockedAnalog_|=analog;
            leftBlocked_=std::hypot(raw.leftX,raw.leftY)>settings.deadzone;
            rightBlocked_=std::hypot(raw.rightX,raw.rightY)>settings.deadzone;
            movementSector_=-1;snapArmed_=false;menuStick_.Reset();menuRepeat_.Reset();
            // Calibration retires overlay ownership as well as its native
            // MMB state. A still-held Y must not reopen the overlay
            // when the calibration click releases; it needs a fresh hold.
            wheelHeld_=wheelSelectionBlocked_=false;wheelDirection_=0;wheelOpenedAt_=0;
            wheelNavigation_.Reset();teamGripHeld_=actionPressed_=false;
            teamCommandButtons_.Reset();
            confirm_.Reset();back_.Reset();reload_.Reset();map_.Reset();inventory_.Reset();zoomOut_.Reset();
            hudButton_.Reset();grenadeButton_.Reset();actionTap_.Reset();reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;
            crouchClimb_.Reset();grenadeType_.Reset();grenadeWasCharging_=grenadeCancelled_=grenadeTypeGesture_=false;
        }
        if(recenterConsumed_&&physicalRecenterHeld) {
            // The physical calibration click owns the entire hold, including
            // new actions pressed after calibration first took effect.
            blockedButtons_|=raw.buttons;blockedAnalog_|=analog;
            leftBlocked_=leftBlocked_||std::hypot(raw.leftX,raw.leftY)>settings.deadzone;
            rightBlocked_=rightBlocked_||std::hypot(raw.rightX,raw.rightY)>settings.deadzone;
            return result;
        }
        const bool menuButton=(input.buttons&Menu)!=0;
        if(context==Context::Loading) {
            // The loader/briefing can stop producing world cameras while the
            // native input reader is still waiting for Enter or Space. These
            // isolated pulses remain available without valid gameplay yaw or
            // fresh scene pixels; they can never become fire/move/throw.
            result.keys[Enter]=loadingConfirm_.Step(((input.buttons&A)!=0||rightTrigger)?1:0,now);
            result.keys[Space]=loadingSkip_.Step((input.buttons&X)!=0?1:0,now);
            return result;
        }
        if(context==Context::Cinematic) {
            result.keys[Enter]=confirm_.Step((input.buttons&(A|X))!=0||rightTrigger,now);
            result.keys[Escape]=back_.Step(menuButton||(input.buttons&B)!=0||
                (settings.immersiveLayout&&(input.buttons&Y)!=0),now);
            return result;
        }
        if(context==Context::Game&&settings.immersiveLayout) {
            const auto closeOverlay=[&] {
                // Consume all other held inputs through the return to gameplay.
                // Preserve a pending Escape pulse so it lasts long enough for
                // DirectInput, even while waiting for native pause to appear.
                blockedButtons_|=raw.buttons;blockedAnalog_|=analog;
                leftBlocked_=std::hypot(raw.leftX,raw.leftY)>settings.deadzone;
                rightBlocked_=std::hypot(raw.rightX,raw.rightY)>settings.deadzone;
                wheelHeld_=false;wheelDirection_=0;
                wheelNavigation_.Reset();
                teamGripHeld_=actionPressed_=false;
                teamCommandButtons_.Reset();
                hudButton_.Reset();grenadeButton_.Reset();actionTap_.Reset();reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;
                crouchClimb_.Reset();grenadeType_.Reset();grenadeWasCharging_=grenadeCancelled_=grenadeTypeGesture_=false;
                inventory_.Reset();reload_.Reset();map_.Reset();confirm_.Reset();
                crouchPressed_=runPressed_=false;
            };
            if(menuButton) {
                closeOverlay();result.keys[Escape]=back_.Step(true,now);return result;
            }
            if(wheelHeld_&&(raw.buttons&Y)==0) {
                closeOverlay();
                result.keys[Control]=crouched_&&!input.nativeMovementAxes&&!input.nativeCarried;
                return result;
            }
            const bool grenadeGesture=rightGrip||grenadeWasCharging_||grenadeCancelled_||grenadeTypeGesture_;
            HudButton::Result weapon{};
            if(!wheelHeld_&&!grenadeGesture) {
                const bool pressed=(input.buttons&X)!=0;
                actionTap=actionPressed_&&!pressed;
                actionPressed_=pressed;
                weapon=hudButton_.Step((input.buttons&Y)!=0,now);
                weaponTap=weapon.tap;
            }
            if(wheelHeld_||weapon.show) {
                if(!wheelHeld_) {
                    closeOverlay();
                    wheelHeld_=true;wheelOpenedAt_=now;
                    wheelSelectionBlocked_=std::hypot(raw.rightX,raw.rightY)>settings.deadzone;
                    // The shared panel owns the grip and command buttons
                    // from the next deliberate press.
                    teamGripHeld_=raw.rightGrip>settings.trigger;
                }
                result.middleMouse=result.weaponMenuHeld=result.teamHudHeld=result.gameplayHudHeld=true;
                // The wheel owns gestures, but the native stance is held
                // through both opening and closing the inventory.
                result.keys[Control]=crouched_&&!input.nativeMovementAxes&&!input.nativeCarried;
                if(std::hypot(raw.rightX,raw.rightY)<=settings.deadzone)wheelSelectionBlocked_=false;
                const bool wheelReady=now-wheelOpenedAt_>=100;
                if(wheelReady&&!wheelSelectionBlocked_) {
                    wheelDirection_=wheelNavigation_.Step(input.rightX,input.rightY,settings.deadzone,settings.menuThreshold,now);
                    result.keys['W']=wheelDirection_==2;result.keys['S']=wheelDirection_==-2;
                    result.keys['A']=wheelDirection_==-1;result.keys['D']=wheelDirection_==1;
                }
                result.teamNext=rightGrip&&!teamGripHeld_;
                teamGripHeld_=raw.rightGrip>settings.trigger;
                result.teamCommand=teamCommandButtons_.Step((input.buttons&A)!=0,(input.buttons&B)!=0,rightTrigger,
                    wheelReady&&!result.teamNext);
                return result;
            }
        }
        if(context==Context::Game) {
            float x=input.leftX,y=input.leftY;
            const bool movementReady=!settings.movementHeadRelative||
                (input.movementYawValid&&std::isfinite(input.movementYaw));
            if(movementReady&&std::hypot(x,y)>settings.deadzone) {
                if(settings.movementHeadRelative) {
                    const float sine=std::sin(input.movementYaw),cosine=std::cos(input.movementYaw);
                    const float bodyX=x*cosine+y*sine;
                    y=y*cosine-x*sine;x=bodyX;
                }
                // Keep the current sector through small changes of the native
                // input basis during body rotation. Crossing a sector boundary
                // must be deliberate, otherwise WASD chatters every frame.
                constexpr float sectorWidth=3.1415926536f/4.f;
                const float angle=std::atan2(x,y);
                if(movementSector_<0||std::fabs(std::remainder(angle-movementSector_*sectorWidth,6.2831853072f))>
                        sectorWidth*.5f+5.f*(3.1415926536f/180.f))
                    movementSector_=(static_cast<int>(std::lround(angle/sectorWidth))+8)%8;
                result.keys['W']=movementSector_==0||movementSector_==1||movementSector_==7;
                result.keys['S']=movementSector_>=3&&movementSector_<=5;
                result.keys['A']=movementSector_>=5&&movementSector_<=7;
                result.keys['D']=movementSector_>=1&&movementSector_<=3;
            } else movementSector_=-1;
            if(settings.immersiveLayout) {
                result.keys['E']=actionTap_.Step(actionTap,now);
                const bool crouch=(input.buttons&B)!=0;
                const auto climb=crouchClimb_.Step(crouch,now);
                result.climbHeld=climb.show&&!rightTrigger&&!input.nativeMovementAxes;
                if(climb.tap)crouched_=!crouched_;
                // Right grip owns grenade preparation. Native G charges,
                // deliberate release throws; Y changes type and X cancels.
                const bool grenadeDown=grenadeWasCharging_?
                    (std::isfinite(raw.rightGrip)&&raw.rightGrip>.20f&&raw.rightGrip<=1.f):rightGrip;
                const auto grenade=grenadeButton_.Step(grenadeDown,now);
                if(grenadeDown&&(input.buttons&Y)!=0&&!grenadeCancelled_)grenadeTypeGesture_=true;
                const bool cancel=grenade.show&&!grenadeCancelled_&&(input.buttons&X)!=0;
                if(cancel)grenadeCancelled_=true;
                const bool charging=grenade.show&&!grenadeCancelled_;
                result.grenadeRelease=grenadeWasCharging_&&!grenadeDown&&!grenadeCancelled_;
                grenadeWasCharging_=charging;
                grenadeInputOwned=grenadeDown||charging||grenadeCancelled_||grenadeTypeGesture_;
                if(grenadeInputOwned) {
                    result.keys['G']=charging;result.keys['E']=cancel;
                    result.keys['Q']=(charging||grenadeTypeGesture_)&&grenadeType_.Step((input.buttons&Y)!=0,now);
                    result.climbHeld=false;
                    blockedButtons_|=raw.buttons&(A|B|X|Y);blockedAnalog_|=analog&3u;
                    hudButton_.Reset();actionTap_.Reset();crouchClimb_.Reset();actionPressed_=false;
                    inventory_.Reset();reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;
                } else {
                    grenadeType_.Reset();
                    result.keys['F']=inventory_.Step(weaponTap,now);
                }
                if(!grenadeDown){grenadeCancelled_=false;grenadeTypeGesture_=false;}
                crouchPressed_=crouch;result.keys[Control]=crouched_&&!input.nativeMovementAxes;
                const bool moving=result.keys['W']||result.keys['A']||result.keys['S']||result.keys['D'];
                const bool run=(input.buttons&LThumb)!=0;
                if(run&&!runPressed_&&!crouched_&&!input.nativeMovementAxes)running_=!running_;
                runPressed_=run;
                result.keys[Shift]=moving&&!running_&&!crouched_&&!input.nativeMovementAxes;
                const bool aDown=!grenadeInputOwned&&(input.buttons&A)!=0;
                if(input.firstPersonSkip&&aDown)skipButtonOwned_=true;
                const auto action=reloadSprint_.Step(aDown&&!skipButtonOwned_,moving&&!crouch&&!input.nativeMovementAxes&&!input.firstPersonSkip,now);
                result.keys['R']=reloadTap_.Step(action.reload&&!skipButtonOwned_,now);
                const bool skipPulse=skipTap_.Step(input.firstPersonSkip&&aDown,now);
                result.keys[Enter]=input.firstPersonSkip&&(aDown||skipPulse);
                if(!aDown)skipButtonOwned_=false;
                if(action.sprint){crouched_=false;result.keys[Control]=false;result.keys[Shift]=false;result.keys[Space]=true;}
            } else {
                result.keys['E']=rightGrip||(input.buttons&A)!=0;result.keys[Shift]=leftGrip;
                if(input.nativeMovementAxes)result.keys[Shift]=false;
                result.keys['R']=reload_.Step((input.buttons&B)!=0,now);
                result.keys['M']=map_.Step((input.buttons&X)!=0,now);
                result.keys['Q']=!settings.yOpensMenu&&inventory_.Step((input.buttons&Y)!=0,now);
                result.keys[Control]=(!settings.leftTriggerAim&&leftTrigger)||((input.buttons&RThumb)!=0&&!recenterConsumed_);
                if(input.nativeMovementAxes)result.keys[Control]=false;
            }
            result.keys[Escape]=back_.Step(menuButton||(!settings.immersiveLayout&&settings.yOpensMenu&&(input.buttons&Y)!=0),now);
            result.rightMouse=!settings.immersiveLayout&&!grenadeInputOwned&&settings.leftTriggerAim&&leftTrigger;
            result.leftMouse=!grenadeInputOwned&&rightTrigger;
            const float turn=StickSpeed(input.rightX,settings.deadzone);
            if(input.nativeCarried) {
                // Passenger and fixed-gun aiming is consumed by the native
                // vehicle controller; virtual body yaw cannot turn its gun.
                for(unsigned key:{unsigned('W'),unsigned('A'),unsigned('S'),unsigned('D'),unsigned(Shift),unsigned(Control)})result.keys[key]=false;
                const float speed=settings.mouseSensitivity*(input.nativeMounted?.65f:1.f)*(dt/16.6f);
                result.mouseX=turn*speed;
                result.mouseY=StickSpeed(input.rightY,settings.deadzone)*speed*(input.nativeMounted?1.f:-1.f);
                result.mountedControl=input.nativeMounted;result.passengerControl=!input.nativeMounted;
                result.mountedX=input.nativeMounted?turn:0;result.mountedY=input.nativeMounted?StickSpeed(input.rightY,settings.deadzone):0;
                result.passengerX=input.nativeMounted?0:StickSpeed(input.leftX,settings.deadzone);
                result.passengerY=input.nativeMounted?0:StickSpeed(input.leftY,settings.deadzone);
                result.passengerCrouched=!input.nativeMounted&&crouched_;
            } else if(settings.turnMode==TurnMode::Mouse)
                result.mouseX=turn*settings.mouseSensitivity*(dt/16.6f);
            else if(!recenterConsumed_) {
                if(settings.turnMode==TurnMode::Smooth)
                    result.turnYawDelta=turn*settings.smoothTurnDegreesPerSecond*(3.1415926536f/180000.f)*dt;
                else {
                    // One snap per deliberate horizontal deflection. Holding
                    // or reversing a deflected stick cannot cause repeated
                    // turns; it must return to the neutral deadzone first.
                    if(std::hypot(input.rightX,input.rightY)<=settings.deadzone)snapArmed_=true;
                    if(snapArmed_&&std::fabs(input.rightX)>=.65f&&
                       std::fabs(input.rightX)>=std::fabs(input.rightY)) {
                        result.turnYawDelta=std::copysign(settings.snapTurnDegrees*(3.1415926536f/180.f),input.rightX);
                        snapArmed_=false;
                    }
                }
            } else snapArmed_=false;
            if(!input.nativeCarried&&settings.turnMode==TurnMode::Mouse&&settings.rightStickPitch)
                result.mouseY=-StickSpeed(input.rightY,settings.deadzone)*settings.mouseSensitivity*(dt/16.6f);
        } else {
            const int menuDirection=menuStick_.Step(input.leftX,input.leftY,input.rightX,input.rightY,
                settings.menuBothSticks&&!settings.menuMouse,settings.deadzone,settings.menuThreshold);
            const bool pulse=menuRepeat_.Step(menuDirection,now);
            // Contracts' native SelectForward/Backward/Left/Right actions
            // follow WASD in the retail configuration, including pause menus.
            result.keys[settings.menuWasd?'W':Up]=pulse&&menuDirection==2;
            result.keys[settings.menuWasd?'S':Down]=pulse&&menuDirection==-2;
            result.keys[settings.menuWasd?'A':Left]=pulse&&menuDirection==-1;
            result.keys[settings.menuWasd?'D':Right]=pulse&&menuDirection==1;
            const bool confirm=confirm_.Step((input.buttons&A)!=0||(!settings.menuMouse&&rightTrigger),now);
            const bool back=back_.Step(menuButton||(input.buttons&B)!=0||
                (settings.immersiveLayout&&(input.buttons&Y)!=0)||
                (context==Context::Inventory&&(input.buttons&Y)!=0)||
                (context==Context::Map&&(input.buttons&X)!=0),now);
            if(context==Context::Inventory) {
                // Native inventory equips with LMB and closes with RMB; Enter
                // is only a main/pause-menu confirmation in this game.
                result.leftMouse=confirm;result.rightMouse=back;
            } else if(context==Context::Map) {
                result.leftMouse=confirm;
                result.rightMouse=zoomOut_.Step(leftTrigger,now);
                result.keys['M']=back;
            } else {
                result.keys[Enter]=confirm;result.keys[Escape]=back;
            }
            if(context==Context::Menu) {
                // K&L's title screen requires Space; the inherited mapping
                // can still use Right Arrow for other game menu profiles.
                const bool nextScreen=map_.Step((input.buttons&X)!=0,now);
                const unsigned nextKey=settings.menuXSpace?Space:Right;
                result.keys[nextKey]=result.keys[nextKey]||nextScreen;
            }
            if(settings.menuMouse) {
                result.leftMouse=result.leftMouse||rightTrigger;
                result.mouseX=StickSpeed(input.rightX,settings.deadzone)*settings.menuMouseSensitivity*(dt/16.6f);
                result.mouseY=-StickSpeed(input.rightY,settings.deadzone)*settings.menuMouseSensitivity*(dt/16.6f);
            }
        }
        return result;
    }
    void Reset() {
        initialized_=false;lastTick_=0;blockedButtons_=blockedAnalog_=0;nativeAxes_=false;
        snapArmed_=true;
        wheelHeld_=wheelSelectionBlocked_=false;wheelDirection_=0;wheelOpenedAt_=0;
        wheelNavigation_.Reset();
        teamGripHeld_=actionPressed_=false;
        teamCommandButtons_.Reset();
        crouched_=crouchPressed_=runPressed_=false;
        leftBlocked_=rightBlocked_=recenterConsumed_=false;menuStick_.Reset();menuRepeat_.Reset();movementSector_=-1;
        loadingConfirm_.Reset();loadingSkip_.Reset();
        confirm_.Reset();back_.Reset();reload_.Reset();map_.Reset();inventory_.Reset();zoomOut_.Reset();
        hudButton_.Reset();grenadeButton_.Reset();actionTap_.Reset();reloadSprint_.Reset();reloadTap_.Reset();skipTap_.Reset();skipButtonOwned_=false;
        crouchClimb_.Reset();grenadeType_.Reset();grenadeWasCharging_=grenadeCancelled_=grenadeTypeGesture_=false;
    }
private:
    static std::uint32_t AnalogButtons(const Input& input,float threshold) {
        return (input.leftTrigger>threshold?1u:0u)|(input.rightTrigger>threshold?2u:0u)|
            (input.leftGrip>threshold?4u:0u)|(input.rightGrip>threshold?8u:0u);
    }
    static float StickSpeed(float value,float deadzone) {
        if(!std::isfinite(value)||std::fabs(value)<=deadzone)return 0;
        return std::copysign((std::min(1.f,std::fabs(value))-deadzone)/(1.f-deadzone),value);
    }
    static int MenuDirection(float x,float y,float threshold,int current) {
        const bool vertical=std::fabs(y)>=std::fabs(x);
        const float magnitude=vertical?std::fabs(y):std::fabs(x);
        const int candidate=vertical?(y>0?2:-2):(x>0?1:-1);
        // A little hysteresis avoids rapid release/re-press near the threshold.
        if(magnitude>=threshold)return candidate;
        if(candidate==current&&magnitude>=threshold*0.65f)return current;
        return 0;
    }
    bool initialized_=false,leftBlocked_=false,rightBlocked_=false,recenterConsumed_=false;
    bool snapArmed_=true;
    bool nativeAxes_=false;
    bool wheelHeld_=false,wheelSelectionBlocked_=false;
    bool crouched_=false,crouchPressed_=false,running_=false,runPressed_=false;
    bool grenadeWasCharging_=false,grenadeCancelled_=false,grenadeTypeGesture_=false;
    int wheelDirection_=0;
    std::uint32_t wheelOpenedAt_=0;
    bool teamGripHeld_=false,actionPressed_=false;
    Context context_=Context::Suspended;
    std::uint32_t lastTick_=0,blockedButtons_=0,blockedAnalog_=0;
    int movementSector_=-1;
    MenuStick menuStick_;
    RepeatingDirection menuRepeat_,loadingConfirm_,loadingSkip_;
    TeamCommandButtons teamCommandButtons_;
    InventoryDirection wheelNavigation_;
    PressPulse confirm_,back_,reload_,reloadTap_,skipTap_,map_,inventory_,zoomOut_,actionTap_;
    bool skipButtonOwned_=false;
    HudButton hudButton_,crouchClimb_,grenadeButton_;
    PressPulse grenadeType_;
    ReloadSprintButton reloadSprint_;
};
} // namespace ControllerInput
