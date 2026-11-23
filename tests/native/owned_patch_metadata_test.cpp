// Actual private MinHook metadata; touches only this fixture's own allocation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "owned_patch_audit.h"
#include "owned_patch_audit_win.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
int __cdecl Detour(){return 7;}
}
int main(){
    unsigned int count=99;KL1_MH_OWNED_PATCH record{},sentinel{};std::memset(&sentinel,0xa5,sizeof(sentinel));
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_NOT_INITIALIZED&&count==0,"uninitialized registry rejects");
    Check(KL1_MH_AuditEnabledPatches(nullptr,1,&count)==KL1_MH_AUDIT_INVALID_ARGUMENT,"null output with nonzero capacity rejects");
    Check(MH_Initialize()==MH_OK,"real private MinHook initializes");
    Check(KL1_MH_AuditEnabledPatches(nullptr,0,&count)==KL1_MH_AUDIT_OK&&count==0,"empty registry snapshot succeeds");
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(page!=nullptr,"fixture owns target allocation");if(!page)return 2;
    std::array<unsigned char,16> native{};native.fill(0x90);native[0]=0xb8;native[1]=1;native[5]=0xc3;
    std::memcpy(page,native.data(),native.size());void* original=nullptr;
    Check(MH_CreateHook(page,reinterpret_cast<void*>(&Detour),&original)==MH_OK,"real target hook created disabled");
    Check(KL1_MH_AuditEnabledPatches(nullptr,0,&count)==KL1_MH_AUDIT_OK&&count==0,"disabled entries excluded");
    Check(MH_EnableHook(page)==MH_OK,"private target enabled");
    record=sentinel;
    Check(KL1_MH_AuditEnabledPatches(&record,0,&count)==KL1_MH_AUDIT_CAPACITY&&count==1,"required capacity reported");
    Check(std::memcmp(&record,&sentinel,sizeof(record))==0,"insufficient capacity publishes no partial record");
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_OK&&count==1,"enabled snapshot succeeds");
    Check(record.target==reinterpret_cast<uintptr_t>(page)&&record.patchAddress==record.target&&record.patchLength==5&&!record.patchAbove,"exact target and normal five-byte range");
    Check(record.detour==reinterpret_cast<uintptr_t>(&Detour)&&std::memcmp(record.backup,native.data(),5)==0,"detour and true original bytes retained");
    Check(std::memcmp(record.installed,page,5)==0&&page[0]==0xe9,"installed encoding equals live source branch");
    const auto byte=page[1];page[1]^=1;record=sentinel;
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_LIVE_MISMATCH,"foreign live mutation rejects");
    Check(std::memcmp(&record,&sentinel,sizeof(record))==0,"live mismatch publishes no ownership claim");page[1]=byte;
    DWORD old=0;Check(VirtualProtect(page,4096,PAGE_NOACCESS,&old)!=0,"fixture makes own target unreadable");
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_UNREADABLE,"unreadable target rejects without AV");
    DWORD unused=0;VirtualProtect(page,4096,old,&unused);
    Check(MH_DisableHook(page)==MH_OK&&MH_RemoveHook(page)==MH_OK,"ordinary disable and remove unchanged");
    Check(std::memcmp(page,native.data(),5)==0,"MinHook cleanup restores original bytes");
    auto* tiny=page+64;std::memset(tiny-5,0xcc,5);tiny[0]=0xb0;tiny[1]=1;tiny[2]=0xc3;tiny[3]=0x10;tiny[4]=0x20;
    std::array<unsigned char,7> hotOriginal{};std::memcpy(hotOriginal.data(),tiny-5,7);
    Check(MH_CreateHook(tiny,reinterpret_cast<void*>(&Detour),&original)==MH_OK&&MH_EnableHook(tiny)==MH_OK,"real short function uses supported MinHook hotpatch fallback");
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_OK&&count==1&&record.patchAbove==1&&record.patchLength==7&&record.patchAddress==reinterpret_cast<uintptr_t>(tiny-5),"snapshot exposes exact seven-byte patchAbove range");
    Check(record.installed[5]==0xeb&&record.installed[6]==0xf9&&std::memcmp(record.backup,hotOriginal.data(),7)==0,"hotpatch records EB F9 and original prefix plus entry bytes");
    const auto shortByte=tiny[1];tiny[1]^=1;
    Check(KL1_MH_AuditEnabledPatches(&record,1,&count)==KL1_MH_AUDIT_LIVE_MISMATCH,"malformed live hotpatch short branch rejects");tiny[1]=shortByte;
    Check(MH_DisableHook(tiny)==MH_OK&&MH_RemoveHook(tiny)==MH_OK&&std::memcmp(tiny-5,hotOriginal.data(),7)==0,"ordinary hotpatch cleanup restores all seven source bytes");
    KL1OwnedPatchAudit::Bounds self{};
    Check(KL1OwnedPatchAudit::CurrentOwnerBounds(self)&&self.base&&self.size,"adapter binds to own actual loaded module");
    std::vector<KL1OwnedPatchAudit::Page> pages;unsigned int ledger=9;
    Check(KL1OwnedPatchAudit::AuditCurrentVerifiedGame(pages,ledger)==KL1OwnedPatchAudit::Result::InvalidImage&&pages.empty()&&ledger==0,"non-Steam fixture executable rejected by pinned canonical hash");
    Check(MH_Uninitialize()==MH_OK,"real MinHook uninitializes");VirtualFree(page,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
