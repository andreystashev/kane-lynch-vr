#include "native_team_marker_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>
namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
constexpr std::uint32_t Image=0x400000,Data=0x10000000,Player=0x11000000,List=0x12000000,Records=0x13000000,Camera=0x14000000,Actor=0x15000000,Geometry=0x16000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=p[i];}
    bool Read(std::uintptr_t at,void* out,std::size_t size)const{auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i){auto j=bytes.find(at+i);if(j==bytes.end())return false;p[i]=j->second;}return true;}
    auto Reader()const{return [&](auto at,auto* out,auto size){return Read(at,out,size);};}
    Memory(){
        Set(Image+0x68760C,Data);Set(Data+0xA2C,Player);Set(Player,Image+0x56582C);Set(Player+0x9A0,Camera);Set(Camera,Image+0x545A3C);
        Set(Player+0x804,std::int8_t(0));
        Set(Image+0x8883A4,std::uint16_t(0));Set(Data+0x1550,List);Set(List+8,std::uint32_t(5));Set(List+0x14,Records);
        Set(Actor+4,Geometry);Set(Geometry+0x60,Actor);Set(Geometry+0x3C,std::uint32_t(0));
        for(unsigned i=0;i<5;++i){Set(Records+i*0x64,std::array<unsigned char,0x64>{});Set(Records+i*0x64+0x1C,std::uint32_t(i==0?7:i==1?8:i==2?9:i==3?1:7));Set(Records+i*0x64+4,NativeCameraMath::Vec3{float(i)*10,100,1000});Set(Records+i*0x64+0x44,i==4?Camera+4:0xffffffffu);}
        Set(Records+0x4D,std::uint8_t(1));Set(Records+0x64,Actor);Set(Records+0x64+0x4F,std::uint8_t(1));Set(Records+0x64+0x40,5.f);
    }
};
void Actual(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),{});
    const auto u32=[&](std::size_t at){std::uint32_t v=0;if(at+4<=raw.size())std::memcpy(&v,raw.data()+at,4);return v;};
    const auto u16=[&](std::size_t at){std::uint16_t v=0;if(at+2<=raw.size())std::memcpy(&v,raw.data()+at,2);return v;};
    const auto pe=u32(0x3C),opt=pe+24,base=u32(opt+28),size=u32(opt+56),sections=opt+u16(pe+20);
    Check(u16(0)==0x5A4D&&u32(pe)==0x4550&&u16(pe+4)==0x14C&&size&&size<0x2000000,"actual supported PE maps without opening the game");
    std::vector<unsigned char> mapped(size);
    for(unsigned i=0;i<u16(pe+6);++i){const auto a=sections+i*40,rva=u32(a+12),bytes=u32(a+16),offset=u32(a+20);Check(rva+bytes<=mapped.size()&&offset+bytes<=raw.size(),"actual section stays bounded");std::memcpy(mapped.data()+rva,raw.data()+offset,bytes);}
    const auto read=[&](std::uintptr_t at,void* out,std::size_t bytes){if(at<base||at-base>mapped.size()||bytes>mapped.size()-(at-base))return false;std::memcpy(out,mapped.data()+at-base,bytes);return true;};
    Check(NativeTeamMarkerKL::VerifyImage(base,read),"actual native7/8/9 textures, list, source camera and native command tags agree");
    for(auto at:{0x270490u,0x25418Eu,0x253C56u,0x647B24u,0x574F28u,0x213303u,0x213F1Fu,0x213C35u,0x213549u,0x25308Eu}){mapped[at]^=1;Check(!NativeTeamMarkerKL::VerifyImage(base,read),"changed source proof disables marker interpretation");mapped[at]^=1;}
}
}
int main(int argc,char** argv){
    using namespace NativeTeamMarkerKL;Memory memory;
    const auto original=memory.bytes;auto s=Capture(Image,Player,1000,memory.Reader());
    Check(s.valid&&s.count==3,"only native command markers of the current camera are captured");
    Check(s.markers[0].icon==Icon::Shield&&s.markers[0].position.y==130&&s.markers[0].fixed,"native fixed defend destination is distinct from recipient icons and retains native30-unit lift");
    Check(s.markers[1].icon==Icon::Lightning&&s.markers[1].position.y==105,"native attack target retains current native world position and animation offset");
    Check(s.markers[2].icon==Icon::Circle,"native follow marker retains circle identity");
    Check(memory.bytes==original,"marker snapshot never changes native objects or order state");
    memory.Set(Records+0x44,std::uint32_t(0));Check(Capture(Image,Player,1000,memory.Reader()).count==3,"native index0 record belongs to local display rather than the camera pointer");
    memory.Set(Records+0x44,Camera);Check(Capture(Image,Player,1000,memory.Reader()).count==2,"camera pointer cannot masquerade as native marker player index");memory=Memory{};
    Check(Fresh(s,Player,1150)&&!Fresh(s,Player,1151)&&!Fresh(s,Player+4,1000),"stale and foreign marker frames cannot persist on screen");
    memory.Set(Geometry+0x3C,std::uint32_t(0x400));Check(Capture(Image,Player,1000,memory.Reader()).count==2,"native hidden target geometry remains hidden");
    memory=Memory{};memory.Set(Records,Actor);memory.Set(Records+0x1C,std::uint32_t(21));
    memory.Set(Geometry+0x3C,std::uint32_t(0xC00));
    auto fixed=Capture(Image,Player,1000,memory.Reader());
    Check(fixed.valid&&fixed.count==2&&fixed.markers[0].fixed&&fixed.markers[0].icon==Icon::Shield&&fixed.markers[0].position.y==130,
          "native fixed Move destination remains visible when its lifetime owner mesh is hidden in VR");
    memory.Set(Records+0x4D,std::uint8_t(0));
    Check(Capture(Image,Player,1000,memory.Reader()).count==1,"an attached marker still observes hidden target geometry");
    memory=Memory{};memory.Set(Image+0x8883A5,std::uint8_t(1));Check(!Capture(Image,Player,1000,memory.Reader()).valid,"both native pause bytes disable world markers");
    memory=Memory{};memory.Set(List+8,std::uint32_t(MaximumNativeMarkers+1));Check(!Capture(Image,Player,1000,memory.Reader()).valid,"unbounded native list is rejected");
    memory=Memory{};memory.Set(List+0x14,std::uint32_t(0xfffffff0));Check(!Capture(Image,Player,1000,memory.Reader()).valid,"wrapped marker records cannot be read");
    memory=Memory{};unsigned visits=0;const auto race=[&](auto at,auto* out,auto bytes){if(at==Records+0x64&&++visits==1)memory.Set(Records+0x64+0x1C,std::uint32_t(7));return memory.Read(at,out,bytes);};
    Check(!Capture(Image,Player,1000,race).valid,"a changed command record cannot create a torn world marker");
    memory=Memory{};memory.Set(List+8,std::uint32_t(0));memory.Set(List+0x14,std::uint32_t(0));s=Capture(Image,Player,1000,memory.Reader());Check(s.valid&&!s.count,"native empty marker list stays empty");
    Check(StatusIcon(7)==Icon::Shield&&StatusIcon(8)==Icon::Lightning&&StatusIcon(9)==Icon::Circle,"roster status uses the same native icon meanings");
    Icon icon=Icon::Circle;
    Check(TypeIcon(20,icon)&&icon==Icon::Lightning&&TypeIcon(21,icon)&&icon==Icon::Shield&&TypeIcon(27,icon)&&icon==Icon::Shield,
          "native alternate command marker tags share their proven Attack and Defend resources");
    Check(!TypeIcon(6,icon)&&!TypeIcon(28,icon),"unproven native tags are not interpreted as squad orders");
    if(argc>1)Actual(argv[1]);std::printf("Native command marker observation: %u checks PASS.\n",checks);return 0;
}
