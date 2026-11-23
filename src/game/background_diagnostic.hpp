#pragma once
#include <windows.h>
#include <cwchar>
// Synthetic tracking is restricted to an explicitly owned private test desktop.
namespace BackgroundDiagnostic {
inline bool Active(){
 static const bool enabled=[](){wchar_t root[32768]{},desktop[128]{};DWORD needed=0;BOOL owned=FALSE;
  if(!GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",root,32768)||!GetEnvironmentVariableW(L"KL1_BACKGROUND_RUNTIME",root,32768))return false;
  if(!IsProcessInJob(GetCurrentProcess(),nullptr,&owned)||!owned)return false;
  return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)&&!std::wcsncmp(desktop,L"KL1-Test-",9);
 }();return enabled;
}
inline bool Focused(bool real){return real||Active();}
inline DWORD Age(bool gameClock=false){
 wchar_t tick[32]{};
 if(!(gameClock&&GetEnvironmentVariableW(L"KL1_BACKGROUND_GAME_TICK",tick,32))&&
    !GetEnvironmentVariableW(L"KL1_BACKGROUND_BEGIN_TICK",tick,32))return 0;
 const LONG elapsed=LONG(GetTickCount()-std::wcstoul(tick,nullptr,10));
 return elapsed>0?DWORD(elapsed):0;
}
inline DWORD GameAge(){return Age(true);}
inline bool Exercise(){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_EXERCISE",value,4)&&value[0]==L'1';}
inline bool Rack(){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_RACK",value,4)&&value[0]==L'1';}
inline bool ScopedReload(){
 static const bool enabled=[](){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_SCOPE_RELOAD",value,4)==1&&value[0]==L'1';}();
 return enabled;
}
inline bool PreGrab(){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_PREGRAB",value,4)&&value[0]==L'1';}
inline bool Driven(){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_DRIVEN",value,4)&&value[0]==L'1';}
inline bool Controls(){wchar_t value[4]{};return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_CONTROLS",value,4)&&value[0]==L'1';}
inline bool TeamPanelTest(){
 static const bool enabled=[](){wchar_t value[4]{};
  return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_TEAM_TEST",value,4)==1&&value[0]==L'1';
 }();
 return enabled;
}
inline bool TeamPanelHeld(DWORD age){return age>=27500&&age<42000;}
inline DWORD TeamPanelGrip(DWORD age){
 constexpr DWORD presses[]{29000,36500,39000};
 for(const auto press:presses)if(age>=press&&age<press+250)return press;
 return 0;
}
inline unsigned TeamPanelOrder(DWORD age){
 if(age>=31000&&age<31800)return 1;
 if(age>=34500&&age<35300)return 2;
 return 0;
}
enum VisualBenchFeature : unsigned {BenchReticle=1,BenchLaser=2,BenchWrists=4};
inline bool VisualBench(){
 static const bool enabled=[](){wchar_t value[4]{};
  return Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_VISUAL_BENCH",value,4)==1&&value[0]==L'1';
 }();
 return enabled;
}
inline bool BenchInventory(DWORD age){return age>=58000&&age<82000;}
inline unsigned BenchFeatures(DWORD age){
 constexpr unsigned aiming[]{0,BenchReticle,BenchLaser,BenchLaser|BenchReticle,BenchLaser,BenchReticle,0};
 if(age>=30000&&age<58000)return aiming[(age-30000)/4000];
 return BenchInventory(age)&&((age-58000)/4000)%2?BenchWrists:0u;
}
inline SHORT Key(int key){if(!Active())return GetAsyncKeyState(key);DWORD age=Age();return key=='R'&&Exercise()&&!Controls()&&age>=32000&&age<32250?SHORT(0x8000):0;}
}
