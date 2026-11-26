#include "sdk_own_cache_12.hpp"
#include <cstdio>
unsigned checks=0,failures=0,setCalls=0,endCalls=0,mode=0;
void Check(bool x,const char* text){++checks;if(!x){++failures;std::printf("FAIL %s\n",text);}}
HRESULT __fastcall MockBegin(void* self,void*){auto* c=static_cast<DWORD*>(self);if(mode==5)return E_FAIL;if(c[8])return E_FAIL;c[8]=1;return S_OK;}
HRESULT __fastcall MockSet(void* self,void*,DWORD index,DWORD value){auto* c=static_cast<DWORD*>(self);++setCalls;auto* a=reinterpret_cast<DWORD*>(c[2]);if(index>=4||!c[8])return E_FAIL;
 if(mode==1&&setCalls==2)return E_FAIL;a[index]=value;if(mode==2&&setCalls==2)return E_FAIL;if(mode==6&&setCalls==1)a[0]=99;return S_OK;}
HRESULT __fastcall MockEnd(void* self,void*){auto* c=static_cast<DWORD*>(self);++endCalls;if(mode==3&&endCalls==1)return E_FAIL;if(mode==4&&endCalls==1)return S_OK;if(c[8]!=1)return E_FAIL;c[8]=0;return S_OK;}
void CheckSdk35Owner() {
    using namespace SdkOwnCache12;
    DWORD table[0x8c / 4]{};
    DWORD owner[5]{};
    DWORD rows[2 * (0x214 / 4)]{};
    DWORD foreignModule[0x4c / 4]{};
    DWORD gameModule[0x198 / 4]{};
    DWORD ranges[2]{}, indices[1]{}, sums[1]{};

    DWORD key = 0x1234;
    DWORD contents[] = {0, reinterpret_cast<DWORD>(owner)};
    DWORD buffer[] = {reinterpret_cast<DWORD>(contents), 0};
    table[0] = 1;
    table[0x4c / 4] = sizeof(DWORD);
    table[0x88 / 4] = reinterpret_cast<DWORD>(buffer) + key;
    owner[0] = reinterpret_cast<DWORD>(rows);
    owner[2] = 2;
    owner[4] = 1;
    foreignModule[0x48 / 4] = 0x10000000;
    gameModule[0x48 / 4] = gameImageBase;
    rows[0] = reinterpret_cast<DWORD>(foreignModule);
    rows[1] = foreignModule[0x48 / 4];
    auto* gameRow = rows + 0x214 / 4;
    gameRow[0] = reinterpret_cast<DWORD>(gameModule);
    gameRow[1] = gameImageBase;

    auto* cache = gameModule + 0x174 / 4;
    cache[0] = reinterpret_cast<DWORD>(ranges);
    cache[1] = reinterpret_cast<DWORD>(indices);
    cache[2] = reinterpret_cast<DWORD>(sums);
    cache[5] = 1;
    cache[6] = 1;
    std::vector<DWORD> sdk((sdk35.key + sizeof(DWORD)) / sizeof(DWORD));
    sdk[sdk35.table / 4] = reinterpret_cast<DWORD>(table);
    sdk[sdk35.key / 4] = reinterpret_cast<DWORD>(&key);
    const auto base = reinterpret_cast<Address>(sdk.data());
    Owner resolved;
    Check(Resolve(base, resolved, sdk35) && resolved.cache == reinterpret_cast<DWORD>(cache),
          "3.5 SDK selects the verified game in the second owner row");
    gameRow[1] = foreignModule[0x48 / 4];
    Check(!Resolve(base, resolved, sdk35), "3.5 SDK rejects a mismatch in the registered game image");
    gameRow[1] = gameImageBase;
    table[0x4c / 4] = 8;
    Check(!Resolve(base, resolved, sdk35), "3.5 SDK rejects an unexpected owner entry length");
}

int main(){
 CheckSdk35Owner();
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
