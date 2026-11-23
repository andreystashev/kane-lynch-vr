// Actual production sprite emit/cache routing in private CPU memory. Native
// geometry/stamp/material echoes deliberately stay IDENTICAL on foreign reuse.
// No D3D device, game, runtime, input or live detours are created.
#define main MarkerPositionFixtureMain
#include "native_objective_marker_production_test.cpp"
#undef main
namespace SpriteFixture {
std::uintptr_t entry=0;
std::uintptr_t __thiscall Emit(void*,std::uint32_t,const void*,const void*,const void*,std::uint32_t,std::uint32_t,std::uint32_t){return entry;}
void* __thiscall Position(void* record,void* output){std::memcpy(output,static_cast<unsigned char*>(record)+4,12);return output;}
void* Thunk(std::uintptr_t base,unsigned returnRva,void* function){std::vector<unsigned char> code{0x55,0x8B,0xEC};
    for(unsigned i=7;i;--i){code.push_back(0xFF);code.push_back(0x75);code.push_back(static_cast<unsigned char>(4+i*4));}
    code.push_back(0xB8);const auto target=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function));
    for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(target>>(8*i)));
    code.push_back(0xFF);code.push_back(0xD0);const auto end=code.size();code.insert(code.end(),{0xC9,0xC2,0x1C,0});
    const auto at=base+returnRva-end;std::memcpy(reinterpret_cast<void*>(at),code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),code.size());return reinterpret_cast<void*>(at);}
}
int main(int argc,char** argv){if(MarkerPositionFixtureMain(argc,argv))return 1;
    using namespace Fixture;using namespace NativeHudKL;
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(image!=0,"allocate private sprite producer image");
    SpriteFixture::entry=image+0x10000;const auto entry=SpriteFixture::entry,camera=image+0x20000,ui=image+0x21000;
    originalSpriteEmit=SpriteFixture::Emit;originalMarkerPosition=SpriteFixture::Position;
    Put(image+0x7678A0,std::uint32_t(0));Put(image+0x7678A4,std::uint32_t(0));Put(entry,std::uint32_t(image+0x540EAC));
    std::array<float,10> payload{};payload[7]=payload[8]=.6f;const std::uint32_t header[]={1,0,std::uint32_t(reinterpret_cast<std::uintptr_t>(payload.data()))};
    const std::uint32_t material[]={1,32,32,5,0};for(unsigned slot=0;slot<2;++slot){Put(entry+0x100+slot*4,std::uint32_t(reinterpret_cast<std::uintptr_t>(header)));Put(entry+0x130+slot*4,std::uint32_t(71));std::memcpy(reinterpret_cast<void*>(entry+0x108+slot*20),material,sizeof(material));}
    Put(camera+0x14C,std::uint32_t(ui));Put(ui,std::uint32_t(image+0x545A3C));Put(image+0x545A3C+0x244,std::uint32_t(image+0xF5BD0));Put(ui+0x38,1.f);Put(ui+0x3C,1.f);Put(ui+0x54,.5f);
    std::array<unsigned char,0x64> first{},second{};const NativeCameraMath::Vec3 target{100,20,300},other{400,50,600},sprite{0,0,6};
    Put(reinterpret_cast<std::uintptr_t>(first.data())+4,target);Put(reinterpret_cast<std::uintptr_t>(first.data())+0x1C,std::uint32_t(26));
    Put(reinterpret_cast<std::uintptr_t>(second.data())+4,other);Put(reinterpret_cast<std::uintptr_t>(second.data())+0x1C,std::uint32_t(26));
    const auto emit=reinterpret_cast<SpriteEmit>(SpriteFixture::Thunk(image,0x253CF5,reinterpret_cast<void*>(&SpriteEmitHook)));
    const auto owned=[&](void* record){NativeCameraMath::Vec3 point;MarkerPositionHook(record,nullptr,&point);return emit(reinterpret_cast<void*>(0x123400),0x27,header,nullptr,&sprite,10,1,0);};
    BeginNativeMarkers(0x11000000,reinterpret_cast<void*>(0x123400),reinterpret_cast<void*>(camera));Check(owned(first.data())==entry,"exact owned native caller captures an objective packet");EndNativeMarkers();
    MarkerPacket packet;Check(MatchMarker(entry,packet)&&!packet.ambiguous&&packet.target.x==100,"deferred original objective still owns its WORLD target");
    ReticlePacket reticle;reticle.player=0x11000000;std::memcpy(reticle.material,material,sizeof(material));Check(ReticleGeometry(reinterpret_cast<std::uintptr_t>(header),reticle)&&CaptureReticle(entry,reticle),"seed an independently proved reticle source at the same native entry");
    std::uintptr_t player=0;Check(MatchReticle(entry,player),"reticle proof is current before the next native producer");
    std::array<unsigned char,0x180> unchanged{};std::memcpy(unchanged.data(),reinterpret_cast<void*>(entry),unchanged.size());
    Check(SpriteEmitHook(reinterpret_cast<void*>(0x777700),nullptr,0x27,header,nullptr,&sprite,10,1,0)==entry,"foreign weapon-menu/UI emit preserves its exact native return value");
    Check(!MatchMarker(entry,packet)&&!MatchReticle(entry,player),"same-pointer/same-stamp/same-material UI reuse cannot inherit old WORLD or reticle provenance");
    Check(!std::memcmp(unchanged.data(),reinterpret_cast<void*>(entry),unchanged.size()),"retirement changes only private provenance, never native entry bytes");
    BeginNativeMarkers(0x11000000,reinterpret_cast<void*>(0x123400),reinterpret_cast<void*>(camera));owned(first.data());owned(second.data());EndNativeMarkers();
    Check(MatchMarker(entry,packet)&&packet.ambiguous,"same native marker producer still rejects ambiguous multi-target reuse");
    BeginNativeMarkers(0x11000000,reinterpret_cast<void*>(0x123400),reinterpret_cast<void*>(camera));owned(first.data());EndNativeMarkers();
    Check(MatchMarker(entry,packet)&&!packet.ambiguous,"next proved producer reacquires normal goal rendering after ambiguity");
    Put(image+0x7678A0,std::uint32_t(1));Put(image+0x7678A4,std::uint32_t(1));BeginNativeMarkers(0x11000000,reinterpret_cast<void*>(0x123400),reinterpret_cast<void*>(camera));owned(first.data());EndNativeMarkers();
    Put(image+0x7678A0,std::uint32_t(0));SpriteEmitHook(reinterpret_cast<void*>(0x777700),nullptr,0x27,header,nullptr,&sprite,10,1,0);
    Check(MatchMarker(entry,packet)&&packet.slot==1,"reusing source slot0 cannot revoke the independently queued slot1");
    Check(Snapshot().spriteMarkerRetired>=2&&Snapshot().spriteReticleRetired>=1,"new counters distinguish source retirement from projection rejection");
    wristSource.player=0x11000000;wristSource.element=WristHudElement::WeaponWheel;
    for(unsigned slot=0;slot<2;++slot){
        Put(image+0x7678A0,slot);
        for(unsigned i=0;i<10;++i){
            // All ten addresses used the same bucket in the old eight-probe table.
            const auto spriteEntry=entry+i*0x80000;
            Put(spriteEntry,std::uint32_t(image+0x540EAC));
            Put(spriteEntry+0x100+slot*4,std::uint32_t(reinterpret_cast<std::uintptr_t>(header)));
            Put(spriteEntry+0x130+slot*4,std::uint32_t(72));
            std::memcpy(reinterpret_cast<void*>(spriteEntry+0x108+slot*20),material,sizeof(material));
            ObserveWristSprite(spriteEntry,true,13,nullptr);
        }
    }
    for(unsigned slot=0;slot<2;++slot){
        Put(image+0x7678A4,slot);
        for(unsigned i=0;i<10;++i)
            Check(MatchWristSprite(entry+i*0x80000).entry==entry+i*0x80000,
                  "a queued wrist sprite keeps its placement despite colliding addresses");
    }
    Put(image+0x7678A0,0u);ObserveWristSprite(entry,false,13,nullptr);
    Put(image+0x7678A4,0u);Check(!MatchWristSprite(entry).entry,"foreign reuse clears the wrist packet for its slot");
    Put(image+0x7678A4,1u);const auto wrist=MatchWristSprite(entry);
    Check(wrist.entry==entry,"foreground reuse cannot clear the other render slot");
    wristDrawing=&wrist;
    Check(IndexedHook(nullptr,nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK,
          "an unexpected indexed path cannot draw a qualified wheel at the screen centre");
    wristDrawing=nullptr;wristSource={};
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);std::printf("Native sprite provenance CPU producer: %u checks PASS.\n",checks);
}
