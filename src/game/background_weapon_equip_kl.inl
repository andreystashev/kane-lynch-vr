// Owned private-desktop fixture only. Borrow an actual scene weapon through
// the native hand/attachment API; no invented weapon, model or ammo fields.
namespace BackgroundWeaponEquip {
void Tick(std::uintptr_t player){
 if(!BackgroundDiagnostic::Active()||!NativeCoverKL::SimulationCallbackActive(player)||BackgroundDiagnostic::Age()<27500)return;
 static bool done=false,qualified=false,checked=false;static DWORD last=0;if(done||GetTickCount()-last<100)return;last=GetTickCount();
 wchar_t name[32]{};if(!GetEnvironmentVariableW(L"KL1_BACKGROUND_WEAPON",name,32)||!wcscmp(name,L"Original"))return;
 const bool p90=!wcscmp(name,L"P90"),mp5=!wcscmp(name,L"MP5");
 const unsigned wanted=!wcscmp(name,L"Revolver")?1:!wcscmp(name,L"Shotgun")?5:p90||mp5?2:
  !wcscmp(name,L"M4")?4:!wcscmp(name,L"Sniper")?15:!wcscmp(name,L"MachineGun")?3:UINT32_MAX;
 if(wanted==UINT32_MAX)return;
 const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char bytes[sizeof(expected)]{};return ReadBytes(image+rva,bytes,sizeof(bytes))&&!memcmp(bytes,expected,sizeof(bytes));};
 if(!checked){checked=true;const unsigned char right[]={0x56,0x8b,0x74,0x24,8,0x8b,0xc1,0x8b,0x90,0x58,3,0,0,0x8d,0x88,0x58,3,0,0,0x56,0x50,0x8b,2,0xff};
  const unsigned char left[]={0x56,0x8b,0x74,0x24,8,0x8b,0xc1,0x8b,0x90,0xd8,2,0,0,0x8d,0x88,0xd8,2,0,0,0x56,0x50,0x8b,2,0xff};
  const unsigned char remove[]={0x56,0x57,0x8b,0x7c,0x24,0xc,0x8b,0xf1,0x8b,0x4f,0x60,0x8b,1,0x8b,0x90,0xd0,1,0,0,0x6a,0,0xff,0xd2,0xa1};
  const unsigned char memberActor[]={0x55,0x8b,0xec,0x8b,0x41,0x40,0x8b,0x50,0x54};
  // Native ammo was verified before installation. Re-running its pristine
  // request signature after our request hook is installed would reject it.
  qualified=reloadAmmoProfile&&match(0x12a5f0,right)&&match(0x12a5d0,left)&&match(0x124d20,remove)&&match(0x308aa0,memberActor);
 }if(!qualified)return;
 const auto action=NativeActionKL::Current();if(!action.valid||action.player!=player||action.action||NativeCameraKL::CinematicActive())return;
 std::uint32_t control=0,vt=0,leader=0,count=0,data=0,actors=0;
 if(!Read(image+0x68760c,data)||!Read(data+4,actors)||!actors||actors>2048)return;
 const bool squad=Read(player+0xAA8,control)&&Read(control,vt)&&vt==image+NativeTeamCommandsImage::ControlVtable&&
   Read(control+4,leader)&&leader==player&&Read(control+0x14,count)&&count<=4;
 using Set=void(__thiscall*)(void*,std::uint32_t);using Remove=void(__thiscall*)(void*,void*,unsigned);
 const auto setRight=reinterpret_cast<Set>(image+0x12a5f0),setLeft=reinterpret_cast<Set>(image+0x12a5d0);const auto detach=reinterpret_cast<Remove>(image+0x124d20);
 const auto capacity=[](std::uintptr_t object,std::uintptr_t method){using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(object));};
 NativeWeaponOwnership::HandState localLeft;
 if(!NativeWeaponOwnership::ReadHandState(player+0x2d8,ReadBytes,localLeft)||localLeft.item)return;
 const auto borrow=[&](std::uint32_t actor){
  if(!actor||actor==player||!Read(actor,vt)||vt!=image+NativeTeamCommandsImage::ActorVtable)return false;
  NativeWeaponOwnership::HandState sourceRight,sourceLeft;
  if(!NativeWeaponOwnership::ReadHandState(actor+0x358,ReadBytes,sourceRight)||
     !NativeWeaponOwnership::ReadHandState(actor+0x2d8,ReadBytes,sourceLeft))return false;
  NativeWeaponOwnership::Snapshot inventory;if(!NativeWeaponOwnership::Capture(image,actor,ReadBytes,inventory))return false;
  for(unsigned i=0;i<inventory.count+2;++i){
   std::uintptr_t root=0;
   std::uint32_t item=0,model=0,type=0,ownerMethod=0;
   if(i<2){
    item=i==0?sourceRight.item:sourceLeft.item;
    std::uint32_t direct=0;
    if(!Read(item+4,direct)||!direct)continue;
    root=direct;
   }else if(!NativeWeaponOwnership::ResolveGeometry(image,inventory.records[i-2].geometryRef,ReadBytes,root)||!Read(root+0x60,item))continue;
   std::uint32_t owner=0;
   if(!item||!Read(root+0x60,owner)||owner!=item||!Read(item,vt)||(vt!=image+0x54ea4c&&vt!=image+0x577b74)||
      !Read(vt+0x290,ownerMethod)||ownerMethod!=image+0x11d230||
      reinterpret_cast<Owner>(ownerMethod)(reinterpret_cast<void*>(item))!=reinterpret_cast<void*>(actor)||
      !Read(item+0xC0,model)||!Read(model+0xA4,type))continue;
   if(type!=wanted)continue;
   // Two rifle families and SMGs share categories. Prove the requested
   // native frame shape instead of calling every category4 model an M4.
   if(wanted==2||wanted==4){std::uint32_t h=0;std::uintptr_t asset=0;ReloadPartsKL::Catalog catalog;bool shape=false;
    if(Read(root+0x68,h)&&NativeWeaponOwnership::Resource(image,h,ReadBytes,asset)&&ReloadPartsKL::ModelCatalog(image,asset,ReadBytes,catalog))for(unsigned j=0;j<catalog.count;++j){ReloadPartsKL::Part p;if(ReloadPartsKL::Bone(catalog,j,ReadBytes,p)&&!strcmp(p.name,"w_bone_frame")){
     shape=p90?ReloadContactGeometryKL::P90Frame(p.half):mp5?
       ReloadContactGeometryKL::MP5Frame(p.half):
       std::fabs(p.half.x-3.45747f)<.01f&&std::fabs(p.half.z-45.25363f)<.01f;break;}}
    if(!shape)continue;
   }
   std::uint32_t getter=0;
   if(p90&&(!Read(vt+0x310,getter)||getter!=image+0x11ddd0||capacity(item,getter)!=50))continue;
   std::uint32_t reference=0;if(!NativeTeamCommandsImage::Reference(image,root,ReadBytes,reference))continue;
   done=true;
   if(sourceRight.item==item)setRight(reinterpret_cast<void*>(actor),0);if(sourceLeft.item==item)setLeft(reinterpret_cast<void*>(actor),0);
   NativeWeaponOwnership::Snapshot remaining;if(NativeWeaponOwnership::Capture(image,actor,ReadBytes,remaining))for(unsigned j=0;j<remaining.count;++j)if(remaining.records[j].geometryRef==reference){detach(reinterpret_cast<void*>(actor),reinterpret_cast<void*>(root),remaining.records[j].bone);break;}
   setRight(reinterpret_cast<void*>(player),0);setRight(reinterpret_cast<void*>(player),reference);
   NativeReloadAmmoKL::Snapshot after;
   bool accepted=NativeReloadAmmoKL::Capture(image,player,item,ReadBytes,capacity,after)&&after.category==wanted&&
       (!p90||after.ammo.capacity==50);
   const auto vacancies=wanted==3?12:wanted==5?
       (BackgroundDiagnostic::Rack()?after.ammo.capacity-1:int(ShotgunReloadKL::ShellsPerGrip)):wanted==15?1:0;
   if(accepted&&BackgroundDiagnostic::Controls()&&vacancies&&
      after.ammo.count==after.ammo.capacity&&after.ammo.count>vacancies){
    // Move existing rounds into reserve to exercise the loader in this private replay.
    auto moved=after.ammo;
    moved.count-=vacancies;moved.reserve+=vacancies;
    const auto setter=[](std::uintptr_t method,std::uintptr_t object,int value){
     using Call=void(__thiscall*)(void*,int);reinterpret_cast<Call>(method)(reinterpret_cast<void*>(object),value);
    };
    accepted=NativeReloadAmmoKL::Commit(image,after,moved,ReadBytes,capacity,setter)&&
        NativeReloadAmmoKL::Capture(image,player,item,ReadBytes,capacity,after);
   }
   wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))if(FILE* f=_wfopen((std::wstring(folder)+L"\\weapon-equip.json").c_str(),L"wb")){
    fprintf(f,"{\"nativeAcknowledged\":%s,\"sourceActor\":%u,\"item\":%u,\"category\":%u,\"count\":%d,\"reserve\":%d,\"capacity\":%d}\n",accepted?"true":"false",actor,item,wanted,after.ammo.count,after.ammo.reserve,after.ammo.capacity);fclose(f);
   }
   if(folder[0]&&(wanted==5||wanted==15)){
    std::uint32_t handle=0;std::uintptr_t asset=0;ReloadPartsKL::Catalog catalog;
    if(Read(root+0x68,handle)&&NativeWeaponOwnership::Resource(image,handle,ReadBytes,asset)&&
       ReloadPartsKL::ModelCatalog(image,asset,ReadBytes,catalog))
     if(FILE* f=_wfopen((std::wstring(folder)+L"\\equipped-parts.jsonl").c_str(),L"wb")){
      for(unsigned j=0;j<catalog.count;++j){ReloadPartsKL::Part part;
       if(!ReloadPartsKL::Bone(catalog,j,ReadBytes,part))continue;
       fprintf(f,"{\"bone\":%u,\"name\":\"%s\",\"bounds\":%s,\"bind\":%s,\"centre\":[%.4f,%.4f,%.4f],\"half\":[%.4f,%.4f,%.4f],\"position\":[%.4f,%.4f,%.4f]}\n",
           j,part.name,part.boundsValid?"true":"false",part.bindValid?"true":"false",
           part.centre.x,part.centre.y,part.centre.z,part.half.x,part.half.y,part.half.z,
           part.bind.position.x,part.bind.position.y,part.bind.position.z);
      }
      fclose(f);
     }
   }
   return true;
  }
  return false;
 };
 if(squad)for(unsigned member=0;member<count;++member){
  std::uint32_t receiver=0,back=0,actor=0;
  if(!Read(control+0x18+member*4,receiver)||!Read(receiver,vt)||vt!=image+0x58dfbc||
     !Read(receiver+0x238,back)||back!=player||!Read(receiver+0x40,actor))continue;
  for(unsigned a=0;a<actors;++a){
   std::uint32_t listed=0;
   if(Read(data+8+a*4,listed)&&listed==actor){if(borrow(actor))return;break;}
  }
 }
 // Other actual scene actors can carry a model absent from the local squad.
 for(unsigned a=0;a<actors;++a){std::uint32_t actor=0;if(Read(data+8+a*4,actor)&&borrow(actor))return;}
}
}
