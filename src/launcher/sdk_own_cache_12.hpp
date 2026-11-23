// Verified native Steam compatibility: certify our exact owned game patches,
// then update only their original SDK page-cache entries. No SDK code patches.
#pragma once
#include "owned_patch_audit_win.hpp"
#include "profile.hpp"
#include "diagnostic_session.hpp"
#include "sdk_own_cache_codeproof.hpp"
#include "sdk_own_cache_11_codeproof.hpp"
#include <cstdio>
#include <atomic>
#include <vector>
#include <string>
namespace SdkOwnCache12 {
enum class Phase:unsigned {NotStarted,Preparing,LegacyNoOp,UnsupportedGame,UnsupportedSdk,Rejected,NoChanges,Applied};
struct Status {Phase phase=Phase::NotStarted;unsigned certifiedPages=0,enabledHooks=0;const char* reason="not_started";};
inline std::atomic<unsigned> statusPhase{unsigned(Phase::NotStarted)},statusPages{0},statusHooks{0};
inline std::atomic<const char*> statusReason{"not_started"};
inline Status Snapshot(){Status s;s.phase=Phase(statusPhase.load(std::memory_order_acquire));s.certifiedPages=statusPages.load();s.enabledHooks=statusHooks.load();s.reason=statusReason.load();return s;}
inline bool Ready(){const auto p=Snapshot().phase;return p==Phase::LegacyNoOp||p==Phase::NoChanges||p==Phase::Applied;}
inline void StatusResult(Phase phase,const char* reason){statusReason.store(reason);statusPhase.store(unsigned(phase),std::memory_order_release);}
using Address=std::uintptr_t;
template<class T> inline bool Read(Address a,T& v){SIZE_T n=0;return a&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),&v,sizeof(v),&n)&&n==sizeof(v);}
inline bool Bytes(Address a,void* p,std::size_t size){SIZE_T n=0;return a&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),p,size,&n)&&n==size;}
inline bool U(Address a,DWORD& v){return Read(a,v);}
inline DWORD Rol(DWORD x,unsigned n){n&=31;return (x<<n)|(x>>((32-n)&31));}
inline DWORD Checksum(const std::vector<unsigned char>& data){
 if(data.empty()||data.size()%16)return 0;std::size_t end=data.size()/4;DWORD value=0xf6537a58;
 const auto word=[&](std::size_t i){DWORD v=0;std::memcpy(&v,data.data()+i*4,4);return v;};
 for(;;){value=Rol(value,value&255);if(!end)break;const auto v=word(end-1);end-=4;value=Rol(value+v,7)+v;value=Rol(value+word(end+2),23);value=Rol(value^word(end+1),23);value=Rol(value^word(end),13);}
 return Rol(value*0x9e3779b1,29);
}
struct Log {
 FILE* file=nullptr;
 explicit Log(const wchar_t* package=nullptr){if(package&&DiagnosticsSession::Enabled(package)){const auto d=DiagnosticsSession::CurrentDirectory(package);if(!d.empty())file=_wfopen((d+L"\\sdk-own-cache.jsonl").c_str(),L"wbx");}}
 ~Log(){if(file)std::fclose(file);}
 void Event(const char* phase,bool okay,unsigned mode,const char* reason="",DWORD a=0,DWORD b=0){
  if(!std::strcmp(phase,"begin"))StatusResult(Phase::Preparing,"certifying");
  else if(!std::strcmp(phase,"prepared")){statusPages.store(a);statusHooks.store(b);}
  else if(!std::strcmp(phase,"reject")||!std::strcmp(phase,"rollback"))StatusResult(Phase::Rejected,reason);
  else if(!std::strcmp(phase,"complete"))StatusResult(okay?Phase::Applied:Phase::Rejected,reason);
  if(!file)return;std::fprintf(file,"{\"phase\":\"%s\",\"okay\":%s,\"mode\":%u,\"reason\":\"%s\",\"pid\":%lu,\"creation\":%llu,\"a\":%lu,\"b\":%lu}\n",phase,okay?"true":"false",mode,reason,GetCurrentProcessId(),static_cast<unsigned long long>(DiagnosticsSession::Creation(GetCurrentProcess())),a,b);std::fflush(file);
 }
};
struct SdkLayout {DWORD timestamp,imageSize,table,key,begin,set,end;const char* hash;const SdkCodeProof* proofs;std::size_t proofCount;};
inline constexpr SdkLayout sdk12{0x474ba865,0x9ad000,0x4acb18,0x4f10d4,0xdb736,0xdb86a,0xdb776,
 "447e2106fce0a529a26555db6aada35df28c7f1a7128385bc02211643abd8024",sdkCodeProofs,sizeof(sdkCodeProofs)/sizeof(*sdkCodeProofs)};
