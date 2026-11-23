#include "controller_input.hpp"
#define NOGDI
#include "mono_frame_policy.hpp"
#include <cstdio>
#include <cstdlib>

using namespace ControllerInput;
namespace {
unsigned checks=0;
void Check(bool condition,const char* message) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void MovementAndButtons() {
    Mapper mapper;Settings settings;Input input;
    settings.movementHeadRelative=false;
    mapper.Step(input,settings,Context::Game,100);
    input.leftY=1;input.leftX=-1;input.rightX=1;input.rightY=1;
    input.rightTrigger=1;input.leftTrigger=1;input.rightGrip=1;input.leftGrip=1;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(out.keys['W']&&!out.keys['S'],"positive Touch Y moves forward");
    Check(out.keys['A']&&!out.keys['D'],"negative Touch X strafes left");
    Check(out.leftMouse&&out.keys[Control]&&!out.rightMouse,"fire and crouch can be held without opening inventory");
    Check(out.keys['E']&&out.keys[Shift],"grips preserve native action and run");
    Check(out.mouseX>0&&out.mouseY==0,"right stick turns without affecting HMD pitch by default");
    input.rightTrigger=0;
    out=mapper.Step(input,settings,Context::Game,134);
    Check(!out.leftMouse&&out.keys[Control],"releasing fire preserves crouch");
    input={};input.leftY=-1;
    out=mapper.Step(input,settings,Context::Game,151);
    Check(out.keys['S']&&!out.keys['W']&&!out.rightMouse,"backward and independent trigger release");
    settings.rightStickPitch=true;input.rightY=1;
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.mouseY<0,"optional stick pitch uses Windows mouse-up for positive Touch Y");
}
void MenuNavigation() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Menu,100);
    input.leftY=.5f;
    Check(!mapper.Step(input,settings,Context::Menu,101).keys['W'],"menu stick ignores motion below engage threshold");
    input.leftY=.8f;
    auto out=mapper.Step(input,settings,Context::Menu,110);
    Check(out.keys['W']&&!out.keys[Up],"menu stick uses native WASD selection by default");
    input.leftY=.4f;
    Check(mapper.Step(input,settings,Context::Menu,130).keys['W'],"menu direction hysteresis tolerates stick noise");
    Check(!mapper.Step(input,settings,Context::Menu,175).keys['W'],"menu releases the first navigation pulse");
    Check(!mapper.Step(input,settings,Context::Menu,459).keys['W'],"menu waits before repeating");
    Check(mapper.Step(input,settings,Context::Menu,460).keys['W'],"menu repeats after 350 ms");
    Check(!mapper.Step(input,settings,Context::Menu,525).keys['W'],"menu repeat has a release phase");
    Check(mapper.Step(input,settings,Context::Menu,620).keys['W'],"menu repeats at 160 ms interval");
    input.leftY=.8f;input.leftX=.9f;
    out=mapper.Step(input,settings,Context::Menu,630);
    Check(out.keys['W']&&!out.keys['D'],"a held vertical menu step survives diagonal drift without producing a horizontal press");
    input={};input.buttons=A;input.rightTrigger=1;input.rightY=1;
    out=mapper.Step(input,settings,Context::Menu,647);
    Check(out.keys[Enter]&&!out.leftMouse,"A and trigger share a single native menu confirmation");
    Check(out.mouseX==0&&out.mouseY==0&&!out.rightMouse,"menu never moves the mouse by default");
    input={};input.buttons=B;
    Check(mapper.Step(input,settings,Context::Menu,664).keys[Escape],"B is back in menus");
    settings.menuWasd=false;input={};
    mapper.Step(input,settings,Context::Menu,681);
    input.leftY=1;
    out=mapper.Step(input,settings,Context::Menu,698);
    Check(out.keys[Up]&&!out.keys['W'],"optional arrow menu layout does not send WASD");
    input.leftY=-1;
    out=mapper.Step(input,settings,Context::Menu,715);
    Check(!out.keys[Down]&&!out.keys[Up],"direct reversal cancels the held step until neutral");
    input.leftY=0;mapper.Step(input,settings,Context::Menu,732);
    input.leftY=-1;out=mapper.Step(input,settings,Context::Menu,749);
    Check(out.keys[Down]&&!out.keys['S'],"optional arrow layout preserves deliberate downward selection");
}
void TransitionsAndFocus() {
    Mapper mapper;Settings settings;Input input;
    settings.movementHeadRelative=false;
    mapper.Step(input,settings,Context::Menu,100);
    input.rightTrigger=1;input.leftY=1;input.buttons=A;
    mapper.Step(input,settings,Context::Menu,117);
    auto out=mapper.Step(input,settings,Context::Game,134);
    Check(!out.leftMouse&&!out.keys['E']&&!out.keys['W'],"held menu actions cannot become fire, action or movement on mission entry");
    input={};mapper.Step(input,settings,Context::Game,151);
    input.rightTrigger=1;input.leftY=1;input.buttons=A;
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.leftMouse&&out.keys['E']&&!out.keys[Space]&&out.keys['W'],"controls rearm native action, without toggling crouch, after returning to neutral");
    out=mapper.Step(input,settings,Context::Game,185,false);
    Check(!out.leftMouse&&!out.keys['E']&&!out.keys['W'],"focus loss releases every desired input");
    out=mapper.Step(input,settings,Context::Game,202);
    Check(!out.leftMouse&&!out.keys['E']&&!out.keys['W'],"focus regain cannot resume held inputs");
    input={};mapper.Step(input,settings,Context::Game,219);
    input.buttons=Menu;
    Check(mapper.Step(input,settings,Context::Game,236).keys[Escape],"Touch Menu button opens the game menu");
    out=mapper.Step(input,settings,Context::Menu,253);
    Check(!out.keys[Escape],"held menu button cannot immediately close the newly opened menu");
    out=mapper.Step(input,settings,Context::Suspended,270);
    Check(!out.keys[Escape]&&!out.leftMouse,"loading or suspended context releases inputs");
}
void HeadRelativeMovement() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.leftY=1;input.rightTrigger=1;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(!out.keys['W']&&!out.keys['A']&&!out.keys['S']&&!out.keys['D'],
        "missing camera basis suppresses movement rather than walking sideways");
    Check(out.leftMouse,"missing movement basis does not block other gameplay controls");
    input.movementYawValid=true;
    out=mapper.Step(input,settings,Context::Game,134);
    Check(out.keys['W']&&!out.keys['A']&&!out.keys['D'],"zero relative yaw preserves forward movement");
    input.movementYaw=1.5707963268f;
    out=mapper.Step(input,settings,Context::Game,151);
    Check(out.keys['D']&&!out.keys['W']&&!out.keys['S'],"looking ninety degrees right converts forward stick to native right");
    input.movementYaw=-1.5707963268f;
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.keys['A']&&!out.keys['W']&&!out.keys['S'],"looking ninety degrees left converts forward stick to native left");
    input.movementYaw=3.1415926536f;
    out=mapper.Step(input,settings,Context::Game,185);
    Check(out.keys['S']&&!out.keys['A']&&!out.keys['D'],"looking behind converts forward stick to native backward");
    input.movementYaw=.7853981634f;
    out=mapper.Step(input,settings,Context::Game,202);
    Check(out.keys['W']&&out.keys['D']&&!out.keys['S']&&!out.keys['A'],"diagonal view preserves diagonal movement");
    input.leftX=1;input.leftY=0;input.movementYaw=1.5707963268f;
    out=mapper.Step(input,settings,Context::Game,219);
    Check(out.keys['S']&&!out.keys['D'],"right strafe is transformed in the same frame as forward movement");
    input.movementYawValid=false;
    out=mapper.Step(input,settings,Context::Game,236);
    Check(!out.keys['S']&&!out.keys['D'],"expired camera basis releases an ongoing movement");
    input.movementYawValid=true;input.movementYaw=INFINITY;
    out=mapper.Step(input,settings,Context::Game,253);
    Check(!out.keys['S']&&!out.keys['D'],"nonfinite camera basis cannot steer movement");
    settings.movementHeadRelative=false;
    out=mapper.Step(input,settings,Context::Game,270);
    Check(out.keys['D'],"optional native movement mode does not require a camera basis");
    input.leftX=.25f;input.leftY=.25f;
    out=mapper.Step(input,settings,Context::Game,287);
    Check(out.keys['W']&&out.keys['D'],"radial deadzone treats diagonal stick deflection consistently");
    input.leftX=.2f;input.leftY=.2f;
    out=mapper.Step(input,settings,Context::Game,304);
    Check(!out.keys['W']&&!out.keys['D'],"small radial deflection remains neutral");
}
void MenuButtonsAndOptionalCursor() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Menu,100);
    input.buttons=A;input.rightTrigger=1;input.rightX=1;input.rightY=1;
    auto out=mapper.Step(input,settings,Context::Menu,117);
    Check(out.keys[Enter]&&!out.leftMouse,"confirmation never doubles as a mouse click");
    Check(out.mouseX==0&&out.mouseY==0,"right stick is inactive in the default menu mode");
    out=mapper.Step(input,settings,Context::Menu,178);
    Check(!out.keys[Enter],"held confirm releases after one pulse");
    input.buttons=0;
    Check(!mapper.Step(input,settings,Context::Menu,195).keys[Enter],"releasing A while holding trigger cannot re-confirm");
    input={};mapper.Step(input,settings,Context::Menu,212);
    input.rightTrigger=1;
    Check(mapper.Step(input,settings,Context::Menu,229).keys[Enter],"trigger confirms after all confirm inputs were released");
    input={};mapper.Step(input,settings,Context::Menu,300);
    input.buttons=B;
    Check(mapper.Step(input,settings,Context::Menu,317).keys[Escape],"B produces a native back pulse");
    Check(!mapper.Step(input,settings,Context::Menu,378).keys[Escape],"holding B does not navigate back through several screens");
    input={};mapper.Step(input,settings,Context::Menu,395);
    input.leftTrigger=1;input.leftGrip=1;input.rightGrip=1;input.buttons=Y|X;
    out=mapper.Step(input,settings,Context::Menu,412);
    Check(!out.keys[Escape]&&!out.keys['Q']&&!out.keys['M']&&!out.leftMouse&&!out.rightMouse,
        "game actions and left trigger do not become unexpected menu commands");
    settings.menuMouse=true;input={};mapper.Step(input,settings,Context::Menu,429);
    input.rightX=1;input.rightY=1;input.rightTrigger=1;
    out=mapper.Step(input,settings,Context::Menu,446);
    Check(out.mouseX>0&&out.mouseY<0&&out.leftMouse&&!out.keys[Enter],
        "explicit pointer compatibility option restores stick cursor and click only");
}
void InventoryMapAndPause() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=Y;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(out.keys['Q']&&!out.keys[Escape],"Y opens native quick inventory without pausing");
    Check(!mapper.Step(input,settings,Context::Game,178).keys['Q'],"held inventory is a single tap");
    input.buttons=0;mapper.Step(input,settings,Context::Game,195);
    input.buttons=X;
    out=mapper.Step(input,settings,Context::Game,212);
    Check(out.keys['M']&&!out.keys['Q']&&!out.keys[Escape],"X opens the native map");
    input.buttons=0;mapper.Step(input,settings,Context::Game,280);
    input.buttons=B;
    out=mapper.Step(input,settings,Context::Game,297);
    Check(out.keys['R']&&!out.keys[Escape],"B reloads during gameplay");
    input.buttons=0;mapper.Step(input,settings,Context::Game,365);
    input.buttons=Menu;
    out=mapper.Step(input,settings,Context::Game,382);
    Check(out.keys[Escape]&&!out.keys['Q'],"dedicated Menu button pauses gameplay");
    out=mapper.Step(input,settings,Context::Menu,399);
    Check(!out.keys[Escape],"opening pause cannot immediately dismiss it with the same held press");
    input.buttons=0;mapper.Step(input,settings,Context::Menu,416);
    input.buttons=A;mapper.Step(input,settings,Context::Menu,433);
    out=mapper.Step(input,settings,Context::Game,450);
    Check(!out.keys['E']&&!out.keys[Space],"menu confirmation cannot leak into native gameplay actions");
}
void InventoryAndMapContexts() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=Y;
    Check(mapper.Step(input,settings,Context::Game,117).keys['Q'],"Y requests quick inventory");
    auto out=mapper.Step(input,settings,Context::Inventory,134);
    Check(!out.rightMouse&&!out.keys['Q'],"held inventory opener cannot immediately close inventory");
    input={};mapper.Step(input,settings,Context::Inventory,151);
    input.buttons=A;input.leftX=1;
    out=mapper.Step(input,settings,Context::Inventory,168);
    Check(out.leftMouse&&!out.keys[Enter]&&out.keys['D'],"inventory uses native equip click and horizontal item selection");
    Check(!mapper.Step(input,settings,Context::Inventory,229).leftMouse,"inventory equip click is a single pulse");
    input={};mapper.Step(input,settings,Context::Inventory,246);
    input.buttons=B;
    out=mapper.Step(input,settings,Context::Inventory,263);
    Check(out.rightMouse&&!out.keys[Escape],"inventory back closes using native RMB without opening pause");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Map,400);
    input.buttons=A;input.leftTrigger=1;
    out=mapper.Step(input,settings,Context::Map,417);
    Check(out.leftMouse&&out.rightMouse&&!out.keys[Enter],"map confirm and left trigger use native zoom clicks");
    input={};mapper.Step(input,settings,Context::Map,500);
    input.buttons=B;
    out=mapper.Step(input,settings,Context::Map,517);
    Check(out.keys['M']&&!out.keys[Escape],"map back toggles the map without opening pause");
    out=mapper.Step(input,settings,Context::Game,534);
    Check(!out.keys['R'],"held map back cannot become reload when gameplay resumes");
}
void EquipmentNextScreen() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Menu,100);
    input.buttons=X;
    auto out=mapper.Step(input,settings,Context::Menu,117);
    Check(out.keys[Right]&&!out.keys['D']&&!out.keys[Enter]&&!out.keys['M'],
        "menu X advances equipment with Right Arrow without confirming an item");
    Check(!mapper.Step(input,settings,Context::Menu,178).keys[Right],
        "held equipment next button produces only one pulse");
    input={};mapper.Step(input,settings,Context::Menu,195);
    input.buttons=X;
    Check(mapper.Step(input,settings,Context::Menu,212).keys[Right],
        "equipment next rearms after releasing X");
    out=mapper.Step(input,settings,Context::Game,229);
    Check(!out.keys['M']&&!out.keys[Right],"held equipment next cannot open map when a mission starts");
    input={};mapper.Step(input,settings,Context::Game,246);
    input.buttons=X;
    out=mapper.Step(input,settings,Context::Game,263);
    Check(out.keys['M']&&!out.keys[Right],"gameplay X retains its map action");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Inventory,400);
    input.buttons=X;
    out=mapper.Step(input,settings,Context::Inventory,417);
    Check(!out.keys[Right]&&!out.keys['M'],"inventory X does not become equipment next");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Map,500);
    input.buttons=X;
    out=mapper.Step(input,settings,Context::Map,517);
    Check(out.keys['M']&&!out.keys[Right],"map X retains its close-map action");
}
void KaneLynchTitlePauseAndAim() {
    Mapper mapper;Settings settings;Input input;
    settings.menuMouse=true;settings.menuXSpace=true;
    settings.leftTriggerAim=true;settings.yOpensMenu=true;
    mapper.Step(input,settings,Context::Menu,100);
    input.buttons=X;
    auto out=mapper.Step(input,settings,Context::Menu,117);
    Check(out.keys[Space]&&!out.keys[Right],"K&L title screen advances with X as Space");
    out=mapper.Step(input,settings,Context::Game,134);
    Check(!out.keys['M']&&!out.keys[Space],"title confirmation cannot open game map during transition");
    input={};mapper.Step(input,settings,Context::Game,151);
    input.buttons=Y;input.leftTrigger=1;input.rightTrigger=1;
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.keys[Escape]&&!out.keys['Q'],"K&L Y opens pause instead of the inherited inventory action");
    Check(out.rightMouse&&out.leftMouse&&!out.keys[Control],"K&L uses native RMB aim and LMB fire");
    out=mapper.Step(input,settings,Context::Menu,185);
    Check(!out.rightMouse&&!out.leftMouse&&!out.keys[Escape],"pause consumes held aim/fire/menu buttons");
    input={};mapper.Step(input,settings,Context::Menu,202);
    input.buttons=A;out=mapper.Step(input,settings,Context::Menu,219);
    Check(out.keys[Enter]&&!out.leftMouse,"K&L menu A confirms the highlighted entry");
    input={};mapper.Step(input,settings,Context::Menu,300);
    input.rightTrigger=1;input.rightX=1;
    out=mapper.Step(input,settings,Context::Menu,317);
    Check(out.leftMouse&&out.mouseX>0&&!out.rightMouse,"K&L menu cursor and click work after context release");
}
void RecenterAndClockWrap() {
    Mapper mapper;Settings settings;Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=RThumb;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(out.keys[Control]&&!out.recenter,"single right stick click remains sneak");
    input.buttons=LThumb|RThumb;
    out=mapper.Step(input,settings,Context::Game,134);
    Check(out.recenter&&!out.keys[Control],"both stick clicks recenter and consume sneak");
    Check(!mapper.Step(input,settings,Context::Game,151).recenter,"held recenter chord does not repeat");
    input.buttons=RThumb;
    out=mapper.Step(input,settings,Context::Game,155);
    Check(!out.keys[Control]&&!out.recenter,"partial release of recenter chord cannot trigger sneak");
    input.buttons=LThumb|RThumb;
    Check(!mapper.Step(input,settings,Context::Game,159).recenter,"both chord buttons must be released before recenter rearms");
    input.buttons=0;mapper.Step(input,settings,Context::Game,168);
    input.buttons=LThumb|RThumb;
    Check(mapper.Step(input,settings,Context::Game,185).recenter,"recenter rearms after chord release");
    RepeatingDirection repeat;
    Check(repeat.Step(2,0xfffffff0u),"menu pulse starts before tick counter wrap");
    Check(!repeat.Step(2,0x40u),"menu releases across tick counter wrap");
    Check(repeat.Step(2,0x14eu),"menu repeat deadline survives tick counter wrap");
}
void DirectTurns() {
    Mapper mapper;Settings settings;Input input;
    settings.turnMode=TurnMode::Snap;
    mapper.Step(input,settings,Context::Game,100);
    input.rightX=.6f;
    Check(mapper.Step(input,settings,Context::Game,117).turnYawDelta==0,"snap ignores small horizontal deflections");
    input.rightX=1;
    auto out=mapper.Step(input,settings,Context::Game,134);
    Check(std::fabs(out.turnYawDelta-.523598776f)<1e-6f&&out.mouseX==0&&out.mouseY==0,
        "right snap emits exactly thirty degrees without native mouse input");
    Check(mapper.Step(input,settings,Context::Game,500).turnYawDelta==0,"holding snap stick never repeats");
    input.rightX=-1;
    Check(mapper.Step(input,settings,Context::Game,517).turnYawDelta==0,"reversing a deflected stick does not rearm snap");
    input.rightX=0;input.rightY=1;mapper.Step(input,settings,Context::Game,534);
    input.rightX=-1;input.rightY=0;
    Check(mapper.Step(input,settings,Context::Game,551).turnYawDelta==0,"moving around the stick rim does not rearm snap");
    input={};mapper.Step(input,settings,Context::Game,568);
    input.rightX=-1;
    Check(std::fabs(mapper.Step(input,settings,Context::Game,585).turnYawDelta+.523598776f)<1e-6f,
        "left snap rearms only after neutral");
    mapper.Step(input,settings,Context::Menu,602);
    out=mapper.Step(input,settings,Context::Game,619);
    Check(out.turnYawDelta==0,"held menu stick cannot snap on resuming gameplay");
    input={};mapper.Step(input,settings,Context::Game,636);
    input.rightX=1;input.buttons=LThumb|RThumb;
    out=mapper.Step(input,settings,Context::Game,653);
    Check(out.recenter&&out.turnYawDelta==0,"recenter chord consumes a concurrent snap request");
    input.buttons=0;
    Check(mapper.Step(input,settings,Context::Game,670).turnYawDelta==0,"recenter requires stick neutral before another snap");
    input={};mapper.Step(input,settings,Context::Game,687);
    input.rightX=1;input.rightY=1.01f;
    Check(mapper.Step(input,settings,Context::Game,704).turnYawDelta==0,"vertical stick gesture cannot accidentally snap");
    mapper.Reset();settings.turnMode=TurnMode::Smooth;settings.rightStickPitch=true;input={};
    mapper.Step(input,settings,Context::Game,1000);
    input.rightX=1;input.rightY=1;
    out=mapper.Step(input,settings,Context::Game,1020);
    Check(std::fabs(out.turnYawDelta-.0314159265f)<1e-6f&&out.mouseX==0&&out.mouseY==0,
        "smooth turn integrates ninety degrees per second and leaves pitch to the HMD");
    input.rightX=-1;
    Check(std::fabs(mapper.Step(input,settings,Context::Game,1040).turnYawDelta+.0314159265f)<1e-6f,
        "smooth turn sign follows the stick");
    Check(std::fabs(mapper.Step(input,settings,Context::Game,2040).turnYawDelta+.0785398163f)<1e-6f,
        "long stalls cap smooth turn integration to fifty milliseconds");
    input.rightX=.2f;
    Check(mapper.Step(input,settings,Context::Game,2057).turnYawDelta==0,"smooth turn respects deadzone");
    input.rightX=1;
    out=mapper.Step(input,settings,Context::Game,2074,false);
    Check(out.turnYawDelta==0,"focus loss stops direct turning");
    Check(mapper.Step(input,settings,Context::Game,2091).turnYawDelta==0,"focus regain consumes held turn input");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Game,0xfffffff0u);
    input.rightX=1;
    Check(std::fabs(mapper.Step(input,settings,Context::Game,4u).turnYawDelta-.0314159265f)<1e-6f,
        "smooth turn integration survives tick wrap");
    mapper.Reset();settings.menuMouse=true;input={};mapper.Step(input,settings,Context::Menu,100);
    input.rightX=1;
    out=mapper.Step(input,settings,Context::Menu,117);
    Check(out.mouseX>0&&out.turnYawDelta==0,"menu cursor works independently of direct turn mode");
}
void StableWalkingAndRecenter() {
    Mapper mapper;Settings settings;Input input;
    settings.turnMode=TurnMode::Smooth;settings.immersiveLayout=true;
    input.movementYawValid=true;
    mapper.Step(input,settings,Context::Game,100);
    input.leftY=1;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(out.keys['W']&&!out.keys['D'],"walking starts in the closest sector");
    for(float degrees : {21.f,23.f,22.f,26.f}) {
        input.movementYaw=degrees*(3.1415926536f/180.f);
        out=mapper.Step(input,settings,Context::Game,134);
        Check(out.keys['W']&&!out.keys['D'],"small body/camera yaw changes do not chatter diagonal movement");
    }
    input.movementYaw=29.f*(3.1415926536f/180.f);
    out=mapper.Step(input,settings,Context::Game,151);
    Check(out.keys['W']&&out.keys['D'],"deliberate boundary crossing changes walking direction");
    input.movementYaw=20.f*(3.1415926536f/180.f);
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.keys['W']&&out.keys['D'],"walking sector remains stable when yaw returns near a boundary");
    input.leftY=0;mapper.Step(input,settings,Context::Game,185);
    input.leftY=1;
    out=mapper.Step(input,settings,Context::Game,202);
    Check(out.keys['W']&&!out.keys['D'],"neutral stick clears movement hysteresis for the next gesture");
    input.movementYaw=3.1415926536f;
    out=mapper.Step(input,settings,Context::Game,219);
    Check(out.keys['S']&&!out.keys['W'],"large character turns immediately select the correct direction");
    input.movementYaw=-3.1415926536f+.01f;
    out=mapper.Step(input,settings,Context::Game,236);
    Check(out.keys['S']&&!out.keys['A']&&!out.keys['D'],"walking sector is continuous across the yaw wrap");
    input.movementYawValid=false;mapper.Step(input,settings,Context::Game,253);
    input.movementYawValid=true;input.movementYaw=20.f*(3.1415926536f/180.f);
    out=mapper.Step(input,settings,Context::Game,270);
    Check(out.keys['W']&&!out.keys['D'],"tracking recovery does not reuse a stale walking sector");
    input.rightX=1;input.rightTrigger=1;input.buttons=LThumb|RThumb;
    out=mapper.Step(input,settings,Context::Game,287);
    Check(out.recenter&&!out.keys['W']&&!out.keys['D']&&!out.leftMouse&&out.turnYawDelta==0,
          "recenter releases walking, fire and smooth turning in the same poll");
    input.buttons=0;
    out=mapper.Step(input,settings,Context::Game,954);
    Check(!out.leftMouse&&!out.keys['W']&&out.turnYawDelta==0,
          "held sticks and trigger cannot resume against a new tracking origin");
    input={};input.movementYawValid=true;mapper.Step(input,settings,Context::Game,971);
    input.leftY=1;input.rightX=1;input.rightTrigger=1;
    out=mapper.Step(input,settings,Context::Game,988);
    Check(out.keys['W']&&out.leftMouse&&out.turnYawDelta>0,
          "walking, fire and smooth turn resume after deliberate release");
}
void MenuBothSticksAndCinematics() {
    for(unsigned key:{0x21u,0x22u,0x23u,0x24u,0x25u,0x26u,0x27u,0x28u,0x2du,0x2eu})
        Check(ExtendedScan(key,0x48),"navigation keys keep extended flag when the keyboard layout omits E0");
    Check(ExtendedScan(0xa3,0xe01d),"native extended scan prefix remains supported");
    Check(!ExtendedScan('W',0x11)&&!ExtendedScan(Enter,0x1c),"ordinary keys and Enter retain their native scan mode");
    Mapper mapper;Settings settings;Input input;
    settings.menuBothSticks=true;settings.menuWasd=false;settings.menuMouse=false;
    settings.turnMode=TurnMode::Smooth;settings.immersiveLayout=true;
    mapper.Step(input,settings,Context::Menu,100);
    input.rightY=-1;
    auto out=mapper.Step(input,settings,Context::Menu,117);
    Check(out.keys[Down]&&out.mouseX==0&&out.mouseY==0&&out.turnYawDelta==0,
          "right stick navigates menu lists instead of moving an invisible cursor");
    input={};mapper.Step(input,settings,Context::Menu,200);
    input.rightX=1;out=mapper.Step(input,settings,Context::Menu,217);
    Check(out.keys[Right]&&!out.keys[Left],"right stick changes native setting values");
    input={};mapper.Step(input,settings,Context::Menu,300);
    input.leftY=1;out=mapper.Step(input,settings,Context::Menu,317);
    Check(out.keys[Up],"left stick still navigates menu lists");
    input={};mapper.Step(input,settings,Context::Menu,400);
    input.leftX=.68f;input.leftY=.65f;
    out=mapper.Step(input,settings,Context::Menu,410);
    Check(!out.keys[Up]&&!out.keys[Right],"diagonal entry waits for a clearly intended navigation axis");
    input.leftX=.88f;input.leftY=.34f;
    out=mapper.Step(input,settings,Context::Menu,427);
    Check(out.keys[Right]&&!out.keys[Up],"clear horizontal entry makes one menu step");
    input.leftX=.52f;input.leftY=.85f;input.rightY=1;
    out=mapper.Step(input,settings,Context::Menu,810);
    Check(out.keys[Right]&&!out.keys[Up],"diagonal drift and the other stick cannot change the repeating axis");
    input.leftX=-.85f;
    out=mapper.Step(input,settings,Context::Menu,980);
    Check(!out.keys[Left]&&!out.keys[Up],"reversing without centring cannot generate another menu press");
    input.leftX=input.leftY=0;
    out=mapper.Step(input,settings,Context::Menu,1000);
    Check(!out.keys[Up],"a held secondary stick cannot take over on release");
    input={};mapper.Step(input,settings,Context::Menu,1020);
    input.rightY=1;out=mapper.Step(input,settings,Context::Menu,1040);
    Check(out.keys[Up]&&!out.keys[Right],"centring rearms a deliberate vertical step on the other stick");

    // Restart the mapper timeline for the existing confirm and context checks.
    mapper.Reset();
    input={};mapper.Step(input,settings,Context::Menu,400);
    input.rightTrigger=1;out=mapper.Step(input,settings,Context::Menu,417);
    Check(out.keys[Enter]&&!out.leftMouse,"menu trigger confirms current selection in stick-navigation mode");
    mapper.Step(input,settings,Context::Cinematic,500);
    out=mapper.Step(input,settings,Context::Cinematic,517);
    Check(!out.keys[Enter]&&!out.keys[Space]&&!out.leftMouse,"held menu confirm does not immediately skip a cutscene");
    input={};mapper.Step(input,settings,Context::Cinematic,600);
    input.leftY=1;input.rightX=1;input.rightGrip=1;input.leftTrigger=1;input.buttons=A;
    out=mapper.Step(input,settings,Context::Cinematic,617);
    Check(out.keys[Enter]&&!out.keys[Space]&&!out.keys['W']&&!out.keys['E']&&!out.leftMouse&&!out.rightMouse&&!out.middleMouse&&out.turnYawDelta==0,
          "cinematic confirm cannot move, fire, turn or manipulate the scripted rig");
    input={};mapper.Step(input,settings,Context::Cinematic,700);
    input.buttons=Menu;out=mapper.Step(input,settings,Context::Cinematic,717);
    Check(out.keys[Escape],"pause remains available in a cinematic");
    input={};input.rightTrigger=1;input.leftY=1;input.rightX=1;
    mapper.Step(input,settings,Context::Game,800);
    out=mapper.Step(input,settings,Context::Game,817);
    Check(!out.leftMouse&&!out.keys['W']&&out.turnYawDelta==0,"cinematic exit consumes held game inputs until neutral");
}
void NativeConstrainedMovement() {
    Mapper mapper;Settings settings;Input input;
    settings.immersiveLayout=true;settings.movementHeadRelative=true;
    mapper.Step(input,settings,Context::Game,100);
    input.movementYawValid=true;input.leftY=-1;
    auto out=mapper.Step(input,settings,Context::Game,117);
    Check(out.keys['S']&&out.keys[Shift],"ordinary backward walking retains the preferred quarter-speed modifier");
    input.nativeMovementAxes=true;
    out=mapper.Step(input,settings,Context::Game,134);
    Check(out.keys['S']&&!out.keys[Shift],"rappel sends the full negative axis needed to cross native minus-half threshold");
    input.buttons=LThumb;
    out=mapper.Step(input,settings,Context::Game,151);
    Check(out.keys['S']&&!out.keys[Shift],"stick click cannot restore quarter-speed movement during rappel");
    input.buttons=0;input.leftY=1;
    out=mapper.Step(input,settings,Context::Game,168);
    Check(out.keys['W']&&!out.keys[Shift],"climb forward also uses its native full axis");
    input.buttons=A;mapper.Step(input,settings,Context::Game,200);
    out=mapper.Step(input,settings,Context::Game,600);
    Check(!out.keys[Space],"constrained forward movement cannot turn A hold into ground sprint");
    input.nativeMovementAxes=false;
    out=mapper.Step(input,settings,Context::Game,617);
    Check(out.keys['W']&&out.keys[Shift]&&!out.keys[Space]&&!out.keys['R'],"leaving rappel with A held restores walking without ground sprint or reload");
    out=mapper.Step(input,settings,Context::Game,1200);
    Check(!out.keys[Space]&&!out.keys['R'],"old constrained A remains consumed until physical release");
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,1217);
    Check(!out.keys['R'],"releasing consumed constrained A cannot reload on landing");
    input.buttons=A;mapper.Step(input,settings,Context::Game,1234);
    out=mapper.Step(input,settings,Context::Game,1634);
    Check(out.keys[Space],"a fresh deliberate A hold after landing still sprints");
    input.nativeMovementAxes=true;input.movementYawValid=false;
    out=mapper.Step(input,settings,Context::Game,1651);
    Check(!out.keys['W']&&!out.keys['S'],"native axis flag never bypasses the fresh movement-heading gate");
    Mapper legacy;settings.immersiveLayout=false;settings.movementHeadRelative=false;input={};
    legacy.Step(input,settings,Context::Game,100);
    input.leftY=-1;input.leftGrip=1;input.nativeMovementAxes=true;
    out=legacy.Step(input,settings,Context::Game,117);
    Check(out.keys['S']&&!out.keys[Shift],"legacy layout cannot reintroduce the walk modifier during rappel");
}

