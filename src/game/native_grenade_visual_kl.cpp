#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_grenade_visual_kl.hpp"
#include "native_weapon_ownership.hpp"
#include "native_weapon_type_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_hands_kl.hpp"
#include <atomic>
namespace NativeGrenadeVisualKL {
namespace {
using namespace NativeCameraMath;
std::uintptr_t image=0;
std::atomic<std::uint64_t> frames{0},candidates{0},owned{0},suppressed{0},rejected{0};
bool ReadBytes(std::uintptr_t at,void* out,std::size_t count){SIZE_T got=0;return NativeWeaponOwnership::Address(at,count)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,count,&got)&&got==count;}
template<class T>bool Read(std::uintptr_t at,T& out){return NativeWeaponOwnership::Read(at,out,ReadBytes);}
struct Draw {
    bool active=false,candidate=false,bound=false;unsigned draws=0,index=0;
    std::uintptr_t player=0,state=0,entry=0,geometry=0,item=0,root=0,model=0;
    std::uint32_t reference=0;std::uint64_t pair=0,generation=0;
    CameraPose world{};NativeWeaponOwnership::Snapshot attachments{};
    unsigned chainCount=0;std::uintptr_t chain[8]{};NativeWeaponOwnership::GeometryLinks chainLinks[8]{};
};
thread_local Draw draw;
bool CurrentItem(){
    std::uint32_t data=0,player=0,vt=0,root=0,owner=0,model=0,reference=0;
    return Read(image+0x68760C,data)&&Read(data+0xA2C,player)&&player==draw.player&&
        Read(player,vt)&&vt==image+0x56582C&&Read(draw.item,vt)&&vt==image+0x577B74&&
        Read(draw.item+4,root)&&root==draw.root&&Read(root+0x60,owner)&&owner==draw.item&&
        Read(draw.item+0xC0,model)&&model==draw.model&&Read(draw.item+0x58,reference)&&reference==draw.reference&&
        NativeWeaponOwnership::StillAttached(image,draw.attachments,draw.index,ReadBytes);
}
bool CurrentChain(){
    if(!draw.chainCount||draw.chainCount>8)return false;
    for(unsigned i=0;i<draw.chainCount;++i){NativeWeaponOwnership::GeometryLinks current;
        if(!NativeWeaponOwnership::ReadGeometryLinks(draw.chain[i],ReadBytes,current)||
           current.parent!=draw.chainLinks[i].parent||current.owner!=draw.chainLinks[i].owner)return false;
    }
    return true;
}
}
bool InitializeVerifiedImage(std::uintptr_t verified){
    image=0;if(!verified||!NativeWeaponTypeKL::Verify(verified,ReadBytes))return false;image=verified;return true;
}
void SetFrame(std::uintptr_t player,std::uint64_t pair,bool enabled){
    draw={};if(!image||!player||!pair||!enabled)return;
    draw.active=true;draw.player=player;draw.pair=pair;++frames;
}
void ClearFrame(){draw={};}
void ObserveWorldBind(void* state,const CameraPose* source){
    draw.candidate=draw.bound=false;draw.draws=0;
    if(!draw.active||!state||!source)return;
    std::uint32_t index=0,vt=0,geometry=0;
    if(!Read(image+0x7678A4,index)||index>1||reinterpret_cast<std::uintptr_t>(source)<0x10000+0x10+index*48)return;
    const auto entry=reinterpret_cast<std::uintptr_t>(source)-0x10-index*48;
    if(!Read(entry,vt)||(vt-image!=5508832&&vt-image!=5509172&&vt-image!=5509232&&vt-image!=5509112)||!Read(entry+0xF4,geometry))return;
    ++candidates;std::uintptr_t seen[8]{};
    for(unsigned depth=0;depth<8&&geometry;++depth){
        for(unsigned n=0;n<depth;++n)if(seen[n]==geometry)return;seen[depth]=geometry;
        NativeWeaponOwnership::GeometryLinks links;if(!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,links))return;
        draw.chain[depth]=geometry;draw.chainLinks[depth]=links;
        std::uint32_t ownerVt=0;
        if(links.owner&&Read(links.owner,ownerVt)&&ownerVt==image+0x577B74){
            std::uint32_t root=0,model=0,modelVt=0,reference=0,category=0;
            if(!Read(links.owner+4,root)||root!=geometry||!Read(links.owner+0xC0,model)||!Read(model,modelVt)||
               (modelVt!=image+0x577864&&modelVt!=image+0x54E6D4)||!Read(model+NativeWeaponTypeKL::CategoryOffset,category)||
               !(category<=6||category==15)||!Read(links.owner+0x58,reference))return;
            NativeWeaponOwnership::Snapshot attached;
            if(!NativeWeaponOwnership::Capture(image,draw.player,ReadBytes,attached))return;
            for(unsigned slot=0;slot<attached.count;++slot){
                if(!attached.geometry[slot])NativeWeaponOwnership::ResolveGeometry(image,attached.records[slot].geometryRef,ReadBytes,attached.geometry[slot]);
                if(attached.geometry[slot]!=root)continue;
                draw.state=reinterpret_cast<std::uintptr_t>(state);draw.entry=entry;draw.geometry=seen[0];draw.item=links.owner;
                draw.root=root;draw.model=model;draw.reference=reference;draw.attachments=attached;draw.index=slot;
                draw.chainCount=depth+1;
                draw.candidate=CurrentItem();if(draw.candidate)++owned;return;
            }
            return;
        }
        geometry=links.parent;
    }
}
bool SuppressDraw(){
    if(!draw.active||!draw.candidate)return false;
    bool firstPerson=false;std::uint64_t pair=0,generation=0;CameraPose world;std::uint32_t geometry=0;
    if(draw.draws>=64||!NativeCameraKL::DrawStateActive(draw.state,&firstPerson)||!firstPerson||
       !NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=draw.pair||!CurrentItem()||!CurrentChain()||
       !Read(draw.entry+0xF4,geometry)||geometry!=draw.geometry||!Read(draw.state+0x8C,world)||!ValidatePose(world,.05f)||
       (draw.bound&&(generation!=draw.generation||std::memcmp(&world,&draw.world,sizeof(world))))){
        draw.candidate=false;++rejected;return false;
    }
    draw.bound=true;draw.world=world;draw.generation=generation;++draw.draws;++suppressed;return true;
}
Stats Snapshot(){return {image!=0,frames.load(),candidates.load(),owned.load(),suppressed.load(),rejected.load()};}
}
