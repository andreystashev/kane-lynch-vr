#pragma once
#include "native_player_pose.hpp"
#include <array>
#include <cstdint>
#include <cstring>
namespace NativeObjectiveGoalKL {
constexpr unsigned Type=26,Texture=0x27,Maximum=48,Stride=0x64;
constexpr std::uint32_t Colour=0xFF00CC66;
inline bool Objective(unsigned type){return type==Type;}
using Record=std::array<unsigned char,Stride>;
inline bool AttachedSource(const Record& source,std::uint32_t entity,unsigned playerIndex){
    std::uint32_t owner=0,type=0,filter=0;NativeCameraMath::Vec3 point;float scale=0,offset=0;
    std::memcpy(&owner,source.data(),4);std::memcpy(&type,source.data()+0x1C,4);std::memcpy(&filter,source.data()+0x44,4);
    std::memcpy(&point,source.data()+4,sizeof(point));std::memcpy(&scale,source.data()+0x3C,4);std::memcpy(&offset,source.data()+0x50,4);
    return owner==entity&&type==Type&&!source[0x4D]&&source[0x4F]<=1&&(filter==0xFFFFFFFFu||filter==playerIndex)&&
        NativeCameraMath::Finite(point)&&std::isfinite(scale)&&scale>0&&std::isfinite(offset);
}
struct Goal {std::uint32_t slot=0,entity=0,geometry=0,filter=0,index=0;Record source{};};
struct Available {bool valid=false;std::uint32_t data=0,player=0,manager=0,listed=0,count=0;std::array<Goal,Maximum> goals{};};
template<class Reader> Available Capture(std::uintptr_t image,std::uintptr_t player,int playerIndex,Reader read){
    Available out;if(!player||playerIndex<0||playerIndex>1)return out;
    const auto get=[&](std::uintptr_t at,auto& value){return at>=0x10000u&&at<=0xffffffffu-sizeof(value)&&read(at,&value,sizeof(value));};
    std::uint32_t current=0,vt=0;std::array<std::uint32_t,Maximum> pointers{},afterPointers{};
    if(!get(image+0x68760C,out.data)||!get(out.data+0xA2C,current)||current!=player||!get(player,vt)||vt!=image+0x56582C||
       !get(out.data+0x1554,out.manager)||out.manager<0x10000u||out.manager>0xffffffffu-0x1394||
       !get(out.manager+0x1384,out.listed)||out.listed>Maximum||
       (out.listed&&!read(out.manager+0x12C4,pointers.data(),out.listed*4)))return {};
    out.player=static_cast<std::uint32_t>(player);
    for(unsigned i=0;i<out.listed;++i){const auto slot=pointers[i];
        if(slot<out.manager+4||slot>=out.manager+4+Maximum*Stride||(slot-out.manager-4)%Stride)return {};
        for(unsigned j=0;j<i;++j)if(pointers[j]==slot)return {};
        Record record{};if(!read(slot,record.data(),record.size()))return {};
        std::uint32_t entity=0,type=0,filter=0;std::memcpy(&entity,record.data(),4);std::memcpy(&type,record.data()+0x58,4);std::memcpy(&filter,record.data()+0x18,4);
        if(type!=Type||!entity||!record[0x2C]||(filter!=0xffffffffu&&filter!=2&&filter!=static_cast<unsigned>(playerIndex)))continue;
        std::uint32_t geometry=0,owner=0,entityVt=0,method=0;
        NativePlayerPose::CameraPose pose;
        if(!get(entity,entityVt)||entityVt<image+0x530000||entityVt>=image+0x650000||
           !get(entityVt+0x30,method)||method<image+0x1000||method>=image+0x4C0000||
           !get(entity+4,geometry)||!get(geometry+0x60,owner)||owner!=entity||!NativePlayerPose::World(geometry,read,pose))return {};
        bool duplicate=false;for(unsigned j=0;j<out.count;++j)if(out.goals[j].entity==entity)duplicate=true;
        if(duplicate)continue;
        out.goals[out.count++]={slot,entity,geometry,filter==2?0xffffffffu:filter,(slot-out.manager-4)/Stride,record};
    }
    std::uint32_t afterData=0,afterPlayer=0,afterManager=0,afterCount=0;
    if(!get(image+0x68760C,afterData)||afterData!=out.data||!get(out.data+0xA2C,afterPlayer)||afterPlayer!=player||
       !get(out.data+0x1554,afterManager)||afterManager!=out.manager||!get(out.manager+0x1384,afterCount)||afterCount!=out.listed||
       (out.listed&&(!read(out.manager+0x12C4,afterPointers.data(),out.listed*4)||afterPointers!=pointers)))return {};
    for(unsigned i=0;i<out.count;++i){const auto& goal=out.goals[i];Record after{};std::uint32_t geometry=0,owner=0;
        if(!read(goal.slot,after.data(),after.size())||after!=goal.source||!get(goal.entity+4,geometry)||geometry!=goal.geometry||!get(geometry+0x60,owner)||owner!=goal.entity)return {};}
    out.valid=true;return out;
}
template<class Reader>bool Verify(std::uintptr_t image,Reader read){
    const auto match=[&](std::uintptr_t rva,const unsigned char* bytes,std::size_t n){std::array<unsigned char,72> actual{};return n<=actual.size()&&read(image+rva,actual.data(),n)&&!std::memcmp(actual.data(),bytes,n);};
    const unsigned char getter[]={0x8B,0x81,0x54,0x15,0,0,0xC3},count[]={0x8B,0x41,8,0xC3};
    const unsigned char index[]={0x8B,0x44,0x24,4,0x85,0xC0,0x7C,5,0x3B,0x41,8,0x7C,1,0xCC,0x6B,0xC0,0x64,0x03,0x41,0x14,0xC2,4,0};
    const unsigned char mapPointers[]={0x05,0xC4,0x12,0,0,0x52,0x8B,0xCE,0x89,0x46,0x68};
    const unsigned char goalCount[]={0x89,0x91,0x84,0x13,0,0};
    const unsigned char ctor[]={0x8B,0x44,0x24,4,0x33,0xD2,0x3B,0xC2,0x75,1,0xCC};
    const unsigned char update[]={0x83,0xEC,0x24,0x56,0x8B,0xF1,0x8B,0x0E,0x85,0xC9};
    const unsigned char resource[]="tex:UserInterface/Icons/UI_KL_Communications_01#Objective";
    std::uint32_t texture=0,id=0,path=0;
    return match(0x2704A0,getter,sizeof(getter))&&match(0x12F500,count,sizeof(count))&&match(0x254180,index,sizeof(index))&&
        match(0x26645D,mapPointers,sizeof(mapPointers))&&match(0x263440,goalCount,sizeof(goalCount))&&
        match(0x252F50,ctor,sizeof(ctor))&&match(0x253210,update,sizeof(update))&&
        read(image+0x647B08+Type*4,&texture,4)&&texture==Texture&&read(image+0x648004,&id,4)&&id==Texture&&
        read(image+0x648008,&path,4)&&path==image+0x574CCC&&match(0x574CCC,resource,sizeof(resource));
}
}
