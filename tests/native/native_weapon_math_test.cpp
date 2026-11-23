#include "native_weapon_math.hpp"
#include "native_weapon_ownership.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>

namespace {
using namespace NativeCameraMath;
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL: %s\n",message);}}
bool Near(Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<.0001f;}
bool Near(const CameraPose& a,const CameraPose& b){return Near(a.position,b.position)&&Near(a.basis.forward,b.basis.forward)&&Near(a.basis.up,b.basis.up)&&Near(a.basis.right,b.basis.right);}
CameraPose Pose(float yaw,float pitch,Vec3 position) {
    const Quat y{0,std::sin(yaw/2),0,std::cos(yaw/2)},p{std::sin(pitch/2),0,0,std::cos(pitch/2)};
    const auto q=Multiply(y,p);CameraPose pose;
    pose.basis={Rotate(q,pose.basis.forward),Rotate(q,pose.basis.up),Rotate(q,pose.basis.right)};
    pose.position=position;return pose;
}
void AnimatedVisibleBarrel() {
    const auto raw=Pose(.2f,-.1f,{100,140,-200}),marker=Pose(.7f,.3f,{20,4,1});
    for(int yaw=-12;yaw<=12;yaw+=3)for(int turn=-2;turn<=2;++turn) {
        const float half=yaw*.0174532925f*.5f;const Quat pump{0,std::sin(half),0,std::cos(half)};
        CameraPose mesh=raw;mesh.position=Add(raw.position,{2,1,0});
        mesh.basis={Rotate(pump,raw.basis.forward),Rotate(pump,raw.basis.up),Rotate(pump,raw.basis.right)};
        const auto final=Pose(float(turn)*.5f,.15f,{110,142,-195});
        Vec3 direction{};float angle=0;
        Check(NativeWeaponMath::VisibleBarrel(raw,final,mesh,direction,angle),"small native pump/recoil yaw supplies a bounded visible barrel axis");
        CameraPose drawn;
        Check(NativeWeaponMath::Remap(raw,final,mesh,drawn)&&Dot(direction,drawn.basis.right)>.99999f,
              "shot ray and actual remapped model +X agree through the same wrist transform");
        CameraPose oldAttachment,newAttachment;Vec3 oldOrigin,newOrigin,oldTarget,newTarget;
        Check(NativeWeaponMath::Shot(raw,final,mesh,marker,100,oldAttachment,oldOrigin,oldTarget)&&
              NativeWeaponMath::Shot(raw,final,mesh,marker,100,newAttachment,newOrigin,newTarget,&direction),
              "animated barrel fixture retains the ordinary native muzzle composition");
        Check(Near(oldOrigin,newOrigin)&&Near(oldAttachment,newAttachment),"visible-axis correction changes no physical muzzle or mesh attachment");
        Vec3 actual;Check(NativeArmMath::Unit(Subtract(newTarget,newOrigin),actual)&&Dot(actual,direction)>.99999f,
              "corrected target starts at the actual muzzle along the visible pump barrel");
    }
    Vec3 unchanged{7,8,9};float angle=0;const auto far=Pose(1.f,0,{});
    Check(!NativeWeaponMath::VisibleBarrel(Pose(0,0,{}),Pose(0,0,{}),far,unchanged,angle)&&Near(unchanged,{7,8,9}),
          "unproved large axis mismatch keeps caller fallback instead of guessing a marker direction");
}
void PhysicalHeldAttachment() {
    const auto final=Pose(.7f,.2f,{110,150,-90});
    CameraPose marker=Pose(0,0,{60,2,0}),reference;
    marker.basis={{0,0,1},{0,1,0},{1,0,0}};
    Vec3 referenceMuzzle{};
    for(int idle=-90;idle<=90;idle+=15) {
        const auto item=Pose(idle*.0174532925f,0,{100,140,-100});
        const Vec3 grip{-5,0,0};auto raw=item;
        raw.position=Add(item.position,NativeRigMath::Transform(item.basis,grip));
        // Wrist orientation is independently animated, including a sideways
        // idle stance. It cannot rotate the physical VR barrel away again.
        raw.basis=Pose(-.3f,.1f,{}).basis;CameraPose held;
        Check(NativeWeaponMath::HeldAttachment(raw,final,item,marker,1,100,held),"owned model grip/muzzle axis proves its physical held attachment");
        Check(Dot(held.basis.right,final.basis.right)>.99999f,"idle/aim animation cannot steer the visible barrel away from the physical ray");
        Check(Near(Add(held.position,NativeRigMath::Transform(held.basis,grip)),final.position),"model grip stays attached to the solved wrist");
        const auto muzzle=NativeRigMath::ToWorld(held,{marker.basis,marker.position}).position;
        if(idle==-90){reference=held;referenceMuzzle=muzzle;}
        Check(Near(held,reference)&&Near(muzzle,referenceMuzzle),"switching native idle to firing pose does not jump gun or physical muzzle");
        const auto partLocal=Pose(.1f,0,{10,1,0});const auto nativePart=NativeRigMath::ToWorld(item,{partLocal.basis,partLocal.position});CameraPose carried;
        Check(NativeWeaponMath::CarryPart(item,held,nativePart,carried)&&Near(NativeRigMath::ToModel(held,carried),partLocal),"all child parts retain authored offsets under one canonical item attachment");
    }
    auto native=Pose(0,0,{}),raw=native;CameraPose left;
    Check(NativeWeaponMath::HeldAttachment(raw,final,native,marker,0,100,left)&&
          Dot(left.basis.right,final.basis.right)>.99999f&&Dot(left.basis.up,final.basis.up)<-.99999f,
          "left weapon keeps a forward barrel and mirrors native thumb/palm anatomy only");
    marker.position={0,0,60};const auto before=left;
    Check(!NativeWeaponMath::HeldAttachment(raw,final,native,marker,1,100,left)&&Near(before,left),"orthogonal unproved marker axis preserves the caller fallback");
    marker.position={60,2,0};
    raw.basis=Pose(.2f,0,{}).basis;
    const Vec3 palm{0,0,5};
    CameraPose wristPivot,palmPivot;
    Check(NativeWeaponMath::HeldAttachment(raw,final,native,marker,1,100,wristPivot)&&
          NativeWeaponMath::HeldAttachment(raw,final,native,marker,1,100,palmPivot,nullptr,nullptr,&palm),
          "measured palm can refine an owned weapon attachment");
    const auto authoredPalm=NativeRigMath::ToWorld(raw,{{},palm}).position;
    const auto meshPalm=NativeRigMath::Inverse(native.basis,Subtract(authoredPalm,native.position));
    const auto carriedPalm=Add(palmPivot.position,NativeRigMath::Transform(palmPivot.basis,meshPalm));
    const auto trackedPalm=Add(final.position,NativeRigMath::Transform(final.basis,palm));
    Check(Near(carriedPalm,trackedPalm)&&Near(palmPivot.basis.right,wristPivot.basis.right)&&
          NativeArmMath::Length(Subtract(palmPivot.position,wristPivot.position))<=2.5f,
          "grip follows the tracked palm within the correction bound without steering the barrel");
    // Actual retained pistol source: item root barrel+Z, model mesh barrel+X
    // behind a quarter-turn child frame, marker.right=-Z (not the fire ray).
    marker.basis={{1,4.37114e-8f,0},{-4.37114e-8f,1,2.23305e-8f},{0,2.23305e-8f,-1}};
    marker.position={-.0526923f,13.281f,17.2654f};const Vec3 actualGrip{2.06871f,5.49869f,-12.37699f};
    for(int side=0;side<2;++side)for(int yaw=-150;yaw<=150;yaw+=30) {
        const auto item=Pose(yaw*.0174532925f,.2f,{100,150,400});auto wrist=item;
        wrist.position=Add(item.position,NativeRigMath::Transform(item.basis,actualGrip));
        CameraPose held;Basis localAim;unsigned proof=0;
        Check(NativeWeaponMath::HeldAttachment(wrist,final,item,marker,side,100,held,&proof,&localAim)&&proof==5,
              "actual native reversed marker proves item-Z barrel rather than invented item-X");
        Check(Dot(NativeRigMath::Transform(held.basis,localAim.right),final.basis.right)>.99999f&&
              Dot(held.basis.forward,final.basis.right)>.99999f,
              "actual item-Z barrel maps to physical controller fingers for either hand and arbitrary actor yaw");
        Check(Near(Add(held.position,NativeRigMath::Transform(held.basis,actualGrip)),final.position),
              "muzzle-axis mapping preserves the independently measured model grip point");
    }
}
struct Memory {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    template<class T> void Put(std::uintptr_t address,const T& value) {
        const auto* source=reinterpret_cast<const std::uint8_t*>(&value);
        for(unsigned i=0;i<sizeof(value);++i)bytes[address+i]=source[i];
    }
    bool Read(std::uintptr_t address,void* value,std::size_t size) const {
        auto* destination=static_cast<std::uint8_t*>(value);
        for(std::size_t i=0;i<size;++i) {
            const auto found=bytes.find(address+i);
            if(found==bytes.end())return false;
            destination[i]=found->second;
        }
        return true;
    }
};
void AdjacentNativeOwnershipReads() {
    using namespace NativeWeaponOwnership;
    Memory memory;
    const std::uintptr_t geometry=0x23000,hand=0x34000;
    GeometryLinks link;link.parent=0x24000;link.owner=0x45000;
    link.reserved[0]=0xfefefefe;link.reserved[1]=0xabababab;link.reserved[2]=0x01020304;
    HandState state;state.item=0x45000;state.enabled=1;state.attacher=31;
    state.reserved[0]=0xff;state.reserved[1]=0xab;state.reserved[2]=0x71;
    memory.Put(geometry+0x50,link);memory.Put(hand+0x60,state);
    unsigned reads=0;
    const auto reader=[&](std::uintptr_t address,void* output,std::size_t bytes){++reads;return memory.Read(address,output,bytes);};
    constexpr std::size_t pageBytes=0x1000,entrySpan=0xF8;
    constexpr std::uintptr_t entry=0x58000+pageBytes-entrySpan;
    std::array<std::uint8_t,entrySpan> storage{};memory.Put(entry,storage);
    memory.Put(entry,std::uint32_t{0x540000});
    memory.Put(entry+0xF4,static_cast<std::uint32_t>(geometry));
    DrawEntryFields draw;
    Check(ReadDrawEntryOnPage(entry,pageBytes,reader,draw)&&draw.vtable==0x540000&&
          draw.geometry==geometry&&reads==1,
          "native draw identity uses one exact span ending at the page boundary");
    reads=0;
    const auto saved=draw;
    Check(!ReadDrawEntryOnPage(entry+1,pageBytes,reader,draw)&&
          !ReadDrawEntryOnPage(entry,0,reader,draw)&&!ReadDrawEntryOnPage(entry,3000,reader,draw)&&
          !ReadDrawEntryOnPage(0xfffffff0u,pageBytes,reader,draw)&&reads==0&&
          draw.vtable==saved.vtable&&draw.geometry==saved.geometry,
          "page crossings, invalid page sizes and x86 overflow leave scalar fields untouched");
    constexpr std::uintptr_t split=0x59000+pageBytes-0x40;
    memory.Put(split,std::uint32_t{0x540001});memory.Put(split+0xF4,std::uint32_t{0x640002});
    std::uint32_t vtable=0,root=0;
    Check(!ReadDrawEntryOnPage(split,pageBytes,reader,draw)&&Read(split,vtable,reader)&&
          Read(split+0xF4,root,reader)&&reads==2&&vtable==0x540001&&root==0x640002,
          "entry identities spanning pages retain the exact scalar read path");
    constexpr std::uintptr_t sparse=0x5B000;
    memory.Put(sparse,std::uint32_t{0x540002});memory.Put(sparse+0xF4,std::uint32_t{0x640003});
    reads=0;
    Check(!ReadDrawEntryOnPage(sparse,pageBytes,reader,draw)&&draw.vtable==saved.vtable&&
          draw.geometry==saved.geometry&&Read(sparse,vtable,reader)&&Read(sparse+0xF4,root,reader)&&
          reads==3&&vtable==0x540002&&root==0x640003,
          "failed combined read leaves scalar fallback and values intact");
    reads=0;
    std::uint32_t parent=0,owner=0;
    Check(Read(geometry+0x50,parent,reader)&&Read(geometry+0x60,owner,reader)&&reads==2,
          "legacy geometry fields require two native reads");
    GeometryLinks actual;
    reads=0;
    Check(ReadGeometryLinks(geometry,reader,actual)&&actual.parent==parent&&actual.owner==owner&&reads==1,
          "adjacent geometry fields preserve exact native values with one read");
    std::uint32_t item=0,attacher=0;std::uint8_t enabled=0;
    reads=0;
    Check(Read(hand+0x60,item,reader)&&Read(hand+0x64,enabled,reader)&&Read(hand+0x68,attacher,reader)&&reads==3,
          "legacy native hand selection requires three field reads");
    HandState selected;reads=0;
    Check(ReadHandState(hand,reader,selected)&&selected.item==item&&selected.enabled==enabled&&
          selected.attacher==attacher&&reads==1,"batched hand read preserves item, byte-enabled flag and attacher without interpreting padding");
    const GeometryLinks sentinel=actual;const HandState handSentinel=selected;
    const auto partial=[&](std::uintptr_t,void* output,std::size_t bytes){++reads;std::memset(output,0xa5,bytes/2);return false;};
    Check(!ReadGeometryLinks(geometry,partial,actual)&&!std::memcmp(&actual,&sentinel,sizeof(actual)),
          "partial geometry read cannot publish a mixed parent or owner");
    Check(!ReadHandState(hand,partial,selected)&&!std::memcmp(&selected,&handSentinel,sizeof(selected)),
          "partial hand read cannot retain a new item with old activation or attachment");
    reads=0;
    for(const std::uintptr_t bad:{std::uintptr_t(0),std::uintptr_t(0xff00),std::uintptr_t(0xfffffff0u),
                                (std::numeric_limits<std::uintptr_t>::max)()}) {
        Check(!ReadGeometryLinks(bad,reader,actual)&&!ReadHandState(bad,reader,selected)&&reads==0,
              "null, low, overflowing and host-width addresses fail before native reads");
    }
    Check(!ReadGeometryLinks(0x56000,reader,actual)&&!ReadHandState(0x56000,reader,selected),
          "unmapped native controls never establish ownership");
    GeometryLinks empty{};memory.Put(geometry+0x50,empty);HandState disabled{};memory.Put(hand+0x60,disabled);
    Check(ReadGeometryLinks(geometry,reader,actual)&&!actual.parent&&!actual.owner&&
          ReadHandState(hand,reader,selected)&&!selected.item&&!selected.enabled&&!selected.attacher,
          "zero native sentinels are copied exactly rather than creating a positive owner");

    // Compare the existing hard-bounded/cycle-checked chain's selected root
    // using the old scalar reads and the production batched field reader.
    const std::uintptr_t base=0x60000,target=0x88000;
    for(unsigned n=0;n<8;++n){GeometryLinks node;node.parent=n==7?0:static_cast<std::uint32_t>(base+(n+1)*0x100);
        node.owner=n==7?static_cast<std::uint32_t>(target):0;memory.Put(base+n*0x100+0x50,node);}
    const auto walk=[&](bool batch,unsigned& calls,std::uintptr_t& found){
        calls=0;std::uintptr_t at=base,seen[8]{};
        const auto counted=[&](std::uintptr_t address,void* output,std::size_t bytes){++calls;return memory.Read(address,output,bytes);};
        for(unsigned depth=0;depth<8&&at;++depth){
            for(unsigned i=0;i<depth;++i)if(seen[i]==at)return false;
            seen[depth]=at;GeometryLinks node;
            if(batch){if(!ReadGeometryLinks(at,counted,node))return false;}
            else if(!Read(at+0x60,node.owner,counted))return false;
            if(node.owner==target){found=at;return true;}
            if(!batch&&!Read(at+0x50,node.parent,counted))return false;
            at=node.parent;
        }
        return false;
    };
    unsigned oldReads=0,newReads=0;std::uintptr_t oldRoot=0,newRoot=0;
    Check(walk(false,oldReads,oldRoot)&&walk(true,newReads,newRoot)&&oldRoot==newRoot&&oldRoot==base+7*0x100&&
          oldReads==15&&newReads==8,"eight-level child ownership selects the same root with fifteen to eight native reads");
    GeometryLinks cycle;cycle.parent=static_cast<std::uint32_t>(base);memory.Put(base+2*0x100+0x50,cycle);
    Check(!walk(false,oldReads,oldRoot)&&!walk(true,newReads,newRoot)&&oldReads==6&&newReads==3,
          "cycle rejection is preserved and never reuses a previous positive root");
    for(unsigned n=0;n<9;++n){GeometryLinks node;node.parent=static_cast<std::uint32_t>(base+(n+1)*0x100);
        node.owner=n==8?static_cast<std::uint32_t>(target):0;memory.Put(base+n*0x100+0x50,node);}
    Check(!walk(false,oldReads,oldRoot)&&!walk(true,newReads,newRoot)&&oldReads==16&&newReads==8,
          "the ninth descendant remains ineligible after bounded read batching");
}
void AttachmentOwnership() {
    using namespace NativeWeaponOwnership;
    Memory memory;
    constexpr std::uintptr_t image=0x400000,player=0x220000,component=0x230000;
    constexpr std::uint32_t manager=0x200000,pool=0x210000,data=0x300000,generations=0x310000;
    memory.Put(image+0x6c99b8,manager);memory.Put(manager+0xc,pool);
    memory.Put(pool+4,std::uint32_t{0x70});memory.Put(pool+8,std::uint32_t{8});
    memory.Put(pool+0xc,data);memory.Put(pool+0x10,generations);
    for(unsigned i=0;i<8;++i)memory.Put(generations+i*2,std::uint16_t{3});
    memory.Put(player+0x88,std::uint32_t(component));memory.Put(component+0x14,std::uint32_t{3});
    Record back;back.geometryRef=3*8+2+1;back.bone=35;
    Record hand;hand.geometryRef=3*8+4+1;hand.bone=31;
    Record hip;hip.geometryRef=3*8+6+1;hip.bone=36;
    memory.Put(component+0x18,back);memory.Put(component+0x50,hand);memory.Put(component+0x88,hip);
    auto read=[&](std::uintptr_t a,void* value,std::size_t size){return memory.Read(a,value,size);};
    const auto original=memory.bytes;Snapshot snapshot;
    Check(Capture(image,player,read,snapshot),"native player body-slot list reads safely");
    Check(snapshot.count==3&&snapshot.geometry[0]==data+2*0x70&&snapshot.geometry[2]==data+6*0x70,
          "spine and hip slots resolve exact generation-checked geometry references");
    Check(snapshot.geometry[1]==0,"hand slot remains reserved for wrist remapping");
    Check(StillAttached(image,snapshot,0,read)&&StillAttached(image,snapshot,2,read),"fresh owned body slots revalidate");
    Check(!StillAttached(image,snapshot,1,read)&&!StillAttached(image,snapshot,3,read),"held slot and out-of-range index rejected");
    Check(memory.bytes==original,"capture and ownership revalidation never write native memory");
    memory.Put(generations+2*2,std::uint16_t{4});
    Check(!StillAttached(image,snapshot,0,read),"recycled geometry generation cannot inherit ownership");
    Check(StillAttached(image,snapshot,2,read),"recycling another geometry preserves this exact attachment");
    memory.Put(generations+2*2,std::uint16_t{3});
    memory.Put(component+0x14,std::uint32_t{0});
    Check(!StillAttached(image,snapshot,0,read),"detach immediately retires cached ownership");
    memory.Put(component+0x14,std::uint32_t{3});
    auto changed=back;changed.bone=31;memory.Put(component+0x18,changed);
    Check(!StillAttached(image,snapshot,0,read),"moving from back to hand retires cached body slot");
    memory.Put(component+0x18,back);memory.Put(player+0x88,std::uint32_t(component+0x1000));
    Check(!StillAttached(image,snapshot,0,read),"replaced native attachment component rejected");
    memory.Put(player+0x88,std::uint32_t(component));memory.Put(component+0x14,std::uint32_t{13});
    const auto sentinel=snapshot;
    Check(!Capture(image,player,read,snapshot)&&snapshot.component==sentinel.component&&snapshot.count==sentinel.count,
          "invalid native count preserves caller snapshot");
    std::uintptr_t geometry=0xabcdef;
    Check(!ResolveGeometry(image,0,read,geometry)&&geometry==0xabcdef,"null geometry reference rejected without output mutation");
    Check(!ResolveGeometry(image,0x40000070,read,geometry),"internal compressed reference is not treated as a normal ZREF");
    Check(!ResolveGeometry(image,0x3fffffff,read,geometry),"generation outside signed native range rejected");
    memory.Put(pool+4,std::uint32_t{0x60});
    Check(!ResolveGeometry(image,back.geometryRef,read,geometry),"unexpected geometry pool layout rejected");
    memory.Put(pool+4,std::uint32_t{0x70});memory.Put(pool+8,std::uint32_t{0});
    Check(!ResolveGeometry(image,back.geometryRef,read,geometry),"zero pool capacity rejected before division");
    memory.Put(pool+8,std::uint32_t{8});memory.Put(pool+0xc,std::uint32_t{0xfffffff0});
    Check(!ResolveGeometry(image,back.geometryRef,read,geometry),"geometry address overflow rejected on x86 and x64");
    Check(!Capture(image,player+0x10000,read,snapshot),"unreadable player does not produce an attachment owner");
}
void HandGroupOwnership() {
    using namespace NativeWeaponOwnership;
    Memory memory;
    constexpr std::uintptr_t image=0x400000,player=0x220000,component=0x230000,model=0x240000,lookup=0x250000;
    constexpr std::uint32_t manager=0x200000,pool=0x210000,data=0x300000,generations=0x310000;
    constexpr std::uint32_t left=0x600000,right=0x600100,playerRoot=0x700000;
    constexpr std::uint32_t modelResource=0x410000,parts=0x420000,header=0x430000,definitions=0x440000,primitives=0x450000;
    memory.Put(image+0x6c99b8,manager);memory.Put(manager+0xc,pool);
    memory.Put(pool+4,std::uint32_t{0x70});memory.Put(pool+8,std::uint32_t{8});
    memory.Put(pool+0xc,data);memory.Put(pool+0x10,generations);
    for(unsigned i=0;i<8;++i)memory.Put(generations+i*2,std::uint16_t{3});
    memory.Put(player,std::uint32_t(image+0x56582c));memory.Put(player+4,playerRoot);
    memory.Put(player+0x70,std::uint32_t(model));memory.Put(player+0x88,std::uint32_t(component));
    memory.Put(model+0xc,std::uint32_t(lookup));memory.Put(model+0x4a0,std::uint32_t{20});
    memory.Put(lookup+18,std::uint8_t{3});memory.Put(lookup+24,std::uint8_t{5});
    memory.Put(playerRoot+0x68,std::uint32_t{10});
    const std::uint32_t resources[]={modelResource,parts,header,definitions,primitives};
    for(unsigned i=0;i<5;++i)memory.Put(image+0x71f308+(10+i)*4,resources[i]);
    memory.Put(modelResource+2,std::uint16_t{7});memory.Put(modelResource+0xc,std::uint8_t{1});
    memory.Put(modelResource+0x10,std::uint32_t{11});memory.Put(parts+4,std::uint32_t{12});
    memory.Put(header,std::uint32_t{20});memory.Put(header+4,std::uint32_t{13});
    memory.Put(header+0x14,std::uint32_t{14});
    memory.Put(primitives+20,std::uint8_t{3});memory.Put(primitives+25,std::uint8_t{5});
    const char leftName[16]="LEFT_HAND",rightName[16]="RIGHT_HAND";
    memory.Put(definitions+3*64+0x1c,leftName);memory.Put(definitions+5*64+0x1c,rightName);
    Record leftRecord;leftRecord.geometryRef=3*8+2+1;leftRecord.bone=20;
    Record rightRecord;rightRecord.geometryRef=3*8+6+1;rightRecord.bone=25;
    memory.Put(component+0x14,std::uint32_t{2});memory.Put(component+0x18,leftRecord);memory.Put(component+0x50,rightRecord);
    memory.Put(data+2*0x70+0x60,left);memory.Put(data+6*0x70+0x60,right);
    for(const auto& item: {std::pair<std::uint32_t,std::uint32_t>{left,data+2*0x70},{right,data+6*0x70}}) {
        memory.Put(item.first,std::uint32_t(image+0x5462cc));memory.Put(item.first+4,item.second);
    }
    auto read=[&](std::uintptr_t a,void* value,std::size_t size){return memory.Read(a,value,size);};
    Snapshot snapshot;Check(Capture(image,player,read,snapshot),"hand groups use the native attachment list");
    const auto untouched=memory.bytes;int side=-1;std::uintptr_t owner=0;
    Check(HandGroup(image,snapshot,0,read,side,owner)&&side==0&&owner==left,"exact owned LEFT_HAND group resolves through native model resources");
    Check(HandGroup(image,snapshot,1,read,side,owner)&&side==1&&owner==right,"exact owned RIGHT_HAND group resolves through native model resources");
    Check(memory.bytes==untouched,"hand group authorization never edits native resource/attachment memory");
    memory.Put(primitives+25,std::uint8_t{3});
    Check(!HandGroup(image,snapshot,1,read,side,owner),"primitive number alone cannot authorize a group on the other wrist");
    memory.Put(primitives+25,std::uint8_t{5});
    const char foreignName[16]="RIGHT_FOREARM";memory.Put(definitions+5*64+0x1c,foreignName);
    Check(!HandGroup(image,snapshot,1,read,side,owner),"standard lookup must also have the exact native wrist name");
    char unterminated[16];std::memset(unterminated,'A',sizeof(unterminated));memory.Put(definitions+5*64+0x1c,unterminated);
    Check(!HandGroup(image,snapshot,1,read,side,owner),"unterminated foreign bone name rejected within the bounded read");
    memory.Put(definitions+5*64+0x1c,rightName);
    memory.Put(right,std::uint32_t(image+0x577b74));
    Check(!HandGroup(image,snapshot,1,read,side,owner),"a firearm cannot inherit the accessory group scope");
    memory.Put(right,std::uint32_t(image+0x5462cc));memory.Put(right+4,std::uint32_t(data+2*0x70));
    Check(!HandGroup(image,snapshot,1,read,side,owner),"foreign group/root backpointer mismatch rejected");
    memory.Put(right+4,std::uint32_t(data+6*0x70));memory.Put(generations+6*2,std::uint16_t{4});
    Check(!HandGroup(image,snapshot,1,read,side,owner),"recycled cuff geometry rejected immediately");
    Check(HandGroup(image,snapshot,0,read,side,owner)&&side==0,"recycling the other accessory does not disable the owned left group");
    memory.Put(generations+6*2,std::uint16_t{3});
    auto detached=rightRecord;detached.bone=35;memory.Put(component+0x50,detached);
    Check(!HandGroup(image,snapshot,1,read,side,owner),"moving a group away from the wrist retires cached attachment authority");
    memory.Put(component+0x50,rightRecord);memory.Put(player,std::uint32_t(image+0x552684));
    Check(!HandGroup(image,snapshot,1,read,side,owner),"foreign actor type rejected even with copied attachment records");
    memory.Put(player,std::uint32_t(image+0x56582c));memory.Put(modelResource+2,std::uint16_t{8});
    Check(!HandGroup(image,snapshot,1,read,side,owner),"unexpected model resource type retains the native accessory draw");
    memory.Put(modelResource+2,std::uint16_t{7});memory.Put(header,std::uint32_t{4});
    Check(!HandGroup(image,snapshot,1,read,side,owner),"model bone must lie within the verified resource count");
    memory.Put(header,std::uint32_t{20});memory.Put(component+0x14,std::uint32_t{0});
    side=9;owner=0xabcdef;
    Check(!HandGroup(image,snapshot,1,read,side,owner)&&side==9&&owner==0xabcdef,"detaching leaves every caller output unchanged");
    Check(!HandGroup(image,snapshot,2,read,side,owner),"out-of-range accessory index rejected");
    memory.Put(component+0x14,std::uint32_t{2});memory.bytes.erase(image+0x71f308+14*4);
    Check(!HandGroup(image,snapshot,1,read,side,owner),"unreadable native primitive resource safely rejects remap");
}
void NativeMuzzleRay() {
    const auto rawWrist=Pose(-.7f,.21f,{-2000,170,-5000});
    const auto grip=Pose(.3f,-.1f,{3,-5,8});
    const auto nativeAttachment=NativeRigMath::ToWorld(rawWrist,{grip.basis,grip.position});
    const auto marker=Pose(-.2f,.05f,{.0576f,14.7976f,45.7741f});
    const auto originalMuzzle=NativeRigMath::ToWorld(nativeAttachment,{marker.basis,marker.position});
    const auto savedAttachment=nativeAttachment,savedMarker=marker;
    for(int yaw=-180;yaw<=180;yaw+=15)for(int pitch=-75;pitch<=75;pitch+=15) {
        const auto wrist=Pose(yaw*.0174532925f,pitch*.0174532925f,{-1980,158,-5010});
        CameraPose attachment,expected;Vec3 muzzle,target;
        Check(NativeWeaponMath::Shot(rawWrist,wrist,nativeAttachment,marker,100,attachment,muzzle,target),"owned native muzzle ray builds across controller rotations");
        Check(NativeWeaponMath::Remap(rawWrist,wrist,originalMuzzle,expected)&&Near(expected.position,muzzle),
              "native marker composition and wrist remap agree on exact muzzle origin");
        const auto direction=Subtract(target,muzzle);const float length=std::sqrt(Dot(direction,direction));
        Check(std::fabs(length-10000.f)<.02f&&Dot(Scale(direction,1.f/length),wrist.basis.right)>.9999f,
              "native aim begins at the muzzle and follows Touch pointing ray at preserved scale");
        Check(!Near(muzzle,wrist.position),"muzzle retains authored offset instead of spawning bullets from the wrist");
        Check(!std::memcmp(&nativeAttachment,&savedAttachment,sizeof(savedAttachment))&&
              !std::memcmp(&marker,&savedMarker,sizeof(savedMarker)),"native source attachment and marker remain byte-exact");
    }
    CameraPose attachment=Pose(0,0,{7,8,9}),saved=attachment;Vec3 muzzle{1,2,3},target{4,5,6};
    auto invalid=marker;invalid.position={0,0,301};
    Check(!NativeWeaponMath::Shot(rawWrist,rawWrist,nativeAttachment,invalid,100,attachment,muzzle,target),"implausibly distant marker rejected");
    Check(!NativeWeaponMath::Shot(rawWrist,rawWrist,nativeAttachment,marker,0,attachment,muzzle,target),"invalid world scale rejected");
    Check(Near(attachment,saved)&&Near(muzzle,{1,2,3})&&Near(target,{4,5,6}),"invalid shot preserves every caller output");
}
void NativePendingMuzzle() {
    using Pending=NativeWeaponMath::PendingMuzzle;
    using Exit=Pending::Exit;
    Pending scope;
    const Vec3 nativeCoverOrigin{1200,154,-2000},trackedMuzzle{1256,161,-1910};
    Check(scope.Prepare(nativeCoverOrigin,trackedMuzzle),"native one-shot world muzzle can be scoped to the actual tracked barrel");
    // FireFF reads the pending origin instead of its attachment+marker path.
    // Simulate both native exits: it consumes the override on a real shot,
    // but an early return can leave the replacement untouched.
    Vec3 nativeField=scope.temporary;
    Check(Near(nativeField,trackedMuzzle)&&!Near(nativeField,nativeCoverOrigin),
          "pending native origin cannot bypass the tracked attachment with the old third-person point");
    Check(scope.Resolve(nativeField)==Exit::Restore,"early native Fire retains only our still-owned replacement for restoration");
    nativeField={0,0,0};
    Check(scope.Resolve(nativeField)==Exit::Consumed,"native zero-on-consume must not resurrect the previous cached origin");
    nativeField={-0.f,0.f,-0.f};
    Check(scope.Resolve(nativeField)==Exit::Consumed,"native signed zero has the same absent-muzzle sentinel");
    nativeField={1205,155,-1998};
    Check(scope.Resolve(nativeField)==Exit::Changed,"a newer native origin written during Fire cannot be overwritten on scope exit");
    nativeField.x=std::numeric_limits<float>::quiet_NaN();
    Check(scope.Resolve(nativeField)==Exit::Changed,"unexpected nonfinite native state is never claimed as our value");
    Pending absent;const Vec3 sentinel{7,8,9};absent.previous=sentinel;absent.temporary=sentinel;
    Check(!absent.Prepare({0,0,0},trackedMuzzle)&&!absent.active,
          "absent native pending origin stays absent rather than adding another shot path");
    Check(Near(absent.previous,sentinel)&&Near(absent.temporary,sentinel),"failed cached-muzzle preparation leaves the caller state unchanged");
    Check(!absent.Prepare(nativeCoverOrigin,{0,0,0}),"world origin sentinel cannot be installed as a nonzero native override");
    Check(!absent.Prepare({std::numeric_limits<float>::infinity(),1,2},trackedMuzzle),"nonfinite pending origin is rejected");
    Check(absent.Resolve(nativeCoverOrigin)==Exit::Inactive,"an inactive scope never authorizes restoration");
    // This is independent of marker basis and any one weapon's authored
    // barrel axis; the tracked origin comes from the existing exact Shot.
    const auto raw=Pose(-.3f,.1f,{1220,160,-1990});
    const auto grip=Pose(.2f,-.2f,{2,-4,12});
    const auto attachment=NativeRigMath::ToWorld(raw,{grip.basis,grip.position});
    const auto marker=Pose(.8f,-.3f,{0,12,60});
    for(int yaw=-150;yaw<=150;yaw+=30) {
        const auto tracked=Pose(yaw*.0174532925f,.4f,{1250,175,-1980});
        CameraPose corrected;Vec3 muzzle,target;
        Check(NativeWeaponMath::Shot(raw,tracked,attachment,marker,100,corrected,muzzle,target),"pending-muzzle fixture uses a real remapped long-gun attachment");
        Pending pending;
        Check(pending.Prepare(nativeCoverOrigin,muzzle)&&Near(pending.temporary,muzzle),
              "native cached origin and the authored VR barrel coincide for every tested wrist turn");
        const auto direction=Subtract(target,pending.temporary);
        const float length=std::sqrt(Dot(direction,direction));
        Check(Dot(Scale(direction,1.f/length),tracked.basis.right)>.9999f,
              "cached-origin branch preserves the same controller direction as the noncached muzzle path");
    }
}
}
void RenderGenerationNegativeOwnership() {
    using namespace NativeWeaponOwnership;
    Snapshot held;held.player=0x11000;held.component=0x12000;held.count=2;
    held.records[0].geometryRef=37;held.records[0].bone=1;held.geometry[0]=0x13000;
    held.records[1].geometryRef=98;held.records[1].bone=25;held.geometry[1]=0x14000;
    std::uintptr_t weapons[2]={0,0x15000};std::uint32_t refs[2]={0,55};
    const auto signature=OwnershipSignature(held,weapons,refs);
    NegativeDrawKey key{10,signature,held.player,0x16000,0x17000,{0x18000,{0,0,0},0x19000}};
    NegativeDrawMemo memo;
    Check(!memo.Contains(key),"first world draw requires a real ownership traversal");memo.Store(key);
    Check(memo.Contains(key),"same exact negative world draw can reuse its current-render decision");
    NegativeDrawMemo geometryMemo(true);geometryMemo.Store(key);auto material=key;material.entry+=0x200;
    Check(geometryMemo.Contains(material),"another validated material entry shares the same geometry-only negative proof");
    for(unsigned field=0;field<9;++field){auto changed=material;
        switch(field){case 0:++changed.generation;break;case 1:++changed.signature;break;case 2:++changed.player;break;
        case 3:changed.entry=0;break;case 4:++changed.geometry;break;case 5:++changed.root.parent;break;
        case 6:++changed.root.owner;break;default:++changed.root.reserved[field-7];break;}
        Check(!geometryMemo.Contains(changed),"geometry memo preserves every ownership/generation/root guard and requires a validated entry");}
    for(unsigned field=0;field<9;++field) {
        auto changed=key;
        switch(field){case 0:++changed.generation;break;case 1:++changed.signature;break;case 2:++changed.player;break;
        case 3:++changed.entry;break;case 4:++changed.geometry;break;case 5:++changed.root.parent;break;
        case 6:++changed.root.owner;break;default:++changed.root.reserved[field-7];break;}
        Check(!memo.Contains(changed),"camera generation, item snapshot or reused root/entry identity invalidates a negative hit");
    }
    for(unsigned field=0;field<10;++field) {
        auto changed=held;std::uintptr_t w[2]={weapons[0],weapons[1]};std::uint32_t r[2]={refs[0],refs[1]};
        switch(field){case 0:++changed.player;break;case 1:++changed.component;break;case 2:--changed.count;break;
        case 3:++w[0];break;case 4:++w[1];break;case 5:++r[0];break;case 6:++r[1];break;
        case 7:++changed.records[0].geometryRef;break;case 8:++changed.records[1].bone;break;default:++changed.geometry[0];break;}
        Check(OwnershipSignature(changed,w,r)!=signature,"held hand, ZREF generation and exact body-slot changes refresh negative decisions");
    }
    auto collision=key;collision.entry+=0x4000;memo.Store(collision);
    Check(memo.Contains(collision)&&!memo.Contains(key),"bounded direct-map collision never returns another draw's negative decision");
    auto invalid=key;invalid.generation=0;memo.Store(invalid);
    Check(!memo.Contains(invalid),"no camera generation cannot populate or reuse a negative memo");
    Memory memory;constexpr std::uintptr_t base=0x22000;constexpr std::uint32_t unowned=0x44000;
    for(unsigned i=0;i<8;++i){GeometryLinks link;link.parent=i==7?0:static_cast<std::uint32_t>(base+(i+1)*0x100);link.owner=unowned;
        memory.Put(base+i*0x100+0x50,link);}
    unsigned reads=0;auto read=[&](auto address,void* output,auto count){++reads;return memory.Read(address,output,count);};
    NegativeDrawMemo bounded;
    const auto traverse=[&](std::uint64_t generation) {
        GeometryLinks root;if(!ReadGeometryLinks(base,read,root))return false;
        const NegativeDrawKey current{generation,signature,held.player,0x33000,base,root};
        if(bounded.Contains(current))return false;
        std::uintptr_t at=base;
        for(unsigned depth=0;depth<8&&at;++depth){GeometryLinks link=root;if(depth&&!ReadGeometryLinks(at,read,link))return false;
            if(link.owner==weapons[1])return true;at=link.parent;}
        bounded.Store(current);return false;
    };
    Check(!traverse(1)&&reads==8,"first bounded negative chain reads all eight actual geometry nodes");
    reads=0;Check(!traverse(1)&&reads==1,"repeated same-generation world pass needs only current root proof rather than eight kernel reads");
    reads=0;Check(!traverse(2)&&reads==8,"next render generation always traverses geometry anew");
    GeometryLinks pickedUp; pickedUp.owner=static_cast<std::uint32_t>(weapons[1]);memory.Put(base+0x50,pickedUp);
    reads=0;Check(traverse(2)&&reads==1,"same-address pickup/root-owner mutation bypasses cached negative and finds the held firearm immediately");
}
int main() {
    PhysicalHeldAttachment();
    AnimatedVisibleBarrel();
    RenderGenerationNegativeOwnership();
    AdjacentNativeOwnershipReads();
    // Independently compose item->child, then recover the item from the GPU
    // child world. Marker origins must never be composed with child twice.
    for(int angle=-180;angle<=180;angle+=15) {
        const auto item=Pose(angle*.0174532925f,.31f,{1234,152,-830});
        const auto child=Pose(-.71f,.22f,{3,-7,5});
        const auto childWorld=NativeRigMath::ToWorld(item,{child.basis,child.position});
        CameraPose recovered;
        Check(NativeWeaponMath::ParentFrame(childWorld,child,recovered)&&Near(recovered,item),"GPU child world recovers the item frame including authored grip rotation");
        const auto detail=Pose(.42f,-.1f,{-1,2,6});
        const auto chain=NativeRigMath::ToWorld(child,{detail.basis,detail.position});
        const auto detailWorld=NativeRigMath::ToWorld(childWorld,{detail.basis,detail.position});
        Check(NativeWeaponMath::ParentFrame(detailWorld,chain,recovered)&&Near(recovered,item),"multiple child offsets cannot shift the recovered muzzle frame");
    }
    AttachmentOwnership();
    HandGroupOwnership();
    NativeMuzzleRay();
    NativePendingMuzzle();
    // Both authored grips retain position and orientation under arbitrary
    // body yaw, controller pitch and wrist translations, rather than simply
    // snapping the gun origin to the controller origin.
    for(int side=0;side<2;++side)for(int yaw=-180;yaw<=180;yaw+=15)for(int pitch=-75;pitch<=75;pitch+=15) {
        const float sign=side?1.f:-1.f;
        const auto raw=Pose(-.71f,.24f,{-1234,168,976});
        const auto solved=Pose(yaw*.0174532925f,pitch*.0174532925f,{-1200+sign*20,150,950});
        const auto grip=Pose(.21f*sign,-.37f,{sign*3,-4,12});
        const auto mesh=NativeRigMath::ToWorld(raw,{grip.basis,grip.position});
        const auto rawCopy=raw,solvedCopy=solved,meshCopy=mesh;
        CameraPose result;
        Check(NativeWeaponMath::Remap(raw,solved,mesh,result),"valid native grip remaps");
        Check(Near(NativeRigMath::ToModel(solved,result),grip),"authored wrist-to-mesh offset preserved");
        Check(Near(raw,rawCopy)&&Near(solved,solvedCopy)&&Near(mesh,meshCopy),"source draw and wrist poses stay unchanged");
    }
    const auto wrist=Pose(.3f,-.2f,{6,7,8}),mesh=Pose(.4f,.5f,{20,15,40});CameraPose output;
    Check(NativeWeaponMath::Remap(wrist,wrist,mesh,output)&&std::memcmp(&mesh,&output,sizeof(mesh))==0,"untracked unchanged wrist preserves mesh bytes");
    const auto sentinel=output;auto invalid=wrist;invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    Check(!NativeWeaponMath::Remap(invalid,wrist,mesh,output),"nonfinite raw wrist rejected");
    Check(!NativeWeaponMath::Remap(wrist,invalid,mesh,output),"nonfinite solved wrist rejected");
    Check(!NativeWeaponMath::Remap(wrist,wrist,invalid,output),"nonfinite mesh rejected");
    invalid=wrist;invalid.basis.right=invalid.basis.forward;
    Check(!NativeWeaponMath::Remap(invalid,wrist,mesh,output),"degenerate grip rejected");
    Check(std::memcmp(&sentinel,&output,sizeof(output))==0,"failure leaves output unchanged");
    // An unequipped rifle must remain on the native spine side while the
    // visual body turns independently of gameplay heading, including 180deg.
    const auto rawBody=Pose(-.37f,0,{-1842,12,325});
    const auto backSlot=Pose(.1f,.23f,{4,132,24});
    const auto rawBack=NativeRigMath::ToWorld(rawBody,{backSlot.basis,backSlot.position});
    for(int yaw=-180;yaw<=180;yaw+=10) {
        const auto renderBody=Pose(yaw*.0174532925f,0,{-1845,12,326});CameraPose carried;
        Check(NativeWeaponMath::Remap(rawBody,renderBody,rawBack,carried),"stowed rifle follows visual body root");
        Check(Near(NativeRigMath::ToModel(renderBody,carried),backSlot),"stowed rifle retains native spine offset and animated part orientation");
        const auto local=NativeRigMath::ToModel(renderBody,carried);
        Check(local.position.z>0&&local.position.y>100,"back firearm remains behind model-facing -Z after turning around");
    }
    std::printf("Native weapon grip tests: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
