#pragma once
#include "native_weapon_ownership.hpp"
// Exact pointer-free x86 spans. Read all identity fields, preserve the caller's
// ownership/count comparisons and late revalidation; never cache native memory.
namespace NativeSkinIdentityReads {
struct Bones {std::uint32_t geometry=0,pad0[4]{},cache=0,pad1[2]{},count=0;};
struct Player {std::uint32_t vtable=0,pad[27]{},model=0;};
static_assert(sizeof(Bones)==0x24&&offsetof(Bones,cache)==0x14&&offsetof(Bones,count)==0x20,"native entry F4..117");
static_assert(sizeof(Player)==0x74&&offsetof(Player,model)==0x70,"native player vtable/model span");
template<class Reader>bool ReadBones(std::uintptr_t entry,Reader read,Bones& output){
 if(!NativeWeaponOwnership::Address(entry,0x118))return false;Bones next;
 if(!read(entry+0xF4,&next,sizeof(next)))return false;output=next;return true;
}
template<class Reader>bool ReadPlayer(std::uintptr_t player,Reader read,Player& output){
 if(!NativeWeaponOwnership::Address(player,sizeof(Player)))return false;Player next;
 if(!read(player,&next,sizeof(next)))return false;output=next;return true;
}
inline bool Same(const Bones& a,const Bones& b){return a.geometry==b.geometry&&a.cache==b.cache&&a.count==b.count;}
}
