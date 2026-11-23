// Included in NativeWeaponKL private namespace. Live controller mode is opt-in;
// scripted contacts additionally require the owned private background desktop.
namespace ReloadDiagnostic {
namespace M=PhysicalReloadRuntimeKL;namespace G=ReloadContactGeometryKL;namespace R=NativeRevolverReloadKL;
bool Enabled();
void Survey(std::uintptr_t entry,std::uintptr_t extra,std::uintptr_t state){
 if(!BackgroundDiagnostic::Active()||!extra)return;
 std::uint32_t geometry=0;if(!Read(entry+0xF4,geometry))return;auto at=geometry;
 for(unsigned depth=0;at&&depth<12;++depth){std::uint32_t owner=0,vt=0,model=0,type=0,category=0,reference=0,parent=0;
  if(!Read(at+0x60,owner)||!Read(at+0x50,parent))return;
  if(owner&&Read(owner,vt)&&(vt==image+0x54EA4C||vt==image+0x577B74)&&Read(owner+0xC0,model)&&Read(model,type)&&(type==image+0x54E6D4||type==image+0x577864)&&Read(model+0xA4,category)&&PhysicalReloadKL::Supported(category)&&Read(owner+0x58,reference)){
   ReloadPartsKL::Part part;if(!ReloadPartsKL::Inspect(image,entry,extra,geometry,ReadBytes,part)||!part.boundsValid||!part.bindValid)return;
   wchar_t folder[32768]{};GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768);
   if(!strcmp(part.name,"w_bone_frame")){
    static std::uintptr_t known[64]{};static unsigned used=0;bool found=false;for(unsigned i=0;i<used;++i)if(known[i]==part.model)found=true;
    if(!found&&used<64){known[used++]=part.model;ReloadPartsKL::Catalog catalog;
     if(ReloadPartsKL::ModelCatalog(image,part.model,ReadBytes,catalog))if(FILE* file=_wfopen((std::wstring(folder)+L"\\weapon-catalog.jsonl").c_str(),L"ab")){
      for(unsigned i=0;i<catalog.count;++i){ReloadPartsKL::Part b;if(!ReloadPartsKL::Bone(catalog,i,ReadBytes,b)||!b.bindValid||!b.boundsValid)continue;
       fprintf(file,"{\"model\":%u,\"category\":%u,\"bone\":%u,\"name\":\"%s\",\"centre\":[%.6f,%.6f,%.6f],\"half\":[%.6f,%.6f,%.6f],\"bind\":[",model,category,i,b.name,b.centre.x,b.centre.y,b.centre.z,b.half.x,b.half.y,b.half.z);
       const float* p=reinterpret_cast<const float*>(&b.bind);for(unsigned j=0;j<12;++j)fprintf(file,"%s%.7f",j?",":"",p[j]);fprintf(file,"]}\n");
      }fclose(file);
     }
    }
   }
   if((category==1||category==5)&&BackgroundDiagnostic::Age()>=26000&&BackgroundDiagnostic::Age()<39500){
    struct MotionKey {std::uintptr_t owner=0;unsigned bone=0;DWORD tick=0;};static MotionKey keys[128]{};MotionKey* key=nullptr;
    for(auto& k:keys)if(k.owner==owner&&k.bone==part.bone){key=&k;break;}if(!key)for(auto& k:keys)if(!k.owner){k.owner=owner;k.bone=part.bone;key=&k;break;}
    CameraPose skin,root,bone;std::uint32_t index=0;
    if(key&&GetTickCount()-key->tick>=70&&Read(image+kGpuIndex,index)&&index<2&&Read(entry+0x10+index*0x30,root)&&Read(state+0x8C,skin)&&G::BoneWorld(skin,part.bind,bone)){
     const auto local=ReloadMathKL::ToLocal(root,bone);if(ValidatePose(local,.05f)){key->tick=GetTickCount();if(FILE* file=_wfopen((std::wstring(folder)+L"\\weapon-motion.jsonl").c_str(),L"ab")){
      fprintf(file,"{\"ageMs\":%lu,\"owner\":%u,\"model\":%u,\"category\":%u,\"bone\":%u,\"name\":\"%s\",\"pose\":[",BackgroundDiagnostic::Age(),owner,model,category,part.bone,part.name);const float* p=reinterpret_cast<const float*>(&local);for(unsigned j=0;j<12;++j)fprintf(file,"%s%.7f",j?",":"",p[j]);fprintf(file,"]}\n");fclose(file);
     }}
    }
   }
   struct Seen {std::uintptr_t model=0;unsigned bone=0;};static Seen seen[256]{};static unsigned count=0;
   for(unsigned i=0;i<count;++i)if(seen[i].model==model&&seen[i].bone==part.bone)return;if(count>=256)return;seen[count++]={model,part.bone};
   for(char& c:part.name)if(c&&((unsigned char)c<32||c=='"'||c=='\\'))c='_';
   if(folder[0])if(FILE* file=_wfopen((std::wstring(folder)+L"\\weapon-survey.jsonl").c_str(),L"ab")){
    fprintf(file,"{\"owner\":%u,\"model\":%u,\"reference\":%u,\"category\":%u,\"bone\":%u,\"name\":\"%s\",\"centre\":[%.5f,%.5f,%.5f],\"half\":[%.5f,%.5f,%.5f],\"bind\":[%.5f,%.5f,%.5f]}\n",owner,model,reference,category,part.bone,part.name,part.centre.x,part.centre.y,part.centre.z,part.half.x,part.half.y,part.half.z,part.bind.position.x,part.bind.position.y,part.bind.position.z);fclose(file);
   }
   return;
  }if(parent==at)return;at=parent;
 }
}
struct Contacts {unsigned category=0;std::uintptr_t item=0,model=0;std::uint32_t reference=0;DWORD tick=0;bool frame=false,clip=false,slide=false,akBolt=false,akBoltPose=false;G::Bounds frameBox{};CameraPose akBoltBind{},akBoltLocal{};ReloadPartsKL::Part clipPart{},slidePart{};CameraPose clipBone{},slideBone{};
 R::Contacts revolver;ShotgunReloadKL::Parts shotgun;
};
struct View {int rounds=0,reserve=0;bool ready=false,inserted=true,held=false,blocked=false,supportBusy=false,scoped=false;
 bool seatReleased=false,slideAllowed=false,slideShared=false,needsChamber=false,ak74=false;
 DWORD roundTick=0;CameraPose roundPose{};std::uintptr_t magazineAsset=0;
 std::uint32_t recenter=UINT32_MAX;DWORD palmTick=0;G::SlideContact slideLocal{};
 PhysicalReloadKL::Loader loader=PhysicalReloadKL::Loader::Native;CameraPose reloadSocket{};unsigned category=0;
 R::Geometry revolver;CameraPose spareShells[ShotgunReloadKL::ShellsPerGrip-1]{};unsigned shellRounds=0;bool slideReturning=false,integratedTube=false,mp5=false,breech=false,boltOpen=false,cylinderDocked=false;unsigned cylinderRounds=0;float cylinderOpening=0;std::uintptr_t player=0,item=0,model=0;std::uint32_t reference=0;DWORD tick=0;CameraPose magazine{},wrist{},slideAnchor{},slideHand{},palmLocal{};Vec3 barrelLocal{},magHalf{};float units=100,feedLength=0;bool palmValid=false;M::WorldMagazine world[3]{};float slideTravel=0;unsigned phase=0;};
