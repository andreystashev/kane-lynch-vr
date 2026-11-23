#include "native_scope_kl.hpp"
#include "native_scope_gesture_kl.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Put(std::uintptr_t at,const T& value) {
        const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i)bytes[at+i]=p[i];
    }
    bool Read(std::uintptr_t at,void* out,std::size_t count)const {
        auto* p=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<count;++i){auto found=bytes.find(at+i);if(found==bytes.end())return false;p[i]=found->second;}return true;
    }
};
struct FileImage {
    std::vector<unsigned char> bytes;struct Section{std::uint32_t virtualSize,rva,rawSize,raw;};std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& out)const{if(at>bytes.size()||sizeof(T)>bytes.size()-at)return false;std::memcpy(&out,bytes.data()+at,sizeof(T));return true;}
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);auto size=f.tellg();
        if(size<=0||size>64*1024*1024)return false;bytes.resize(static_cast<std::size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),size);
        std::uint32_t pe=0,sig=0;std::uint16_t count=0,optional=0,magic=0;
        if(!f||!Get(0x3c,pe)||!Get(pe,sig)||sig!=0x4550||!Get(pe+6,count)||count>96||!Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10b)return false;
        for(unsigned i=0;i<count;++i){Section s{};if(!Get(pe+24+optional+40*i+8,s))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t count)const {
        if(at<0x400000)return false;const auto rva=at-0x400000;
        for(const auto& s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize) {
            const auto o=rva-s.rva;if(count>s.rawSize-o||s.raw+o>bytes.size()||count>bytes.size()-s.raw-o)return false;
            std::memcpy(out,bytes.data()+s.raw+o,count);return true;
        }
        return false;
    }
};
void OwnedState(std::uint32_t moverVtable=NativeCameraMoverKL::MouseVtable,std::uintptr_t categoryOffset=0xA4) {
    constexpr std::uint32_t image=0x400000,data=0x11000,player=0x22000,camera=0x33000,aim=0x44000,item=0x55000,root=0x66000,model=0x77000;
    Memory memory;memory.Put(image+0x8883A4,std::uint8_t(0));memory.Put(image+0x68760C,data);memory.Put(data+0xA2C,player);
    memory.Put(player,image+0x56582C);memory.Put(player+0x9A0,camera);memory.Put(camera,image+0x545A3C);
    memory.Put(player+0x9AC,aim);memory.Put(aim,image+moverVtable);memory.Put(aim+8,player);memory.Put(aim+0x9C,std::uint8_t(1));memory.Put(aim+0x68,std::uint32_t(4));
    memory.Put(player+0x358,image+0x5508B4);memory.Put(player+0x2D8,image+0x5508B4);
    NativeWeaponOwnership::HandState right;right.item=item;right.enabled=1;right.attacher=31;
    memory.Put(player+0x358+0x60,right);memory.Put(player+0x2D8+0x60,NativeWeaponOwnership::HandState{});
    memory.Put(item,image+0x577B74);memory.Put(item+4,root);memory.Put(root+0x60,item);memory.Put(item+0xC0,model);
    memory.Put(model,image+0x577864);memory.Put(model+0xA0,std::uint32_t(1));memory.Put(model+categoryOffset,std::uint32_t(15));memory.Put(item+0x58,std::uint32_t(37));
    const auto read=[&](auto at,void* out,auto count){return memory.Read(at,out,count);};
    auto value=NativeScopeKL::Snapshot(image,1000,read);
    Check(value.valid&&value.active&&value.precision&&value.player==player&&value.sourceCamera==camera&&value.item==item&&value.hand==1&&value.proofStage==8,
          "only owned active sniper plus native precision latch proves the optic context");
    memory.Put(aim,image+0x56AB44);value=NativeScopeKL::Snapshot(image,1008,read);
    Check(!value.valid&&!value.active&&value.proofStage==3&&value.moverVtable==image+0x56AB44,
          "the rendering ZFFCamera1 vtable cannot impersonate the player input mover at +9AC");
    memory.Put(aim,image+moverVtable);
    memory.Put(aim+0x9C,std::uint8_t(0));value=NativeScopeKL::Snapshot(image,1016,read);
    Check(value.valid&&!value.active,"equipping a sniper without precision does not replace ordinary VR gameplay");
    memory.Put(aim+0x9C,std::uint8_t(1));
    for(std::uint32_t category=0;category<17;++category){memory.Put(model+0xA4,category);value=NativeScopeKL::Snapshot(image,1032,read);
        Check(value.valid&&value.active==(category==15),"RMB or camera mode alone never turns pistols, rifles or grenade launchers into mono optics");}
    memory.Put(model+0xA4,std::uint32_t(15));
    for(unsigned fault=0;fault<7;++fault) {
        auto changed=memory;
        switch(fault){case 0:changed.Put(aim+8,player+4);break;case 1:changed.Put(camera,image+0x1234);break;
        case 2:changed.Put(aim+0x9C,std::uint8_t(2));break;case 3:changed.Put(image+0x8883A4,std::uint8_t(2));break;
        case 4:changed.Put(player,image+0x1234);break;case 5:changed.Put(aim,image+0x1234);break;default:changed.bytes.erase(player+0x9AC);break;}
        value=NativeScopeKL::Snapshot(image,1048,[&](auto at,void* out,auto count){return changed.Read(at,out,count);});
        Check(!value.valid&&!value.active,"foreign, UI, malformed or missing native ownership cannot authorize an optic");
    }
    unsigned refs=0;const auto changedDuringRead=[&](auto at,void* out,auto count){if(!read(at,out,count))return false;
        if(at==item+0x58&&++refs==2)++*static_cast<std::uint32_t*>(out);return true;};
    value=NativeScopeKL::Snapshot(image,1064,changedDuringRead);
    Check(!value.valid&&!value.active,"late item generation change retires the whole scope proof");
    unsigned cameras=0;const auto changedCamera=[&](auto at,void* out,auto count){if(!read(at,out,count))return false;
        if(at==player+0x9A0&&++cameras==2)++*static_cast<std::uint32_t*>(out);return true;};
    Check(!NativeScopeKL::Snapshot(image,1080,changedCamera).active,"late native camera replacement cannot retain another source's optic");
    right.enabled=0;memory.Put(player+0x358+0x60,right);value=NativeScopeKL::Snapshot(image,1096,read);
    Check(value.valid&&!value.active,"disabled hand does not retain a stale sniper optic");
}
void ActualImage(const char* path) {
    FileImage image;Check(image.Load(path),"supported PE32 loads read-only");
    const auto read=[&](auto at,void* out,auto count){return image.Read(at,out,count);};
    Check(NativeScopeKL::VerifyImage(0x400000,read),"actual PE proves native aim owner path, precision latch, display consumer and sniper category");
    for(std::uintptr_t rva:{0x1E7910u,0x231DE0u,0x231E37u,0x254428u,0x279A50u,0x1EE55Bu,0x120D05u,0x577864u+0x2BCu,0x54E6D4u+0x2BCu,0x65E3F4u,0x65E3F8u,0x584D5Cu,
                          0x1E75F4u,0x1E7602u,0x22FE80u,0x2338B0u,0x2338B2u,0x233495u,0x233497u}) {
        const auto corrupt=[&](auto at,void* out,auto count){if(!read(at,out,count))return false;
            if(at==0x400000+rva)static_cast<unsigned char*>(out)[0]^=1;return true;};
        Check(!NativeScopeKL::VerifyImage(0x400000,corrupt),"changed native scope evidence disables the optics proof");
    }
    // Derive the fixture vtable from each real native constructor, rather
    // than mirroring the production enum into invented runtime memory.
    for(const auto rva:{0x2338B2u,0x233497u}) {
        std::uint32_t constructorVtable=0;
        Check(read(0x400000+rva,&constructorVtable,4),"read current mover identity from the actual native constructor");
        std::uint32_t categoryOperand=0;
        Check(read(0x400000+0x279A52,&categoryOperand,4)&&categoryOperand==NativeWeaponTypeKL::CategoryOffset,
              "read runtime category offset from the actual getter operand, not serialized field metadata");
        OwnedState(constructorVtable-0x400000,categoryOperand);
    }
}
}
void GesturePolicy() {
    using namespace NativeCameraMath;using namespace NativeScopeGestureKL;
    const Quat identity{0,0,0,1};const Vec3 head{0,1.6f,0};
    const auto raised=Evaluate(head,identity,{.1f,1.55f,-.2f},identity);
    Check(raised.valid&&raised.enter&&raised.stay,"right controller near the face enters regardless of left support-grip state");
    const auto relaxed=Evaluate(head,identity,{0,1.6f,-.4f},identity);
    Check(relaxed.valid&&!relaxed.enter&&relaxed.stay,"scope has independent 32cm entry and45cm exit thresholds");
    Check(!Evaluate(head,identity,{0,1.6f,.2f},identity).enter,"controller behind the headset cannot enter");
    Check(!Evaluate(head,identity,{0,1.6f,-.2f},{0,1,0,0}).enter,"backwards rifle cannot open the scope");
    Gesture gesture;Check(gesture.Tick(true,true,raised,100)==Command::None,"one near-face sample cannot enter");
    Check(gesture.Tick(true,true,raised,219)==Command::None,"entry requires sustained120ms gesture");
    Check(gesture.Tick(true,true,raised,220)==Command::Enter&&gesture.requested,"sustained right controller gesture requests precision");
    Check(gesture.Tick(true,true,relaxed,230)==Command::None&&gesture.requested,"a small lowering does not flicker the optic");
    const auto lowered=Evaluate(head,identity,{0,1.1f,-.5f},identity);
    Check(gesture.Tick(true,true,lowered,240)==Command::None,"lowering begins the exit hysteresis");
    Check(gesture.Tick(true,true,lowered,420)==Command::Exit&&!gesture.requested,"lowering exits after180ms");
    gesture.Tick(true,true,raised,430);gesture.Tick(true,true,raised,550);
    Check(gesture.Tick(true,false,raised,560)==Command::Exit&&!gesture.requested,"tracking loss immediately exits owned precision");
    gesture.Tick(true,true,raised,570);gesture.Tick(true,true,raised,690);
    Check(gesture.Tick(false,true,raised,700)==Command::Exit,"UI/native action invalidation exits without a delay");
}
int main(int argc,char** argv) {GesturePolicy();OwnedState();if(argc>1)ActualImage(argv[1]);
    std::printf("KL1 native sniper optic: %u checks, %u failures; no game, headset or input opened.\n",checks,failures);return failures?1:0;}
