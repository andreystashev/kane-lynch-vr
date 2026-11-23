#include "native_gameplay_hud_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){unsigned checks=0;
    const auto check=[&](bool value){++checks;if(!value)std::exit(1);};
    using namespace NativeGameplayHudKL;
    for(unsigned i=0;i<static_cast<unsigned>(Producer::Count);++i){const auto p=static_cast<Producer>(i);
        check(!Hide(p,false,true));check(!Hide(p,true,false));
        for(unsigned mode=1;mode<=6;++mode)check(Hide(p,true,true,mode,0,true)==
            (mode==3&&(p==Producer::UseAction||p==Producer::Crosshair)));
    }
    check(Hide(Producer::Crosshair,true,true));check(!Hide(Producer::Direction,true,true));
    check(!Hide(Producer::ActionMarker,true,true));
    check(Hide(Producer::Inventory,true,true));check(Hide(Producer::UseAction,true,true));
    check(!Hide(Producer::TutorialText,true,true));check(!Hide(Producer::BriefingAdvice,true,true));
    check(Hide(Producer::Minimap,true,true,0,0,true));
    check(!Hide(Producer::Minimap,true,true,0,0,false));
    for(unsigned state=1;state<=3;++state)check(Hide(Producer::Minimap,true,true,0,state,true));
    check(!Hide(Producer::Minimap,true,true,0,4,true));
    check(!Hide(Producer::Minimap,true,true));
    check(!Hide(Producer::Count,true,true));
    char action[128]{};
    check(FormatWristAction("\xEE\x84\x80" "\xD1\x83" "\xEE\x84\x80 Use Adrenaline",false,action,sizeof(action))&&
          !std::strcmp(action,"\xEE\x84\x80" "X" "\xEE\x84\x80 Use Adrenaline"));
    check(FormatWristAction("",true,action,sizeof(action))&&
          !std::strcmp(action,"\xEE\x84\x80" "X" "\xEE\x84\x80"));
    check(FormatWristAction("Enter Vehicle",false,action,sizeof(action))&&
          !std::strcmp(action,"\xEE\x84\x80" "X" "\xEE\x84\x80 Enter Vehicle"));
    check(FormatWristAction("Climb",false,action,sizeof(action),true)&&
          !std::strcmp(action,"\xEE\x84\x80" "B" "\xEE\x84\x80 (hold) Climb"));
    char labels[256]{};
    check(TutorialBindings("Next {NextCommandReceiver} or previous {PrevCommandReceiver}",labels,sizeof(labels))&&
          std::strstr(labels,"Right Grip")&&!std::strstr(labels,"previous"));
    check(TutorialBindings("Hold {IngameMapActive} to find {objective}",labels,sizeof(labels))&&
          !std::strcmp(labels,"Hold \xEE\x84\x80" "Y" "\xEE\x84\x80 to find {objective}"));
    check(TutorialBindings("Hold {InventoryWheelActive} and push {HoldMoveForward}",labels,sizeof(labels))&&
          std::strstr(labels,"Right Stick Up")&&!std::strstr(labels,"InventoryWheelActive"));
    check(TutorialBindings("{HoldDefend} then {HoldAttack} or {HoldFollow}",labels,sizeof(labels))&&
          std::strstr(labels,"Y (hold) + B")&&std::strstr(labels,"Y (hold) + Right Trigger")&&std::strstr(labels,"Y (hold) + A"));
    check(TutorialBindings("Click and hold {HoldPrecisionAim} to aim while in cover",labels,sizeof(labels))&&
          !std::strcmp(labels,"Aim the weapon by moving the right controller."));
    check(!TutorialBindings("Objective marker {objective}",labels,sizeof(labels))&&
          !std::strcmp(labels,"Objective marker {objective}"));
    check(!TutorialBindings("{HoldAttack}",labels,16));
    using namespace NativeHudFixture;Memory memory;
    constexpr std::uint32_t Display=0x15000000,Manager=0x16000000,Frontend=0x17000000,Speech=0x18000000;
    const auto setup=[&]{memory=Memory{};memory.Set(Base+0x8883A4,std::uint8_t(0));memory.Set(Base+0x68760C,Data);
        memory.Set(Data+0xA2C,Player);memory.Set(Player,Base+0x56582C);memory.Set(Player+0x804,std::int8_t(0));
        memory.Set(Display,Base+0x575A4C);memory.Set(Display+0x10,Player);memory.Set(Base+0x888104,Speech);
        memory.Set(Data+0x154C,Manager);memory.Set(Manager,Frontend);memory.Set(Manager+8,std::int8_t(0));
        memory.Set(Manager+20,std::int8_t(-1));memory.Set(Frontend,Base+0x5755FC);memory.Set(Frontend+0x8FC,Display);};
    setup();const auto original=memory.bytes;
    check(OwnedTutorial(Base,Display,Player,memory.Reader()));check(memory.bytes==original);
    for(unsigned broken=0;broken<8;++broken){setup();switch(broken){
        case 0:memory.Set(Base+0x888104,Display);break;case 1:memory.Set(Frontend+0x8FC,Speech);break;
        case 2:memory.Set(Display+0x10,Player+4);break;case 3:memory.Set(Manager+8,std::int8_t(1));break;
        case 4:memory.Set(Manager+20,std::int8_t(0));memory.Set(Manager+12,Frontend);break;
        case 5:memory.Set(Display,Base+0x5759C4);break;case 6:memory.Set(Base+0x8883A4,std::uint8_t(1));break;
        case 7:memory.Set(Data+0xA2C,Player+4);break;}
        check(!OwnedTutorial(Base,Display,Player,memory.Reader()));}
    setup();unsigned reads=0;const auto race=[&](auto at,void* output,auto size){if(at==Frontend+0x8FC&&++reads==2)memory.Set(at,Speech);return memory.Read(at,output,size);};
    check(!OwnedTutorial(Base,Display,Player,race));
    setup();memory.Set(Display,Base+0x575C3C);memory.Set(Frontend+0x90C,Display);
    check(OwnedBriefing(Base,Display,Player,memory.Reader()));
    memory.Set(Base+0x8883A4,std::uint8_t(3));check(OwnedBriefing(Base,Display,Player,memory.Reader()));
    memory.Set(Frontend+0x90C,Speech);check(!OwnedBriefing(Base,Display,Player,memory.Reader()));
    setup();memory.Set(Base+0x8883A4,std::uint8_t(3));check(OwnedTutorial(Base,Display,Player,memory.Reader()));
    if(argc>1){Image actual;check(actual.Load(argv[1]));check(VerifyTutorialImage(Base,actual.Reader()));
        actual.mapped[0x257469]^=1;check(!VerifyTutorialImage(Base,actual.Reader()));}
    std::printf("Native gameplay HUD ownership/menu policy: %u checks passed.\n",checks);
}