std::atomic<bool> configured{false},renderProp{false};
SRWLOCK lock=SRWLOCK_INIT;Contacts contacts;View view;M::Engine engine;ReloadBodyPocketKL::State pocket;
M::DryTrigger dryTrigger;
DWORD consumedReload=0;
DWORD ReloadTap(DWORD now){
 const auto intent=VRBridge::ReadSecondaryFire();
 return intent.valid&&intent.reloadTick&&intent.reloadTick!=consumedReload&&now-intent.reloadTick<=NativeReloadKL::IntentMaximumAgeMs&&
    !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()?intent.reloadTick:0;
}
DWORD worldTick=0;float floors[3]{};bool floorKnown[3]{};unsigned floorSerial[3]{};
std::atomic<unsigned> feedbackEvents{0},feedbackCategory{0},sounds{0},impacts{0};ReloadFeedbackIPC::Channel feedback;
std::atomic<bool> feedbackReady{false};
std::atomic<unsigned> nativeCues{0};
void FeedbackWorker(){
 if(!Enabled())return;static bool initialized=false;if(!initialized){initialized=ReloadAudioKL::Initialize();nativeCues=ReloadAudioKL::nativeVoices;feedbackReady=feedback.Open(GetCurrentProcessId(),true);}
 const auto events=feedbackEvents.exchange(0),category=feedbackCategory.load();
 using E=ReloadAudioKL::Effect;const auto play=[&](E e){if(ReloadAudioKL::Play(e,category))++sounds;};
 if(events&M::Eject)play(E::Release);if(events&M::Take)play(E::Take);if(events&M::Insert)play(E::Insert);if(events&M::Back)play(E::Back);if(events&M::Forward)play(E::Forward);if(events&M::Impact)play(E::Impact);
 if(events&M::DryFire)play(E::Empty);
}
void Feedback(unsigned events,unsigned category,DWORD now){
 feedbackCategory=category;feedbackEvents.fetch_or(events);
 if(!feedbackReady)return;
 if(events&M::Eject)feedback.Send(1,.35f,28,now);if(events&M::Take)feedback.Send(0,.25f,24,now);
 if(events&M::Insert){feedback.Send(0,.7f,42,now);feedback.Send(1,.65f,36,now);}
 if(events&M::Back)feedback.Send(0,.45f,26,now);if(events&M::Forward)feedback.Send(0,.65f,36,now);
 if(events&M::DryFire)feedback.Send(1,.20f,24,now);
}
std::atomic<unsigned> dryClicks{0};
DWORD ejectedAt=0,heldAt=0,latchedAt=0;
std::atomic<unsigned> ejected{0},taken{0},inserted{0},racked{0},rejected{0},remapped{0},hidden{0};
thread_local bool hidePending=false;thread_local std::uintptr_t hiddenEntry=0,hiddenState=0;
thread_local std::uint32_t hiddenRecord=0;
struct ClipPacket {bool valid=false;std::uintptr_t item=0,model=0,state=0,entry=0;std::uint32_t reference=0;CameraPose skin{},seated{},spares[2]{};unsigned spareCount=0;M::WorldMagazine world[3]{};};
thread_local ClipPacket clipPending,clipBound;thread_local bool copying=false;
std::atomic<unsigned> worldDraws{0},worldRejected{0},spareDraws{0};
std::atomic<std::uintptr_t> detachedRoot{0};
std::atomic<std::uintptr_t> detachedMagazineAsset{0};
bool Scripted(){wchar_t flag[4]{};return BackgroundDiagnostic::Active()&&GetEnvironmentVariableW(L"KL1_BACKGROUND_MANUAL",flag,4)&&flag[0]==L'1';}
bool Enabled(){return configured.load()||Scripted();}
View Current(){AcquireSRWLockShared(&lock);auto value=view;ReleaseSRWLockShared(&lock);return value;}
bool SameOwner(const View& v,std::uintptr_t player,std::uintptr_t item,std::uintptr_t model,std::uint32_t ref,std::uint32_t recenter){
 return v.player==player&&v.item==item&&v.model==model&&v.reference==ref&&v.recenter==recenter;
}
const CameraPose* HeldPalm(const View& v,const NativeHandsKL::VisualFrame& hands){
 if(!hands.tracked[0])return nullptr;
 return hands.palmValid[0]?&hands.palmLocal[0]:v.palmValid?&v.palmLocal:nullptr;
}
void Publish(const View& next){
 AcquireSRWLockExclusive(&lock);view=next;ReleaseSRWLockExclusive(&lock);
 renderProp.store((next.held&&next.loader==PhysicalReloadKL::Loader::Shell)||next.roundTick,std::memory_order_relaxed);
 std::uint32_t root=0;
 if(!next.inserted&&(next.loader==PhysicalReloadKL::Loader::MagazineAndSlide||
                     next.loader==PhysicalReloadKL::Loader::MagazineOnly))Read(next.item+4,root);
 detachedMagazineAsset.store(root?next.magazineAsset:0,std::memory_order_relaxed);
 detachedRoot.store(root,std::memory_order_relaxed);
}
void Invalidate(){
 renderProp.store(false,std::memory_order_relaxed);
 AcquireSRWLockExclusive(&lock);
 view.ready=false;
 if(view.loader==PhysicalReloadKL::Loader::Shell)view.held=false;
 ReleaseSRWLockExclusive(&lock);
}
bool Owns(std::uintptr_t item){
 if(!Enabled()||!trackedReloadContext||!VRBridge::Ready()||NativeCameraKL::CinematicActive())return false;
 const auto v=Current();
 const bool opened=v.item&&(!v.inserted||v.held);
 // A transient pose miss immediately pauses cylinder gestures. Keep the
 // last qualified owner's short lease so A can reach the next valid tick
 // instead of dispatching a native reload in the middle of that pause.
 const bool fresh=(v.ready||v.loader==PhysicalReloadKL::Loader::Cylinder)&&GetTickCount()-v.tick<=150;
 // A proven tube loader owns the reload policy while the exact shotgun is
 // held. An empty tube or a temporary missing draw pauses gestures, but
 // cannot authorize the game's automatic refill.
 const bool shell=v.loader==PhysicalReloadKL::Loader::Shell;
 std::uint32_t ref=0,model=0;NativeReloadKL::Observation held{};
 return (opened||fresh||shell)&&v.item==item&&NativeReloadKL::Capture(image,v.player,item,ReadBytes,held)&&
    held.side==1&&Read(item+0x58,ref)&&ref==v.reference&&Read(item+0xC0,model)&&model==v.model;
}
bool DetachedClip(std::uintptr_t entry,std::uintptr_t extra){
 const auto detached=detachedRoot.load(std::memory_order_relaxed);
 std::uint32_t geometry=0;
 if(!detached||!extra||!Read(entry+0xF4,geometry)||!geometry||!Enabled())return false;
 if(geometry!=detached){
     const auto clipAsset=detachedMagazineAsset.load(std::memory_order_relaxed);
     std::uint32_t handle=0;std::uintptr_t asset=0;
     if(!clipAsset||!Read(geometry+0x68,handle)||!NativeWeaponOwnership::Resource(image,handle,ReadBytes,asset)||
        asset!=clipAsset)return false;
 }
 ReloadPartsKL::Part part;
 if(!ReloadPartsKL::Inspect(image,entry,extra,geometry,ReadBytes,part)||std::strcmp(part.name,"w_bone_clip"))return false;
 // Native rigid materials can bind a child of the firearm model. The clip
 // must reach the exact detached gun root by the same geometry parent links.
 auto parent=std::uintptr_t(geometry);
 for(unsigned depth=0;parent&&parent!=detached&&depth<8;++depth){
     NativeWeaponOwnership::GeometryLinks links;
     if(!NativeWeaponOwnership::ReadGeometryLinks(parent,ReadBytes,links)||links.parent==parent)return false;
     parent=links.parent;
 }
 if(parent!=detached)return false;
 const auto v=Current();
 if(v.inserted||!v.item||
    (v.loader!=PhysicalReloadKL::Loader::MagazineAndSlide&&v.loader!=PhysicalReloadKL::Loader::MagazineOnly))return false;
 std::uint32_t root=0,owner=0,ref=0,model=0;
 // The magazine remains removed during a render pose miss or an ammo
 // transaction. Verify the draw and equipped item without reading ammo.
 if(!Read(v.item+4,root)||root!=detached||!Read(root+0x60,owner)||owner!=v.item||
    !NativeReloadKL::IsLocalPlayer(image,v.player,ReadBytes)||!HeldBy(v.player,v.item,1,false)||
    !Read(v.item+0x58,ref)||ref!=v.reference||!Read(v.item+0xC0,model)||model!=v.model)return false;
 return true;
}
struct BlockedFire {NativeReloadAmmoKL::Snapshot before;unsigned attempts=0;};
thread_local BlockedFire pendingFire;
int ReadCapacity(std::uintptr_t item,std::uintptr_t method){
 using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(item));
}
void SetAmmo(std::uintptr_t method,std::uintptr_t item,int value){
 using Set=void(__thiscall*)(void*,int);reinterpret_cast<Set>(method)(reinterpret_cast<void*>(item),value);
}
bool FireBlocked(std::uintptr_t item){
 if(!Owns(item))return false;
 const auto current=Current();if(current.item!=item||!current.blocked)return false;
 NativeReloadAmmoKL::Snapshot before;
 if(reloadAmmoProfile&&NativeReloadAmmoKL::Capture(image,current.player,item,ReadBytes,ReadCapacity,before)){
  if(!NativeReloadAmmoKL::SameOwner(pendingFire.before,before))pendingFire={before,0};
  ++pendingFire.attempts;
 }
 return true;
}
void RestoreBlockedFire(std::uintptr_t player){
 if(!pendingFire.attempts)return;
 const auto pending=pendingFire;pendingFire={};
 if(player!=pending.before.player)return;
 // The native updater can debit a round after our rejected Fire callback.
 // Reconcile that exact debit before observing ammo or starting a reload.
 NativeReloadAmmoKL::Snapshot after;
 if(NativeReloadAmmoKL::Capture(image,player,pending.before.item,ReadBytes,ReadCapacity,after)&&
    NativeReloadAmmoKL::BlockedFireDebit(pending.before,after,pending.attempts)&&
    !NativeReloadAmmoKL::Commit(image,after,pending.before.ammo,ReadBytes,ReadCapacity,SetAmmo))++rejected;
}
void HandlingInput(const NativeReloadAmmoKL::Snapshot& source,const View& v){
 const auto now=GetTickCount();const auto action=NativeActionKL::Current();
 const bool allowed=action.valid&&action.player==source.player&&action.WeaponTrackingAllowed()&&
    !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible();
 if(!allowed){dryTrigger={};return;}
 const auto ref=static_cast<std::uint32_t>(VRBridge::RecenterAck());
 const bool manual=v.ready&&SameOwner(v,source.player,source.item,source.model,source.reference,ref)&&now-v.tick<=150;
 thread_local HandsIPC::Consumer input;
 HandsIPC::Frame hands{};
 const bool valid=input.Open(GetCurrentProcessId())&&input.Read(hands,now)&&HandsMath::ValidFrame(hands,now,ref)&&
    (hands.inputValid[1]&HandsIPC::kTriggerInput);
 const bool dry=dryTrigger.Step(source.Token(),source.reference,ref,hands.tick,hands.trigger[1],valid,
                               manual?v.blocked:source.ammo.count==0,source.ammo.count);
 if(dry){++dryClicks;Feedback(M::DryFire,source.category,now);}
}
bool HandTarget(std::uintptr_t player,unsigned side,const CameraPose& gunWrist,CameraPose& target){
 if(side||!Enabled())return false;const auto v=Current();if(v.player!=player||!v.ready||GetTickCount()-v.tick>100||!Owns(v.item))return false;
 if(v.loader==PhysicalReloadKL::Loader::Shell){
  const auto phase=static_cast<PhysicalReloadKL::Phase>(v.phase);
  if(!v.palmValid||!ValidatePose(target,.05f))return false;
  Vec3 goal;
  const auto rebase=[&](const CameraPose& pose){return ReloadMathKL::ToWorld(gunWrist,ReloadMathKL::ToLocal(v.wrist,pose));};
  const bool pump=phase==PhysicalReloadKL::Phase::Slide&&!v.slideReturning;
  if(pump)goal=Subtract(rebase(v.slideAnchor).position,Scale(ReloadMathKL::Transform(gunWrist.basis,v.barrelLocal),v.slideTravel*v.units));
  else if(v.held&&phase==PhysicalReloadKL::Phase::Seated)
   goal=ShotgunReloadKL::PalmForShell(rebase(v.magazine),v.magHalf,v.units,v.breech).position;
  else return false;
  return ReloadPresentationKL::GuidePalm(target,v.palmLocal,goal,(pump?.09f:.055f)*v.units);
 }
 if(v.loader==PhysicalReloadKL::Loader::Cylinder){
  if(!v.cylinderDocked||!v.palmValid||!ValidatePose(target,.05f))return false;
  auto grip=ReloadMathKL::ToWorld(gunWrist,ReloadMathKL::ToLocal(v.wrist,v.magazine));
  grip.position=Subtract(grip.position,Scale(grip.basis.forward,v.feedLength));
  const auto palm=ReloadPresentationKL::PalmFromGrip(grip,v.magHalf,v.units);
  return ReloadPresentationKL::GuidePalm(target,v.palmLocal,palm.position,.09f*v.units);
 }
 if(v.phase==unsigned(PhysicalReloadKL::Phase::Finishing)&&v.held&&!v.seatReleased&&v.palmValid){
  auto grip=ReloadMathKL::ToWorld(gunWrist,ReloadMathKL::ToLocal(v.wrist,v.magazine));
  grip.position=Subtract(grip.position,Scale(grip.basis.forward,v.feedLength));
  const auto palm=ReloadPresentationKL::PalmFromGrip(grip,v.magHalf,v.units,v.mp5);
  return ReloadPresentationKL::GuidePalm(target,v.palmLocal,palm.position,.075f*v.units);
 }
 if(v.phase==unsigned(PhysicalReloadKL::Phase::Slide)){auto slide=ReloadMathKL::ToWorld(gunWrist,ReloadMathKL::ToLocal(v.wrist,v.slideAnchor));slide.position=Subtract(slide.position,Scale(ReloadMathKL::Transform(gunWrist.basis,v.barrelLocal),v.slideTravel*v.units));target=ReloadMathKL::ToWorld(slide,v.slideHand);return ValidatePose(target,.05f);}return false;
}
#include "native_shotgun_reload_kl.inl"

