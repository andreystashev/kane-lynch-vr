#include "native_reticle_style_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace NativeReticleStyleKL;using namespace NativeHudFixture;
unsigned checks=0;void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL: %s\n",why);std::exit(1);}}
constexpr std::uint32_t At=0x15000000,Table=0x16000000,Mover=0x17000000,Copy=0x18000000;
std::uint32_t Field(const Display& bytes,unsigned offset){std::uint32_t value=0;std::memcpy(&value,bytes.data()+offset,4);return value;}
Memory Source(){Memory m;m.Set(At,Display{});m.Set(At,Base+0x573F2C);m.Set(At+8,Table);m.Set(At+0x10,Player);
    m.Set(At+0x2C,std::uint32_t(1));m.Set(At+0x34,At+0x38);m.Set(At+0x64,std::uint32_t(5));m.Set(At+0x6C,At+0x70);
    m.Set(At+0x60,std::uint32_t(0));m.Set(Player+0x9AC,Mover);m.Set(Mover,Base+NativeCameraMoverKL::MouseVtable);m.Set(Mover+8,Player);m.Set(Mover+0x68,std::uint32_t(5));
    for(unsigned i=0;i<18;++i)m.Set(Table+i*12,std::array<std::uint32_t,3>{100+i,32,32});return m;}
}
int main(int argc,char** argv){
    if(argc>1){Image source;Check(source.Load(argv[1]),"load supported Steam image without game execution");Check(VerifyImage(Base,source.Reader()),"exact native draw/constructor/mover/mode table and original named resources match");
        for(auto rva:{0x2545A0u,0x254610u,0x25439Bu,0x2543B6u,0x647C48u,0x647D48u,0x5752E8u}){
            source.mapped[rva]^=1;Check(!VerifyImage(Base,source.Reader()),"changed code/resource/name cannot authorize original-reticle replacement");source.mapped[rva]^=1;}}
    auto m=Source();Display copy{};
    for(const auto style:{Style::Dot,Style::Crosshair})for(unsigned mode:{1u,2u,3u,5u,6u,7u,11u,13u}){
        m.Set(Mover+0x68,mode);const auto before=m.bytes;
        Check(Prepare(Base,At,Player,style,Copy,m.Reader(),copy),"known armed modes produce a presentation-only display copy");
        Check(Field(copy,0x60)==Table+Glyph(mode,style)*12,"actual selected native texture index reaches the copy");
        Check(Field(copy,0x34)==Copy+0x38&&Field(copy,0x6C)==Copy+0x70,"both copied native geometry headers own their copied payloads");
        Check(m.bytes==before,"original display, precision, mover and resource table remain byte exact");
    }
    Check(Glyph(5,Style::Crosshair)==4&&Glyph(6,Style::Crosshair)==4,"both native MG modes use original MountedMG resource, not generic rifle crosshair");
    for(unsigned mode:{0u,4u,8u,9u,10u,12u,14u,15u,16u,99u})Check(!Glyph(mode,Style::Crosshair)&&!Glyph(mode,Style::Dot),"unarmed/command/heal/grenade/transfer modes retain native drawing");
    m=Source();
    for(unsigned bad=0;bad<14;++bad){auto broken=m;switch(bad){
        case 0:broken.Set(At,Base+0x573F30);break;case 1:broken.Set(At+0x10,Player+4);break;case 2:broken.Set(Mover,Base+0x545A3C);break;
        case 3:broken.Set(Mover+8,Player+4);break;case 4:broken.Set(At+0x2C,std::uint32_t(2));break;case 5:broken.Set(At+0x34,At+0x3C);break;
        case 6:broken.Set(At+0x64,std::uint32_t(4));break;case 7:broken.Set(At+0x6C,At+0x74);break;case 8:broken.Set(Table+4*12,std::uint32_t(0));break;
        case 9:broken.Set(Table+4*12+4,std::uint32_t(0));break;case 10:broken.Set(Table+4*12+8,std::uint32_t(2049));break;
        case 11:broken.Set(Mover+0x68,std::uint32_t(99));break;case 12:broken.bytes.erase(At+0x137);break;default:broken.Set(At+8,std::uint32_t(0));break;}
        copy.fill(0xCA);const auto before=copy;Check(!Prepare(Base,At,Player,Style::Crosshair,Copy,broken.Reader(),copy),"broken identity/layout/payload/resource fails closed");
        Check(copy==before,"rejected replacement leaves destination copy untouched");}
    copy.fill(0xCB);const auto before=copy;unsigned reads=0;
    const auto changing=[&](auto at,void* out,auto size){if(at==Mover+0x68&&++reads==2){const std::uint32_t mode=2;std::memcpy(out,&mode,4);return true;}return m.Read(at,out,size);};
    Check(!Prepare(Base,At,Player,Style::Crosshair,Copy,changing,copy)&&copy==before,"mode change while copying cannot authorize stale shape");
    Check(!Prepare(Base,At,Player,Style::Crosshair,0xfffffff0u,m.Reader(),copy),"copy pointer cannot overflow native x86 payloads");
    Check(!Prepare(Base,At,Player,static_cast<Style>(2),Copy,m.Reader(),copy),"unknown reticle style cannot select arbitrary resources");
    std::printf("KL1 original reticle style: %u checks PASS.\n",checks);
}
