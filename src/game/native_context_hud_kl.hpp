#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only prompt telemetry for the hash-gated KL1 executable. AD0 is the
// committed native prompt enum, not a target object or the running action.
// AE0 optionally overrides its localization key; AE8 is the input binding.
// No native virtual functions or localization routines are invoked here.
namespace NativeContextHudKL {
inline constexpr std::uintptr_t GameDataRva=0x68760C,PlayerVtableRva=0x56582C;
enum class Prompt : std::uint32_t {
    None,Pickup,PickupWeapon,PickupSpecialProp,Select,Talk,Generic,Unimplemented,
    Rappel,EnterVehicle,ExitVehicle,TakeMountedWeapon,LeaveMountedWeapon,
    ThrowBackGrenade,GiveHealth,CloseCombat
};
struct State {
    bool valid=false,eligible=false,bindingKnown=false,textOverride=false;
    std::uintptr_t player=0,target=0,binding=0,bindingType=0,textHandle=0;
    std::uint32_t nativeArg=0,bindingIndex=0;
    Prompt prompt=Prompt::None;
    // These are native localization keys, not translated display text. Keep
    // bindingLabel separate: it describes an input binding, not the action.
    char nativeText[128]{},bindingLabel[128]{};
};
inline const wchar_t* EnglishAction(const State& state) {
    if(!state.valid||!state.eligible)return nullptr;
    // Exact keys from the game's English UseActions section. No localized
    // keyboard tokens, substring guesses or tutorial text are copied.
    const struct {const char* key;const wchar_t* label;} labels[]={
        {"PickupPhone",L"Pick up phone"},{"OpenDoor",L"Open door"},{"PickupSuitcase",L"Pick up briefcase"},
        {"PutGasInVentilation",L"Put gas in ventilation"},{"UseMountedMachineGun",L"Use mounted machine gun"},
        {"M02_PA_10_PickUpShotgun",L"Pick up shotgun"},{"M02_PA_20_PickUpHandgun",L"Pick up Magnum handgun"},
        {"M02_PA_21_PickUpUsp",L"Pick up SLP handgun"},{"M02_PA_30_PickUpP90",L"Pick up P900"},
        {"M02_PA_40_PickUpGrenades",L"Pick up grenades"},{"M02_PA_50_PickUpMoreGrenades",L"Pick up grenades"},
        {"M03B_EnterTrain",L"Enter subway train"},{"M03B_EnterVan",L"Enter van"},{"M03B_PickupBag",L"Pick up bag"},
        {"M05_KnockOutYoko",L"Knock out Yoko"},{"M05_PickupYoko",L"Pick up Yoko"},{"M07_KillMute",L"Attack Mute"},
        {"M08_BlockAControl",L"Open mercenaries' cells"},{"M08_BoilerRoom",L"Open boiler room"},
        {"M08_ControlPanel01",L"Open gates"},{"M08_Escape",L"Escape in prison transport"},
        {"M08_MeetMerc03",L"Talk to Shelly"},{"M08_OpenBlockBCells",L"Open all cells"},
        {"M08_OpenCell",L"Open Rific's cell"},{"M08_Tower",L"Open yard gates"},
        {"M09A_PlaceBomb",L"Place bomb"},{"M09A_TakeBriefcase",L"Take briefcase"},
        {"M13B_ExitNextLevel",L"Go to the village"},{"M13B_ExitTraitor",L"Escape in helicopter"}};
    for(const auto& entry:labels)if(!std::strcmp(state.nativeText,entry.key))return entry.label;
    if(state.textOverride&&state.prompt==Prompt::Generic&&std::strcmp(state.nativeText,"Generic"))return nullptr;
    switch(state.prompt) {
    case Prompt::Pickup:case Prompt::PickupSpecialProp:return L"Pick up";
    case Prompt::PickupWeapon:return L"Pick up weapon";
    case Prompt::Select:return L"Select";case Prompt::Talk:return L"Talk";case Prompt::Generic:return L"Use";
    case Prompt::Rappel:return L"Rappel";case Prompt::EnterVehicle:return L"Enter vehicle";
    case Prompt::ExitVehicle:return L"Exit vehicle";case Prompt::TakeMountedWeapon:return L"Use mounted weapon";
    case Prompt::LeaveMountedWeapon:return L"Leave mounted weapon";case Prompt::ThrowBackGrenade:return L"Throw back grenade";
    case Prompt::GiveHealth:return L"Use adrenaline";case Prompt::CloseCombat:return L"Close combat";
    default:return nullptr;
    }
}
inline bool Address(std::uintptr_t at,std::size_t size) {
    return at>=0x10000u&&size<=0xffffffffu&&at<=0xffffffffu-size;
}
inline const char* PromptKey(Prompt prompt) {
    constexpr const char* keys[]={"","PickUp","PickUpWeaponNoText","PickUpSpecialProp",
        "Select","Talk","Generic","FIX ME MSL","Rappel","EnterVehicle","ExitVehicle",
        "TakeMountedWeapon","LeaveMountedWeapon","ThrowBackGrenade","GiveHealth","TakeHimOut"};
    const auto index=static_cast<std::uint32_t>(prompt);
    return index<16?keys[index]:"";
}
inline bool IsOpenDoorKey(const char* key) {
    // Shared English LOC: OpenDoor -> "{TapUse} Open Door"; M05A PRP
    // uses this exact standalone key for its ROOT\Doors interactions.
    return key&&!std::strcmp(key,"OpenDoor");
}
inline bool PrintableKey(char (&key)[128]) {
    for(std::size_t i=0;i<sizeof(key);++i){
        const auto value=static_cast<unsigned char>(key[i]);
        if(!value){std::memset(key+i,0,sizeof(key)-i);return true;}
        if(value<32||value>126)return false;
    }
    return false;
}

// Run once after the supported-EXE SHA check. Prove the active-field copies,
// prompt enum consumer, string override and independent binding record layout.
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    if(!Address(image,GameDataRva+4))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[64]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char source[]={0x8B,0x46,0x10,0x8B,0x88,0xD0,0x0A,0,0,
        0x3B,0x8E,0x18,1,0,0,0x8B,0x90,0xE0,0x0A,0,0,0x8B,0x80,0xE8,0x0A,0,0};
    const unsigned char commit[]={0x89,0x86,0xD0,0x0A,0,0,0x33,0xC0,
        0x89,0x8E,0xE0,0x0A,0,0,0x89,0x86,0xD4,0x0A,0,0,
        0x89,0x86,0xE4,0x0A,0,0,0x89,0x96,0xE8,0x0A,0,0};
    const unsigned char promptConsumer[]={0x8B,0x86,0x18,1,0,0,0x50,0x8B,0xCE,
        0xC7,0x44,0x24,0x24,0,0,0,0,0xE8,0x4D,0xFA,0xFF,0xFF,
        0x8B,0x8E,0x1C,1,0,0};
    const unsigned char textOverride[]={0x8B,0x11,0x85,0xD2,0x74,0x12,0x83,0xC2,0x0C,
        0x74,0x0D,0x8B,0x09,0x85,0xC9,0x74,5,0x8D,0x41,0x0C};
    const unsigned char bindingType[]={0x8B,0x80,0xC0,0,0,0};
    const unsigned char recordName[]={0x8D,0x56,0x49,0x2B,0xD0,0xEB,3,0x8D,0x49,0,
        0x8A,8,0x88,0x0C,2,0x83,0xC0,1,0x84,0xC9,0x75,0xF4};
    if(!match(0x25BDD6,source)||!match(0x1F8D4C,commit)||
       !match(0x25C4ED,promptConsumer)||!match(0x25C50D,textOverride)||
       !match(0x25C0CB,bindingType)||!match(0x2115B6,recordName))return false;
    // The switch values themselves are checked, rather than inferring action
    // names from binding IDs, animation names or active-action classes.
    constexpr std::uint32_t branches[]={0x25BFD9,0x25BF60,0x25BFD0,0x25BF90,
        0x25BF70,0x25BF78,0x25BFC8,0x25BF88,0x25BF68,0x25BF98,
        0x25BFA0,0x25BFA8,0x25BFB0,0x25BFC0,0x25BF80,0x25BFB8};
    constexpr std::uint32_t strings[]={0,0x5759B8,0x575904,0x57597C,0x55CE84,
        0x5759A8,0x544868,0x575990,0x5759B0,0x57596C,0x575960,0x57594C,
        0x575938,0x575918,0x57599C,0x57592C};
    std::uint32_t table[16]{};
    if(!read(image+0x25BFE0,table,sizeof(table)))return false;
    for(unsigned i=0;i<16;++i){
        if(table[i]!=image+branches[i])return false;
        if(!i)continue;
        unsigned char branch[8]{};std::uint32_t string=0;
        if(!read(table[i],branch,sizeof(branch))||branch[0]!=0xB8||branch[5]!=0xC2||branch[6]!=4||branch[7])return false;
        std::memcpy(&string,branch+1,4);
        const auto* key=PromptKey(static_cast<Prompt>(i));char actual[24]{};
        const auto size=std::strlen(key)+1;
        if(string!=image+strings[i]||size>sizeof(actual)||!read(string,actual,size)||std::memcmp(actual,key,size))return false;
    }
    return true;
}

