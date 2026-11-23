#include "native_team_hud_kl.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
constexpr std::uint32_t Image=0x400000,Data=0x10000000,Player=0x11000000,Display=0x12000000;
constexpr std::uint32_t Control=0x13000000,Text=0x14000000,MemberBase=0x15000000,NameBase=0x16000000;
constexpr std::uint32_t Manager=0x17000000,Frontend=0x18000000;
std::uint32_t MemberAt(unsigned i){return MemberBase+i*0x1000;}
std::uint32_t Name(unsigned i){return NameBase+i*0x1000;}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t at,const T& value) {
        const auto* source=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)bytes[at+i]=source[i];
    }
    bool Read(std::uintptr_t at,void* output,std::size_t size)const {
        auto* target=static_cast<unsigned char*>(output);
        for(std::size_t i=0;i<size;++i){const auto found=bytes.find(at+i);if(found==bytes.end())return false;target[i]=found->second;}
        return true;
    }
    auto Reader()const{return [&](std::uintptr_t at,void* output,std::size_t size){return Read(at,output,size);};}
    Memory() {
        Set(Image+0x8883A4,std::uint8_t(0));Set(Image+0x68760C,Data);Set(Data+0xA2C,Player);
        Set(Player,Image+0x56582C);Set(Player+0xAA8,Control);Set(Display,Image+0x575A94);
        Set(Player+0x804,std::int8_t(0));Set(Data+0x154C,Manager);
        Set(Manager,Frontend);Set(Manager+8,std::int8_t(0));Set(Manager+12,std::uint32_t(0));Set(Manager+20,std::int8_t(-1));
        Set(Frontend,Image+0x5755FC);Set(Frontend+0x904,Display);
        Set(Display+0x10,Player);Set(Display+0x3C,Text);Set(Text,Image+0x558414);
        Set(Control,Image+0x568374);Set(Control+0x10,Control+0x1C);Set(Control+0x14,std::uint32_t(3));
        Set(Display+0x5F8,std::uint32_t(3));
        for(unsigned i=0;i<4;++i){
            Set(Control+0x18+i*4,i<3?MemberAt(i):0u);Set(Display+0x60C+i*4,i<3?MemberAt(i):0u);
            Set(Display+0x5FC+i*4,std::uint32_t(7+i));
            if(i==3)continue;
            Set(MemberAt(i)+0x261,std::uint8_t(1));Set(MemberAt(i)+0x2D0,Name(i));Set(MemberAt(i)+0x2DC,std::uint32_t(100+i));
            char name[64]{};std::snprintf(name,sizeof(name),"Native %c",'A'+i);Set(Name(i)+0x3C,name);
        }
    }
};
bool Capture(const Memory& memory,NativeTeamHudKL::Roster& output,std::uint32_t tick=1000) {
    return NativeTeamHudKL::CaptureRoster(Image,Display,Player,tick,memory.Reader(),output);
}
std::uint32_t U32(const std::vector<unsigned char>& bytes,std::size_t at) {
    std::uint32_t value=0;if(at+4<=bytes.size())std::memcpy(&value,bytes.data()+at,4);return value;
}
std::uint16_t U16(const std::vector<unsigned char>& bytes,std::size_t at) {
    std::uint16_t value=0;if(at+2<=bytes.size())std::memcpy(&value,bytes.data()+at,2);return value;
}
void ActualImage(const char* path) {
    std::ifstream file(path,std::ios::binary);
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
    Check(!raw.empty(),"actual pinned EXE can be read without starting the game");if(raw.empty())return;
    const auto nt=U32(raw,0x3C),optional=nt+24,base=U32(raw,optional+28),size=U32(raw,optional+56);
    Check(U16(raw,0)==0x5A4D&&U32(raw,nt)==0x4550&&U16(raw,nt+4)==0x14C&&size<0x2000000,
          "actual fixture maps a bounded x86 PE image");
    if(U16(raw,0)!=0x5A4D||U32(raw,nt)!=0x4550||U16(raw,nt+4)!=0x14C||!size||size>=0x2000000)return;
    std::vector<unsigned char> mapped(size);
    const auto sections=U16(raw,nt+6);const auto sectionTable=optional+U16(raw,nt+20);
    for(unsigned i=0;i<sections;++i) {
        const auto at=sectionTable+i*40,rva=U32(raw,at+12),count=U32(raw,at+16),offset=U32(raw,at+20);
        if(rva<=mapped.size()&&count<=mapped.size()-rva&&offset<=raw.size()&&count<=raw.size()-offset)
            std::memcpy(mapped.data()+rva,raw.data()+offset,count);
    }
    const auto reader=[&](std::uintptr_t at,void* output,std::size_t bytes){
        if(at<base||at-base>mapped.size()||bytes>mapped.size()-(at-base))return false;
        std::memcpy(output,mapped.data()+at-base,bytes);return true;
    };
    Check(NativeTeamHudKL::VerifyImage(base,reader),"actual native draw, getters and char-name setter qualify independently");
    const auto countByte=mapped[0x12F500];mapped[0x12F500]=0xE9;
    Check(!NativeTeamHudKL::VerifyImage(base,reader),"TEAM proof must precede Objective Count detour; weakening shared original-byte proof would hide a real install-order regression");
    mapped[0x12F500]=countByte;
    Check(NativeTeamHudKL::VerifyImage(base,reader),"restoring original shared Count dependency restores the independent TEAM source proof");
    mapped[0x162723]^=1;
    Check(!NativeTeamHudKL::VerifyImage(base,reader),"a changed actual char-name consumer disables roster observation");
}
}
int main(int argc,char** argv) {
    using NativeTeamHudKL::WristCircleColour;
    Check(WristCircleColour(0x00ffffffu,0)==0xffffffffu&&WristCircleColour(0x04aa2222u,1)==0xffaa2222u&&
          WristCircleColour(0x05888888u,2)==0xff888888u,"native circles keep selection and health colours without losing alpha");
    Check(WristCircleColour(0x11233547u,7)==0x11233547u&&WristCircleColour(0x01998877u,8)==0x01998877u,
          "native shield and lightning keep their exact animation and colour");
    using namespace NativeTeamHudKL;
    Memory memory;Roster roster;
    const auto original=memory.bytes;
    Check(Capture(memory,roster),"native owned roster captured from the actual display/control graph shape");
    Check(roster.count==3&&std::strcmp(roster.members[0].name,"Native C")==0&&
          std::strcmp(roster.members[1].name,"Native B")==0&&std::strcmp(roster.members[2].name,"Native A")==0,
          "VR roster preserves native reversed display order and native names");
    Check(!roster.members[0].selected&&roster.members[1].selected&&!roster.members[2].selected,
          "selected member comes from the real control selection pointer");
    Check(roster.members[0].nativeStatus==9&&roster.members[1].nativeStatus==8&&roster.members[2].nativeStatus==7,
          "native indicator IDs are preserved without inventing command labels");
    Check(StatusGlyph(7)!=StatusGlyph(8)&&StatusGlyph(8)!=StatusGlyph(9)&&StatusGlyph(7)!=StatusGlyph(9),"native defend attack and follow have distinct vector roster icons");
    Check(!*StatusText(7)&&!*StatusText(8)&&!*StatusText(9),"native state is represented by icons without command status words");
    Check(!*StatusText(123),"unknown native indicator does not invent a follow order");
    Check(!std::wcscmp(EnglishName(L"\u041B\u0438\u043D\u0447"),L"Lynch")&&!std::wcscmp(EnglishName(L"\u041A\u0415\u0419\u041D"),L"Kane")&&
          !std::wcscmp(EnglishName(L"Native C"),L"Native C"),"known localized character names are English while unknown proper names remain native");
    Check(memory.bytes==original,"roster observation is read-only");
    Check(CaptureCurrentRoster(Image,Player,1000,memory.Reader(),roster)&&roster.count==3,
          "held VR roster resolves its live native display without any Draw callback");
    memory.Set(Display+0x5FC,std::uint32_t(8));
    Check(CaptureCurrentRoster(Image,Player,5000,memory.Reader(),roster)&&roster.tick==5000&&roster.members[2].nativeStatus==8,
          "long held panel reads changed native status after old draw cache would have expired");
    memory=Memory{};Capture(memory,roster);
    for(unsigned broken=0;broken<7;++broken) {
        memory=Memory{};Roster untouched;untouched.player=1;
        switch(broken) {
        case 0:memory.Set(Player+0x804,std::int8_t(-1));break;
        case 1:memory.Set(Manager+8,std::int8_t(1));break;
        case 2:memory.Set(Frontend,std::uint32_t(0));break;
        case 3:memory.Set(Frontend+0x904,Display+4);break;
        case 4:memory.Set(Manager+20,std::int8_t(0));memory.Set(Manager+12,Frontend);break;
        case 5:memory.Set(Data+0x154C,std::uint32_t(0xfffffff0));break;
        case 6:memory.Set(Data+0xA2C,Player+4);break;
        }
        Check(!CaptureCurrentRoster(Image,Player,1000,memory.Reader(),untouched)&&untouched.player==1,
              "foreign ambiguous or invalid live frontend cannot replace the roster");
    }
    for(unsigned race=0;race<3;++race) {
        memory=Memory{};unsigned hits=0;
        const auto reader=[&](std::uintptr_t at,void* output,std::size_t bytes) {
            const std::uintptr_t watched[]={Frontend+0x904,Manager,Manager+20};
            if(at==watched[race]&&++hits==2) {
                if(race==0)memory.Set(Frontend+0x904,Display+4);
                if(race==1)memory.Set(Manager,Frontend+4);
                if(race==2){memory.Set(Manager+20,std::int8_t(0));memory.Set(Manager+12,Frontend);}
            }
            return memory.Read(at,output,bytes);
        };
        Check(!CaptureCurrentRoster(Image,Player,1000,reader,roster),"live display identity or slot races cannot publish a torn roster");
    }
    memory=Memory{};Capture(memory,roster);
    Check(MirroredRoster(roster,roster,1000,1000)&&MirroredRoster(roster,roster,1000,1200),
          "only an actual matching fresh VR draw authorizes held-panel source suppression");
    Check(!MirroredRoster({},roster,1000,1000)&&!MirroredRoster(roster,roster,0,1000),
          "an unrendered or cleared team panel retains the native source");
    Check(!MirroredRoster(roster,roster,1000,1201)&&!MirroredRoster(roster,roster,1001,1000),
          "stale or future render acknowledgment cannot hide the native roster");
    for(unsigned change=0;change<7;++change) {
        auto mismatch=roster;
        switch(change) {
        case 0:mismatch.player+=4;break;
        case 1:mismatch.count-=1;break;
        case 2:mismatch.members[0].nativeId+=1;break;
        case 3:mismatch.members[0].selected=true;break;
        case 4:mismatch.members[0].name[0]='X';break;
        case 5:mismatch.valid=false;break;
        case 6:mismatch.members[0].nativeStatus+=1;break;
        }
        Check(!MirroredRoster(mismatch,roster,1000,1000),"a changed owner, roster, receiver selection or native name retains the source until redrawn");
    }
    Check(Fresh(roster,Player,1000)&&Fresh(roster,Player,1200),"matching roster is fresh within the bounded UI interval");
    Check(!Fresh(roster,Player,1201)&&!Fresh(roster,Player,999)&&!Fresh(roster,Player+4,1000),"stale, future and foreign rosters are rejected");
    auto changed=roster;changed.valid=false;Check(!Fresh(changed,Player,1000),"invalid roster is not rendered");
    changed=roster;changed.tick=0;Check(!Fresh(changed,Player,1000),"zero timestamp is not a live roster");
    changed=roster;changed.count=5;Check(!Fresh(changed,Player,1000),"oversized roster cannot be rendered");
    changed=roster;changed.tick=0xFFFFFFF0u;Check(Fresh(changed,Player,100)&&!Fresh(changed,Player,185),"roster freshness handles the native clock wrapping");
    for(unsigned broken=0;broken<15;++broken) {
        memory=Memory{};Roster untouched;untouched.player=1;untouched.tick=2;
        switch(broken) {
        case 0:memory.Set(Data+0xA2C,Player+4);break;
        case 1:memory.Set(Display+0x10,Player+4);break;
        case 2:memory.Set(Display,std::uint32_t(0));break;
        case 3:memory.Set(Control,std::uint32_t(0));break;
        case 4:memory.Set(Text,std::uint32_t(0));break;
        case 5:memory.Set(Control+0x14,std::uint32_t(5));break;
        case 6:memory.Set(Control+0x10,Control+0x19);break;
        case 7:memory.Set(Control+0x10,Control+0x14);break;
        case 8:memory.Set(Control+0x10,Control+0x28);break;
        case 9:memory.Set(Control+0x1C,MemberAt(0));break;
        case 10:memory.Set(Display+0x60C,MemberAt(1));break;
        case 11:memory.Set(Display+0x5F8,std::uint32_t(2));break;
        case 12:memory.Set(Image+0x8883A4,std::uint8_t(2));break;
        case 13:memory.bytes.erase(Name(1)+0x3C);break;
        case 14:memory.Set(MemberAt(1)+0x2D0,std::uint32_t(0xFFFFFFE0u));break;
        }
        Check(!Capture(memory,untouched)&&untouched.player==1&&untouched.tick==2,
              "ownership, type, bounds and partial-read failures retain output and native fallback");
    }
    for(unsigned mode=1;mode<7;++mode){memory=Memory{};memory.Set(Image+0x8883A4,static_cast<std::uint8_t>(mode));
        Check(!Capture(memory,roster),"map, inventory and other native modes retain the source roster");}
    for(unsigned kind=0;kind<3;++kind){memory=Memory{};char invalid[64]{};
        if(kind==0)std::memset(invalid,'x',sizeof(invalid));else if(kind==1){invalid[0]='x';invalid[1]='\n';}
        memory.Set(Name(0)+0x3C,invalid);Check(!Capture(memory,roster),"empty, unterminated and control-character names cannot be fabricated");}
    memory=Memory{};memory.Set(MemberAt(1)+0x261,std::uint8_t(0));memory.Set(Display+0x5F8,std::uint32_t(2));
    Check(Capture(memory,roster)&&roster.count==2,"disabled receivers are filtered exactly as native display update");
    memory=Memory{};memory.Set(Control+0x10,Control+0x24);
    Check(Capture(memory,roster)&&!roster.members[0].selected&&!roster.members[1].selected&&!roster.members[2].selected,
          "native end selection does not fabricate a selected teammate");
    Check(roster.allSelected,"native end selection is displayed as the whole squad");
    auto individual=roster;individual.allSelected=false;
    Check(!MirroredRoster(individual,roster,1000,1000),"an all-receiver change must be visible before source roster suppression");
    auto invalidAll=roster;invalidAll.members[0].selected=true;
    Check(!Fresh(invalidAll,Player,1000),"whole squad and individual highlight cannot coexist");
    invalidAll=roster;invalidAll.count=0;
    Check(!Fresh(invalidAll,Player,1000),"empty squad cannot claim an all-receiver highlight");
    TeamCommandIPC::Result result;result.serial=1;result.tick=1000;result.command=TeamCommandIPC::Command::Move;
    result.code=TeamCommandIPC::ResultCode::Accepted;
    Check(CommandResultText(result,roster,1000)==nullptr,"accepted order adds no textual command status beside the icon-only roster");
    result.nativeId=999;
    Check(CommandResultText(result,roster,1000)==nullptr,"an individual result cannot masquerade as all-squad acknowledgement");
    result.code=TeamCommandIPC::ResultCode::SelectionChanged;
    Check(CommandResultText(result,roster,1000)!=nullptr,"selection-change rejection remains visible after the highlighted receiver changes");
    result.code=TeamCommandIPC::ResultCode::NoTarget;
    Check(CommandResultText(result,roster,1000)==nullptr,"no-target result remains bound to the receiver that requested it");
    result.code=TeamCommandIPC::ResultCode::Accepted;
    result.nativeId=0;result.tick=0xffff0000u;
    Check(CommandResultText(result,roster,1000)==nullptr,"old command result is not displayed as a current acknowledgement");
    memory=Memory{};memory.Set(Control+0x14,std::uint32_t(0));memory.Set(Control+0x10,Control+0x18);memory.Set(Display+0x5F8,std::uint32_t(0));
    Check(Capture(memory,roster)&&roster.valid&&!roster.count,"proven empty native roster remains an empty panel");
    for(unsigned race=0;race<4;++race) {
        memory=Memory{};unsigned hits=0;
        const auto reader=[&](std::uintptr_t at,void* output,std::size_t bytes){
            const std::uintptr_t watched[]={Control+0x10,Name(1)+0x3C,Display+0x60C,Image+0x68760C};
            if(at==watched[race]&&++hits==2) {
                if(race==0)memory.Set(Control+0x10,Control+0x18);
                if(race==1){char text[64]="Changed native name";memory.Set(Name(1)+0x3C,text);}
                if(race==2)memory.Set(Display+0x60C,MemberAt(1));
                if(race==3)memory.Set(Data+0xA2C,Player+4);
            }
            return memory.Read(at,output,bytes);
        };
        Check(!CaptureRoster(Image,Display,Player,1000,reader,roster),"concurrent selection, name, roster or player changes produce no torn VR list");
    }
    memory=Memory{};
    Check(!CaptureRoster(0,Display,Player,1000,memory.Reader(),roster)&&
          !CaptureRoster(Image,0xFFFFFFF0u,Player,1000,memory.Reader(),roster)&&
          !CaptureRoster(Image,Display,Player,0,memory.Reader(),roster),"wrapped addresses and missing live tick are rejected before traversal");
    memory=Memory{};memory.Set(Image+0x8883A4,std::uint8_t(3));std::uintptr_t owned=0;
    Check(OwnedDisplay(Image,Display,memory.Reader(),owned,true)&&owned==Player,"VR weapon inventory can hide only the proved local roster display");
    Check(!OwnedDisplay(Image,Display,memory.Reader(),owned),"ordinary roster readers still preserve native inventory policy");
    if(argc>1)ActualImage(argv[1]);
    std::printf("Native KL1 team HUD: %u checks, %u failures.\n",checks,failures);
    return failures?1:0;
}
