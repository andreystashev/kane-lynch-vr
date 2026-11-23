#include "native_team_commands_kl_image.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class V>void Put(std::uintptr_t at,const V& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(unsigned i=0;i<sizeof(v);++i)bytes[at+i]=p[i];}
    bool Read(std::uintptr_t at,void* out,std::size_t size)const{auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i){auto j=bytes.find(at+i);if(j==bytes.end())return false;p[i]=j->second;}return true;}
    auto Reader()const{return [&](auto at,auto* out,auto size){return Read(at,out,size);};}
};
struct PE {
    std::vector<unsigned char> bytes;struct Section{std::uint32_t virtualSize,rva,rawSize,raw;};std::vector<Section> sections;
    template<class V>bool Get(std::size_t at,V& v)const{if(at>bytes.size()||sizeof(v)>bytes.size()-at)return false;std::memcpy(&v,bytes.data()+at,sizeof(v));return true;}
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);auto n=f.tellg();if(n<=0||n>64*1024*1024)return false;bytes.resize(std::size_t(n));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),n);std::uint32_t pe=0,sig=0;std::uint16_t count=0,opt=0;
        if(!f||!Get(0x3C,pe)||!Get(pe,sig)||sig!=0x4550||!Get(pe+6,count)||count>96||!Get(pe+20,opt))return false;
        for(unsigned i=0;i<count;++i){Section s{};if(!Get(pe+24+opt+40*i+8,s))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t size)const{if(at<0x400000)return false;const auto rva=at-0x400000;for(const auto& s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize){const auto o=rva-s.rva;if(size>s.rawSize-o||s.raw+o>bytes.size()||size>bytes.size()-s.raw-o)return false;std::memcpy(out,bytes.data()+s.raw+o,size);return true;}return false;}
};
constexpr std::uint32_t image=0x400000,data=0x11000,player=0x22000,control=0x33000,member=0x44000,squad=0x55000;
Memory Roster(){Memory m;m.Put(image+0x8883A4,std::uint16_t(0));m.Put(image+0x68760C,data);m.Put(data+0xA2C,player);m.Put(player,image+0x56582C);m.Put(player+0xAA8,control);m.Put(control,image+NativeTeamCommandsImage::ControlVtable);m.Put(control+4,player);m.Put(control+0x14,std::uint32_t(1));m.Put(control+0x10,control+0x18);m.Put(control+0x18,member);m.Put(control+0x9C,std::uint8_t(0));m.Put(control+0xA0,squad);m.Put(member+0x238,player);m.Put(member+0xE8,squad);m.Put(member+0x261,std::uint8_t(1));m.Put(member+0x2DC,std::uint32_t(19));return m;}
void Receivers(){namespace I=NativeTeamCommandsImage;auto m=Roster();auto a=I::Receiver(image,1000,5,m.Reader());
    Check(a.valid&&!a.all&&a.player==player&&a.control==control&&a.receiver==member&&a.nativeId==19,"owned currently selected native receiver certificate");
    Check(TeamCommandIPC::Fresh(a,1200,5)&&!TeamCommandIPC::Fresh(a,1201,5),"render certificate expiry is bounded");
    Check(!TeamCommandIPC::Fresh(a,999,5)&&!TeamCommandIPC::Fresh(a,1000,6),"future/recenter mismatch rejected");
    auto b=a;b.receiver+=4;Check(!I::SameReceiver(a,b),"selection mismatch cannot redirect order");
    b=a;b.nativeId++;Check(!I::SameReceiver(a,b),"native identity generation mismatch rejected");
    m.Put(control+0x10,control+0x1C);b=I::Receiver(image,1001,5,m.Reader());Check(b.valid&&b.all&&!b.receiver&&!b.nativeId,"native end-of-array is all receivers");
    Check(!I::SameReceiver(a,b),"selected individual cannot silently become all");
    m.Put(control+0x10,control+0x19);Check(!I::Receiver(image,1000,5,m.Reader()).valid,"misaligned selector rejected");
    m=Roster();m.Put(control+0x9C,std::uint8_t(1));Check(!I::Receiver(image,1000,5,m.Reader()).valid,"network squad command mode unsupported");
    m=Roster();m.Put(image+0x8883A5,std::uint8_t(1));Check(!I::Receiver(image,1000,5,m.Reader()).valid,"second pause byte rejects scene");
    m=Roster();m.Put(member+0x261,std::uint8_t(0));Check(!I::Receiver(image,1000,5,m.Reader()).valid,"disabled native receiver rejected");
    m=Roster();m.Put(member+0x238,player+4);Check(!I::Receiver(image,1000,5,m.Reader()).valid,"foreign receiver owner rejected");
    m=Roster();m.Put(member+0xE8,squad+4);Check(!I::Receiver(image,1000,5,m.Reader()).valid,"foreign squad rejected");
    m=Roster();m.Put(control+0x14,std::uint32_t(17));Check(!I::Receiver(image,1000,5,m.Reader()).valid,"unbounded receiver list rejected");
    m=Roster();unsigned n=0;auto tearing=[&](auto at,auto* out,auto size){if(at==control+0x10&&++n==2)m.Put(control+0x10,control+0x1C);return m.Read(at,out,size);};Check(!I::Receiver(image,1000,5,tearing).valid,"late receiver transition rejects certificate");
}
void Targets(){namespace I=NativeTeamCommandsImage;Memory m;constexpr std::uint32_t pool=0x66000,geo=0x77000,gen=0x88000,actor=0x99000,faction=0xAA000,ownFaction=0xBB000;
    m.Put(image+0x68760C,data);m.Put(data+4,std::uint32_t(1));m.Put(data+8,actor);
    m.Put(image+0x6C99B8,std::uint32_t(0xCC000));m.Put(0xCC00C,pool);m.Put(pool+4,std::uint32_t(0x70));m.Put(pool+8,std::uint32_t(8));m.Put(pool+0xC,geo);m.Put(pool+0x10,gen);m.Put(gen,std::uint16_t(2));
    m.Put(actor,image+I::ActorVtable);m.Put(actor+4,geo);m.Put(geo+0x60,actor);m.Put(geo+0x50,std::uint32_t(0));m.Put(geo+0x3C,std::uint32_t(0));m.Put(actor+0x536,std::uint8_t(1));m.Put(actor+0x54,faction);m.Put(faction+4,std::uint32_t(3));m.Put(player+0x54,ownFaction);m.Put(ownFaction+0xC,std::uint32_t(1u<<3));
    std::uintptr_t result=0;std::uint32_t reference=0;Check(I::Reference(image,geo,m.Reader(),reference)&&reference==17,"native ZREF generated from actual pool index/generation");
    Check(I::HostileActor(image,player,17,m.Reader(),result,reference)&&result==actor&&reference==17,"only native-listed live hostile actor selected from ray contact");
    m.Put(ownFaction+0xC,std::uint32_t(0));Check(!I::HostileActor(image,player,17,m.Reader(),result,reference),"friendly faction cannot be attacked");m.Put(ownFaction+0xC,std::uint32_t(1u<<3));
    m.Put(actor+0x536,std::uint8_t(0));Check(!I::HostileActor(image,player,17,m.Reader(),result,reference),"dead actor rejected");m.Put(actor+0x536,std::uint8_t(1));
    m.Put(data+8,player);Check(!I::HostileActor(image,player,17,m.Reader(),result,reference),"unlisted contact cannot impersonate NPC");m.Put(data+8,actor);
    m.Put(geo+0x3C,std::uint32_t(0x400));Check(!I::HostileActor(image,player,17,m.Reader(),result,reference),"disabled geometry rejected");m.Put(geo+0x3C,std::uint32_t(0));
    m.Put(gen,std::uint16_t(3));Check(!I::HostileActor(image,player,17,m.Reader(),result,reference),"stale contact generation rejected");
    Check(!I::Reference(image,geo+1,m.Reader(),reference),"non-pool-aligned geometry rejected");
    m.Put(pool+8,std::uint32_t(0));Check(!I::Reference(image,geo,m.Reader(),reference),"zero pool capacity rejected before modulo");
}
void Image(const char* path){PE pe;Check(pe.Load(path),"supported PE file loads read-only");auto read=[&](auto at,auto* out,auto size){return pe.Read(at,out,size);};Check(NativeTeamCommandsImage::Verify(image,read),"native squad/receiver/target/faction image proof matches actual PE");
    namespace I=NativeTeamCommandsImage;
    Check(I::VerifyOrderAdvance(image,read)&&I::OrderAdvance(0x213D7C)&&!I::OrderAdvance(0x238260),
        "order-internal selection calls are distinct from the explicit native Tab action");
    for(auto rva:{NativeTeamCommandsImage::Recall,NativeTeamCommandsImage::Move,NativeTeamCommandsImage::Attack,NativeTeamCommandsImage::Target,NativeTeamCommandsImage::FallbackTarget,std::uintptr_t(0x23252A),std::uintptr_t(0x232760),std::uintptr_t(0x116800)}){
        auto corrupt=[&](auto at,auto* out,auto size){if(!pe.Read(at,out,size))return false;if(at==image+rva)reinterpret_cast<unsigned char*>(out)[0]^=1;return true;};Check(!NativeTeamCommandsImage::Verify(image,corrupt),"changed native command source disables module");}
}
}
int main(int argc,char** argv){Receivers();Targets();if(argc>1)Image(argv[1]);std::printf("Native team command ownership/hostility: %u checks PASS; no game or orders opened.\n",checks);return 0;}