Settings NewLayout() {
    Settings settings;settings.immersiveLayout=true;settings.supportGrip=true;
    settings.movementHeadRelative=false;settings.turnMode=TurnMode::Smooth;
    // A stale old configuration must not restore native LT aiming.
    settings.leftTriggerAim=true;return settings;
}
bool NoGameplay(const Output& out) {
    return !out.leftMouse&&!out.rightMouse&&!out.keys['E']&&!out.keys['R']&&!out.keys['F']&&
        !out.keys[Control]&&!out.keys[Space]&&!out.keys['G']&&!out.climbHeld;
}
void NewTapHoldLayout() {
    Mapper mapper;const auto settings=NewLayout();Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=X;
    Check(!mapper.Step(input,settings,Context::Game,117).keys['E'],"X interacts on release");
    input.buttons=0;auto out=mapper.Step(input,settings,Context::Game,200);
    Check(out.keys['E']&&!out.teamHudHeld,"short X release invokes native action");
    Check(mapper.Step(input,settings,Context::Game,259).keys['E'],"X action pulse reaches DirectInput for sixty milliseconds");
    Check(!mapper.Step(input,settings,Context::Game,260).keys['E'],"released X action pulse ends");
    input.buttons=A;
    Check(!mapper.Step(input,settings,Context::Game,300).keys['R'],"A down does not reload");
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,330);
    Check(out.keys['R']&&!out.keys['E']&&!out.keys[Space],"short A release reloads only");
    Check(mapper.Step(input,settings,Context::Game,389).keys['R']&&!mapper.Step(input,settings,Context::Game,390).keys['R'],"reload pulse is sixty milliseconds");
    input.buttons=B;
    Check(!mapper.Step(input,settings,Context::Game,400).keys[Control],"B down waits for tap or climb hold");
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,430);
    Check(out.keys[Control]&&!out.climbHeld&&!out.keys['G'],"short B release toggles crouch without grenade");
    input.buttons=B;mapper.Step(input,settings,Context::Game,500);input.buttons=0;
    Check(!mapper.Step(input,settings,Context::Game,530).keys[Control],"second B tap stands");
    input.buttons=B;mapper.Step(input,settings,Context::Game,600);
    Check(!mapper.Step(input,settings,Context::Game,949).climbHeld,"B climb needs full350ms");
    out=mapper.Step(input,settings,Context::Game,950);
    Check(out.climbHeld&&!out.keys['E']&&!out.keys['G']&&!out.keys[Control],"B hold publishes pure climb permission without fake E or crouch");
    input.rightTrigger=1;out=mapper.Step(input,settings,Context::Game,970);
    Check(out.leftMouse&&!out.climbHeld,"trigger takes priority over a simultaneous climb hold");
    input.rightTrigger=0;
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,1000);
    Check(!out.climbHeld&&!out.keys[Control],"climb release clears intent without crouch tap");
    input.leftY=1;out=mapper.Step(input,settings,Context::Game,1100);
    Check(out.keys['W']&&out.keys[Shift],"default standing movement walks");
    input.buttons=LThumb;out=mapper.Step(input,settings,Context::Game,1117);
    Check(out.keys['W']&&!out.keys[Shift]&&!out.keys[Space],"L-stick click toggles run");
    input.buttons=0;mapper.Step(input,settings,Context::Game,1200);input.buttons=LThumb;
    Check(mapper.Step(input,settings,Context::Game,1217).keys[Shift],"next L-stick click toggles back to walk");
    input.buttons=A;mapper.Step(input,settings,Context::Game,1300);
    out=mapper.Step(input,settings,Context::Game,1650);
    Check(out.keys[Space]&&!out.keys['R']&&!out.keys[Shift],"moving A hold sprints after350ms");
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,1667);
    Check(!out.keys[Space]&&!out.keys['R']&&out.keys[Shift],"sprint release cannot reload");
    input={};mapper.Step(input,settings,Context::Game,1700);input.buttons=Y;
    Check(!mapper.Step(input,settings,Context::Game,1717).keys['F'],"Y down waits for weapon switch or inventory");
    input.buttons=0;out=mapper.Step(input,settings,Context::Game,1800);
    Check(out.keys['F']&&!out.weaponMenuHeld&&!out.keys['Q'],"short Y release cycles weapon");
    input={};mapper.Step(input,settings,Context::Game,1900);input.leftTrigger=1;input.leftGrip=1;
    out=mapper.Step(input,settings,Context::Game,1917);
    Check(!out.rightMouse&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.keys[Shift]&&!out.keys[Control],"LT never aims and left grip is foregrip only");
    input={};mapper.Step(input,settings,Context::Game,2000);input.buttons=RThumb;input.rightTrigger=1;input.leftY=1;
    out=mapper.Step(input,settings,Context::Game,2017);
    Check(out.recenter&&NoGameplay(out)&&!out.keys['W'],"R-stick click recentres immediately and consumes carried fire/movement");
    Check(!mapper.Step(input,settings,Context::Game,2700).recenter,"held calibration click never repeats");
    input.buttons=0;Check(!mapper.Step(input,settings,Context::Game,2717).leftMouse,"recenter release drains held trigger");
    input={};mapper.Step(input,settings,Context::Game,2800);input.buttons=RThumb;
    Check(mapper.Step(input,settings,Context::Game,2817).recenter,"recenter rearms after physical release");
    input.buttons=RThumb|A|B|X|Y;input.rightTrigger=1;
    out=mapper.Step(input,settings,Context::Game,2834);
    Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld,"calibration hold consumes buttons first pressed after the recenter click");
    input.buttons=A|B|X|Y;out=mapper.Step(input,settings,Context::Game,2851);
    Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld,"releasing calibration cannot restore newly carried buttons");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Game,2900);
    input.leftGrip=1;auto noSupport=settings;noSupport.supportGrip=false;
    out=mapper.Step(input,noSupport,Context::Game,2917);
    Check(!out.teamHudHeld&&!out.weaponMenuHeld&&!out.middleMouse,"disabled foregrip does not repurpose left grip as a menu gesture");
    for(auto button:{A,B,Y}) {
        mapper.Reset();input={};mapper.Step(input,settings,Context::Game,3000);input.buttons=button;
        mapper.Step(input,settings,Context::Game,3017);input.buttons=0;
        out=mapper.Step(input,settings,Context::Game,3700);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld,"missed polls cannot turn other long holds into gameplay taps");
    }
    HudButton hold;hold.Step(true,0xfffffff0u);
    Check(hold.Step(true,334).show&&!hold.Step(false,351).tap,"tap/hold threshold survives clock wrap");
}
void NewTeamCommands() {
    using Command=TeamCommandIPC::Command;
    Mapper mapper;const auto settings=NewLayout();Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=Y;input.leftGrip=1;mapper.Step(input,settings,Context::Game,117);
    Check(!mapper.Step(input,settings,Context::Game,466).teamHudHeld,"Y opens the HUD only after 350ms");
    auto out=mapper.Step(input,settings,Context::Game,467);
    Check(out.gameplayHudHeld&&out.teamHudHeld&&out.weaponMenuHeld&&out.middleMouse&&NoGameplay(out),
        "held Y opens the native wheel and left panels together");
    Check(!mapper.Step(input,settings,Context::Game,567).teamNext,"a grip carried into the panel cannot change recipients");
    input.leftGrip=0;mapper.Step(input,settings,Context::Game,580);input.leftGrip=1;
    Check(!mapper.Step(input,settings,Context::Game,584).teamNext,"left Grip has no recipient action while the HUD is open");
    input.leftGrip=0;input.rightGrip=1;input.rightX=1;
    out=mapper.Step(input,settings,Context::Game,601);
    Check(out.teamNext&&out.keys['D']&&NoGameplay(out)&&!out.grenadeRelease,"right Grip advances once while right stick selects a weapon");
    Check(!mapper.Step(input,settings,Context::Game,750).teamNext,"held Grip does not repeat");
    input.rightGrip=0;input.rightX=0;mapper.Step(input,settings,Context::Game,767);
    input.rightGrip=1;input.rightX=-1;out=mapper.Step(input,settings,Context::Game,827);
    Check(out.teamNext&&out.keys['A']&&!out.keys[Tab]&&!out.keys[Shift],"second Grip press advances in the same direction");
    input.rightGrip=0;input.rightX=0;input.buttons=Y|A;out=mapper.Step(input,settings,Context::Game,837);
    Check(out.teamCommand==Command::Recall&&NoGameplay(out),"A recalls without reloading while the HUD is open");
    Check(mapper.Step(input,settings,Context::Game,900).teamCommand==Command::None,"held orders do not repeat");
    input.buttons=Y;mapper.Step(input,settings,Context::Game,917);
    input.buttons=Y|B;out=mapper.Step(input,settings,Context::Game,934);
    Check(out.teamCommand==Command::Move&&NoGameplay(out),"B orders movement without crouching or climbing");
    input.buttons=Y;input.rightTrigger=1;out=mapper.Step(input,settings,Context::Game,951);
    Check(out.teamCommand==Command::Attack&&!out.leftMouse,"trigger orders attack without firing");
    input.buttons=A|B|X;input.rightGrip=input.rightTrigger=1;input.rightX=1;
    out=mapper.Step(input,settings,Context::Game,968);
    Check(!out.teamHudHeld&&!out.weaponMenuHeld&&!out.middleMouse&&NoGameplay(out),"Y release drains held command and action buttons");
    Check(NoGameplay(mapper.Step(input,settings,Context::Game,1400)),"carried inputs remain drained past the grenade hold threshold");
    input={};out=mapper.Step(input,settings,Context::Game,1417);
    Check(!out.grenadeRelease,"releasing the carried Grip cannot throw a grenade");
    input.rightTrigger=1;
    Check(mapper.Step(input,settings,Context::Game,1434).leftMouse,"a fresh trigger fires after the HUD closes");
    input.rightTrigger=0;input.rightGrip=1;mapper.Step(input,settings,Context::Game,1451);
    Check(mapper.Step(input,settings,Context::Game,1801).keys['G'],"a fresh right Grip readies a grenade after the HUD closes");
    input.rightGrip=0;
    Check(mapper.Step(input,settings,Context::Game,1818).grenadeRelease,"the newly readied grenade throws on release");
}
void CrouchedTeamPanel() {
    Mapper mapper;const auto settings=NewLayout();Input input;
    mapper.Step(input,settings,Context::Game,100);
    input.buttons=B;mapper.Step(input,settings,Context::Game,117);
    input.buttons=0;
    Check(mapper.Step(input,settings,Context::Game,165).keys[Control],"B tap engages the native crouch latch");
    input.buttons=Y;mapper.Step(input,settings,Context::Game,183);
    auto out=mapper.Step(input,settings,Context::Game,533);
    Check(out.middleMouse&&out.keys[Control],"opening the native inventory keeps crouch held");
    input.buttons=Y|B;out=mapper.Step(input,settings,Context::Game,650);
    Check(out.teamCommand==TeamCommandIPC::Command::Move&&out.keys[Control]&&!out.climbHeld,
          "B commands movement while crouch stays latched");
    input.buttons=B;out=mapper.Step(input,settings,Context::Game,667);
    Check(!out.middleMouse&&out.keys[Control],"closing the wheel keeps crouch with B still consumed");
    input.buttons=0;
    Check(mapper.Step(input,settings,Context::Game,700).keys[Control],"releasing a consumed team B does not stand up");
    input.buttons=B;mapper.Step(input,settings,Context::Game,733);
    input.buttons=0;
    Check(!mapper.Step(input,settings,Context::Game,783).keys[Control],"a new gameplay B tap stands up");
}
void NewInventory() {
    Mapper mapper;const auto settings=NewLayout();Input input;
    mapper.Step(input,settings,Context::Game,100);input.buttons=Y;
    mapper.Step(input,settings,Context::Game,117);
    Check(!mapper.Step(input,settings,Context::Game,466).middleMouse,"Y inventory needs350ms hold");
    auto out=mapper.Step(input,settings,Context::Game,467);
    Check(out.middleMouse&&out.weaponMenuHeld&&NoGameplay(out),"Y hold opens native MMB weapon inventory");
    input.rightX=1;Check(!mapper.Step(input,settings,Context::Game,500).keys['D'],"inventory gives native wheel100ms entry time");
    out=mapper.Step(input,settings,Context::Game,567);
    Check(out.middleMouse&&out.keys['D']&&!out.leftMouse&&out.turnYawDelta==0,"right stick selects inventory without turning or firing");
    input.rightY=1.01f;out=mapper.Step(input,settings,Context::Game,584);
    Check(out.keys['D']&&!out.keys['W'],"held diagonal does not accidentally select another wheel direction");
    input.rightX=input.rightY=0;mapper.Step(input,settings,Context::Game,600);input.rightX=1;
    Check(!mapper.Step(input,settings,Context::Game,617).keys['D'],"exchange direction enforces observable neutral gap");
    Check(mapper.Step(input,settings,Context::Game,660).keys['D'],"repeat excursion reaches native teammate exchange ring");
    input.buttons=Y|A|B|X;input.rightTrigger=input.rightGrip=input.leftY=1;
    out=mapper.Step(input,settings,Context::Game,700);
    Check(out.weaponMenuHeld&&out.teamHudHeld&&NoGameplay(out),"Y inventory retains ownership against team/grenade and game buttons");
    input.buttons=A|B|X;out=mapper.Step(input,settings,Context::Game,717);
    Check(!out.middleMouse&&!out.weaponMenuHeld&&NoGameplay(out)&&!out.keys['D'],"Y release equips by native MMB-up and drains carried inputs");
    Check(NoGameplay(mapper.Step(input,settings,Context::Game,1200)),"inventory release cannot replay held actions");
    input={};Check(NoGameplay(mapper.Step(input,settings,Context::Game,1217)),"drained inventory button releases are not taps");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Game,2000);input.rightX=1;
    mapper.Step(input,settings,Context::Game,2017);input.buttons=Y;
    mapper.Step(input,settings,Context::Game,2034);out=mapper.Step(input,settings,Context::Game,2384);
    Check(out.weaponMenuHeld&&!out.keys['D'],"predeflected turning stick cannot select weapon until neutral");
    input.rightX=0;mapper.Step(input,settings,Context::Game,2400);input.rightX=-1;
    Check(mapper.Step(input,settings,Context::Game,2500).keys['A'],"inventory selection rearms after neutral");
}
void NewGrenadesAndCancellation() {
    Mapper mapper;const auto settings=NewLayout();Input input;
    mapper.Step(input,settings,Context::Game,100);input.rightGrip=1;
    Check(!mapper.Step(input,settings,Context::Game,117).keys['G'],"right grip grenade requires a hold rather than accidental squeeze");
    auto out=mapper.Step(input,settings,Context::Game,467);
    Check(out.keys['G']&&!out.middleMouse&&!out.weaponMenuHeld&&!out.grenadeRelease,"right grip350ms charges native grenade without opening inventory");
    input.rightGrip=.3f;Check(mapper.Step(input,settings,Context::Game,484).keys['G'],"charged grenade grip has release hysteresis");
    input.buttons=Y;out=mapper.Step(input,settings,Context::Game,500);
    Check(out.keys['G']&&out.keys['Q']&&!out.weaponMenuHeld&&!out.keys['F'],"Y changes grenade type without weapon switch or inventory");
    input.buttons=0;mapper.Step(input,settings,Context::Game,570);input.rightGrip=0;
    out=mapper.Step(input,settings,Context::Game,600);
    Check(!out.keys['G']&&out.grenadeRelease&&!out.keys[Control],"deliberate right grip release throws without crouch");
    mapper.Reset();input={};mapper.Step(input,settings,Context::Game,1000);input.rightGrip=1;
    mapper.Step(input,settings,Context::Game,1017);mapper.Step(input,settings,Context::Game,1367);input.buttons=X;
    out=mapper.Step(input,settings,Context::Game,1384);
    Check(out.keys['E']&&!out.keys['G']&&!out.grenadeRelease&&!out.teamHudHeld,"X still cancels a charged grenade");
    input.buttons=0;input.rightGrip=0;
    out=mapper.Step(input,settings,Context::Game,1400);
    Check(!out.grenadeRelease&&!out.keys['E'],"cancelled grenade release never certifies throw or second action");
    NativeGrenadeCancel cancel;Output requested;requested.keys[Escape]=true;
    out=cancel.Step(requested,0xfffffff0u,true);
    Check(out.keys['E']&&out.keys['G']&&!out.keys[Escape]&&cancel.Active(),"UI transition starts native grenade E/G cancellation handshake");
    out=cancel.Step({},43,true);Check(out.keys['E']&&out.keys['G'],"cancel keeps G for native observable60ms across timer wrap");
    out=cancel.Step({},44,true);Check(out.keys['E']&&!out.keys['G']&&!out.keys[Escape],"cancel retires G only after E was observable");
    out=cancel.Step({},104,false);Check(!out.keys['E']&&out.keys[Escape]&&!cancel.Active(),"queued pause follows successful cancellation handshake");
    Check(cancel.Step({},163,false).keys[Escape]&&!cancel.Step({},164,false).keys[Escape],"deferred pause retains60ms pulse");
    requested={};requested.grenadeRelease=true;
    Check(!cancel.Step(requested,200,true).keys['E']&&!cancel.Active(),"explicit throw release bypasses cancellation");
    requested={};requested.recenter=true;out=cancel.Step(requested,300,true);
    Check(!out.recenter,"recenter waits while grenade is armed");
    out=cancel.Step({},420,false);Check(out.recenter&&!out.keys['G'],"deferred recenter follows cancellation");
    Check(!cancel.Step({},437,false).recenter,"deferred recenter is delivered only once");
}
void NewLayoutCancellationMatrix() {
    const auto settings=NewLayout();
    for(unsigned gesture=0;gesture<6;++gesture)for(auto destination:{Context::Menu,Context::Inventory,Context::Map,Context::Actions,Context::Cinematic,Context::Loading,Context::Suspended}) {
        Mapper mapper;Input input;mapper.Step(input,settings,Context::Game,100);
        input.buttons=gesture==0?A:gesture==1?B:gesture==2?X:gesture==3?Y:0;
        if(gesture==4)input.rightGrip=1;
        if(gesture==5)input.buttons=RThumb;
        mapper.Step(input,settings,Context::Game,117);mapper.Step(input,settings,Context::Game,467);
        auto out=mapper.Step(input,settings,destination,484);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.middleMouse&&!out.grenadeRelease,"native UI/context transition retires held gameplay gesture without throw");
        out=mapper.Step(input,settings,Context::Game,501);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.middleMouse,"native context return consumes held owner until release");
        input={};out=mapper.Step(input,settings,Context::Game,518);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld,"releasing a consumed owner never replays action");
    }
    {
        constexpr auto owner=Y;
        Mapper mapper;Input input;mapper.Step(input,settings,Context::Game,100);input.buttons=owner;
        mapper.Step(input,settings,Context::Game,117);mapper.Step(input,settings,Context::Game,467);input.buttons|=Menu;
        auto out=mapper.Step(input,settings,Context::Game,500);
        Check(out.keys[Escape]&&NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"pause closes each held overlay before native Escape");
        out=mapper.Step(input,settings,Context::Game,517);
        Check(out.keys[Escape]&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"held pause remains full DirectInput pulse without reopening overlay");
    }
    {
        constexpr auto owner=Y;
        Mapper mapper;Input input;mapper.Step(input,settings,Context::Game,100);input.buttons=owner;
        mapper.Step(input,settings,Context::Game,117);
        auto out=mapper.Step(input,settings,Context::Game,467);
        Check(out.teamHudHeld&&out.weaponMenuHeld&&out.middleMouse,"calibration fixture opens the held owner overlay");
        input.buttons=owner|RThumb;out=mapper.Step(input,settings,Context::Game,484);
        Check(out.recenter&&NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"recenter immediately retires an already open team or inventory overlay");
        input.buttons|=A|B;input.rightTrigger=input.leftY=input.rightX=1;
        out=mapper.Step(input,settings,Context::Game,501);
        Check(NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.keys['W']&&out.turnYawDelta==0,"held calibration owns new buttons and axes while the former overlay owner remains down");
        input.buttons=owner|A|B;out=mapper.Step(input,settings,Context::Game,518);
        Check(NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.keys['W']&&out.turnYawDelta==0,"releasing only recenter cannot reopen a carried overlay or reuse its commands");
        out=mapper.Step(input,settings,Context::Game,900);
        Check(NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"a carried X or Y hold remains consumed beyond the overlay hold threshold");
        input={};out=mapper.Step(input,settings,Context::Game,917);
        Check(NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"releasing a calibration-retired overlay owner cannot emit its short E or F tap");
        input.buttons=owner;out=mapper.Step(input,settings,Context::Game,934);
        Check(!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"retired overlay rearms only from a fresh physical press");
        out=mapper.Step(input,settings,Context::Game,1284);
        Check(out.teamHudHeld&&out.weaponMenuHeld&&out.middleMouse,"fresh post-calibration hold opens the requested overlay normally");
        input={};out=mapper.Step(input,settings,Context::Game,1301);
        Check(NoGameplay(out)&&!out.middleMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"fresh overlay owner release returns to gameplay without E or F");
        input.rightTrigger=1;out=mapper.Step(input,settings,Context::Game,1318);
        Check(out.leftMouse&&!out.teamHudHeld&&!out.weaponMenuHeld,"new gameplay trigger works after all calibration and overlay owners released");
    }
    for(unsigned gesture=0;gesture<5;++gesture) {
        Mapper mapper;Input input;mapper.Step(input,settings,Context::Game,100);
        input.buttons=gesture==0?A:gesture==1?B:gesture==2?X:gesture==3?Y:0;
        if(gesture==4)input.rightGrip=1;
        mapper.Step(input,settings,Context::Game,117);mapper.Step(input,settings,Context::Game,467);
        auto out=mapper.Step(input,settings,Context::Game,484,false);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld&&!out.grenadeRelease,"focus loss neutralizes every hold owner without implicit throw");
        out=mapper.Step(input,settings,Context::Game,501);
        Check(NoGameplay(out)&&!out.teamHudHeld&&!out.weaponMenuHeld,"focus regain drains carried gesture before rearming");
    }
}
}
int main() {
    {
        Settings settings;settings.immersiveLayout=true;Mapper mapper;Input input;
        mapper.Step(input,settings,Context::Game,100);input.buttons=X;
        auto out=mapper.Step(input,settings,Context::Game,117);
        Check(!out.gameplayHudHeld&&!out.teamHudHeld,"a short X interaction does not open hand telemetry");
        out=mapper.Step(input,settings,Context::Game,517);
        Check(!out.gameplayHudHeld&&!out.teamHudHeld&&!out.weaponMenuHeld,"holding X opens no panel");
        input={};out=mapper.Step(input,settings,Context::Game,534);
        Check(!out.gameplayHudHeld&&!out.teamHudHeld&&out.keys['E'],"X still interacts after a hold");
        mapper.Step(input,settings,Context::Game,550);input.buttons=Y;
        mapper.Step(input,settings,Context::Game,567);out=mapper.Step(input,settings,Context::Game,967);
        Check(out.weaponMenuHeld&&out.gameplayHudHeld&&out.teamHudHeld,"Y hold opens all gameplay panels");
    }
    {
        Settings settings;settings.immersiveLayout=true;settings.turnMode=TurnMode::Smooth;settings.rightStickPitch=false;
        Mapper mapper;Input input;input.nativeCarried=true;mapper.Step(input,settings,Context::Game,100);
        input.rightX=.8f;input.rightY=.6f;input.leftY=1;input.rightTrigger=1;
        const auto mounted=mapper.Step(input,settings,Context::Game,117);
        Check(mounted.mouseX>0&&mounted.mouseY<0&&!mounted.turnYawDelta,"mounted right stick drives both native gun axes even when ordinary VR uses smooth turn");
        Check(mounted.leftMouse&&!mounted.keys['W']&&!mounted.keys[Shift]&&!mounted.keys[Control],"mounted native fire remains available without walking or sprinting inside the vehicle");
        Check(mounted.passengerControl&&!mounted.mountedControl&&mounted.passengerY>0&&mounted.passengerX==0,
            "passenger left stick has a dedicated camera-navigation output while native walking stays suppressed");
        input.nativeMounted=true;const auto faster=mapper.Step(input,settings,Context::Game,134);
        Check(faster.mouseY>0&&faster.mouseX>0&&faster.mouseX<mounted.mouseX,"actual fixed MG has corrected vertical sign and controllable fine right-stick traverse");
        Check(faster.mountedControl&&!faster.passengerControl&&faster.mountedX>0&&faster.mountedY>0&&faster.passengerX==0&&faster.passengerY==0,
            "mounted steering exports normalized local rate axes separately from legacy mouse fallback and passenger movement");
        input={};mapper.Step(input,settings,Context::Menu,150);input.rightY=-1;
        const auto menu=mapper.Step(input,settings,Context::Menu,167);
        Check(!menu.leftMouse&&!menu.turnYawDelta,"vehicle aim cannot escape into pause menu gameplay");
        Check(!menu.mountedControl&&!menu.passengerControl&&!menu.mountedX&&!menu.passengerY,"menus publish no lingering vehicle axes");
        input={};mapper.Step(input,settings,Context::Game,200);input.rightX=1;
        const auto foot=mapper.Step(input,settings,Context::Game,217);
        Check(foot.turnYawDelta>0&&foot.mouseX==0,"ordinary smooth turn returns after leaving the carried gun mode");
    }
    for(bool mounted:{false,true}){
        Settings settings;settings.immersiveLayout=true;Mapper mapper;Input input;input.nativeCarried=true;input.nativeMounted=mounted;
        mapper.Step(input,settings,Context::Game,1000);input.buttons=Y;mapper.Step(input,settings,Context::Game,1017);
        auto out=mapper.Step(input,settings,Context::Game,1417);input.buttons=Y|B;input.leftX=input.rightX=1;
        out=mapper.Step(input,settings,Context::Game,1434);
        Check(out.teamHudHeld&&!out.passengerControl&&!out.mountedControl&&!out.passengerCrouched&&!out.mountedX&&!out.passengerX,
            "held team B and selection sticks cannot become passenger crouch/navigation or mounted traverse");
        input.buttons=B;out=mapper.Step(input,settings,Context::Game,1451);out=mapper.Step(input,settings,Context::Game,1500);
        Check(!out.passengerCrouched&&!out.mountedX&&!out.passengerX,"closing team panel drains held command B and both sticks before vehicle control resumes");
        input={};input.nativeCarried=true;input.nativeMounted=mounted;mapper.Step(input,settings,Context::Game,1600);
        input.buttons=Y;mapper.Step(input,settings,Context::Game,1617);out=mapper.Step(input,settings,Context::Game,2017);
        input.leftY=input.rightY=1;out=mapper.Step(input,settings,Context::Game,2034);
        Check(out.weaponMenuHeld&&!out.passengerControl&&!out.mountedControl&&!out.mountedY&&!out.passengerY,"held inventory selection has no vehicle control output");
        input.buttons=RThumb|B;out=mapper.Step(input,settings,Context::Game,2051);
        Check(!out.passengerCrouched&&!out.passengerControl&&!out.mountedControl,"calibration/overlay chord cannot leak vehicle crouch or aim");
    }
    {
        Settings settings;settings.immersiveLayout=true;Mapper mapper;Input input;input.nativeCarried=true;
        mapper.Step(input,settings,Context::Game,1000);input.buttons=B;mapper.Step(input,settings,Context::Game,1017);input.buttons=0;
        const auto crouch=mapper.Step(input,settings,Context::Game,1050);
        Check(crouch.passengerControl&&crouch.passengerCrouched&&!crouch.keys[Control],"deliberate ordinary B toggles only passenger camera crouch while native body remains seated");
    }
    MovementAndButtons();MenuNavigation();TransitionsAndFocus();RecenterAndClockWrap();
    HeadRelativeMovement();MenuButtonsAndOptionalCursor();InventoryMapAndPause();
    InventoryAndMapContexts();
    EquipmentNextScreen();
    KaneLynchTitlePauseAndAim();
    DirectTurns();StableWalkingAndRecenter();MenuBothSticksAndCinematics();NativeConstrainedMovement();
    NewTapHoldLayout();NewTeamCommands();CrouchedTeamPanel();NewInventory();NewGrenadesAndCancellation();NewLayoutCancellationMatrix();
    ReloadSprintButton stationary;stationary.Step(true,false,1000);stationary.Step(true,false,1400);
    Check(!stationary.Step(false,false,1450).reload,"long stationary A never reloads on release");
    ReloadSprintButton threshold;threshold.Step(true,false,0xfffffff0u);
    auto thresholdResult=threshold.Step(true,true,334);
    Check(thresholdResult.sprint&&!thresholdResult.reload,"long A can begin sprinting after movement starts across clock wrap");
    thresholdResult=threshold.Step(true,false,350);
    Check(!thresholdResult.sprint&&!thresholdResult.reload,"stationary long A stops sprinting without reloading");
    Check(!threshold.Step(false,false,351).reload,"long A release remains consumed when movement stopped");
    ReloadSprintButton missedThreshold;missedThreshold.Step(true,false,100);
    Check(!missedThreshold.Step(false,false,450).reload,"release at threshold is long even if intermediate polls are missed");
    ReloadIntentLatch intent;
    Check(intent.Step(true,true,true,100).tick==100&&intent.Step(true,true,false,120).tick==100,
          "one reload pulse publishes a single stable event across repeated input polls");
    Check(intent.Step(false,true,true,160).tick==100&&intent.Step(true,true,true,200).tick==200,
          "the next separate reload tap receives a new event");
    Check(intent.Step(true,false,true,210).tick==0&&intent.Step(true,true,true,220).tick==0,
          "menu or scene cancellation clears the event without replaying a held pulse");
    intent.Step(false,true,true,240);
    Check(intent.Step(true,true,true,250).tick==250,"new tap after cancellation works");
    intent.Reset();Check(intent.Step(false,true,true,260).tick==0,"focus or context reset clears stale reload intent");
    auto fallback=intent.Step(true,true,false,270);
    Check(fallback.keyboard&&!fallback.tick,"unavailable or stale native adapter uses keyboard R only");
    fallback=intent.Step(true,true,true,280);
    Check(fallback.keyboard&&!fallback.tick,"ready appearing during a pulse cannot create a second direct event");
    intent.Step(false,true,true,300);auto direct=intent.Step(true,true,true,310);
    Check(!direct.keyboard&&direct.tick==310,"next tap uses a freshly available direct adapter only");
    direct=intent.Step(true,true,false,320);
    Check(!direct.keyboard&&direct.tick==310,"losing readiness during a direct pulse cannot inject a duplicate R");
    VRIPC::SharedFrames shared{};shared.version=VRIPC::kVersion;
    shared.gameplaySequence=2;shared.gameplayTick=1000;shared.gameplayFlags=1024;
    LONG flags=0;ReloadIntentLatch freshness;
    const bool fresh=MonoFrame::ReadGameplay(shared,1100,flags);
    Check(fresh&&!freshness.Step(true,true,fresh&&(flags&1024),1100).keyboard,
          "fresh confirmed native adapter selects direct reload");
    freshness.Reset();flags=0;
    const bool stale=MonoFrame::ReadGameplay(shared,1250,flags);
    Check(!stale&&freshness.Step(true,true,stale&&(flags&1024),1250).keyboard,
          "stale game readiness selects keyboard fallback without a duplicate event");
    freshness.Reset();shared.gameplaySequence=3;flags=0;
    const bool torn=MonoFrame::ReadGameplay(shared,1100,flags);
    Check(!torn&&freshness.Step(true,true,torn&&(flags&1024),1100).keyboard,
          "incomplete readiness publication fails over to keyboard input");
    {Mapper skip;Settings settings;settings.immersiveLayout=true;Input input;
        skip.Step(input,settings,Context::Game,1000);input.firstPersonSkip=true;input.buttons=A;const auto pressed=skip.Step(input,settings,Context::Game,1100);
        Check(pressed.keys[Enter]&&!pressed.keys['R'],"first-person authored scene maps A press to native Enter without flattening its VR camera");
        Check(skip.Step(input,settings,Context::Game,1400).keys[Enter],"held A keeps Enter observable across a slow cinematic update");
        input.firstPersonSkip=false;const auto finished=skip.Step(input,settings,Context::Game,1410);input.buttons=0;
        Check(!finished.keys['R']&&!skip.Step(input,settings,Context::Game,1450).keys['R'],"completed skip consumes the same A press until release without reloading");}
    std::printf("Controller mapping: %u checks passed.\n",checks);return 0;
}
