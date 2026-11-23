#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include "team_command_types.hpp"
#include "native_team_marker_kl.hpp"

// Read-only ZCommandDisplay2 / ZSquadControl observations for the pinned KL1
// executable. Selection is performed by the game's existing Tab/Shift+Tab
// bindings; this module never invokes AI orders or writes game objects.
namespace NativeTeamHudKL {
inline constexpr std::uintptr_t DisplayVtableRva=0x575A94,ControlVtableRva=0x568374;
inline constexpr unsigned MaxMembers=4,NameBytes=64;
struct Member {
    std::uint32_t nativeId=0,nativeStatus=0;
    bool selected=false;
    char name[NameBytes]{};
};
struct Roster {
    std::uintptr_t player=0;
    std::uint32_t tick=0;
    bool valid=false,allSelected=false;
    unsigned count=0;
    std::array<Member,MaxMembers> members{};
};
inline std::uint32_t WristCircleColour(std::uint32_t colour,std::uint32_t status){
    return NativeTeamMarkerKL::StatusIcon(status)==NativeTeamMarkerKL::Icon::Circle?
        colour|0xff000000u:colour;
}
// Native Update65DAC4 selects shield for7, lightning for8 and the circle
// otherwise. Private glyph IDs are rendered as vector outlines, not emoji.
inline wchar_t StatusGlyph(std::uint32_t status){return wchar_t(0xE100u+unsigned(NativeTeamMarkerKL::StatusIcon(status)));}
inline const wchar_t* StatusText(std::uint32_t status) {
    // State is conveyed by the native circle/shield/lightning only.
    (void)status;return L"";
}
inline const wchar_t* EnglishName(const wchar_t* name) {
    if(!name)return L"";
    const struct {const wchar_t* local;const wchar_t* english;} names[]={
        {L"\u041A\u0435\u0439\u043D",L"Kane"},{L"\u041A\u0415\u0419\u041D",L"Kane"},{L"\u041B\u0438\u043D\u0447",L"Lynch"},{L"\u041B\u0418\u041D\u0427",L"Lynch"},
        {L"\u0428\u0435\u043B\u043B\u0438",L"Shelly"},{L"\u0420\u0438\u0444\u0438\u043A",L"Rific"},{L"\u0422\u0430\u043F\u0430",L"Thapa"}};
    for(const auto& item:names)if(!std::wcscmp(name,item.local))return item.english;
    return name;
}
inline bool Address(std::uintptr_t at,std::size_t bytes) {
    return at>=0x10000u&&bytes<=0xffffffffu&&at<=0xffffffffu-bytes;
}
inline bool NameValid(const char (&name)[NameBytes]) {
    for(unsigned i=0;i<NameBytes;++i) {
        const auto c=static_cast<unsigned char>(name[i]);
        if(!c)return i!=0;
        if(c<32||c==127)return false;
    }
    return false;
}
inline bool Fresh(const Roster& roster,std::uintptr_t player,std::uint32_t now) {
    if(!roster.valid||!player||roster.player!=player||!roster.tick||
       static_cast<std::int32_t>(now-roster.tick)<0||now-roster.tick>200||roster.count>MaxMembers)return false;
    if(roster.allSelected&&!roster.count)return false;
    for(unsigned i=0;i<roster.count;++i)if(!NameValid(roster.members[i].name)||(roster.allSelected&&roster.members[i].selected))return false;
    return true;
}
inline bool MirroredRoster(const Roster& rendered,const Roster& native,std::uint32_t renderedTick,std::uint32_t now) {
    if(!renderedTick||static_cast<std::int32_t>(now-renderedTick)<0||now-renderedTick>200||
       !Fresh(rendered,native.player,now)||!Fresh(native,native.player,now)||rendered.count!=native.count||rendered.allSelected!=native.allSelected)return false;
    for(unsigned i=0;i<native.count;++i) {
        const auto& a=rendered.members[i];const auto& b=native.members[i];
        if(a.nativeId!=b.nativeId||a.nativeStatus!=b.nativeStatus||a.selected!=b.selected||std::memcmp(a.name,b.name,sizeof(a.name)))return false;
    }
    return true;
}
inline const wchar_t* CommandResultText(const TeamCommandIPC::Result& result,const Roster& roster,std::uint32_t now) {
    using TeamCommandIPC::ResultCode;
    if(!result.serial||!result.tick||!TeamCommandIPC::Known(result.command)||!TeamCommandIPC::Known(result.code)||
       std::int32_t(now-result.tick)<0||now-result.tick>2000||!Fresh(roster,roster.player,now))return nullptr;
    // A changed-selection rejection explains why no receiver was commanded;
    // unlike an acceptance it must remain visible after the highlight moves.
    if(result.code==ResultCode::SelectionChanged)return L"Selection changed; try again";
    bool selected=result.nativeId==0&&roster.allSelected;
    for(unsigned i=0;i<roster.count;++i)selected=selected||(roster.members[i].selected&&roster.members[i].nativeId==result.nativeId);
    if(!selected)return nullptr;
    switch(result.code) {
    case ResultCode::Accepted:return nullptr;
    case ResultCode::NoTarget:return L"No target";
    case ResultCode::Stale:return L"Try again";
    case ResultCode::Rejected:return L"Unavailable";
    case ResultCode::Unsupported:return L"Unavailable";
    default:return nullptr;
    }
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    if(!Address(image,0x687610))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[64]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    std::uint32_t draw=0,update=0;
    if(!read(image+DisplayVtableRva+8,&draw,sizeof(draw))||draw!=image+0x25D770||
       !read(image+DisplayVtableRva+0x14,&update,sizeof(update))||update!=image+0x25D8A0)return false;
    const unsigned char drawHead[]={0x83,0xEC,0x3C,0x56,0x8B,0xF1};
    const unsigned char updateHead[]={0x83,0xEC,0x28,0x55,0x8B,0xE9,0x8D,0x8D,0x9C,0x05,0,0};
    const unsigned char circleLayout[]={0x8D,0x86,0x18,0x01,0,0,0x89,0x4E,0x60,0x89,0x46,0x6C};
    const unsigned char circleColour[]={0x89,0x9C,0x2E,0x3C,0x01,0,0};
    const unsigned char drawReturn[]={0xC2,8,0};
    const unsigned char layoutOrigin[]={0xD9,0x86,0x7C,0x05,0,0,0x8B,0x56,0x0C};
    const unsigned char spriteSubmit[]={0xFF,0xD2,0x83,0xC3,1};
    const unsigned char textSubmit[]={0xFF,0xD2,0x0F,0xB7,0x46,0x2A};
    const unsigned char source[]={0x8B,0x4D,0x10,0x8B,0xB9,0xA8,0x0A,0,0};
    const unsigned char count[]={0x8B,0x41,0x14,0xC3};
    const unsigned char member[]={0x8B,0x44,0x81,0x18,0xC2,4,0};
    const unsigned char nameId[]={0x8B,0x8E,0xDC,2,0,0,0x8B,0x86,0xD0,2,0,0};
    const unsigned char nameArgument[]={0x83,0xC0,0x3C,0x50};
    // ZCHAROBJ +244 accepts char*, tests its first byte and passes it to
    // native ZString assignment. The +3C argument is an inline name buffer,
    // not a pointer/handle which the reader could mistakenly print as text.
    const unsigned char setter[]={0x53,0x8B,0x5C,0x24,8,0x85,0xDB,0x56,0x8B,0xF1};
    const unsigned char textByte[]={0x80,0x3B,0};
    const unsigned char frontendGetter[]={0x56,0x8B,0x74,0x24,8,0x33,0xC0,0x83,0xFE,0xFF};
    const unsigned char displayField[]={0x8B,0x89,4,9,0,0};
    std::uint32_t textSetter=0;
    if(!read(image+0x558414+0x244,&textSetter,sizeof(textSetter))||textSetter!=image+0x162700)return false;
    return NativeTeamMarkerKL::VerifyImage(image,read)&&match(0x25D770,drawHead)&&match(0x25D88E,drawReturn)&&match(0x25D8A0,updateHead)&&
        match(0x25D531,circleLayout)&&match(0x25DADE,circleColour)&&
        match(0x25D813,layoutOrigin)&&match(0x25D877,spriteSubmit)&&match(0x161465,textSubmit)&&match(0x25D8EA,source)&&
        match(0x3531D0,count)&&match(0x2120CE,member)&&match(0x25D958,nameId)&&
        match(0x25D973,nameArgument)&&match(0x162700,setter)&&match(0x162723,textByte)&&
        match(0x2763B0,frontendGetter)&&match(0x212549,displayField);
}
template<class Reader> bool OwnedDisplay(std::uintptr_t image,std::uintptr_t display,Reader read,std::uintptr_t& player,bool allowInventory=false) {
    if(!Address(image,0x8883A5)||!Address(display,0x61C))return false;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t vt=0,owner=0,data=0,current=0,playerVt=0;
    unsigned char mode=0xff;
    if(!get(image+0x8883A4,mode)||(mode!=0&&!(allowInventory&&mode==3))||!get(display,vt)||vt!=image+DisplayVtableRva||
       !get(display+0x10,owner)||!get(image+0x68760C,data)||!get(std::uintptr_t(data)+0xA2C,current)||
       !owner||owner!=current||!get(owner,playerVt)||playerVt!=image+0x56582C)return false;
    player=owner;return true;
}
template<class Reader> bool CaptureRoster(std::uintptr_t image,std::uintptr_t display,std::uintptr_t player,
                                        std::uint32_t tick,Reader read,Roster& output,bool allowInventory=false) {
    if(!tick||!Address(image,0x8883A5)||!Address(display,0x61C)||!Address(player,0xAAC))return false;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    struct Observation {
        std::uint32_t control=0,selected=0,count=0,displayCount=0;
        std::uint32_t members[MaxMembers]{},status[MaxMembers]{},displayMembers[MaxMembers]{};
        Roster roster{};
    } first,second;
    const auto collect=[&](Observation& state) {
        std::uintptr_t owned=0;std::uint32_t controlVt=0,selection=0,text=0,textVt=0;
        if(!OwnedDisplay(image,display,read,owned,allowInventory)||owned!=player||!get(player+0xAA8,state.control)||
           !get(display+0x3C,text)||!get(text,textVt)||textVt!=image+0x558414||
           !Address(state.control,0x28)||!get(state.control,controlVt)||controlVt!=image+ControlVtableRva||
           !get(std::uintptr_t(state.control)+0x10,selection)||!get(std::uintptr_t(state.control)+0x14,state.count)||
           state.count>MaxMembers||!read(std::uintptr_t(state.control)+0x18,state.members,sizeof(state.members))||
           !get(display+0x5F8,state.displayCount)||state.displayCount>MaxMembers||
           !read(display+0x5FC,state.status,sizeof(state.status))||
           !read(display+0x60C,state.displayMembers,sizeof(state.displayMembers)))return false;
        const auto begin=std::uintptr_t(state.control)+0x18,end=begin+state.count*4;
        if(!(state.count==0&&selection==0)&&(selection<begin||selection>end||(selection-begin)%4))return false;
        if(selection<end)state.selected=state.members[(selection-begin)/4];
        state.roster.player=player;state.roster.tick=tick;state.roster.valid=true;
        // Native Update 65D9C0 visits the receiver array in reverse and only
        // displays enabled +261 entries represented by a +60C slot.
        for(unsigned reverse=state.count;reverse;--reverse) {
            const auto member=std::uintptr_t(state.members[reverse-1]);
            if(!Address(member,0x2E0))return false;
            for(unsigned i=0;i<reverse-1;++i)if(state.members[i]==member)return false;
            unsigned char enabled=0;if(!get(member+0x261,enabled)||enabled>1)return false;
            if(!enabled)continue;
            unsigned slot=MaxMembers;
            for(unsigned i=0;i<MaxMembers;++i)if(state.displayMembers[i]==member){if(slot!=MaxMembers)return false;slot=i;}
            if(slot==MaxMembers)return false;
            auto& next=state.roster.members[state.roster.count];std::uint32_t name=0;
            if(!get(member+0x2D0,name)||!get(member+0x2DC,next.nativeId)||
               !Address(name,0x3C+NameBytes)||!read(std::uintptr_t(name)+0x3C,next.name,sizeof(next.name))||
               !NameValid(next.name))return false;
            const auto length=std::strlen(next.name);
            std::memset(next.name+length,0,sizeof(next.name)-length);
            next.nativeStatus=state.status[slot];next.selected=member==state.selected;
            ++state.roster.count;
        }
        state.roster.allSelected=state.roster.count!=0&&selection==end;
        return state.roster.count==state.displayCount;
    };
    if(!collect(first)||!collect(second)||first.control!=second.control||first.selected!=second.selected||
       first.count!=second.count||first.displayCount!=second.displayCount||
       std::memcmp(first.members,second.members,sizeof(first.members))||
       std::memcmp(first.status,second.status,sizeof(first.status))||
       std::memcmp(first.displayMembers,second.displayMembers,sizeof(first.displayMembers)))return false;
    for(unsigned i=0;i<first.roster.count;++i) {
        const auto& a=first.roster.members[i];const auto& b=second.roster.members[i];
        if(a.nativeId!=b.nativeId||a.nativeStatus!=b.nativeStatus||a.selected!=b.selected||
           std::memcmp(a.name,b.name,sizeof(a.name)))return false;
    }
    output=first.roster;return true;
}
template<class Reader> bool CaptureCurrentRoster(std::uintptr_t image,std::uintptr_t expected,std::uint32_t tick,Reader read,Roster& output) {
    if(!Address(image,0x8883A5)||!Address(expected,0xAAC)||!tick)return false;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t data=0,player=0,manager=0,frontend=0,display=0,vt=0;
    std::int8_t playerIndex=-1;
    if(!get(image+0x68760C,data)||!Address(data,0x1550)||!get(std::uintptr_t(data)+0xA2C,player)||player!=expected||
       !get(std::uintptr_t(player)+0x804,playerIndex)||playerIndex<0||!get(std::uintptr_t(data)+0x154C,manager)||!Address(manager,24))return false;
    for(unsigned i=0;i<2;++i) {
        std::int8_t id=-1;if(!get(manager+i*12+8,id))return false;
        if(id==playerIndex) {
            if(frontend||!get(manager+i*12,frontend)||!frontend)return false;
        }
    }
    Roster next;
    if(!Address(frontend,0x908)||!get(frontend,vt)||vt!=image+0x5755FC||!get(frontend+0x904,display)||
       !CaptureRoster(image,display,expected,tick,read,next))return false;
    std::uint32_t afterManager=0,afterFrontend=0,afterDisplay=0,afterPlayer=0;
    std::int8_t afterIndex=-1;
    if(!get(data+0xA2C,afterPlayer)||afterPlayer!=player||!get(player+0x804,afterIndex)||afterIndex!=playerIndex||
       !get(data+0x154C,afterManager)||afterManager!=manager||!get(frontend+0x904,afterDisplay)||afterDisplay!=display)return false;
    unsigned matching=0;
    for(unsigned i=0;i<2;++i) {
        std::int8_t id=-1;if(!get(manager+i*12+8,id))return false;
        if(id==playerIndex) {
            if(++matching!=1||!get(manager+i*12,afterFrontend)||afterFrontend!=frontend)return false;
        }
    }
    if(matching!=1)return false;
    output=next;return true;
}
using SceneProvider=bool(*)();
using LogCallback=void(*)(const char*);
bool Install(SceneProvider scene,LogCallback log);
void DrawWrist(std::uintptr_t display,std::uintptr_t player,void* renderer,void* parameter);
NativeTeamMarkerKL::Snapshot SnapshotMarkers(std::uintptr_t player,std::uint32_t now);
struct Stats {bool installed=false;std::uint64_t calls=0,hidden=0,captured=0,rejected=0,markerCalls=0,markerSnapshots=0,markerObserved=0,markerRejected=0,markerFixedObserved=0;unsigned members=0;};
Stats SnapshotStats();
}
