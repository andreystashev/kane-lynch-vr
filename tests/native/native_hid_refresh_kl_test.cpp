#include "native_hid_refresh_kl.hpp"
#include <cstdio>

int main() {
    using namespace NativeHidRefreshKL;
    unsigned checks=0,failures=0;
    const auto check=[&](bool pass,const char* label) {
        ++checks;
        if(!pass){++failures;std::printf("FAIL %s\n",label);}
    };
    std::uintptr_t image=0x400000;
    auto* input=reinterpret_cast<IDirectInput8A*>(0x100000);
    auto* manager=reinterpret_cast<void*>(0x200000);
    auto* callback=reinterpret_cast<LPDIENUMDEVICESCALLBACKA>(image+PollCallbackRva);
    const auto result=image+PollReturnRva;
    check(OwnedCall(image,result,input,input,DI8DEVCLASS_ALL,callback,manager,manager,DIEDFL_ATTACHEDONLY),"exact periodic scan");
    check(!OwnedCall(image,image+0x4C011,input,input,DI8DEVCLASS_ALL,callback,manager,manager,DIEDFL_ATTACHEDONLY),"startup call remains native");
    check(!OwnedCall(image,result,input,nullptr,DI8DEVCLASS_ALL,callback,manager,manager,DIEDFL_ATTACHEDONLY),"old DirectInput instance remains native");
    check(!OwnedCall(image,result,input,input,DI8DEVCLASS_GAMECTRL,callback,manager,manager,DIEDFL_ATTACHEDONLY),"different DirectInput query remains native");
    check(!OwnedCall(image,result,input,input,DI8DEVCLASS_ALL,nullptr,manager,manager,DIEDFL_ATTACHEDONLY),"foreign callback remains native");
    check(!OwnedCall(image,result,input,input,DI8DEVCLASS_ALL,callback,manager,nullptr,DIEDFL_ATTACHEDONLY),"foreign manager remains native");
    check(!OwnedCall(image,result,input,input,DI8DEVCLASS_ALL,callback,manager,manager,0),"different flags remain native");
    check(!VerifiedCode(0),"unknown executable base rejected");

    Refresh state;unsigned calls=0;HRESULT expected=DI_OK;
    const auto native=[&]{++calls;return expected;};
    check(state.Poll(native)==DI_OK&&calls==1,"first scan is synchronous");
    check(state.Poll(native)==DI_OK&&calls==1&&state.skipped==1,"same topology does not call DirectInput twice");
    state.Changed();
    check(state.Poll(native)==DI_OK&&calls==2&&state.changes==1,"interface change invalidates scan");
    check(state.Poll(native)==DI_OK&&calls==2,"new topology is cached");
    state.Changed();
    expected=E_FAIL;
    check(state.Poll(native)==E_FAIL&&calls==3&&state.failures==1,"original failure propagates");
    check(state.Poll(native)==E_FAIL&&calls==4&&state.failures==2,"failure keeps pending refresh");
    expected=DI_OK;
    check(state.Poll(native)==DI_OK&&calls==5,"recovered scan acknowledges same topology");
    state.Changed();
    check(state.Poll([&]{state.Changed();return native();})==DI_OK&&calls==6,"arrival during scan does not invalidate its result");
    check(state.Poll(native)==DI_OK&&calls==7,"arrival during scan is observed by next native poll");
    check(state.Poll(native)==DI_OK&&calls==7&&state.scans==7,"steady state skips only successful generations");
    state.ObserveInstance(input,manager);
    check(state.Poll(native)==DI_OK&&calls==8,"new input manager cannot reuse an old scan");
    state.ObserveInstance(input,manager);
    check(state.Poll(native)==DI_OK&&calls==8,"same native object and manager retain the scan");
    state.Invalidate();
    check(state.Poll(native)==DI_OK&&calls==9,"native startup invalidates identities recycled at the same addresses");

    const auto counter=refresh.changes.load();
    check(OnNotification(nullptr,nullptr,CM_NOTIFY_ACTION_DEVICEQUERYREMOVE,nullptr,0)==ERROR_SUCCESS&&refresh.changes==counter,
          "unrelated notification has zero effect");
    check(OnNotification(nullptr,nullptr,CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL,nullptr,0)==ERROR_SUCCESS&&refresh.changes==counter+1,
          "HID arrival schedules refresh");
    check(OnNotification(nullptr,nullptr,CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL,nullptr,0)==ERROR_SUCCESS&&refresh.changes==counter+2,
          "HID removal schedules refresh");
    std::printf("Native HID refresh: %u checks, %u failures.\n",checks,failures);
    return failures?1:0;
}