void Part(std::uintptr_t item,std::uintptr_t model,std::uint32_t ref,unsigned category,const ReloadPartsKL::Part& part,const NativeHandsKL::VisualFrame& hands,const CameraPose& rendered,CameraPose& output){
 if(!Enabled()||!PhysicalReloadKL::Supported(category)||!part.boundsValid||!part.bindValid||!hands.availableForMesh[1])return;
 CameraPose bone;if(!G::BoneWorld(rendered,part.bind,bone))return;
 const auto local=ReloadMathKL::ToLocal(hands.finalWrist[1],bone);G::Bounds frame;
 AcquireSRWLockExclusive(&lock);
 if(contacts.item!=item||contacts.model!=model||contacts.reference!=ref){contacts={};contacts.item=item;contacts.model=model;contacts.reference=ref;contacts.category=category;}
 contacts.tick=GetTickCount();
 if(!strcmp(part.name,"w_bone_frame")&&G::BuildBounds(local,part.centre,part.half,frame)){
  if(!contacts.frame&&category==4&&G::AK74Frame(part.half)){
   ReloadPartsKL::Catalog catalog;
   if(ReloadPartsKL::ModelCatalog(image,part.model,ReadBytes,catalog))for(unsigned i=0;i<catalog.count;++i){
    ReloadPartsKL::Part anchor;
    if(ReloadPartsKL::Bone(catalog,i,ReadBytes,anchor)&&anchor.bindValid&&!strcmp(anchor.name,"w_bone_slide")){
     contacts.akBoltBind=anchor.bind;contacts.akBolt=true;break;
    }
   }
  }
  if(contacts.akBolt){
   CameraPose world;
   contacts.akBoltPose=G::BoneWorld(rendered,contacts.akBoltBind,world);
   if(contacts.akBoltPose)contacts.akBoltLocal=ReloadMathKL::ToLocal(hands.finalWrist[1],world);
  }
  contacts.frameBox=frame;contacts.frame=true;
 }
 if(!strcmp(part.name,"w_bone_clip")){contacts.clipPart=part;contacts.clipBone=local;contacts.clip=true;}
 if(!strcmp(part.name,"w_bone_slide")){contacts.slidePart=part;contacts.slideBone=local;contacts.slide=true;}
 if(category==1&&R::Classify(part).kind!=R::PartKind::Other&&R::LoadCatalog(contacts.revolver,image,part.model,ReadBytes))
  R::Observe(contacts.revolver,part,local);
 if(category==5||category==15)ShotgunReload::Observe(contacts,part,rendered,hands.finalWrist[1],contacts.tick);
 const auto copy=contacts;auto v=view;ReleaseSRWLockExclusive(&lock);
 const auto matches=[&](const View& pose){return SameOwner(pose,hands.player,item,model,ref,hands.recenterAck);};
 const auto admissible=[&]{return (v.ready||v.loader==PhysicalReloadKL::Loader::Cylinder)&&matches(v)&&GetTickCount()-v.tick<=150;};
 if(hands.pairId){
  // Latch the same reload transaction for all weapon parts and both eyes.
  struct ReloadDraw {std::uint64_t pair=0;View view;};
  thread_local ReloadDraw draw;
  if(draw.pair!=hands.pairId||!matches(draw.view)){
   if(!admissible())return;
   draw={hands.pairId,v};
  }else if(GetTickCount()-draw.view.tick>150)return;
  v=draw.view;
 }else if(!admissible())return;
 const auto* palmLocal=HeldPalm(v,hands);
 if(v.loader==PhysicalReloadKL::Loader::Cylinder){
  const auto wrist=hands.finalWrist[1];
  auto cluster=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,v.magazine));
  if(v.held&&v.phase==unsigned(PhysicalReloadKL::Phase::Holding)&&palmLocal){
   const auto palm=ReloadMathKL::ToWorld(hands.finalWrist[0],*palmLocal);
   cluster=ReloadPresentationKL::HeldGrip(palm,v.magHalf,v.units);
   cluster.position=Add(cluster.position,Scale(cluster.basis.forward,v.feedLength));
  }
  const auto result=R::PosePart(R::Rebase(v.revolver,v.wrist,wrist),part,v.inserted,v.held,v.cylinderRounds,cluster,v.cylinderOpening,output);
  if(result==R::DrawDisposition::Hidden){hidePending=true;++hidden;}
  if(result==R::DrawDisposition::Moved)++remapped;
  return;
 }
 if(v.loader==PhysicalReloadKL::Loader::Shell)ShotgunReload::SuppressShell(v,part);
 if(!strcmp(part.name,"w_bone_clip")&&copy.frame){G::MagazineContact mag;
  if(G::Magazine(local,part.centre,part.half,hands.unitsPerMetre,mag,&copy.frameBox,category)){
   clipPending={true,item,model,0,0,ref,output,ReloadMathKL::ToWorld(hands.finalWrist[1],mag.seatedTip)};
   std::copy(std::begin(v.world),std::end(v.world),std::begin(clipPending.world));
   if(v.held){CameraPose mapped;const auto& seated=clipPending.seated;
    auto carried=ReloadMathKL::ToWorld(hands.finalWrist[1],ReloadMathKL::ToLocal(v.wrist,v.magazine));
    if(v.phase==unsigned(PhysicalReloadKL::Phase::Holding)&&palmLocal){const auto palm=ReloadMathKL::ToWorld(hands.finalWrist[0],*palmLocal);carried=ReloadPresentationKL::HeldGrip(palm,mag.gripHalf,hands.unitsPerMetre,v.mp5);carried.position=Add(carried.position,Scale(carried.basis.forward,mag.feedLength));}
    if(v.inserted){clipPending.spares[0]=carried;clipPending.spareCount=1;}
    else if(NativeWeaponMath::Remap(seated,carried,output,mapped)){output=mapped;++remapped;}
    else{hidePending=true;++hidden;}}
   else if(!v.inserted){hidePending=true;++hidden;}
  }
 }
 if(!strcmp(part.name,"w_bone_slide")&&v.slideTravel>0){output.position=Subtract(output.position,Scale(ReloadMathKL::Transform(hands.finalWrist[1].basis,v.barrelLocal),v.slideTravel*hands.unitsPerMetre));++remapped;}
}
void DrawCopies(IDirect3DDevice9* device,const D3D9Capture::DrawCall& call){
 if(copying||!device||!clipBound.valid||call.kind>1||!Enabled())return;
 const auto& bound=clipBound;
 const bool spare=bound.spareCount!=0;
 bool any=spare;for(const auto& w:bound.world)any=any||(w.valid&&w.model==bound.model);if(!any)return;
 const auto packet=bound;
 std::uintptr_t entry=0;if(!NativeCameraKL::DrawStateActive(packet.state)||!NativeHandsKL::ReadCurrentNativeEntry(packet.state,entry)||entry!=packet.entry||!Owns(packet.item))return;
 IDirect3DVertexShader9* shader=nullptr;if(FAILED(device->GetVertexShader(&shader))||!shader)return;
 thread_local IDirect3DVertexShader9* cached=nullptr;thread_local NativeShaderConstantMap::Map map;
 if(shader!=cached){if(cached)cached->Release();cached=shader;map={};UINT bytes=0;if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536){std::vector<std::uint32_t> code(bytes/4);if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)))NativeShaderConstantMap::Parse(code.data(),bytes,map);}}
 else shader->Release();
 if(BackgroundDiagnostic::Active()){
  static bool dumped=false;if(!dumped){wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768)){
   auto root=std::wstring(folder);UINT n=0;if(cached&&SUCCEEDED(cached->GetFunction(nullptr,&n))&&n<65536){std::vector<unsigned char> bytes(n);cached->GetFunction(bytes.data(),&n);if(FILE* file=_wfopen((root+L"\\drop-shader.bin").c_str(),L"wb")){fwrite(bytes.data(),1,n,file);fclose(file);}}
   float constants[256*4]{};device->GetVertexShaderConstantF(0,constants,256);if(FILE* file=_wfopen((root+L"\\drop-constants.bin").c_str(),L"wb")){fwrite(constants,sizeof(constants),1,file);fclose(file);}
   unsigned char state[512]{};if(ReadBytes(packet.state,state,sizeof(state)))if(FILE* file=_wfopen((root+L"\\drop-state.bin").c_str(),L"wb")){fwrite(state,sizeof(state),1,file);fclose(file);}
   dumped=true;
  }}
 }
 if(map.mvp.count!=4||!map.mvpColumns){++worldRejected;return;}
 float constants[16]{};CameraPose before;
 if(FAILED(device->GetVertexShaderConstantF(map.mvp.start,constants,4))||!Read(packet.state+0x8C,before)){++worldRejected;return;}
 copying=true;
 float eye[4]{};const bool haveEye=map.eye.count==1&&SUCCEEDED(device->GetVertexShaderConstantF(map.eye.start,eye,1));
 const auto draw=[&](const CameraPose& target,bool heldSpare){CameraPose pose;if(!NativeWeaponMath::Remap(packet.seated,target,packet.skin,pose))return;
  float next[16]{};if(!ReloadPresentationKL::Reproject(constants,before,pose,next))return;
  if(FAILED(device->SetVertexShaderConstantF(map.mvp.start,next,4)))return;
  if(haveEye){const auto worldEye=Add(before.position,ReloadMathKL::Transform(before.basis,{eye[0],eye[1],eye[2]}));const auto local=ReloadMathKL::Inverse(pose.basis,Subtract(worldEye,pose.position));float value[4]{local.x,local.y,local.z,eye[3]};device->SetVertexShaderConstantF(map.eye.start,value,1);}
  if(SUCCEEDED(D3D9Capture::ReplayNativeDraw(device,call))){if(heldSpare)++spareDraws;else ++worldDraws;}
 };
 for(const auto& w:packet.world)if(w.valid&&w.model==packet.model)draw(w.pose,false);
 if(spare)for(unsigned i=0;i<packet.spareCount;++i)draw(packet.spares[i],true);
 device->SetVertexShaderConstantF(map.mvp.start,constants,4);if(haveEye)device->SetVertexShaderConstantF(map.eye.start,eye,1);copying=false;
}
void Tick(const NativeReloadAmmoKL::Snapshot& source){
 if(BackgroundDiagnostic::Active()){
  static bool dumped=false;std::uint32_t renderer=0,vt=0,methods[16]{};
  if(!dumped&&Read(image+0x786658,renderer)&&Read(renderer,vt)&&ReadBytes(vt,methods,sizeof(methods))){
   wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))if(FILE* file=_wfopen((std::wstring(folder)+L"\\renderer-methods.txt").c_str(),L"wb")){
    fprintf(file,"renderer=%08x vtable=%08x\n",renderer,vt);for(unsigned i=0;i<16;++i)fprintf(file,"%u %08x\n",i,methods[i]);fclose(file);dumped=true;}
  }
 }
 if(!Enabled()||!PhysicalReloadKL::Supported(source.category)){Invalidate();return;}NativeHandsKL::VisualFrame hands;
 Contacts c;View previous;AcquireSRWLockShared(&lock);c=contacts;previous=view;ReleaseSRWLockShared(&lock);
 const auto optic=source.category==15?NativeWeaponKL::ReadScope():NativeScopeKL::State{};
 const bool scoped=optic.valid&&optic.active&&optic.player==source.player&&optic.item==source.item&&
    optic.model==source.model&&optic.reference==source.reference;
 const bool haveHands=scoped?NativeHandsKL::SnapshotReloadControllers(source.player,static_cast<std::uint32_t>(VRBridge::RecenterAck()),hands):
    NativeHandsKL::SnapshotVisual(hands);
 const auto now=GetTickCount();
 const bool tracked=haveHands&&
    hands.player==source.player&&now-hands.tick<=150&&now-hands.sourcePoseTick<=150&&hands.tracked[0]&&hands.tracked[1]&&
    (hands.inputValid[0]&HandsIPC::kGripInput);
 if(!tracked){if(!ShotgunReload::WaitForIntegrated(source,c,now))Invalidate();return;}
 if((source.category==5||source.category==15)&&ShotgunReload::Tick(source,c,hands,now,scoped))return;
 if(c.item!=source.item||c.model!=source.model||c.reference!=source.reference||now-c.tick>150){Invalidate();return;}
 const bool sameOwner=SameOwner(previous,source.player,source.item,source.model,source.reference,hands.recenterAck);
 const bool palmValid=hands.palmValid[0]||(sameOwner&&previous.palmValid&&now-previous.palmTick<=150);
 const auto& palmLocal=hands.palmValid[0]?hands.palmLocal[0]:previous.palmLocal;
 M::Frame f;f.valid=true;f.tick=now;f.sample=hands.sourcePoseTick;f.reference=hands.recenterAck;f.scene=source.reference;f.units=hands.unitsPerMetre;f.grip=hands.grip[0];f.palm=palmValid?ReloadMathKL::ToWorld(hands.controllerWrist[0],palmLocal):hands.controllerWrist[0];
 const auto wrist=hands.finalWrist[1];NativeWeaponKL::AimFrame aim;
 if(!SnapshotAim(aim)||aim.player!=source.player||aim.weapon!=source.item||!ReloadPresentationKL::Gun(wrist,aim.direction,f.gun)){Invalidate();return;}
 G::MagazineContact mag;G::SlideContact slide;R::Geometry revolver;
 const bool cylinder=source.category==1&&source.ammo.capacity==int(R::ChamberCount)&&R::Build(c.revolver,wrist,f.gun,f.units,revolver);
 const bool mp5=source.category==2&&G::MP5Frame(c.frameBox.half);
 const bool p90=source.category==2&&source.ammo.capacity==50&&G::P90Frame(c.frameBox.half)&&G::P90Magazine(c.clipPart.half);
 const bool clip=c.frame&&c.clip&&G::Magazine(c.clipBone,c.clipPart.centre,c.clipPart.half,f.units,mag,&c.frameBox,source.category)&&
  (!p90||Dot(mag.mouth.position,mag.mouth.position)<.85f*.85f*f.units*f.units);
 const bool ak74=source.category==4&&clip&&G::AK74Frame(c.frameBox.half)&&G::AK74Magazine(c.clipPart.half);
 const bool nativeSlide=source.category!=1&&c.slide&&G::Slide(c.slideBone,c.slidePart.centre,c.slidePart.half,f.units,slide,source.category);
 const bool slideAvailable=nativeSlide||
     (ak74&&c.akBoltPose&&G::AK74Bolt(c.akBoltLocal,ReloadMathKL::ToLocal(wrist,f.gun),f.units,slide));
 const auto loader=PhysicalReloadKL::LoaderForContacts(source.category,c.frame,clip||cylinder,slideAvailable,cylinder,ak74);
 if((!clip&&!cylinder)||loader==PhysicalReloadKL::Loader::Native||
    (loader==PhysicalReloadKL::Loader::MagazineAndSlide&&!slideAvailable)){Invalidate();return;}
 engine.discardPolicy=M::DiscardPolicy::Recover;engine.Select(source.player,source.Token(),source.model,source.category,source.ammo,f.units);
 // The MP5 intentionally shares its charging handle with the authored
 // support hold. Nearby handles on other guns get first refusal at the
 // measured part, even when a round is already chambered.
 const bool slideShared=mp5;
 f.slideAvailable=slideAvailable&&engine.AllowsSlide(slideShared);f.readyOnInsert=loader==PhysicalReloadKL::Loader::MagazineOnly;
 if(cylinder){
  R::Frame(revolver,f);mag.gripHalf=revolver.gripHalf;mag.feedLength=revolver.feedLength;
 }else{
  f.receiver=ReloadMathKL::ToWorld(wrist,mag.mouth);f.seatedReceiver=ReloadMathKL::ToWorld(wrist,mag.seatedTip);f.seatedReceiverValid=true;f.magazineBodyLength=2*mag.feedLength/f.units;
  f.magazine=ReloadPresentationKL::HeldGrip(f.palm,mag.gripHalf,f.units,mp5);f.magazine.position=Add(f.magazine.position,Scale(f.magazine.basis.forward,mag.feedLength));f.magazineValid=true;
 }
 if(slideAvailable){
  f.slide=ReloadMathKL::ToWorld(wrist,slide.top);f.slideBox=ReloadMathKL::ToWorld(wrist,slide.box.world);f.slideBoxValid=true;f.slideHalf=slide.box.half;
  f.slideA=Add(wrist.position,ReloadMathKL::Transform(wrist.basis,slide.a));f.slideB=Add(wrist.position,ReloadMathKL::Transform(wrist.basis,slide.b));f.slideSegmentValid=true;f.slideRadius=slide.radius/f.units;f.slideDriverPoint=hands.controllerWrist[0].position;f.slideDriverValid=true;
 }
 ReloadBodyPocketKL::Input body;body.player=source.player;body.scene=f.scene;body.reference=f.reference;body.pairId=hands.pairId;body.tick=now;body.sample=f.sample;body.units=f.units;body.head=hands.centre;body.rootAnchor=hands.rawRoot.position;body.rootAnchorValid=hands.bodyAvailableForMesh;
 ReloadBodyPocketKL::Volume volume;const bool inventory=pocket.Step(body,volume);
 f.allowSupply=(!cylinder||!engine.inserted)&&inventory&&ReloadBodyPocketKL::CanSupply(volume,f.palm.position,f.gun.position);f.returnToPocket=inventory&&ReloadBodyPocketKL::Contains(volume,f.palm.position);f.supportGripHeld=hands.supportLatched;
 const auto tap=ReloadTap(now);
 if(tap)consumedReload=tap;
 f.eject=tap||(BackgroundDiagnostic::Key('R')&0x8000)!=0;
 if(Scripted()){

 // The script supplies contact poses to the same engine, not completed
 // reload/ammo states. Live controller mode never enters this branch.
 f.grip=0;f.magazine=f.seatedReceiver;f.magazineValid=true;
 if(ejectedAt&&!engine.inserted){
  if(!engine.held){
   f.magazine.position=Add(f.palm.position,Scale(f.magazine.basis.forward,mag.feedLength));
   if(now-ejectedAt>=200&&engine.armed&&!engine.worldPickupBlocked){f.grip=1;f.allowSupply=true;}
  }else{
   f.grip=1;
   if(now-heldAt<400)f.magazine.position=Add(f.palm.position,Scale(f.magazine.basis.forward,mag.feedLength));
   if(latchedAt&&now-latchedAt>=200)f.grip=0;
  }
 }
 if(!engine.armed&&!f.eject)f.grip=0;
 }
 const auto before=engine;unsigned events=engine.Tick(f);
 if((p90||cylinder)&&engine.held&&BackgroundDiagnostic::Controls()){
  static DWORD last=0;static unsigned traces=0;
  if(now-last>=100&&traces<80){
   last=now;++traces;
   const auto delta=Scale(ReloadMathKL::Inverse(f.receiver.basis,Subtract(f.magazine.position,f.receiver.position)),1.f/f.units);
   const auto reach=Scale(Subtract(f.receiver.position,hands.centre.position),1.f/f.units);
   wchar_t folder[32768]{};
   if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))
    if(FILE* file=_wfopen((std::wstring(folder)+L"\\reload-contact.jsonl").c_str(),L"ab")){
     fprintf(file,"{\"ageMs\":%lu,\"category\":%u,\"capacity\":%d,\"grip\":%.3f,\"socketDistanceMetres\":%.5f,\"contactOffset\":[%.5f,%.5f,%.5f],\"alignment\":%.5f}\n",
         BackgroundDiagnostic::Age(),source.category,source.ammo.capacity,f.grip,std::sqrt(Dot(reach,reach)),
         delta.x,delta.y,delta.z,Dot(f.magazine.basis.forward,f.receiver.basis.forward));
     fclose(file);
    }
  }
 }
 const auto capacity=[](std::uintptr_t item,std::uintptr_t method){using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(item));};
 const auto setter=[](std::uintptr_t method,std::uintptr_t item,int value){using Set=void(__thiscall*)(void*,int);reinterpret_cast<Set>(method)(reinterpret_cast<void*>(item),value);};
 if(!NativeReloadAmmoKL::Commit(image,source,engine.ammo,ReadBytes,capacity,setter)){engine=before;++rejected;return;}
 const float dt=worldTick?float(now-worldTick)/1000.f:0;worldTick=now;engine.Advance(dt,f.units);
 for(unsigned i=0;i<3;++i)if(engine.world[i].valid){
  if(floorSerial[i]!=engine.world[i].serial){floorSerial[i]=engine.world[i].serial;floorKnown[i]=false;}
  if(!floorKnown[i])floorKnown[i]=NativeAimKL::TraceReloadFloor(source.player,engine.world[i].pose.position,f.units,floors[i]);
  if(floorKnown[i]&&engine.Land(i,floors[i],f.units,engine.world[i].model==source.model?mag.gripHalf.x:0)){events|=M::Impact;++impacts;}
 }
 Feedback(events,source.category,now);
 const bool slideGrab=before.phase!=PhysicalReloadKL::Phase::Slide&&engine.phase==PhysicalReloadKL::Phase::Slide;
 if((events||slideGrab)&&BackgroundDiagnostic::Active()){wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))if(FILE* file=_wfopen((std::wstring(folder)+L"\\manual-reload.jsonl").c_str(),L"ab")){
  fprintf(file,"{\"ageMs\":%lu,\"category\":%u,\"capacity\":%d,\"events\":%u,\"phase\":%u,\"countBefore\":%d,\"countAfter\":%d,\"reserveBefore\":%d,\"reserveAfter\":%d,\"heldRounds\":%d,\"inserted\":%s,\"chamber\":%s,\"slideGrab\":%s,\"chamberBefore\":%s,\"nativeAcknowledged\":true,\"scriptedContacts\":%s}\n",BackgroundDiagnostic::Age(),source.category,source.ammo.capacity,events,unsigned(engine.phase),source.ammo.count,engine.ammo.count,source.ammo.reserve,engine.ammo.reserve,engine.heldRounds,engine.inserted?"true":"false",engine.chamber?"true":"false",slideGrab?"true":"false",before.chamber?"true":"false",Scripted()?"true":"false");fclose(file);
 }}
 if(events&M::Eject){++ejected;ejectedAt=now;heldAt=latchedAt=0;}if(events&M::Take){++taken;heldAt=now;}if(events&M::Latch)latchedAt=now;if(events&M::Insert)++inserted;if(events&M::Forward)++racked;
 View next;next.ready=true;next.loader=loader;next.category=source.category;next.reloadSocket=f.receiver;next.player=source.player;next.item=source.item;next.model=source.model;next.reference=source.reference;next.tick=now;next.inserted=engine.inserted;next.held=engine.held;next.magazine=engine.lastMagazine;next.slideTravel=engine.slideTravel;next.phase=unsigned(engine.phase);next.blocked=!engine.chamber||!engine.ammo.count||(engine.phase==PhysicalReloadKL::Phase::Slide&&engine.slideTravel>.003f);
 next.supportBusy=engine.ReservesSupportHand();next.wrist=wrist;next.units=f.units;next.barrelLocal=ReloadMathKL::Inverse(wrist.basis,f.gun.basis.forward);next.palmLocal=palmLocal;next.palmValid=palmValid;next.magHalf=mag.gripHalf;next.feedLength=mag.feedLength;
 next.ak74=ak74;next.magazineAsset=c.clipPart.model;
 if(ak74&&slideAvailable&&(events&M::RoundEject)){
  next.roundTick=now;next.roundPose=f.slideBox;
 }else if(ak74&&sameOwner&&previous.roundTick&&now-previous.roundTick<=360){
  next.roundTick=previous.roundTick;next.roundPose=previous.roundPose;
 }
 next.mp5=mp5;next.recenter=f.reference;next.palmTick=hands.palmValid[0]?hands.tick:previous.palmTick;next.seatReleased=engine.seatReleased;
 next.slideLocal=slide;next.slideShared=slideShared;next.slideAllowed=slideAvailable&&engine.AllowsSlide(slideShared);
 next.needsChamber=!engine.inserted||!engine.chamber;next.rounds=engine.ammo.count;next.reserve=engine.ammo.reserve;
 if(cylinder){
  next.revolver=revolver;
  next.cylinderDocked=engine.held&&engine.phase==PhysicalReloadKL::Phase::Finishing&&!engine.seatReleased;
  next.cylinderRounds=unsigned(engine.held?engine.heldRounds:engine.ammo.count);
  const float old=previous.item==next.item&&previous.loader==loader?previous.cylinderOpening:0.f;
  const float step=std::clamp(dt/.14f,.01f,1.f);
  next.cylinderOpening=std::clamp(old+(engine.inserted?-step:step),0.f,1.f);
 }
 next.slideAnchor=f.slideBox;next.slideHand=before.phase==PhysicalReloadKL::Phase::Slide&&sameOwner?previous.slideHand:ReloadMathKL::ToLocal(f.slideBox,hands.finalWrist[0]);
 for(unsigned i=0;i<3;++i)next.world[i]=engine.world[i];
 Publish(next);
}
bool Suppress(){
 if(!hiddenState||!hiddenEntry||!Enabled()||!NativeCameraKL::DrawStateActive(hiddenState))return false;
 std::uintptr_t entry=0;
 if(!NativeHandsKL::ReadCurrentNativeEntry(hiddenState,entry)||entry!=hiddenEntry)return false;
 std::uint32_t record=0;
 return !hiddenRecord||(Read(hiddenState+0x14,record)&&record==hiddenRecord);
}
}
