#include "vehicle_control_ipc.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {unsigned checks=0;void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
VehicleControlIPC::Shared Fresh(){using namespace VehicleControlIPC;Shared shared;shared.magic=Magic;shared.version=Version;shared.bytes=sizeof(shared);shared.gamePid=1234;
    Publish(shared.mounted,Certificate{10,20,30,40,50,6,7,1000});Publish(shared.passenger,Certificate{10,25,0,0,0,8,7,1000});return shared;}}
int main(){using namespace VehicleControlIPC;auto shared=Fresh();Certificate cert;Request input;
    Check(Receiver(shared,1234,1000,7,Kind::Mounted,cert)&&Send(shared,1234,1000,7,Kind::Mounted,.6f,-.2f)&&Input(shared,1234,1000,7,Kind::Mounted,cert,input)&&input.x==.6f&&input.y==-.2f,
        "exact current mount certificate carries normalized axes in a separate pointer-free channel");
    Certificate passenger;Check(Receiver(shared,1234,1000,7,Kind::Passenger,passenger)&&Send(shared,1234,1000,7,Kind::Passenger,-1,1,true)&&
        Input(shared,1234,1000,7,Kind::Passenger,passenger,input)&&input.buttons==1&&input.x==-1&&input.y==1,"passenger-only left stick and crouch remain separate from mounted steering");
    Check(!Input(shared,1234,1000,7,Kind::Passenger,cert,input)&&!Input(shared,1234,1000,7,Kind::Mounted,passenger,input),"passenger and mounted certificates cannot be interchanged");
    Check(!Receiver(shared,1234,1151,7,Kind::Mounted,cert)&&!Receiver(shared,1234,999,7,Kind::Passenger,cert),"stale/future receiver certificate cannot enable input");
    for(unsigned i=0;i<13;++i){auto bad=shared;switch(i){case 0:bad.magic^=1;break;case 1:++bad.version;break;case 2:--bad.bytes;break;
        case 3:++bad.gamePid;break;case 4:bad.mountedInput.value.tick=899;break;case 5:bad.mountedInput.value.tick=1001;break;
        case 6:++bad.mountedInput.value.receiver.epoch;break;case 7:++bad.mountedInput.value.receiver.controller;break;
        case 8:++bad.mountedInput.value.receiver.recenter;break;case 9:bad.mountedInput.value.x=1.01f;break;
        case 10:bad.mountedInput.value.y=std::numeric_limits<float>::quiet_NaN();break;case 11:bad.mountedInput.value.buttons=1;break;default:++bad.mountedInput.sequence;break;}
        Check(!Input(bad,1234,1000,7,Kind::Mounted,shared.mounted.value,input),"stale, foreign, torn, invalid or mismatched mounted input is rejected");}
    auto replaced=shared;auto next=replaced.mounted.value;++next.epoch;Publish(replaced.mounted,next);
    Check(!Input(replaced,1234,1000,7,Kind::Mounted,next,input),"new mount generation cannot borrow a held rate from its previous incarnation");
    Check(!Input(shared,1234,1000,8,Kind::Mounted,shared.mounted.value,input),"recenter invalidates both receiver and input sample");
    Cancel(shared);Check(!Input(shared,1234,1000,7,Kind::Mounted,shared.mounted.value,input)&&!Input(shared,1234,1000,7,Kind::Passenger,passenger,input),"focus, menu or tracking cancellation retires both requests immediately");
    Check(Send(shared,1234,1000,7,Kind::Mounted,0,0)&&Input(shared,1234,1000,7,Kind::Mounted,shared.mounted.value,input)&&!input.x&&!input.y,"fresh neutral axes certify physical hand control without manufacturing a stick deflection");
    Check(!Send(shared,1234,1151,7,Kind::Mounted,1,0),"host cannot suppress native fallback without a live exact receiver");
    shared=Fresh();Intent intent;intent.allowed=intent.mounted=true;intent.mountedX=.5f;float mouseX=9,mouseY=-4;
    Check(Dispatch(shared,1234,1000,7,intent,mouseX,mouseY)==Route::Mounted&&!mouseX&&!mouseY&&Input(shared,1234,1000,7,Kind::Mounted,shared.mounted.value,input)&&input.x==.5f,
        "actual host dispatcher suppresses synthetic mounted mouse only after the exact receiver accepts its local rate");
    Publish(shared.mounted,Certificate{});mouseX=9;mouseY=-4;
    Check(Dispatch(shared,1234,1000,7,intent,mouseX,mouseY)==Route::Native&&mouseX==9&&mouseY==-4,
        "actual host dispatcher preserves native mouse fallback if current mount proof is absent");
    intent.mounted=false;intent.passenger=true;intent.passengerX=-.7f;intent.passengerY=.3f;intent.crouched=true;
    Check(Dispatch(shared,1234,1000,7,intent,mouseX,mouseY)==Route::Passenger&&mouseX==9&&mouseY==-4&&
        Input(shared,1234,1000,7,Kind::Passenger,shared.passenger.value,input)&&input.buttons==1&&input.x==-.7f,
        "actual passenger dispatcher forwards camera navigation while preserving native right-stick aim");
    intent.allowed=false;Check(Dispatch(shared,1234,1000,7,intent,mouseX,mouseY)==Route::Native&&
        !Input(shared,1234,1000,7,Kind::Passenger,shared.passenger.value,input),"actual host context gate cancels old passenger axes immediately on focus, UI or calibration loss");
    std::printf("Vehicle control mailbox: %u checks PASS.\n",checks);
}
