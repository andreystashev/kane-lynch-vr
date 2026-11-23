#include "native_context_hud_kl.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

namespace {
constexpr std::uint32_t Image=0x400000,Data=0x10000000,Player=0x11000000;
constexpr std::uint32_t Binding=0x12000000,Type=0x13000000,Manager=0x14000000;
constexpr std::uint32_t Record=0x15000000,TextHandle=0x16000000,TextObject=0x17000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> data;
    template<class T>void Set(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i)data[at+i]=p[i];}
    void Text(std::uintptr_t at,const char* text){char bytes[128]{};std::snprintf(bytes,sizeof(bytes),"%s",text);Set(at,bytes);}
    bool Read(std::uintptr_t at,void* output,std::size_t count)const{
        auto* p=static_cast<unsigned char*>(output);for(std::size_t i=0;i<count;++i){const auto found=data.find(at+i);if(found==data.end())return false;p[i]=found->second;}return true;
    }
    auto Reader()const{return [&](auto at,void* output,auto count){return Read(at,output,count);};}
    Memory(){
        using namespace NativeContextHudKL;
        Set(Image+GameDataRva,Data);Set(Data+0xA2C,Player);Set(Player,std::uint32_t(Image+PlayerVtableRva));
        const std::uint32_t fields[]={2,0xDEADBEEF,0,0,0,0xDEADBEEF,Binding};Set(Player+0xAD0,fields);
        Set(Binding+0xC0,Type);Set(Data+0x1524,Manager);std::uint32_t records[35]{};records[4]=Record;Set(Manager,records);
        Set(Record,Type);Set(Record+4,std::uint32_t(4));Text(Record+0x19,"UseItem");
        Set(TextHandle,TextObject);Text(TextObject+0xC,"DoorPromptFromNative");
    }
};
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
struct FileImage {
    struct Section {std::uint32_t virtualSize,rva,rawSize,raw;};
    std::vector<unsigned char> data;std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& value)const {if(at>data.size()||sizeof(value)>data.size()-at)return false;std::memcpy(&value,data.data()+at,sizeof(value));return true;}
    bool Load(const char* path){
        std::ifstream file(path,std::ios::binary);if(!file)return false;
        file.seekg(0,std::ios::end);const auto size=file.tellg();if(size<0||size>128*1024*1024)return false;
        data.resize(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),size);if(!file)return false;
        std::uint32_t pe=0,signature=0;std::uint16_t count=0,optional=0,magic=0;
        if(!Get(0x3C,pe)||!Get(pe,signature)||signature!=0x4550||!Get(pe+6,count)||count>96||
           !Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10B)return false;
        for(unsigned i=0;i<count;++i){Section section{};if(!Get(pe+24+optional+40*i+8,section))return false;sections.push_back(section);}
        return true;
    }
    bool Read(std::uintptr_t address,void* output,std::size_t size)const {
        if(address<Image)return false;const auto rva=address-Image;
        for(const auto& section:sections)if(rva>=section.rva&&rva-section.rva<section.rawSize){
            const auto offset=rva-section.rva;
            if(size>section.rawSize-offset||section.raw+offset>data.size()||size>data.size()-section.raw-offset)return false;
            std::memcpy(output,data.data()+section.raw+offset,size);return true;
        }
        return false;
    }
};
}
int main(int argc,char** argv){
    using namespace NativeContextHudKL;
    Memory memory;const auto initial=memory.data;
    auto state=Snapshot(Image,memory.Reader(),Player);
    Check(state.valid&&state.eligible&&state.player==Player&&state.target==2&&state.nativeArg==2&&state.prompt==Prompt::PickupWeapon,
          "AD0 is native numeric prompt; pending AD4/AE4 never substitute for eligibility");
    Check(!std::strcmp(state.nativeText,"PickUpWeaponNoText")&&!state.bindingKnown&&state.binding==Binding,
          "normal telemetry resolves proven action key without binding table work");
    Check(initial==memory.data,"telemetry never writes game memory");
    constexpr const char* keys[]={"","PickUp","PickUpWeaponNoText","PickUpSpecialProp","Select","Talk","Generic","FIX ME MSL",
        "Rappel","EnterVehicle","ExitVehicle","TakeMountedWeapon","LeaveMountedWeapon","ThrowBackGrenade","GiveHealth","TakeHimOut"};
    for(std::uint32_t id=0;id<16;++id){
        memory.Set(Player+0xAD0,id);state=Snapshot(Image,memory.Reader(),Player);
        Check(state.valid&&state.eligible==(id!=0)&&state.nativeArg==id&&state.prompt==static_cast<Prompt>(id)&&
              !std::strcmp(state.nativeText,keys[id]),"all exact native prompt switch IDs preserve their own labels");
    }
    memory=Memory{};state=Snapshot(Image,memory.Reader(),Player,true);
    Check(state.valid&&state.bindingKnown&&state.bindingType==Type&&state.bindingIndex==4&&!std::strcmp(state.bindingLabel,"UseItem"),
          "optional bounded binding proof matches type and keeps control label separate");
    Check(state.prompt==Prompt::PickupWeapon&&!std::strcmp(state.nativeText,"PickUpWeaponNoText"),"binding index cannot reinterpret action prompt");
    memory.Set(Player+0xAE0,TextHandle);state=Snapshot(Image,memory.Reader(),Player);
    Check(state.valid&&state.textOverride&&state.textHandle==TextHandle&&!std::strcmp(state.nativeText,"DoorPromptFromNative"),
          "AE0 handle override follows native handle to C string at object plus C");
    memory.Set(TextHandle,std::uint32_t(0));state=Snapshot(Image,memory.Reader(),Player);
    Check(state.valid&&!state.textOverride&&!std::strcmp(state.nativeText,"PickUpWeaponNoText"),"empty handle object retains native switch fallback");
    memory.Set(Player+0xAD0,std::uint32_t(0));memory.Set(Player+0xAE0,std::uint32_t(0xDEADBEEF));memory.Set(Player+0xAE8,std::uint32_t(0xDEADBEEF));
    state=Snapshot(Image,memory.Reader(),Player,true);Check(state.valid&&!state.eligible,"inactive prompt never follows stale text or binding pointers");
    memory=Memory{};memory.Set(Player+0xAE8,std::uint32_t(0));state=Snapshot(Image,memory.Reader(),Player,true);
    Check(state.valid&&state.eligible&&!state.bindingKnown,"native text prompt can remain active without icon binding");
    memory=Memory{};memory.Set(Record,Type+4);state=Snapshot(Image,memory.Reader(),Player,true);
    Check(state.valid&&state.eligible&&!state.bindingKnown,"unknown binding fallback never invokes game virtual calls");
    memory=Memory{};memory.Set(Record+4,std::uint32_t(35));Check(!Snapshot(Image,memory.Reader(),Player,true).valid,"binding index remains bounded");
    memory=Memory{};memory.Set(Player+0xAD0,std::uint32_t(16));Check(!Snapshot(Image,memory.Reader(),Player).valid,"unknown action enum retains original prompt");
    for(const auto at:{std::uintptr_t(Image+GameDataRva),std::uintptr_t(Data+0xA2C),std::uintptr_t(Player)}){
        memory=Memory{};memory.Set(at,std::uint32_t(0));Check(!Snapshot(Image,memory.Reader(),Player).valid,"missing exact data or player ownership fails closed");
    }
    memory=Memory{};memory.Set(Data+0xA2C,Player+4);Check(!Snapshot(Image,memory.Reader(),Player).valid,"other current player cannot authorize prompt");
    memory=Memory{};memory.Set(Player+0xAE0,TextHandle);char malformed[128];std::memset(malformed,'x',sizeof(malformed));memory.Set(TextObject+0xC,malformed);
    Check(!Snapshot(Image,memory.Reader(),Player).valid,"unterminated string cannot escape native text bound");
    malformed[4]='\0';malformed[1]='\n';memory.Set(TextObject+0xC,malformed);Check(!Snapshot(Image,memory.Reader(),Player).valid,"control characters are rejected");
    malformed[1]=char(0xFE);memory.Set(TextObject+0xC,malformed);Check(!Snapshot(Image,memory.Reader(),Player).valid,"non-key byte encoding is not displayed as native text");
    memory.Text(TextObject+0xC,"");state=Snapshot(Image,memory.Reader(),Player);Check(state.valid&&state.textOverride&&!state.nativeText[0],"native empty override is preserved exactly");
    for(const auto at:{Player+0xAD0,Player+0xAE0,Player+0xAE8}){
        memory=Memory{};unsigned observations=0;
        auto torn=[&](auto address,void* output,auto size){if(address==Player+0xAD0&&++observations==2)memory.Set(at,std::uint32_t(at==Player+0xAE0?TextHandle:0));return memory.Read(address,output,size);};
        Check(!Snapshot(Image,torn,Player).valid,"changed committed prompt tuple between observations is rejected");
    }
    memory=Memory{};memory.Set(Player+0xAE0,TextHandle);unsigned textReads=0;
    auto tornText=[&](auto address,void* output,auto size){if(address==TextObject+0xC&&++textReads==2)memory.Text(address,"OtherNativePrompt");return memory.Read(address,output,size);};
    Check(!Snapshot(Image,tornText,Player).valid,"localization key changes cannot mix across observations");
    memory=Memory{};unsigned recordReads=0;
    auto tornRecord=[&](auto address,void* output,auto size){if(address==Record&&++recordReads==2)memory.Set(Record+4,std::uint32_t(5));return memory.Read(address,output,size);};
    Check(!Snapshot(Image,tornRecord,Player,true).valid,"binding record replacement is rejected");
    Check(!Snapshot(0,memory.Reader(),Player).valid&&!Snapshot(0xFFFF0000u,memory.Reader(),Player).valid&&
          !Snapshot(Image,memory.Reader(),0).valid&&!Snapshot(Image,memory.Reader(),0xFFFFF800u).valid,"x86 address arithmetic is bounded on x86 and x64 test hosts");
    Check(!VerifyImage(Image,memory.Reader()),"missing opcode proof cannot authorize helper");
    Check(IsOpenDoorKey("OpenDoor")&&!IsOpenDoorKey("M05_LynchOpenDoor")&&!IsOpenDoorKey("ROOT\\OpenDoor")&&
          !IsOpenDoorKey("openDoor")&&!IsOpenDoorKey(nullptr),"only exact verified interaction key selects open door label");
    state={};state.valid=state.eligible=true;state.prompt=Prompt::Generic;state.textOverride=true;
    std::strcpy(state.nativeText,"PickupPhone");Check(std::wstring(EnglishAction(state))==L"Pick up phone","exact English phone key becomes the real contextual action");
    std::strcpy(state.nativeText,"M08_ControlPanel01");Check(std::wstring(EnglishAction(state))==L"Open gates","mission-specific generic action retains its actual English meaning");
    std::strcpy(state.nativeText,"M09A_PlaceBomb");Check(std::wstring(EnglishAction(state))==L"Place bomb","bomb context is not flattened into an invented generic Use label");
    std::strcpy(state.nativeText,"M05_LynchOpenDoor");Check(!EnglishAction(state),"dialogue and unknown generic keys do not fabricate a contextual X interaction");
    state.eligible=false;Check(!EnglishAction(state),"inactive prompt cannot display a previous English interaction");
    if(argc>1){
        FileImage executable;Check(executable.Load(argv[1]),"offline supported PE32 image loads");
        auto reader=[&](auto at,void* output,auto size){return executable.Read(at,output,size);};
        Check(VerifyImage(Image,reader),"actual pinned EXE proves all prompt consumers, mapping and labels");
        auto changed=[&](auto at,void* output,auto size){if(!reader(at,output,size))return false;if(at==Image+0x25C4ED)static_cast<unsigned char*>(output)[0]^=1;return true;};
        Check(!VerifyImage(Image,changed),"changed real prompt consumer disables proof");
    }
    std::printf("Pinned native context HUD: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
