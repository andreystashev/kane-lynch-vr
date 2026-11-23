#include "native_passenger_entry_kl.hpp"
#include <map>
#include <vector>
#include <cstdio>
namespace {
using namespace NativeCameraMath;
std::map<std::uintptr_t,unsigned char> bytes;unsigned checks=0,failures=0;
void Check(bool value,const char* reason){++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}}
template<class T>void Put(std::uintptr_t at,const T& value){const auto* source=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i)bytes[at+i]=source[i];}
bool Read(std::uintptr_t at,void* output,std::size_t size){for(std::size_t i=0;i<size;++i){const auto found=bytes.find(at+i);if(found==bytes.end())return false;static_cast<unsigned char*>(output)[i]=found->second;}return true;}
void Text(std::uintptr_t at,const char* value){std::array<char,192> text{};std::snprintf(text.data(),text.size(),"%s",value);Put(at,text);}
constexpr std::uintptr_t image=0x400000,player=0x20000,movement=0x30000,camera=0x40000,record=0x50000,carrier=0x60000,geometry=0x70000,controller=0x80000,dispatcher=0x90000,data=0xA0000,name=0xB0000;
void Seed(){
    bytes.clear();Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));
    Put(player+0x67C,std::uint32_t(movement));Put(movement,std::uint32_t(image+0x566D14));Put(movement+0x38,std::uint32_t(player));Put(movement+0x44,std::uint32_t(12));Put(player+0x5F8+12*4,std::uint32_t(movement));
    Put(player+0x194,std::uint32_t(dispatcher));Put(dispatcher,std::uint32_t(image+0x54DE4C));Put(dispatcher+8,std::uint32_t(player));Put(dispatcher+4,std::uint32_t(0));
    Put(player+0xB04,std::uint32_t(record));Put(record+0x18,std::uint32_t(carrier));Put(carrier+4,std::uint32_t(geometry));
    NativePlayerPose::Geometry node;node.pose={{{0,0,1},{0,1,0},{1,0,0}},{100,200,300}};Put(geometry,node);Put(geometry+0x60,std::uint32_t(carrier));Put(geometry+0x6C,std::uint32_t(name));
    Text(name,"ROOT\\Hero_Allies!Mercs!MercSquad_Driver\\GetawayVan_01");Put(player+0x9A0,std::uint32_t(camera));Put(player+0x9B8,std::uint32_t(controller));Put(controller+0x18,std::uint8_t(1));
}
auto Snapshot(){return NativePassengerEntryKL::Snapshot(image,player,movement,camera,Read);}
}
int main(){
    Seed();const auto original=bytes;auto state=Snapshot();Check(state.valid&&state.known&&state.blocked&&state.stage==100,"exact startup carrier exposes native control lock");Check(bytes==original,"read-only entry snapshot leaves every source byte unchanged");
    Put(controller+0x18,std::uint8_t(0));state=Snapshot();Check(state.valid&&state.known&&!state.blocked,"native controls unlock is an explicit observed phase");
    Put(controller+0x18,std::uint8_t(2));state=Snapshot();Check(state.valid&&!state.known&&!state.blocked,"unrecognized flag never guesses closed doors");
    Seed();Text(name,"GetawayVan_ChaseVersion");Check(!Snapshot().valid,"bank parking carrier is excluded");
    Seed();Text(name,"OtherVan");Check(!Snapshot().valid,"other vehicles cannot receive entry clearance");
    Seed();Put(player+0x9A0,std::uint32_t(camera+4));Check(!Snapshot().valid,"foreign native camera rejected");
    Seed();Put(movement+0x38,std::uint32_t(player+4));Check(!Snapshot().valid,"foreign movement owner rejected");
    Seed();Put(player+0x5F8+12*4,std::uint32_t(movement+4));Check(!Snapshot().valid,"unregistered movement rejected");
    Seed();Put(geometry+0x60,std::uint32_t(carrier+16));Check(!Snapshot().valid,"foreign geometry owner without reciprocal backref rejected");
    Seed();Put(geometry+0x60,std::uint32_t(carrier+16));Put(carrier+20,std::uint32_t(geometry));Check(Snapshot().valid,"reciprocal native carrier alias accepted");
    Seed();const std::uintptr_t parent=0xC0000,parentName=0xD0000;Text(name,"GetawayVan_01");Put(geometry+0x50,std::uint32_t(parent));Put(parent,NativePlayerPose::Geometry{{{{0,0,1},{0,1,0},{1,0,0}},{0,0,0}},{},0});Put(parent+0x6C,std::uint32_t(parentName));Text(parentName,"ROOT\\Hero_Allies!Mercs!MercSquad_Driver");Check(Snapshot().valid,"scoped leaf name under exact startup parent accepted");
    Text(parentName,"ROOT\\OtherMission");Check(!Snapshot().valid,"same leaf under unrelated scene rejected");
    Seed();unsigned reads=0;const auto race=[&](std::uintptr_t at,void* out,std::size_t size){if(at==controller+0x18&&++reads==2)Put(controller+0x18,std::uint8_t(0));return Read(at,out,size);};
    Check(!NativePassengerEntryKL::Snapshot(image,player,movement,camera,race).valid,"control flag changing during capture rejects torn entry sample");
    Seed();unsigned recordReads=0;const auto retired=[&](std::uintptr_t at,void* out,std::size_t size){if(at==player+0xB04&&++recordReads==2)Put(player+0xB04,std::uint32_t(record+4));return Read(at,out,size);};
    Check(!NativePassengerEntryKL::Snapshot(image,player,movement,camera,retired).valid,"retired carrier record rejected before clearance authority");
    Seed();unsigned nameReads=0;const auto ownerRace=[&](std::uintptr_t at,void* out,std::size_t size){if(at==geometry+0x6C&&++nameReads==2)Put(geometry+0x60,std::uint32_t(carrier+16));return Read(at,out,size);};
    Check(!NativePassengerEntryKL::Snapshot(image,player,movement,camera,ownerRace).valid,"late geometry owner replacement cannot authorize a named foreign source");
    Seed();Put(geometry+0x60,std::uint32_t(carrier+16));Put(carrier+20,std::uint32_t(geometry));unsigned aliasReads=0;
    const auto aliasRace=[&](std::uintptr_t at,void* out,std::size_t size){if(at==carrier+20&&++aliasReads==2)Put(carrier+20,std::uint32_t(geometry+16));return Read(at,out,size);};
    Check(!NativePassengerEntryKL::Snapshot(image,player,movement,camera,aliasRace).valid,"late reciprocal alias retirement cannot authorize a foreign source");
    Seed();NativePassengerEntryKL::PairCache pairs;auto first=pairs.Select(Snapshot(),10,100);
    Check(first.valid&&first.blocked,"first eye captures actual locked phase");Put(controller+0x18,std::uint8_t(0));
    Check(pairs.Select(Snapshot(),10,101).blocked,"control unlock between eyes cannot split entry camera position");
    Check(!pairs.Select(Snapshot(),11,102).blocked,"next pair consumes real unlocked phase");
    auto foreign=Snapshot();++foreign.carrier;foreign.blocked=true;Check(pairs.Select(foreign,11,103).blocked,"pair cannot reuse another carrier source");
    foreign.valid=false;Check(!pairs.Select(foreign,11,104).valid,"invalid current source never gains cached authority");
    std::printf("Passenger startup carrier/control scope: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
