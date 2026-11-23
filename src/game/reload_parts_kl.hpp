#pragma once
#include "native_weapon_ownership.hpp"
#include "native_camera_math.hpp"
// Read-only discovery on an already qualified owned rigid gun draw. Resource
// paths match the KL1 rigid binder/known actor skeleton path, not KL2 offsets.
namespace ReloadPartsKL {
struct Part {std::uintptr_t resource=0,model=0;std::uint32_t bone=0,count=0;char name[37]{};NativeCameraMath::Vec3 centre{},half{};NativeCameraMath::CameraPose bind{};bool boundsValid=false,bindValid=false;};
struct Catalog {std::uintptr_t model=0,header=0,definitions=0,bind=0;unsigned count=0;};
template<class Reader>bool ModelCatalog(std::uintptr_t image,std::uintptr_t model,Reader read,Catalog& out){
 const auto get=[&](std::uintptr_t at,auto& value){return NativeWeaponOwnership::Read(at,value,read);};
 std::uint16_t type=0;std::uint8_t flags=0;std::uint32_t a=0,b=0,c=0,d=0;std::uintptr_t parts=0;Catalog next;next.model=model;
 if(!get(model+2,type)||type!=7||!get(model+0xC,flags)||!(flags&1)||!get(model+0x10,a)||!NativeWeaponOwnership::Resource(image,a,read,parts)||!get(parts+4,b)||!NativeWeaponOwnership::Resource(image,b,read,next.header)||!get(next.header,next.count)||!next.count||next.count>256||!get(next.header+4,c)||!NativeWeaponOwnership::Resource(image,c,read,next.definitions)||!get(next.header+8,d)||!NativeWeaponOwnership::Resource(image,d,read,next.bind))return false;
 out=next;return true;
}
template<class Reader>bool Bone(const Catalog& catalog,unsigned index,Reader read,Part& out){
 if(index>=catalog.count||!catalog.model)return false;Part next;next.model=catalog.model;next.bone=index;next.count=catalog.count;const auto at=catalog.definitions+index*64;
 const auto get=[&](std::uintptr_t address,auto& value){return NativeWeaponOwnership::Read(address,value,read);};
 if(!read(at+0x1C,next.name,36)||!get(at,next.centre)||!get(at+0x10,next.half)||!get(catalog.bind+index*sizeof(next.bind),next.bind))return false;
 next.boundsValid=NativeCameraMath::Finite(next.centre)&&NativeCameraMath::Finite(next.half)&&next.half.x>=0&&next.half.y>=0&&next.half.z>=0&&next.half.x<=1000&&next.half.y<=1000&&next.half.z<=1000;
 next.bindValid=NativeCameraMath::ValidatePose(next.bind,.05f);out=next;return true;
}
template<class Reader> bool Inspect(std::uintptr_t image,std::uintptr_t entry,std::uintptr_t extra,std::uintptr_t geometry,Reader read,Part& out){
 const auto get=[&](std::uintptr_t at,auto& value){return NativeWeaponOwnership::Read(at,value,read);};
 std::uint32_t vt=0,g=0,primitive=0,handle=0,modelHandle=0,partsHandle=0,headerHandle=0,definitionsHandle=0;
 std::uintptr_t resource=0,model=0,parts=0,header=0,definitions=0;std::uint16_t type=0;std::uint8_t flags=0;
 Part next;
 if(!extra||!get(entry,vt)||vt!=image+0x541070||!get(entry+0xF4,g)||g!=geometry||!get(extra+4,primitive)||!get(primitive+0x14,handle)||!NativeWeaponOwnership::Resource(image,handle,read,resource)||!get(resource+0x3C,next.bone)||!get(geometry+0x68,modelHandle)||!NativeWeaponOwnership::Resource(image,modelHandle,read,model)||!get(model+2,type)||type!=7||!get(model+0xC,flags)||!(flags&1)||!get(model+0x10,partsHandle)||!NativeWeaponOwnership::Resource(image,partsHandle,read,parts)||!get(parts+4,headerHandle)||!NativeWeaponOwnership::Resource(image,headerHandle,read,header)||!get(header,next.count)||!next.count||next.count>256||next.bone>=next.count||!get(header+4,definitionsHandle)||!NativeWeaponOwnership::Resource(image,definitionsHandle,read,definitions)||!read(definitions+next.bone*64+0x1C,next.name,36))return false;
 std::uint32_t after=0;if(!get(entry+0xF4,after)||after!=geometry||!get(extra+4,after)||after!=primitive||!get(primitive+0x14,after)||after!=handle)return false;
 const auto definition=definitions+next.bone*64;
 next.boundsValid=get(definition,next.centre)&&get(definition+0x10,next.half)&&NativeCameraMath::Finite(next.centre)&&NativeCameraMath::Finite(next.half)&&next.half.x>=0&&next.half.y>=0&&next.half.z>=0&&next.half.x<=1000&&next.half.y<=1000&&next.half.z<=1000;
 std::uint32_t bindHandle=0;std::uintptr_t bind=0;
 next.bindValid=get(header+8,bindHandle)&&NativeWeaponOwnership::Resource(image,bindHandle,read,bind)&&get(bind+next.bone*sizeof(next.bind),next.bind)&&NativeCameraMath::ValidatePose(next.bind,.05f);
 next.resource=resource;next.model=model;out=next;return true;
}
}
