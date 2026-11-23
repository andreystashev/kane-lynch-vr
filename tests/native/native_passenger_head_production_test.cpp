// Actual owned PrepareDraw/palette path, including a displaced passenger eye
// and foreign-actor draw. No game, GPU, headset, hooks or input events.
#define main UnusedTwoHandPassengerHeadMain
#include "native_two_hand_production_test.cpp"
#undef main

int main() {
    using namespace NativeHandsKL;using namespace NativeCameraMath;
    constexpr std::uintptr_t base=0x2B000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private passenger owned-palette layout allocated");if(!memory)return 1;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,geometry=base+0x30000,model=base+0x40000,
        cache=base+0x50000,entry=base+0x60000,lookup=base+0x70000,parentsAt=base+0x71000,rest=base+0x72000;
    const auto put=[](std::uintptr_t at,const auto& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));};
    image=base;installed=true;
    put(base+kGameData,data);put(data+0xA2C,player);put(player,base+kPlayerVtable);put(player+0x70,model);
    put(entry,base+kBonesVtable);put(entry+0xF4,geometry);put(geometry+0x60,player);put(entry+0x108,cache);
    put(entry+0x114,11u);put(model+0x4A0,11u);put(model+0xC,lookup);put(model+4,rest);put(model+0x10,parentsAt);
    const std::uint8_t parentBytes[]={0,0,1,2,3,2,5,6,2,8,9};
    std::memcpy(reinterpret_cast<void*>(parentsAt),parentBytes,sizeof(parentBytes));
    put(lookup+0,std::uint8_t(1));put(lookup+6,std::uint8_t(3));put(lookup+64,std::uint8_t(4));
    put(lookup+22,std::uint8_t(5));put(lookup+23,std::uint8_t(6));put(lookup+24,std::uint8_t(7));
    put(lookup+16,std::uint8_t(8));put(lookup+17,std::uint8_t(9));put(lookup+18,std::uint8_t(10));
    NativeSkinMath::BindLocal local[11];local[1].position={0,90,0};local[2].position={0,50,0};local[3].position={0,15,0};local[4].position={0,4,-8};
    local[5].position={18,0,0};local[6].position={0,0,25};local[7].position={0,0,25};
    local[8].position={-18,0,0};local[9].position={0,0,25};local[10].position={0,0,25};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));
    std::uint32_t parents[11];for(unsigned i=0;i<11;++i)parents[i]=parentBytes[i];CameraPose bind[11];
    Check(NativeSkinMath::BindPose(local,parents,11,bind),"actual passenger bind tree generated");
    for(unsigned i=0;i<11;++i){float rows[12];Check(NativeSkinMath::Palette(bind[i],{bind[i].basis,bind[i].position},rows),"native source palette generated");
        std::memcpy(reinterpret_cast<void*>(cache+0x90+i*sizeof(rows)),rows,sizeof(rows));}
    std::array<unsigned char,11*48> nativePalette{};std::memcpy(nativePalette.data(),reinterpret_cast<void*>(cache+0x90),nativePalette.size());
    auto root=Frame();root.position={300,1200,-900};put(entry+0x10,root);put(entry+0x40,root);put(base+kGpuPoseIndex,0u);
    CameraFrame camera;camera.valid=true;camera.firstPerson=true;camera.nativeBodyAction=true;camera.units=100;camera.tick=Fixture::Clock();
    camera.centre=root;camera.centre.position=Add(root.position,{55,bind[4].position.y-65,0});camera.rawHeadOrientation[3]=1;
    camera.hands.present=camera.hands.mounted=1;camera.hands.tick=camera.tick;camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    camera.hands.head.orientation[3]=1;
    Check(CaptureRigMetadata(model,player,camera.rig),"production native rig metadata reader proves current local player model");
    actionFixture={};actionFixture.valid=true;actionFixture.player=player;actionFixture.movementKind=NativeActionKL::Movement::Vehicle;
    const auto prepare=[&](PreparedDraw& draw,bool nextPair=true){camera.generation=frameGeneration.fetch_add(1)+1;if(nextPair)++camera.pairId;
        publishedFrame=camera;return PrepareDraw(camera,entry,draw);};
    const auto head=NativeRigMath::ToWorld(root,{bind[3].basis,bind[3].position}).position;
    Check(!NativeVisualPolicy::HideHead(true,true,head,camera.centre.position,100),"fixture actually exceeds unchanged ordinary65cm head-mask distance");
    PreparedDraw first;
    Check(prepare(first)&&first.hidden&&first.modified&&first.head==3,"actual owned passenger palette hides local head at combined roomscale/crouch boundary");
    Check(first.palette[3][0]<.001f&&first.palette[4][0]<.001f,"actual head and child eye palette rows are collapsed to finite positive scale");
    Fixture::tick+=200;camera.hands={};PreparedDraw second;
    Check(prepare(second,false)&&second.hidden,"same-pair authorized passenger head mask survives temporarily missing hand data in slow second eye");
    put(geometry+0x60,player+4);PreparedDraw foreign;
    Check(!prepare(foreign)&&!foreign.hidden,"foreign actor geometry cannot obtain a local passenger head-mask palette");put(geometry+0x60,player);
    actionFixture.player=player+4;PreparedDraw foreignAction;
    Check(prepare(foreignAction)&&!foreignAction.hidden,"foreign passenger action cannot force local head hiding");actionFixture.player=player;
    actionFixture.movementKind=NativeActionKL::Movement::Stand;PreparedDraw ordinary;
    Check(prepare(ordinary)&&!ordinary.hidden,"ordinary gameplay retains original head-mask distance policy");
    actionFixture.movementKind=NativeActionKL::Movement::Vehicle;camera.actorHeadOnly=true;PreparedDraw authored;
    Check(prepare(authored)&&!authored.hidden,"actorHeadOnly camera retains original nearby-head policy");camera.actorHeadOnly=false;
    actionFixture.action=1;actionFixture.actionKind=NativeActionKL::Action::MiniAct;camera.nativeAction=true;PreparedDraw action;
    Check(prepare(action)&&!action.hidden,"authored passenger scene does not inherit detached gameplay head hiding");
    actionFixture.actionKind=NativeActionKL::Action::Reload;PreparedDraw reload;
    Check(prepare(reload)&&reload.hidden&&!reload.visual.tracked[0]&&!reload.visual.tracked[1],"manual reload preserves native arms while keeping the same displaced first-person passenger head hidden");
    Check(!std::memcmp(nativePalette.data(),reinterpret_cast<void*>(cache+0x90),nativePalette.size()),"all owned/foreign/action tests leave native source palette byte-identical");
    installed=false;VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 passenger owned head mask: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
