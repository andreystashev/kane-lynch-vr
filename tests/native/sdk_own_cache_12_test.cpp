#include "sdk_own_cache_12.hpp"
#include <cstdio>
unsigned checks=0,failures=0,setCalls=0,endCalls=0,mode=0;
void Check(bool x,const char* text){++checks;if(!x){++failures;std::printf("FAIL %s\n",text);}}
HRESULT __fastcall MockBegin(void* self,void*){auto* c=static_cast<DWORD*>(self);if(mode==5)return E_FAIL;if(c[8])return E_FAIL;c[8]=1;return S_OK;}
HRESULT __fastcall MockSet(void* self,void*,DWORD index,DWORD value){auto* c=static_cast<DWORD*>(self);++setCalls;auto* a=reinterpret_cast<DWORD*>(c[2]);if(index>=4||!c[8])return E_FAIL;
 if(mode==1&&setCalls==2)return E_FAIL;a[index]=value;if(mode==2&&setCalls==2)return E_FAIL;if(mode==6&&setCalls==1)a[0]=99;return S_OK;}
HRESULT __fastcall MockEnd(void* self,void*){auto* c=static_cast<DWORD*>(self);++endCalls;if(mode==3&&endCalls==1)return E_FAIL;if(mode==4&&endCalls==1)return S_OK;if(c[8]!=1)return E_FAIL;c[8]=0;return S_OK;}
int main(){
 using namespace SdkOwnCache12;Log log;const std::vector<DWORD> baseline={10,20,30,40};const std::vector<Change> changes={{0x1000,1,20,21},{0x2000,3,40,41}};
 for(mode=0;mode<=6;++mode){std::vector<DWORD> values=baseline;DWORD cache[9]{};cache[2]=reinterpret_cast<DWORD>(values.data());Owner owner;owner.cache=reinterpret_cast<DWORD>(cache);owner.sums=reinterpret_cast<DWORD>(values.data());owner.pageCount=4;setCalls=endCalls=0;
  const bool result=Transaction(owner,changes,baseline,reinterpret_cast<Begin>(&MockBegin),reinterpret_cast<Set>(&MockSet),reinterpret_cast<Begin>(&MockEnd),log,1);
  Check(result==(mode==0),"only successful complete transaction accepted");Check(cache[8]==0,"cache phase restored on every exit");
  Check(Snapshot().phase==(mode==0?Phase::Applied:Phase::Rejected),"native status is published without a logfile");
  Check(Ready()==(mode==0),"native readiness is independent of diagnostic logging");
  if(mode==0)Check(values==std::vector<DWORD>({10,21,30,41}),"only certified entries changed");
  else if(mode==6)Check(values==std::vector<DWORD>({99,20,30,40}),"foreign table change rejected without overwriting foreign entry");
  else Check(values==baseline,"failure restores every certified entry including write-then-fail");
 }
 std::vector<unsigned char> zero(4096);Check(Checksum(zero)==0xe7e3b952,"fast checksum zero vector matches offline model");
 std::printf("SDK cache transaction fixture: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