inline constexpr SdkLayout sdk11{0x46b9227a,0x833000,0x3a1638,0x3f1b10,0xa632c,0xa6460,0xa636c,
 "218bbd03069ad20a0428584ff8d31dbfcce02c015141d1911be6944c6d5d840f",sdk11CodeProofs,sizeof(sdk11CodeProofs)/sizeof(*sdk11CodeProofs)};
inline const SdkLayout* Layout(const char* hash){if(!hash)return nullptr;for(const auto* layout:{&sdk12,&sdk11})if(!std::strcmp(hash,layout->hash))return layout;return nullptr;}
inline bool SdkIdentity(Address& base,const SdkLayout** selected=nullptr){
 HMODULE module=GetModuleHandleW(L"xlive.dll");wchar_t path[MAX_PATH]{},windows[MAX_PATH]{};
 if(!module)return false;const DWORD n=GetModuleFileNameW(module,path,MAX_PATH),w=GetWindowsDirectoryW(windows,MAX_PATH);
 if(!n||n>=MAX_PATH||!w||w>=MAX_PATH)return false;
 const auto sys=std::wstring(windows)+L"\\System32\\xlive.dll",wow=std::wstring(windows)+L"\\SysWOW64\\xlive.dll";
 char digest[65]{};if(!HashFile(path,digest))return false;const auto* layout=Layout(digest);if(!layout)return false;
 if(_wcsicmp(path,sys.c_str())&&_wcsicmp(path,wow.c_str())){
  // A local original SDK belongs to the selected installation, never another
  // arbitrary search directory. Nothing is copied to/from this path.
  wchar_t game[MAX_PATH]{};const DWORD count=GetModuleFileNameW(nullptr,game,MAX_PATH);
  if(!count||count>=MAX_PATH)return false;const auto parent=[](const wchar_t* p){const std::wstring s=p;const auto at=s.find_last_of(L"\\/");return at==std::wstring::npos?std::wstring{}:s.substr(0,at);};
  if(_wcsicmp(parent(game).c_str(),parent(path).c_str()))return false;
 }
 base=reinterpret_cast<Address>(module);IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
 if(!Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!Read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.TimeDateStamp!=layout->timestamp||nt.OptionalHeader.SizeOfImage!=layout->imageSize)return false;
 for(std::size_t i=0;i<layout->proofCount;++i){const auto& proof=layout->proofs[i];std::vector<unsigned char> actual(proof.length);if(!Bytes(base+proof.rva,actual.data(),actual.size())||std::memcmp(actual.data(),proof.bytes,proof.length))return false;}
 if(selected)*selected=layout;
 return true;
}
inline bool ExistingOfflineSdk(){
 // This is the same already-installed no-cache runtime used by the accepted
 // ReBuild installation. Recognition is read-only; the mod ships no SDK and
 // installs/replaces no game library. Unknown SDKs still fail Preflight.
 HMODULE module=GetModuleHandleW(L"xlive.dll");wchar_t path[MAX_PATH]{};
 const DWORD n=module?GetModuleFileNameW(module,path,MAX_PATH):0;char digest[65]{};
 return n&&n<MAX_PATH&&HashFile(path,digest)&&!std::strcmp(digest,"75a4869178d892825fdd76f4e8b20bfdbcd985f673ba116513045cb84525d344");
}
inline bool ProtectedByte(Address sdk,DWORD handle,DWORD index,unsigned char& result,const SdkLayout& layout=sdk12){
 DWORD keyAddress=0,key=0;if(!U(sdk+layout.key,keyAddress)||!U(keyAddress,key))return false;
 Address s=DWORD(handle-key);DWORD a0=0,a1=0;if(!U(s,a0)||!U(s+4,a1))return false;
 if(!a1)return Read(Address(a0)+index,result);
 s+=(index>>14)*12;index&=0x3fff;WORD a2=0;
 if(!U(s,a0)||!U(s+4,a1)||!Read(s+8,a2))return false;
 const DWORD length=WORD(a2-WORD(a1));if(!length)return false;
 const DWORD step=length/2+(length<16?0:(a1>>16)&7),at=(WORD(step)*index+(a1>>16))%length;
 const DWORD v=WORD(index+0x21),mask=((v*WORD(a1))>>8)+v*(a1>>16);unsigned char encoded=0;
 if(!Read(Address(DWORD(a0-DWORD(a1+key)))+at,encoded))return false;result=encoded^static_cast<unsigned char>(mask);return true;
}
struct Owner {DWORD table=0,owner=0,array=0,count=0,module=0,cache=0,ranges=0,indices=0,sums=0,pageCount=0,rangeCount=0;};
inline bool Resolve(Address sdk,Owner& out,const SdkLayout& layout=sdk12){
 Owner v;DWORD handle=0,offset=0,length=0,initialized=0;
 if(!U(sdk+layout.table,v.table)||!U(v.table,initialized)||initialized!=1||!U(v.table+0x68,handle)||!U(v.table+0xc,offset)||!U(v.table+0x3c,length)||length!=4||offset>4096)return false;
 unsigned char bytes[4]{};for(unsigned i=0;i<4;++i)if(!ProtectedByte(sdk,handle,offset+4+i,bytes[i],layout))return false;std::memcpy(&v.owner,bytes,4);
 DWORD valid=0;if(!U(v.owner,v.array)||!U(v.owner+8,v.count)||!v.count||v.count>64||!U(v.owner+0x10,valid)||!valid)return false;
 unsigned games=0;
 for(DWORD n=0;n<v.count;++n){DWORD kind=0,m=0,image=0;const Address row=Address(v.array)+n*0x218;
  if(!U(row,kind)||!U(row+4,m)||!U(Address(m)+0x48,image))return false;
  if(image==0x400000){if(kind||++games!=1)return false;v.module=m;}
 }
 if(games!=1)return false;v.cache=v.module+0x174;DWORD phase=0;
 if(!U(v.cache,v.ranges)||!U(v.cache+4,v.indices)||!U(v.cache+8,v.sums)||!U(v.cache+0x14,v.rangeCount)||!U(v.cache+0x18,v.pageCount)||!U(v.cache+0x20,phase)||phase||!v.rangeCount||v.rangeCount>128||!v.pageCount||v.pageCount>65536)return false;
 out=v;return true;
}
struct Change {DWORD rva=0,index=0,before=0,after=0;};
inline bool Prepare(const Owner& owner,const std::vector<KL1OwnedPatchAudit::Page>& pages,std::vector<Change>& changes){
 changes.clear();
 for(const auto& page:pages){
  // Partial .text coverage is not a certificate for a full SDK cache page.
  if(page.comparedOffset||page.comparedLength!=4096||page.canonical.size()!=4096||page.live.size()!=4096||!page.ownedRecordCount||!page.changedBytes)return false;
  bool found=false;DWORD index=0;
  for(DWORD n=0;n<owner.rangeCount;++n){DWORD start=0,length=0,first=0;
   if(!U(Address(owner.ranges)+n*8,start)||!U(Address(owner.ranges)+n*8+4,length)||!U(Address(owner.indices)+n*4,first)||start>0x94c000||length>0x94c000-start||first>=owner.pageCount)return false;
   if(page.pageRva>=start&&std::uint64_t(page.pageRva)+4096<=std::uint64_t(start)+length){if(found||(start&4095))return false;found=true;index=first+((page.pageRva-start)>>12);}
  }
  if(!found||index>=owner.pageCount)return false;DWORD before=0;const DWORD expected=Checksum(page.canonical),after=Checksum(page.live);
  if(!U(Address(owner.sums)+index*4,before)||before!=expected||before==after)return false;
  for(const auto& prior:changes)if(prior.index==index)return false;
  changes.push_back({page.pageRva,index,before,after});
 }
 return !changes.empty();
}
inline bool Same(const Owner& a,const Owner& b){return !std::memcmp(&a,&b,sizeof(a));}
// Read-only eligibility check before installing any native game hooks. A
// different SDK is unsupported rather than being modified speculatively.
inline bool Preflight(){
 wchar_t gamePath[MAX_PATH]{};char gameHash[65]{};
 const DWORD length=GetModuleFileNameW(nullptr,gamePath,MAX_PATH);
 if(!length||length>=MAX_PATH||!HashSupportedGameFile(gamePath,gameHash)){StatusResult(Phase::UnsupportedGame,"game_identity_unavailable");return false;}
 if(ExistingOfflineSdk()){StatusResult(Phase::LegacyNoOp,"existing_offline_sdk_no_cache_change");return true;}
 Address sdk=0;Owner owner;const SdkLayout* layout=nullptr;
 if(!SdkIdentity(sdk,&layout)){StatusResult(Phase::UnsupportedSdk,"unsupported_sdk_identity_or_code");return false;}
 if(!Resolve(sdk,owner,*layout)){StatusResult(Phase::Rejected,"sdk_owner_chain_unavailable_before_hooks");return false;}
 StatusResult(Phase::Preparing,"native_preflight_verified");return true;
}
using Begin=HRESULT(__thiscall*)(void*);using Set=HRESULT(__thiscall*)(void*,DWORD,DWORD);
inline bool Transaction(const Owner& owner,const std::vector<Change>& changes,const std::vector<DWORD>& baseline,Begin begin,Set set,Begin end,Log& log,unsigned mode){
 void* cache=reinterpret_cast<void*>(Address(owner.cache));
 std::vector<DWORD> actual(owner.pageCount),expected=baseline;for(const auto& c:changes)expected[c.index]=c.after;
 if(FAILED(begin(cache))){log.Event("reject",false,mode,"sdk_cache_begin");return false;}
 std::size_t applied=0;bool okay=true;
 for(const auto& c:changes){DWORD observed=0;if(FAILED(set(cache,c.index,c.after))||!U(Address(owner.sums)+c.index*4,observed)||observed!=c.after){okay=false;break;}++applied;}
 // Verify the entire expected table, including all entries not certified above.
 if(!Bytes(owner.sums,actual.data(),actual.size()*4)||actual!=expected)okay=false;
 bool rolledBack=true;
 if(!okay){for(const auto& c:changes)if(FAILED(set(cache,c.index,c.before)))rolledBack=false;
  if(!Bytes(owner.sums,actual.data(),actual.size()*4)||actual!=baseline)rolledBack=false;
  log.Event("rollback",rolledBack,mode,"transaction_failed",static_cast<DWORD>(applied));}
 bool ended=SUCCEEDED(end(cache));DWORD phase=1;bool restored=U(owner.cache+0x20,phase)&&phase==0;
 if(okay&&(!ended||!restored)){
  const bool mayRestore=Read(owner.cache+0x20,phase)&&(phase==1||(phase==0&&SUCCEEDED(begin(cache))));rolledBack=mayRestore;
  if(mayRestore){for(const auto& c:changes)if(FAILED(set(cache,c.index,c.before)))rolledBack=false;
   if(!Bytes(owner.sums,actual.data(),actual.size()*4)||actual!=baseline)rolledBack=false;
   ended=SUCCEEDED(end(cache));restored=U(owner.cache+0x20,phase)&&phase==0;}
  okay=false;log.Event("rollback",rolledBack&&ended&&restored,mode,"sdk_cache_end_failed");
 }
 log.Event("complete",okay&&ended&&restored,mode,okay?"only_certified_game_cache_entries":"transaction_rejected",static_cast<DWORD>(applied),ended&&restored?1:0);
 return okay&&ended&&restored;
}
inline void Run(const wchar_t* package){
 static volatile LONG once=0;if(InterlockedCompareExchange(&once,1,0))return;
 const unsigned mode=1;Log log(package);log.Event("begin",true,mode);
 wchar_t gamePath[MAX_PATH]{};char gameHash[65]{};const DWORD pathLength=GetModuleFileNameW(nullptr,gamePath,MAX_PATH);
 if(!pathLength||pathLength>=MAX_PATH||!HashSupportedGameFile(gamePath,gameHash)){StatusResult(Phase::UnsupportedGame,"game_identity_unavailable");return;}
 if(ExistingOfflineSdk()){StatusResult(Phase::LegacyNoOp,"existing_offline_sdk_no_cache_change");return;}
 Address sdk=0;Owner initial;std::vector<KL1OwnedPatchAudit::Page> pages;unsigned ledgerCount=0;const SdkLayout* layout=nullptr;
 if(!SdkIdentity(sdk,&layout)){log.Event("reject",false,mode,"unsupported_sdk_identity_or_code");StatusResult(Phase::UnsupportedSdk,"unsupported_sdk_identity_or_code");return;}
 if(KL1OwnedPatchAudit::AuditCurrentVerifiedGame(pages,ledgerCount)!=KL1OwnedPatchAudit::Result::Okay){log.Event("reject",false,mode,"all_text_ownership");return;}
 if(pages.empty()){StatusResult(Phase::NoChanges,"no_native_game_patches");return;}
 if(!Resolve(sdk,initial,*layout)){log.Event("reject",false,mode,"sdk_owner_chain");return;}
 auto* cs=reinterpret_cast<CRITICAL_SECTION*>(Address(initial.owner)+0x24);const DWORD deadline=GetTickCount()+500;bool locked=false;
 do{if(TryEnterCriticalSection(cs)){locked=true;break;}Sleep(1);}while(static_cast<LONG>(GetTickCount()-deadline)<0);
 if(!locked){log.Event("reject",false,mode,"owner_busy");return;}
 struct Unlock {CRITICAL_SECTION* value;~Unlock(){LeaveCriticalSection(value);}} unlock{cs};
 Owner owner;std::vector<KL1OwnedPatchAudit::Page> finalPages;unsigned finalCount=0;std::vector<Change> changes;
 const SdkLayout* finalLayout=nullptr;
 if(!Resolve(sdk,owner,*layout)||!Same(initial,owner)||!SdkIdentity(sdk,&finalLayout)||finalLayout!=layout||KL1OwnedPatchAudit::AuditCurrentVerifiedGame(finalPages,finalCount)!=KL1OwnedPatchAudit::Result::Okay||finalCount!=ledgerCount||!Prepare(owner,finalPages,changes)){log.Event("reject",false,mode,"locked_owner_or_all_page_audit");return;}
 std::vector<DWORD> baseline(owner.pageCount);if(!Bytes(owner.sums,baseline.data(),baseline.size()*4)){log.Event("reject",false,mode,"cache_snapshot");return;}
 for(const auto& c:changes){log.Event("certified",true,mode,"owned_page_canonical",c.rva,c.index);log.Event("checksums",true,mode,"canonical_before_live_after",c.before,c.after);}
 log.Event("prepared",true,mode,"all_pages_before_any_write",static_cast<DWORD>(changes.size()),ledgerCount);
 const bool committed=Transaction(owner,changes,baseline,reinterpret_cast<Begin>(sdk+layout->begin),reinterpret_cast<Set>(sdk+layout->set),reinterpret_cast<Begin>(sdk+layout->end),log,mode);(void)committed;
}
} // namespace SdkOwnCache12