template<class Reader> State Snapshot(std::uintptr_t image,Reader read,std::uintptr_t expectedPlayer,
                                      bool includeBindingDetails=false) {
    State output;
    if(!Address(image,GameDataRva+4)||!Address(expectedPlayer,0xAEC))return output;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    struct Links {
        std::uint32_t data=0,player=0,vtable=0,prompt=0,textHandle=0,binding=0,textObject=0;
        std::uint32_t manager=0,bindingType=0,record=0,index=0;
        bool bindingKnown=false,textOverride=false;
        char text[128]{},bindingLabel[128]{};
    } first,second;
    const auto collect=[&](Links& links){
        std::uint32_t active[7]{};
        if(!get(image+GameDataRva,links.data)||!Address(links.data,0x1528)||
           !get(links.data+0xA2C,links.player)||links.player!=expectedPlayer||
           !get(links.player,links.vtable)||links.vtable!=image+PlayerVtableRva||
           !read(links.player+0xAD0,active,sizeof(active)))return false;
        links.prompt=active[0];links.textHandle=active[4];links.binding=active[6];
        if(links.prompt>15)return false;
        if(!links.prompt)return true; // Stale inactive auxiliary fields are never followed.
        const auto* key=PromptKey(static_cast<Prompt>(links.prompt));
        std::memcpy(links.text,key,std::strlen(key)+1);
        if(links.textHandle){
            if(!get(links.textHandle,links.textObject))return false;
            if(links.textObject){
                if(!Address(links.textObject,0xC+sizeof(links.text))||
                   !read(links.textObject+0xC,links.text,sizeof(links.text))||!PrintableKey(links.text))return false;
                links.textOverride=true;
            }
        }
        if(!includeBindingDetails||!links.binding)return true;
        if(!Address(links.binding,0xC4)||!get(links.binding+0xC0,links.bindingType)||
           !links.bindingType||!get(links.data+0x1524,links.manager)||!Address(links.manager,35*4))return false;
        std::uint32_t records[35]{};
        if(!read(links.manager,records,sizeof(records)))return false;
        for(const auto record:records){
            if(!record)continue;
            std::uint32_t identity[2]{};
            if(!Address(record,0x19+sizeof(links.bindingLabel))||!read(record,identity,sizeof(identity)))return false;
            if(identity[0]!=links.bindingType)continue;
            if(identity[1]>=35)return false;
            links.record=record;links.index=identity[1];links.bindingKnown=true;
            if(!read(record+0x19,links.bindingLabel,sizeof(links.bindingLabel))||!PrintableKey(links.bindingLabel))return false;
            break; // Native6117E0 uses the first matching type record.
        }
        return true;
    };
    if(!collect(first)||!collect(second)||first.data!=second.data||first.player!=second.player||
       first.vtable!=second.vtable||first.prompt!=second.prompt||first.textHandle!=second.textHandle||
       first.binding!=second.binding||first.textObject!=second.textObject||first.manager!=second.manager||
       first.bindingType!=second.bindingType||first.record!=second.record||first.index!=second.index||
       first.bindingKnown!=second.bindingKnown||first.textOverride!=second.textOverride||
       std::memcmp(first.text,second.text,sizeof(first.text))||std::memcmp(first.bindingLabel,second.bindingLabel,sizeof(first.bindingLabel)))return output;
    output.valid=true;output.eligible=first.prompt!=0;output.player=first.player;
    output.target=first.prompt;output.nativeArg=first.prompt;output.prompt=static_cast<Prompt>(first.prompt);
    output.binding=first.binding;output.textHandle=first.textHandle;output.textOverride=first.textOverride;
    output.bindingType=first.bindingType;output.bindingIndex=first.index;output.bindingKnown=first.bindingKnown;
    std::memcpy(output.nativeText,first.text,sizeof(output.nativeText));
    std::memcpy(output.bindingLabel,first.bindingLabel,sizeof(output.bindingLabel));
    return output;
}
}
