#pragma once
#include "native_reload_kl.hpp"
#include "native_weapon_type_kl.hpp"
#include "physical_reload_runtime_kl.hpp"
// KL1 layout, independently qualified. KL2 offsets are deliberately absent.
// Capacity is a native virtual getter; item+DC stores reserve, not capacity.
namespace NativeReloadAmmoKL {
namespace M=PhysicalReloadRuntimeKL;
struct Snapshot {
 std::uintptr_t player=0,item=0,model=0,countSetter=0,reserveSetter=0;
 std::uint32_t reference=0;unsigned category=0;M::Ammo ammo{};
 std::uint64_t Token()const{return (std::uint64_t(reference)<<32)|std::uint32_t(item);}
};
template<class Reader> bool Verify(std::uintptr_t image,Reader read){
 const auto bytes=[&](std::uintptr_t rva,const auto& expected){unsigned char actual[sizeof(expected)]{};return read(image+rva,actual,sizeof(actual))&&!std::memcmp(actual,expected,sizeof(actual));};
 const unsigned char count[]={0x8B,0x44,0x24,4,0x35,0xEB,0xBE,0xEB,0xBE,0x89,0x81,0xD8,0,0,0,0xC2,4,0};
 const unsigned char reserve[]={0x8B,0x44,0x24,4,0x35,0xEB,0xBE,0xEB,0xBE,0x89,0x81,0xDC,0,0,0,0xC2,4,0};
 const unsigned char reserveRead[]={0x8B,0x86,0xDC,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0x5E,0xC2,4,0};
 // Capacity dispatch: template+2DC special capacity, otherwise owner+2A0.
 const unsigned char capacity[]={0x56,0x8B,0xF1,0x83,0x7E,0x50,0,0x75,5,0x83,0xC8,0xFF,0x5E,0xC3,0x8B,6,0x8B,0x90,0xF8,2,0,0,0xFF,0xD2};
 if(!NativeReloadKL::Verify(image,read)||!NativeWeaponTypeKL::Verify(image,read)||!bytes(0x11DE30,count)||!bytes(0x11DE50,reserve)||!bytes(0x121726,reserveRead)||!bytes(0x11DDD0,capacity))return false;
 for(auto vt:{0x54EA4Cu,0x577B74u}){std::uint32_t c=0,r=0,g=0;
  if(!read(image+vt+0x30C,&c,4)||c!=image+(vt==0x54EA4C?0x11DE30:0x278A20)||!read(image+vt+0x314,&r,4)||r!=image+(vt==0x54EA4C?0x11DE50:0x278A40)||!read(image+vt+0x310,&g,4)||g!=image+0x11DDD0)return false;
 }
 const unsigned char ffCount[]={0x8B,0x44,0x24,4,0x85,0xC0,0x7D,0x0D,0xC7,0x44,0x24,4,0,0,0,0,0xE9,0xFB,0x53,0xEA,0xFF,0x89,0x44,0x24,4,0xE9,0xF2,0x53,0xEA,0xFF};
 const unsigned char ffReserve[]={0x8B,0x44,0x24,4,0x85,0xC0,0x7D,0x0D,0xC7,0x44,0x24,4,0,0,0,0,0xE9,0xFB,0x53,0xEA,0xFF,0x89,0x44,0x24,4,0xE9,0xF2,0x53,0xEA,0xFF};
 return bytes(0x278A20,ffCount)&&bytes(0x278A40,ffReserve);
}
inline bool SameOwner(const Snapshot& a,const Snapshot& b){return a.player==b.player&&a.item==b.item&&a.reference==b.reference&&a.model==b.model&&a.category==b.category&&a.countSetter==b.countSetter&&a.reserveSetter==b.reserveSetter;}
inline bool BlockedFireDebit(const Snapshot& before,const Snapshot& after,unsigned attempts){
 const int debit=before.ammo.count-after.ammo.count;
 return before.item&&SameOwner(before,after)&&before.ammo.capacity==after.ammo.capacity&&
    before.ammo.reserve==after.ammo.reserve&&debit>0&&unsigned(debit)<=attempts;
}
template<class Reader,class Capacity> bool Capture(std::uintptr_t image,std::uintptr_t player,std::uintptr_t item,Reader read,Capacity capacity,Snapshot& out){
 NativeReloadKL::Observation held{};if(!NativeReloadKL::Capture(image,player,item,read,held)||held.side!=1)return false;
 const auto get=[&](std::uintptr_t at,auto& value){return NativeWeaponOwnership::Read(at,value,read);};
 Snapshot next;next.player=player;next.item=item;std::uint32_t model=0,vt=0,templateVt=0,raw=0,setCount=0,setReserve=0,getCapacity=0;NativeWeaponOwnership::HandState left{};
 if(!NativeWeaponOwnership::ReadHandState(player+0x2D8,read,left)||left.item||!get(item+0x58,next.reference)||!next.reference||next.reference==UINT32_MAX||!get(item+0xC0,model)||!get(model,templateVt)||(templateVt!=image+0x54E6D4&&templateVt!=image+0x577864)||!get(model+0xA4,next.category)||!get(item,vt)||!get(vt+0x30C,setCount)||!get(vt+0x314,setReserve)||!get(vt+0x310,getCapacity)||getCapacity!=image+0x11DDD0||!get(item+0xDC,raw))return false;
 if(setCount!=image+(vt==image+0x54EA4C?0x11DE30:0x278A20)||setReserve!=image+(vt==image+0x54EA4C?0x11DE50:0x278A40))return false;
 next.model=model;next.countSetter=setCount;next.reserveSetter=setReserve;next.ammo.count=int(held.magazine);next.ammo.reserve=int(raw^0xBEEBBEEBu);next.ammo.capacity=capacity(item,getCapacity);next.ammo.ready=next.ammo.count>0;
 if(!M::Valid(next.ammo))return false;
 NativeReloadKL::Observation after{};std::uint32_t ref=0,afterModel=0,afterRaw=0;
 if(!NativeReloadKL::Capture(image,player,item,read,after)||after.side!=1||after.magazine!=held.magazine||!get(item+0x58,ref)||ref!=next.reference||!get(item+0xC0,afterModel)||afterModel!=model||!get(item+0xDC,afterRaw)||afterRaw!=raw||!NativeWeaponOwnership::ReadHandState(player+0x2D8,read,left)||left.item)return false;
 out=next;return true;
}
// Caller must own the simulation callback and must have passed Verify. No
// preparation/fire latch or actor action is written as an ammo/chamber field.
template<class Reader,class Capacity,class Setter> bool Commit(std::uintptr_t image,const Snapshot& source,const M::Ammo& target,Reader read,Capacity capacity,Setter set){
 if(!M::Valid(target)||target.capacity!=source.ammo.capacity)return false;
 Snapshot current;if(!Capture(image,source.player,source.item,read,capacity,current)||!SameOwner(source,current)||current.ammo.count!=source.ammo.count||current.ammo.reserve!=source.ammo.reserve)return false;
 if(target.count==source.ammo.count&&target.reserve==source.ammo.reserve)return true;
 set(source.countSetter,source.item,target.count);set(source.reserveSetter,source.item,target.reserve);
 Snapshot after;if(Capture(image,source.player,source.item,read,capacity,after)&&SameOwner(source,after)){
  if(after.ammo.count==target.count&&after.ammo.reserve==target.reserve)return true;
  // Roll back only our exact source, never a newly selected/reused item.
  if((after.ammo.count==target.count||after.ammo.count==source.ammo.count)&&(after.ammo.reserve==target.reserve||after.ammo.reserve==source.ammo.reserve)){set(source.countSetter,source.item,source.ammo.count);set(source.reserveSetter,source.item,source.ammo.reserve);}
 }
 return false;
}
}
