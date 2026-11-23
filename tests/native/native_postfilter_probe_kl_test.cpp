#include "native_postfilter_probe_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;std::uintptr_t fail=0,change=0;unsigned seen=0;
    template<class T>void Put(std::uintptr_t address,T value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(T);++i)bytes[address+i]=p[i];}
    bool Read(std::uintptr_t address,void* out,std::size_t length){
        if(address==fail)return false;
        if(address==change&&++seen==2){std::uint32_t changed=0x180000;Put(address,changed);}
        auto* dest=static_cast<unsigned char*>(out);for(unsigned i=0;i<length;++i){auto item=bytes.find(address+i);if(item==bytes.end())return false;dest[i]=item->second;}return true;
    }
};
}
int main(){
    using namespace NativePostfilterProbeKL;
    constexpr std::uintptr_t image=0x400000,object=0x100000,renderer=0x200000,aux=0x300000;
    Memory m;m.Put(image+0x7c1b0c,std::uint32_t(object));m.Put(image+0x6977e8,std::uint32_t(renderer));
    m.Put(renderer+0x88,2.f);m.Put(object+0x10,.75f);m.Put(object+0xe0,std::uint32_t(1));
    m.Put(renderer+0xa4,std::uint32_t(1));m.Put(object+0x32ac,std::uint32_t(3));m.Put(object+0x3270,std::uint32_t(aux));
    m.Put(aux+0x16b4,std::uint32_t(8001));m.Put(aux+0x16b8,std::uint32_t(8002));m.Put(aux+0x16bc,std::uint32_t(8003));
    const unsigned views[]={0x32c4,0x32c8,0x32cc,0x32d0};
    const unsigned resources[]={0xe4,0xe8,0xec,0xf0,0x100,0x104,0x108,0x210,0x3254,0x3258,0x3260,0x3268,0x326c};
    for(unsigned i=0;i<4;++i)m.Put(object+views[i],std::uint32_t(i+100));
    for(unsigned i=0;i<13;++i)m.Put(object+resources[i],std::uint32_t(i+1000));
    m.Put(object+0x3288,std::uint32_t(7001));m.Put(object+0x328c,std::uint32_t(7002));
    const auto read=[&](std::uintptr_t address,void* out,std::size_t size){return m.Read(address,out,size);};
    StageState out;Check(Capture(image,object,read,out),"certified state captured");
    Check(out.renderer==renderer&&out.postFilter==object&&out.lod==2.f&&out.amount==.75f,"original object/level data");
    Check(out.sequence==1&&out.viewport[2]==102&&out.resources[12]==1012,"selected metadata");
    Check(out.shader[0]==7001&&out.shader[1]==7002,"both stage shader identities");
    Check(out.auxValid&&out.maskVariant==3&&out.resolveFlag==1&&out.auxRenderer==aux&&out.maskTexture==8001&&
          out.maskSurface==8002&&out.resolveSurface==8003,"mask/LUT branch and separate renderer resources");
    m.Put(renderer+0x88,1.f);Check(Capture(image,object,read,out)&&out.lod==1.f,"low setting observed independently");
    m.Put(object+0x3270,std::uint32_t(0));Check(Capture(image,object,read,out)&&out.lod==1.f&&!out.auxValid&&!out.auxRenderer,"null auxiliary retains low control");
    m.Put(object+0x3270,std::uint32_t(0x710000));Check(Capture(image,object,read,out)&&!out.auxValid&&out.auxRenderer==0x710000&&!out.maskTexture,"unreadable auxiliary distinguished from null");
    m.Put(object+0x3270,std::uint32_t(aux));m.fail=aux+0x16b8;Check(Capture(image,object,read,out)&&!out.auxValid&&!out.maskTexture,"partial auxiliary state never interpreted as zero resource");m.fail=0;
    Check(!Capture(image,object+4,read,out),"foreign object rejected");
    m.fail=object+0x3258;Check(!Capture(image,object,read,out),"unreadable resource rejected");m.fail=0;
    m.change=object+0x3270;m.seen=0;Check(!Capture(image,object,read,out),"replaced auxiliary rejected");m.change=0;m.Put(object+0x3270,std::uint32_t(aux));
    m.change=image+0x7c1b0c;m.seen=0;Check(!Capture(image,object,read,out),"replaced object rejected");
    std::printf("Postfilter opt-in read-only state capture: %u checks PASS; no GPU or game.\n",checks);
}
