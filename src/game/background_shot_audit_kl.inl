// Count actual native ammunition consumption, including the unchanged native
// fallback when a rendered hand snapshot expires under concurrent load.
// Tracked-aim calls alone are not an ammunition conservation counter.
struct BackgroundShotAudit {
 inline static thread_local bool active=false;
 inline static bool pending=false,blocked=false;
 inline static NativeReloadAmmoKL::Snapshot before;
 bool owner=false;
 static int Capacity(std::uintptr_t item,std::uintptr_t method){using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(item));}
 explicit BackgroundShotAudit(void* item){
  if(!BackgroundDiagnostic::Active()||active||!reloadAmmoProfile)return;
  owner=true;active=true;Flush();blocked=false;std::uint32_t data=0,player=0;
  pending=Read(image+0x68760c,data)&&Read(data+0xa2c,player)&&NativeReloadAmmoKL::Capture(image,player,reinterpret_cast<std::uintptr_t>(item),ReadBytes,Capacity,before);
 }
 ~BackgroundShotAudit(){if(owner)active=false;}
 void Blocked(){if(owner)blocked=true;}
 static void Flush(){
  if(!BackgroundDiagnostic::Active()||!pending)return;pending=false;
  // The weapon updater decrements ammo after its Fire body returns. Sample
  // on the next owned simulation boundary, before any reload transaction.
  NativeReloadAmmoKL::Snapshot after;
  const bool same=NativeReloadAmmoKL::Capture(image,before.player,before.item,ReadBytes,Capacity,after)&&NativeReloadAmmoKL::SameOwner(before,after);
  wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768)){
   if(FILE* f=_wfopen((std::wstring(folder)+L"\\native-shot-ammo.jsonl").c_str(),L"ab")){
    fprintf(f,"{\"ageMs\":%lu,\"player\":%u,\"item\":%u,\"reference\":%u,\"model\":%u,\"category\":%u,\"sameOwner\":%s,\"blocked\":%s,\"countBefore\":%d,\"countAfter\":%d,\"reserveBefore\":%d,\"reserveAfter\":%d}\n",BackgroundDiagnostic::Age(),unsigned(before.player),unsigned(before.item),before.reference,unsigned(before.model),before.category,same?"true":"false",blocked?"true":"false",before.ammo.count,same?after.ammo.count:-1,before.ammo.reserve,same?after.ammo.reserve:-1);fclose(f);
   }
  }
 }
};
