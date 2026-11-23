#include "native_command_ray_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}
constexpr std::uintptr_t image=0x400000,player=image+0x20000,camera=image+0x30000,data=image+0x10000;
std::vector<unsigned char> memory(0x900000);std::uintptr_t failAt=0;bool race=false;
template<class V>void Put(std::uintptr_t at,const V& value){std::memcpy(memory.data()+at-image,&value,sizeof(value));}
bool Read(std::uintptr_t at,void* output,std::size_t size){if(at==failAt||at<image||at-image+size>memory.size())return false;
    std::memcpy(output,memory.data()+at-image,size);if(race&&at==player+0x358+0x60){race=false;Put(at,std::uint32_t(0x999999));}return true;}
}
int main(){using namespace NativeCommandRayKL;
    HeadGaze head;head.player=player;head.sourceCamera=camera;head.pairId=1;head.tick=100;head.recenterAck=7;head.mounted=true;head.unitsPerMetre=100;head.direction={1,0,0};
    Check(Fresh(head,100,7)&&Fresh(head,150,7),"mounted head accepts current through exact50ms boundary");
    Check(!Fresh(head,151,7)&&!Fresh(head,99,7),"old and future tracking rejected");
    Check(!Fresh(head,100,8),"recenter invalidates head certificate");
    auto changed=head;changed.mounted=false;Check(!Fresh(changed,100,7),"unmounted head rejected");
    changed=head;changed.direction={};Check(!Fresh(changed,100,7),"zero tracking direction rejected");
    changed=head;changed.origin.x=std::numeric_limits<float>::quiet_NaN();Check(!Fresh(changed,100,7),"nonfinite tracking origin rejected");
    changed=head;changed.unitsPerMetre=0;Check(!Fresh(changed,100,7),"invalid world scale rejected");
    changed=head;changed.sourceCamera=0;Check(!Fresh(changed,100,7),"missing native camera rejected");
    changed=head;changed.pairId=0;Check(!Fresh(changed,100,7),"unrendered head pair rejected");
    changed=head;changed.tick=0xfffffff0;Check(Fresh(changed,0x10,7),"unsigned tick wrap remains fresh within50ms");
    Check(Same(head,head),"unchanged head certificate exact");
    changed=head;changed.origin.x=.001f;Check(!Same(head,changed),"any head source motion invalidates completed ray");
    changed=head;++changed.pairId;Check(!Same(head,changed),"new rendered pair cannot reuse old query");
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));
    Put(player+0x9A0,std::uint32_t(camera));Put(camera,std::uint32_t(image+0x545A3C));
    for(unsigned side=0;side<2;++side){auto at=player+(side?0x358:0x2D8);Put(at,std::uint32_t(image+0x5508B4));NativeWeaponOwnership::HandState state;state.enabled=1;Put(at+0x60,state);}
    auto hands=ReadHands(image,player,Read);Check(hands.valid&&hands.empty&&hands.player==player&&hands.camera==camera,"two exact native empty hands authorize head fallback");
    NativeWeaponKL::AimFrame gun;gun.player=player;gun.weapon=image+0x50000;gun.hand=1;
    Check(!Matches(hands,gun),"empty hands cannot authorize old firearm ray");
    Put(player+0x358+0x60,std::uint32_t(gun.weapon));hands=ReadHands(image,player,Read);
    Check(hands.valid&&!hands.empty&&Matches(hands,gun),"held right gun owns only exact matching ray");
    Put(player+0x358+0x64,std::uint8_t(0));hands=ReadHands(image,player,Read);Check(hands.valid&&!hands.empty,"inactive retained gun prevents head fallback");
    gun.hand=0;Check(!Matches(hands,gun),"other hand cannot supply firearm ray");gun.hand=1;++gun.weapon;Check(!Matches(hands,gun),"another item cannot supply firearm ray");--gun.weapon;
    Put(player+0x358+0x60,std::uint32_t(0x123456));hands=ReadHands(image,player,Read);Check(hands.valid&&!hands.empty,"unknown held object conservatively prevents unarmed fallback");
    Put(player+0x358+0x60,std::uint32_t(0));Put(player+0x358+0x64,std::uint8_t(2));Check(!ReadHands(image,player,Read).valid,"impossible native hand enabled value rejected");Put(player+0x358+0x64,std::uint8_t(1));
    for(auto at:{player,player+0x2D8,player+0x358,camera}){std::uint32_t saved;Read(at,&saved,4);Put(at,std::uint32_t(0));Check(!ReadHands(image,player,Read).valid,"wrong player/hand/camera class cannot authorize fallback");Put(at,saved);}
    Check(!ReadHands(image,player+4,Read).valid,"other player cannot borrow local empty state");
    Put(image+0x8883A4,std::uint8_t(1));Check(!ReadHands(image,player,Read).valid,"native pause rejects empty certificate");Put(image+0x8883A4,std::uint8_t(0));
    for(auto at:{image+0x68760C,data+0xA2C,player+0x9A0,player+0x2D8+0x60,player+0x358+0x60}){failAt=at;Check(!ReadHands(image,player,Read).valid,"partial native reads cannot authorize empty state");}failAt=0;
    race=true;Check(!ReadHands(image,player,Read).valid,"native equip between repeated hand reads invalidates certificate");
    std::printf("Native command source certificate: %u checks PASS.\n",checks);return 0;
}
