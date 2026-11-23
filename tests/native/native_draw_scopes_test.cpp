#include "native_draw_scopes.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value,const char* message){++checks;if(!value){std::printf("FAIL: %s\n",message);std::exit(1);}}}
int main(){
    NativeDrawScopes scope;int device=0,first=1,second=2;std::vector<int> order;
    const auto beginFirst=[&](int*)->void*{order.push_back(1);return &first;};const auto beginSecond=[&](int*)->void*{order.push_back(2);return &second;};
    const auto endFirst=[&](void* token,int result){Check(token==&first,"outer adapter receives its own token");order.push_back(5);return result;};
    const auto endSecond=[&](void* token,int result){Check(token==&second,"inner adapter receives its own token");order.push_back(4);return result;};
    auto* token=scope.Begin(&device,beginFirst,beginSecond);Check(token==&scope&&scope.active,"both adapters open one owned draw context");
    Check(!scope.Begin(&device,beginFirst,beginSecond)&&order.size()==2,"nested draw cannot overwrite outer tokens");order.push_back(3);
    Check(scope.Finish(token,-7,endFirst,endSecond)==-7&&order==std::vector<int>({1,2,3,4,5}),"native draw failure still restores both adapters in reverse order");
    Check(!scope.active&&!scope.first&&!scope.second,"finished context retires all tokens");
    const auto none=[](int*)->void*{return nullptr;};order.clear();token=scope.Begin(&device,none,beginSecond);
    Check(token&&scope.Finish(token,0,endFirst,endSecond)==0&&order==std::vector<int>({2,4}),"lighting works when world-effects adapter is absent");
    order.clear();token=scope.Begin(&device,beginFirst,none);
    Check(token&&scope.Finish(token,0,endFirst,endSecond)==0&&order==std::vector<int>({1,5}),"world-effects adapter works when lighting is absent");
    Check(!scope.Begin(&device,none,none)&&!scope.active,"no adapter requires no native end callback");
    order.clear();token=scope.Begin(&device,beginFirst,beginSecond);
    Check(scope.Finish(&device,0,endFirst,endSecond)==0&&scope.active&&order.size()==2,"foreign token cannot finish a current draw");
    const auto failedInner=[&](void* owned,int){Check(owned==&second,"inner failure preserves exact token");order.push_back(4);return -9;};
    Check(scope.Finish(token,0,endFirst,failedInner)==-9&&order==std::vector<int>({1,2,4,5}),"failed inner restore propagates error and still restores outer adapter");
    Check(scope.Finish(token,0,endFirst,endSecond)==0&&!scope.active,"duplicate end cannot reuse retired tokens");
    NativeDrawScopes reentrant;const auto beginReentrant=[&](int* d)->void*{Check(!reentrant.Begin(d,beginFirst,beginSecond),"guard covers nested work during begin itself");return &first;};
    token=reentrant.Begin(&device,beginReentrant,none);reentrant.Finish(token,0,endFirst,endSecond);
    int third=3;order.clear();
    const auto beginThird=[&](int*)->void*{order.push_back(6);Check(!scope.Begin(&device,beginFirst,beginSecond),"third begin cannot recursively replace any open adapter");return &third;};
    const auto endThird=[&](void* owned,int result){Check(owned==&third,"third adapter receives its own token");order.push_back(7);return result;};
    token=scope.Begin(&device,beginFirst,beginSecond,beginThird);
    Check(scope.Finish(token,-11,endFirst,endSecond,endThird)==-11&&order==std::vector<int>({1,2,6,7,4,5}),"third adapter restores before both outer scopes after a native draw failure");
    Check(!scope.active&&!scope.first&&!scope.second&&!scope.third,"all three owned tokens retired");
    order.clear();token=scope.Begin(&device,none,none,beginThird);
    Check(token&&scope.Finish(token,0,endFirst,endSecond,endThird)==0&&order==std::vector<int>({6,7}),"third-only scope receives a finish callback when both outer adapters are absent");
    order.clear();token=scope.Begin(&device,beginFirst,beginSecond,beginThird);
    const auto failedThird=[&](void* owned,int){Check(owned==&third,"third failure retains its own token");order.push_back(7);return -19;};
    Check(scope.Finish(token,0,endFirst,endSecond,failedThird)==-19&&order==std::vector<int>({1,2,6,7,4,5}),"third restore failure still restores both outer adapters");
    int fourth=4;order.clear();
    const auto beginFourth=[&](int*)->void*{order.push_back(8);Check(!scope.Begin(&device,beginFirst,beginSecond),"fourth scope cannot replace any outer callback");return &fourth;};
    const auto endFourth=[&](void* owned,int result){Check(owned==&fourth,"fourth callback retains its own token");order.push_back(9);return result;};
    token=scope.Begin(&device,beginFirst,beginSecond,beginThird,beginFourth);
    Check(scope.Finish(token,-13,endFirst,endSecond,endThird,endFourth)==-13&&order==std::vector<int>({1,2,6,8,9,7,4,5}),"four adapters unwind in reverse order after failed native draw");
    Check(!scope.active&&!scope.first&&!scope.second&&!scope.third&&!scope.fourth,"fourth callback is retired with every outer token");
    order.clear();token=scope.Begin(&device,none,none,none,beginFourth);
    Check(token&&scope.Finish(token,0,endFirst,endSecond,endThird,endFourth)==0&&order==std::vector<int>({8,9}),"fourth-only clip adapter receives finish when other adapters are absent");
    order.clear();token=scope.Begin(&device,beginFirst,beginSecond,beginThird,beginFourth);
    const auto failedFourth=[&](void* owned,int){Check(owned==&fourth,"fourth restore failure retains owned token");order.push_back(9);return -29;};
    Check(scope.Finish(token,0,endFirst,endSecond,endThird,failedFourth)==-29&&order==std::vector<int>({1,2,6,8,9,7,4,5}),"fourth restore failure propagates while all previous scopes restore");
    std::printf("Native reversible draw adapters: %u checks PASS.\n",checks);return 0;
}
