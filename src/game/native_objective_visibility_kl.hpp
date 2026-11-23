#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "native_objective_goal_kl.hpp"
#include "native_team_marker_kl.hpp"
namespace NativeObjectiveVisibilityKL {
constexpr std::uintptr_t ColorRva=0x2537F0,MapRequestRva=0x274B20,ColorCallerRva=0x253C3E;
template<class Reader> bool VerifySelectionMarker(std::uintptr_t image,Reader read){
    constexpr unsigned char call[]={0x6A,0x02,0x52,0xE8,0xF6,0x0C,0x04,0x00};
    constexpr char resource[]="tex:UserInterface/Icons/UI_KL_Communications_01#Ok";
    unsigned char actual[sizeof(call)]{};char name[sizeof(resource)]{};
    std::uint32_t texture=0,path=0;
    return read(image+0x212252,actual,sizeof(actual))&&!std::memcmp(call,actual,sizeof(call))&&
        read(image+0x647B08+4*NativeTeamMarkerKL::SelectionType,&texture,4)&&texture==NativeTeamMarkerKL::SelectionTexture&&
        read(image+0x647E64,&path,4)&&path==image+0x575070&&
        read(path,name,sizeof(name))&&!std::memcmp(resource,name,sizeof(name));
}
struct Context {std::uint32_t tick=0;bool gameplay=false,held=false;std::uint32_t epoch=0;};
inline bool Fresh(Context value,std::uint32_t now){return value.gameplay&&value.tick&&std::int32_t(now-value.tick)>=0&&now-value.tick<=250;}
struct Local {std::uint32_t data=0,player=0;std::int8_t index=-1,input=-1;bool valid=false;};
template<class Reader> Local CaptureLocal(std::uintptr_t image,Reader read,bool inventory=false){
    const auto get=[&](std::uintptr_t at,auto& v){return at>=0x10000u&&at<=0xffffffffu-sizeof(v)&&read(at,&v,sizeof(v));};
    Local out;std::uint32_t vt=0;std::uint8_t mode[2]{1,1};
    if(!get(image+0x68760C,out.data)||!get(out.data+0xA2C,out.player)||!get(out.player,vt)||vt!=image+0x56582C||
       !get(out.player+0x804,out.index)||out.index<0||out.index>1||!get(out.player+0x805,out.input)||out.input<0||out.input>1||
       !read(image+0x8883A4,mode,2)||(mode[0]&&!(inventory&&mode[0]==3))||mode[1])return {};
    Local after;std::uint32_t afterVt=0;std::uint8_t afterMode[2]{1,1};
    if(!get(image+0x68760C,after.data)||after.data!=out.data||!get(out.data+0xA2C,after.player)||after.player!=out.player||
       !get(out.player,afterVt)||afterVt!=vt||!get(out.player+0x804,after.index)||after.index!=out.index||
       !get(out.player+0x805,after.input)||after.input!=out.input||!read(image+0x8883A4,afterMode,2)||afterMode[0]!=mode[0]||afterMode[1])return {};
    out.valid=true;return out;
}
// A return-value change only. Native distance/time fades and all script-owned
// fields continue to update; release exposes that original current result.
template<class Reader> bool GoalAlpha(std::uintptr_t image,std::uintptr_t record,std::uintptr_t camera,std::uintptr_t caller,Reader read,std::uint8_t& alpha){
    if(caller!=image+ColorCallerRva)return false;
    const auto local=CaptureLocal(image,read);if(!local.valid)return false;
    const auto get=[&](std::uintptr_t at,auto& v){return at>=0x10000u&&at<=0xffffffffu-sizeof(v)&&read(at,&v,sizeof(v));};
    std::uint32_t list=0,count=0,records=0,type=0,filter=0,ownedCamera=0;
    if(!get(local.player+0x9A0,ownedCamera)||ownedCamera<0x10000u||ownedCamera!=camera||!get(local.data+0x1550,list)||!get(list+8,count)||!count||count>256||!get(list+0x14,records)||
       records<0x10000u||records>0xffffffffu-std::size_t(count)*0x64||record<records||record>=records+std::size_t(count)*0x64||
       (record-records)%0x64||!get(record+0x1C,type)||!NativeObjectiveGoalKL::Objective(type)||!get(record+0x44,filter)||
       (filter!=0xffffffffu&&filter!=std::uint32_t(local.index)))return false;
    std::array<unsigned char,0x64> before{},after{};
    if(!read(record,before.data(),before.size())||!before[0x4B])return false;
    std::uint32_t observedType=0,observedFilter=0;std::memcpy(&observedType,before.data()+0x1C,4);std::memcpy(&observedFilter,before.data()+0x44,4);
    if(observedType!=type||observedFilter!=filter)return false;
    const auto checked=CaptureLocal(image,read);std::uint32_t afterList=0,afterCount=0,afterRecords=0,afterCamera=0;
    if(!checked.valid||checked.data!=local.data||checked.player!=local.player||checked.index!=local.index||checked.input!=local.input||
       !get(local.player+0x9A0,afterCamera)||afterCamera!=ownedCamera||!get(local.data+0x1550,afterList)||afterList!=list||!get(list+8,afterCount)||afterCount!=count||
       !get(list+0x14,afterRecords)||afterRecords!=records||!read(record,after.data(),after.size())||after!=before)return false;
    alpha=before[0x4B];return true;
}
template<class Reader> bool SuppressMap(std::uintptr_t image,std::uintptr_t manager,int slot,Reader read){
    if(manager!=image+0x888390||slot<0||slot>1)return false;
    const auto local=CaptureLocal(image,read);return local.valid&&local.input==slot;
}
template<class Reader> bool Verify(std::uintptr_t image,Reader read){
    const auto match=[&](std::uintptr_t rva,const unsigned char* bytes,std::size_t n){std::array<unsigned char,48> actual{};
        return n<=actual.size()&&read(image+rva,actual.data(),n)&&!std::memcmp(actual.data(),bytes,n);};
    const unsigned char color[]={0xD9,0xEE,0x83,0xEC,0x0C,0x56,0x8B,0xF1,0xD8,0x56,0x34,0xDF,0xE0,0xF6,0xC4,0x44,0x7A};
    const unsigned char map[]={0x0F,0xBE,0x44,0x24,0x08,0x8B,0x54,0x24,0x04,0x6A,0x03,0x6A,0x02,0x68,0xA8,0x6D,0x97,0,0x50,0x52,0xE8,0xE7,0xFA,0xFF,0xFF,0xC2,8,0};
    const unsigned char caller[]={0x50,0x8B,0xCF,0xE8,0xB2,0xFB,0xFF,0xFF};
    const unsigned char list[]={0x8B,0x81,0x50,0x15,0,0,0xC3},stride[]={0x6B,0xC0,0x64,0x03,0x41,0x14,0xC2,4,0};
    const unsigned char getter[]={0x8B,0x81,0xA0,9,0,0,0xC3};
    std::uint32_t cameraMethod=0;if(!read(image+0x56582C+0x668,&cameraMethod,4)||cameraMethod!=image+0x1E8950)return false;
    const unsigned char name[]={'I','n','g','a','m','e','M','a','p',0};
    return match(ColorRva,color,sizeof(color))&&match(MapRequestRva,map,sizeof(map))&&match(0x253C36,caller,sizeof(caller))&&
        match(0x1E8950,getter,sizeof(getter))&&match(0x270490,list,sizeof(list))&&match(0x25418E,stride,sizeof(stride))&&match(0x576DA8,name,sizeof(name))&&NativeObjectiveGoalKL::Verify(image,read);
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
void SetContext(bool gameplay,bool teamHudHeld);
struct Stats {bool installed=false,forceHeld=false;std::uint64_t colorCalls=0,forced=0,rejected=0,mapCalls=0,mapSuppressed=0,goalsObserved=0;
    unsigned available=0;std::uint64_t heldAdded=0,heldViews=0,viewRejected=0,filteredLive=0,copiedLive=0,recoveredStyle=0,selectionPrepared=0;};
Stats Snapshot();
}
