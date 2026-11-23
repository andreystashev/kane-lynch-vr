// Pure data fixtures: no game/process access and no SDK operations.
#include "owned_patch_page_audit.hpp"
#include <array>
#include <cstdio>
#include <vector>
using namespace KL1OwnedPatchAudit;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
constexpr Bounds game{0x400000,0x900000},self{0x60000000,0x100000};
KL1_MH_OWNED_PATCH Patch(const std::vector<unsigned char>& canonical,std::uint32_t offset,bool above=false){
    KL1_MH_OWNED_PATCH p{};p.patchAddress=game.base+0x1000+offset;p.target=p.patchAddress+(above?5:0);
    p.detour=self.base+0x1234;p.patchLength=above?7:5;p.patchAbove=above;
    std::memcpy(p.backup,canonical.data()+offset,p.patchLength);p.installed[0]=0xe9;
    const auto relative=static_cast<std::uint32_t>(p.detour-(p.patchAddress+5));std::memcpy(p.installed+1,&relative,4);
    if(above){p.installed[5]=0xeb;p.installed[6]=0xf9;}return p;
}
Result Audit(const std::vector<unsigned char>& canonical,const std::vector<unsigned char>& live,
             const std::vector<KL1_MH_OWNED_PATCH>& ledger,std::vector<Page>& pages){
    return CompareText(canonical.data(),live.data(),canonical.size(),0x1000,game,self,ledger.data(),ledger.size(),pages);
}
}
int main(){
    std::vector<unsigned char> canonical(4096+3,0xcc),live=canonical;std::vector<Page> pages(1);
    Check(Audit(canonical,live,{},pages)==Result::Okay&&pages.empty(),"unchanged bytes publish no changed pages");
    auto patch=Patch(canonical,40);std::memcpy(live.data()+40,patch.installed,patch.patchLength);
    Check(Audit(canonical,live,{patch},pages)==Result::Okay&&pages.size()==1,"registered same-page detour accepted");
    Check(pages[0].pageRva==0x1000&&pages[0].comparedLength==4096&&pages[0].ownedRecordCount==1,"full page length and one owner reported");
    Check(pages[0].canonical.size()==4096&&pages[0].live[40]==0xe9,"separate canonical and actual bytes preserved");
    auto invalid=patch;invalid.backup[2]^=1;
    Check(Audit(canonical,live,{invalid},pages)==Result::BackupMismatch&&pages.empty(),"backup must match verified original file");
    invalid=patch;invalid.installed[0]=0x90;
    Check(Audit(canonical,live,{invalid},pages)==Result::MalformedBranch&&pages.empty(),"registered malformed opcode rejected");
    invalid=patch;invalid.installed[1]^=1;
    Check(Audit(canonical,live,{invalid},pages)==Result::MalformedBranch,"registered wrong E9 destination rejected");
    invalid=patch;invalid.detour=0x70000000;
    Check(Audit(canonical,live,{invalid},pages)==Result::ForeignDetour,"foreign detour module rejected");
    invalid=patch;invalid.detour=self.base+self.size;
    Check(Audit(canonical,live,{invalid},pages)==Result::ForeignDetour,"owner upper bound is exclusive");
    invalid=patch;invalid.patchLength=6;
    Check(Audit(canonical,live,{invalid},pages)==Result::InvalidRecord,"unsupported patch length rejected");
    invalid=patch;invalid.target+=1;
    Check(Audit(canonical,live,{invalid},pages)==Result::InvalidRecord,"target and patch address relation checked");
    Check(Audit(canonical,live,{patch,patch},pages)==Result::Overlap&&pages.empty(),"overlap including duplicate ledger entry rejected");
    live[17]^=1;
    Check(Audit(canonical,live,{patch},pages)==Result::UnknownDifference&&pages.empty(),"unregistered byte anywhere in text rejects all output");
    live[17]^=1;live[42]^=1;
    Check(Audit(canonical,live,{patch},pages)==Result::UnknownDifference,"live malformed branch not normalized");
    live=canonical;
    Check(Audit(canonical,live,{patch},pages)==Result::UnknownDifference,"registered but removed live patch rejected");
    patch=Patch(canonical,4094);std::memcpy(live.data()+4094,patch.installed,5);
    Check(Audit(canonical,live,{patch},pages)==Result::Okay&&pages.size()==2,"one owned patch crossing page boundary fully audited");
    Check(pages[0].comparedLength==4096&&pages[1].pageRva==0x2000&&pages[1].comparedLength==3,"partial final text page retains explicit three-byte extent");
    Check(pages[0].ownedRecordCount==1&&pages[1].ownedRecordCount==1,"cross-page owner counted on each covered page");
    Check(pages[1].canonical.size()==3&&pages[1].live.size()==3,"partial page never claims unseen 4093 bytes");
    live=canonical;patch=Patch(canonical,200,true);std::memcpy(live.data()+200,patch.installed,7);
    Check(Audit(canonical,live,{patch},pages)==Result::Okay&&pages.size()==1,"seven-byte hotpatch with EB F9 accepted");
    invalid=patch;invalid.installed[6]=0xf8;
    Check(Audit(canonical,live,{invalid},pages)==Result::MalformedBranch,"malformed hotpatch short branch rejected");
    invalid=patch;invalid.patchAddress=game.base+0xffe;invalid.target=invalid.patchAddress+5;
    Check(Audit(canonical,live,{invalid},pages)==Result::InvalidRange,"patch crossing into text from outside rejected");
    live=canonical;invalid=Patch(canonical,100);invalid.patchAddress=0x70000000;invalid.target=invalid.patchAddress;
    Check(Audit(canonical,live,{invalid},pages)==Result::Okay&&pages.empty(),"external API hook not certified as game text modification");
    Check(CompareText(nullptr,live.data(),live.size(),0x1000,game,self,nullptr,0,pages)==Result::InvalidImage,"missing original image rejected");
    Check(CompareText(canonical.data(),live.data(),0,0x1000,game,self,nullptr,0,pages)==Result::InvalidImage,"zero comparison extent rejected");
    Check(CompareText(canonical.data(),live.data(),live.size(),0x1000,game,Bounds{},nullptr,0,pages)==Result::InvalidImage,"empty owner module rejected");
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
