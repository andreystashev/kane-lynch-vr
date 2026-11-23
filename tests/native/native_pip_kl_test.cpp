#include "native_pip_kl.hpp"
#include "native_pip_render_kl.hpp"
#include <cstdio>
#include <map>
#include <cstring>

namespace {
constexpr std::uint32_t Image=0x400000,Data=0x10000000,Manager=0x11000000,Frontend=0x12000000;
constexpr std::uint32_t Controller=0x13000000,Camera=0x14000000,Geometry=0x15000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> data;
    template<class T>void Set(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i)data[at+i]=p[i];}
    bool Read(std::uintptr_t at,void* output,std::size_t count)const{
        auto* p=static_cast<unsigned char*>(output);for(std::size_t i=0;i<count;++i){const auto found=data.find(at+i);if(found==data.end())return false;p[i]=found->second;}return true;
    }
    auto Reader()const{return [&](auto at,void* output,auto count){return Read(at,output,count);};}
    Memory(){
        using namespace NativePipKL;
        Set(Image+GameDataRva,Data);Set(Data+0x154C,Manager);Set(Manager,Frontend);Set(Manager+12,std::uint32_t(0));
        Set(Frontend,std::uint32_t(Image+FrontendVtableRva));Set(Frontend+0x914,Controller);
        Set(Controller,std::uint32_t(Image+ControllerVtableRva));Set(Controller+0x544,std::uint8_t(1));
        Set(Controller+0x4B8,std::uint32_t(6));Set(Controller+0x24,Camera);Set(Camera,std::uint32_t(Image+CameraVtableRva));
        Set(Camera+4,Geometry);Set(Geometry+0x60,Camera);const std::int32_t rectangle[]={48,34,752,546};Set(Controller+0x4A8,rectangle);
        const unsigned char accessor[]={0x8B,0x44,0x24,4,0x8B,0x89,0x4C,0x15,0,0,0x50,0xE8,0x30,0x5F,0,0,0x8B,0x80,0x14,9,0,0,0xC2,4,0};
        const unsigned char index[]={0x8B,0x44,0x24,4,0x83,0xF8,1,0x77,9,0x8D,4,0x40,0x8B,4,0x81,0xC2,4,0};
        const unsigned char constructor[]={0xC7,6,0xA4,0x3C,0x96,0},activate[]={0xC6,0x86,0x44,5,0,0,1},deactivate[]={0xC6,0x86,0x44,5,0,0,0};
        Set(Image+0x2704B0,accessor);Set(Image+0x2763F0,index);Set(Image+0x23641A,constructor);
        Set(Image+0x236CB8,activate);Set(Image+0x236A38,deactivate);
        Set(Image+ControllerVtableRva,std::uint32_t(Image+0x1DAAF0));Set(Image+ControllerVtableRva+0xE0,std::uint32_t(Image+0x236D70));
        const unsigned char producer[]={0x6A,0xFF,0x68,0x3B,0x10,0x8F,0,0x64,0xA1,0,0,0,0,
            0x50,0xB8,0x74,0xD1,3,0,0xE8,0x98,0xB5,0x3F,0};
        const unsigned char source[]={0x8B,0x87,0xF4,0,0,0,0x89,0x84,0x24,0x94,0,0,0,
            0x8B,0x40,0x60,0x83,0xC4,0x18,0x50};
        const unsigned char epilogue[]={0x81,0xC4,0x80,0xD1,3,0,0xC2,0x18,0};
        Set(Image+0xBF600,producer);Set(Image+0xBF6C9,source);Set(Image+0xC1B79,epilogue);
        Set(Image+NativePipRenderKL::CameraEntryVtableRva+0x10,std::uint32_t(Image+NativePipRenderKL::ProducerRva));
        const unsigned char overlayActivate[]={0x8B,0x96,0x28,6,0,0,0xC6,0x86,0x38,5,0,0,1};
        const unsigned char overlayDeactivate[]={0xC6,0x86,0x38,5,0,0,0};
        Set(Image+0x2368D2,overlayActivate);Set(Image+0x23695B,overlayDeactivate);
        Set(Image+NativePipRenderKL::OverlayCameraVtableRva+0x220,std::uint32_t(Image+0xF6220));
        Set(Image+NativePipRenderKL::OverlayCameraVtableRva+0x228,std::uint32_t(Image+0xF5770));
    }
};
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}}
}
int main(){
    using namespace NativePipKL;
    Memory memory;Check(VerifyImage(Image,memory.Reader()),"pinned native owner accessor and active-field writes match");
    const auto original=memory.data;auto state=Snapshot(Image,memory.Reader());
    Check(state.valid&&state.active&&state.controller==Controller&&state.camera==Camera&&state.geometry==Geometry&&state.mode==6,
          "registered exact PIP resolves its own camera and packet geometry");
    Check(state.left==48&&state.top==34&&state.right==752&&state.bottom==546,"native display rectangle remains exact");
    Check(original==memory.data,"observation performs no writes");
    for(unsigned mode=1;mode<=6;++mode){memory.Set(Controller+0x4B8,mode);Check(Snapshot(Image,memory.Reader()).active,"each native active mode qualifies");}
    memory=Memory{};memory.Set(Controller+0x544,std::uint8_t(0));memory.data.erase(Camera);state=Snapshot(Image,memory.Reader());
    Check(state.valid&&!state.active&&!state.camera,"inactive PIP never dereferences stale camera");
    memory=Memory{};memory.Set(Controller+0x4B8,std::uint32_t(0));Check(!Snapshot(Image,memory.Reader()).active,"idle mode does not qualify");
    for(const std::uintptr_t at:{Image+GameDataRva,std::uintptr_t(Data+0x154C),std::uintptr_t(Manager),std::uintptr_t(Frontend+0x914)}){
        memory=Memory{};memory.Set(at,std::uint32_t(0));state=Snapshot(Image,memory.Reader());Check(state.valid&&!state.active,"absent native owner is inactive");
    }
    for(const auto at:{Frontend,Controller,Camera}){
        memory=Memory{};memory.Set(at,std::uint32_t(0x12345678));Check(!Snapshot(Image,memory.Reader()).valid,"unrelated class cannot establish ownership");
    }
    memory=Memory{};memory.Set(Geometry+0x60,Camera+4);Check(!Snapshot(Image,memory.Reader()).active,"camera geometry backlink must match");
    memory=Memory{};memory.Set(Controller+0x544,std::uint8_t(2));Check(!Snapshot(Image,memory.Reader()).valid,"unknown registered flag fails closed");
    memory=Memory{};memory.Set(Controller+0x4B8,std::uint32_t(7));Check(!Snapshot(Image,memory.Reader()).valid,"unknown mode fails closed");
    memory=Memory{};unsigned reads=0;
    const auto replaced=[&](auto at,void* output,auto size){if(at==Frontend+0x914&&++reads==2)memory.Set(at,std::uint32_t(0));return memory.Read(at,output,size);};
    Check(!Snapshot(Image,replaced).valid,"replacement between observations is rejected");
    memory=Memory{};Check(Snapshot(Image,memory.Reader(),1).valid&&!Snapshot(Image,memory.Reader(),1).active,"unassigned second player is inactive");
    Check(!Snapshot(Image,memory.Reader(),2).valid,"player index is bounded");
    memory.Set(Image+0x2704B0,std::uint8_t(0x90));Check(!VerifyImage(Image,memory.Reader()),"changed native accessor disables proof");
    Check(!Snapshot(0,memory.Reader()).valid&&!Snapshot(0xffff0000u,memory.Reader()).valid,"invalid image address is rejected");
    memory=Memory{};
    Check(NativePipRenderKL::VerifyImage(Image,memory.Reader()),"PIP camera producer slot/source/six-argument return are pinned");
    const std::uintptr_t producerProof[]={Image+0xBF600,Image+0xBF6C9,Image+0xC1B79,Image+NativePipRenderKL::CameraEntryVtableRva+0x10};
    for(const auto at:producerProof){
        memory=Memory{};memory.Set(at,std::uint8_t(0x90));
        Check(!NativePipRenderKL::VerifyImage(Image,memory.Reader()),"changed producer ABI or source disables suppression proof");
    }
    memory=Memory{};
    constexpr std::uintptr_t Entry=0x16000000;
    memory.Set(Entry,std::uint32_t(Image+NativePipRenderKL::CameraEntryVtableRva));memory.Set(Entry+0xF4,Geometry);
    auto entry=NativePipRenderKL::CameraEntry(Image,Entry,memory.Reader());
    Check(entry.valid&&entry.camera==Camera&&entry.geometry==Geometry,"camera producer resolves exact camera and geometry");
    NativeCinematicKL::State cinematic;cinematic.valid=true;state=Snapshot(Image,memory.Reader());
    constexpr std::uintptr_t View=0x17000000;
    const auto suppress=[&](bool enabled=true,std::uintptr_t local=Camera+0x100){return NativePipRenderKL::ShouldSuppress(enabled,state,cinematic,local,entry,View,View,false,0);};
    for(unsigned mode=1;mode<=6;++mode){state.mode=mode;Check(suppress(),"owned auxiliary inset modes may skip only their render producer");}
    state.mode=0;Check(!suppress(),"idle PIP mode is preserved");state.mode=7;Check(!suppress(),"unknown PIP mode is preserved");state.mode=6;
    Check(!suppress(false),"disabled option keeps native producer");Check(!suppress(true,Camera),"local and native-scope camera is preserved");
    Check(!suppress(true,0),"unknown local camera is preserved");
    Check(NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera+4,entry,View,View,true,0),"auxiliary first camera may skip producer which ignores first-camera argument");
    Check(!NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera+4,entry,View,View,false,Geometry),"recursive render pass retains native producer");
    Check(!NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera+4,entry,View+4,View,false,0),"unrelated render view retains native producer");
    Check(!NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera+4,entry,0,0,false,0),"unknown render view retains native producer");
    for(unsigned nativeMode=1;nativeMode<=6;++nativeMode)
        Check(!NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera+4,entry,View,View,false,0,nativeMode),
              "native maps inventory and menus preserve auxiliary camera producers");
    cinematic.full=1;cinematic.camera=Camera;Check(!suppress(),"full authored cut camera including PIP composition is preserved");
    cinematic.camera=Camera+4;Check(!suppress(),"other full authored cinematics also preserve PIP producer");
    cinematic.full=0;cinematic.valid=false;Check(!suppress(),"unknown cinematic context is preserved");cinematic.valid=true;
    state.left=0;state.top=0;Check(!suppress(),"full-screen rectangle is preserved");state.left=48;state.top=34;
    state.right=48;Check(!suppress(),"empty inset rectangle is preserved");state.right=752;
    state.valid=false;Check(!suppress(),"invalid PIP observation is preserved");state.valid=true;
    state.active=false;Check(!suppress(),"unregistered PIP producer is preserved");state.active=true;
    entry.geometry=Geometry+4;Check(!suppress(),"unrelated geometry is preserved");entry.geometry=Geometry;
    entry.camera=Camera+4;Check(!suppress(),"unrelated auxiliary camera is preserved");
    memory.Set(Camera+4,Geometry+4);Check(!NativePipRenderKL::CameraEntry(Image,Entry,memory.Reader()).valid,"producer camera backlink mismatch is rejected");
    memory=Memory{};state=Snapshot(Image,memory.Reader());
    constexpr std::uint32_t Overlay=0x18000000,OverlayGeometry=0x19000000;
    memory.Set(Controller+0x538,std::uint8_t(1));memory.Set(Controller+0x628,Overlay);
    memory.Set(Overlay,std::uint32_t(Image+NativePipRenderKL::OverlayCameraVtableRva));memory.Set(Overlay+4,OverlayGeometry);
    memory.Set(OverlayGeometry+0x60,Overlay);
    auto overlay=NativePipRenderKL::OverlayCamera(Image,state,memory.Reader());
    Check(overlay.valid&&overlay.camera==Overlay&&overlay.geometry==OverlayGeometry,"mode6 registered overlay camera owns the circular PIP composition");
    memory.Set(Entry,std::uint32_t(Image+NativePipRenderKL::CameraEntryVtableRva));memory.Set(Entry+0xF4,OverlayGeometry);
    entry=NativePipRenderKL::CameraEntry(Image,Entry,memory.Reader());
    auto composite=state;composite.camera=Overlay;composite.geometry=OverlayGeometry;
    Check(entry.valid&&NativePipRenderKL::ShouldSuppress(true,composite,cinematic,Camera,entry,View,View,true,0),
          "exact owned derived overlay camera may suppress composition before the main camera");
    Check(!NativePipRenderKL::ShouldSuppress(true,state,cinematic,Camera,entry,View,View,false,0),
          "overlay class alone never authorizes suppression without its exact ownership");
    memory.Set(Controller+0x538,std::uint8_t(0));
    Check(!NativePipRenderKL::OverlayCamera(Image,state,memory.Reader()).valid,"inactive overlay camera is preserved");
    memory.Set(Controller+0x538,std::uint8_t(1));state.mode=5;
    Check(!NativePipRenderKL::OverlayCamera(Image,state,memory.Reader()).valid,"other PIP modes never authorize stale composition camera");state.mode=6;
    memory.Set(Overlay,std::uint32_t(Image+NativePipKL::CameraVtableRva));
    Check(!NativePipRenderKL::OverlayCamera(Image,state,memory.Reader()).valid,"unrelated camera at overlay field is rejected");
    memory.Set(Overlay,std::uint32_t(Image+NativePipRenderKL::OverlayCameraVtableRva));memory.Set(OverlayGeometry+0x60,Camera);
    Check(!NativePipRenderKL::OverlayCamera(Image,state,memory.Reader()).valid,"overlay camera backlink must match");
    memory.Set(OverlayGeometry+0x60,Overlay);unsigned overlayReads=0;
    const auto overlayReplaced=[&](auto at,void* out,auto size){if(at==Controller+0x628&&++overlayReads==2)memory.Set(at,std::uint32_t(0));return memory.Read(at,out,size);};
    Check(!NativePipRenderKL::OverlayCamera(Image,state,overlayReplaced).valid,"concurrent overlay replacement is rejected");
    std::printf("Pinned native PIP observation: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
