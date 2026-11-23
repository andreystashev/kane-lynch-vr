#pragma once
#include "native_camera_math.hpp"
#include "native_weapon_ownership.hpp"
#include <array>
#include <cstring>

// Passive evidence only. None of these predicates grants permission to edit
// a draw. In particular, rejected VIEW/projection draws must remain visible.
namespace NativeDrawContextKL {
constexpr unsigned MaximumAncestors=8;
enum Evidence : std::uint32_t {
    TechniqueScope=1,StateRead=2,RecordRead=4,ProviderRead=8,KnownGeometryLayout=16,
    LocalPlayerRead=32,HandsRead=64,AttachmentsRead=128,CoherentRecord=256,
    HierarchyTruncated=512,HierarchyCycle=1024,SourceWorldRead=2048,
    SkinFieldsRead=4096,InlineWorldRead=8192,HierarchyReadFailed=16384,EntryHeaderRead=32768
};
enum Role : std::uint32_t {
    Unclassified=0,LocalBody=1,LeftHandItem=2,RightHandItem=4,
    ActiveLeftHand=8,ActiveRightHand=16,Firearm=32,AttachedToLocalPlayer=64,
    StowedFirearm=128,OwnerRootMatches=256,AttachmentAtWrist=512
};
struct WorldBind {
    std::uintptr_t state=0,source=0,entry=0;
    std::uint32_t callerRva=0,serial=0;
    std::uint64_t techniqueSerial=0;
    NativeCameraMath::CameraPose world{};
    bool valid=false;
};
struct Ancestor {
    std::uintptr_t geometry=0,parent=0,owner=0,ownerVtable=0,ownerRoot=0,ownerModel=0;
    std::uint32_t roles=0,attachmentIndex=0xffffffffu,attachmentBone=0xffffffffu;
};
struct Context {
    std::uint32_t version=1,evidence=0,techniqueDepth=0,pass=0,gpuIndex=0xffffffffu;
    std::uint64_t techniqueSerial=0;
    std::uintptr_t image=0,state=0,package=0,technique=0,record=0,resource=0,
        entry=0,entryVtable=0,entryBinder=0,geometry=0;
    std::uint32_t recordWords[8]{};
    // Unknown providers remain undecoded; retain bytes/candidate only so a
    // later native layout investigation does not need another user setting.
    std::uint32_t entryWords[72]{};
    std::uintptr_t geometryCandidate=0;
    std::uintptr_t localPlayer=0,playerRoot=0,playerModel=0;
    NativeWeaponOwnership::HandState hands[2]{};
    std::uintptr_t handVtables[2]{};
    Ancestor ancestors[MaximumAncestors]{};
    unsigned ancestorCount=0;
    std::uint32_t roles=0;
    NativeCameraMath::CameraPose world{},view{},inlineWorld[2]{};
    std::array<float,16> projection{};
    std::uintptr_t palette=0,lightPalette=0,paletteCache=0,lastUploadedPalette=0;
    std::uintptr_t leaseSourcePalette=0,leaseStaging=0,leaseSourceRows=0;
    std::uint32_t paletteIndex=0,bones=0;
    std::uint32_t leaseSourceIndex=0,leaseSourceInstances=0;
    WorldBind sourceBind{},nativeSourceBind{};
    bool sourceBindMatchesState=false,nativeSourceMatchesEntry=false,skinLease=false,skinLeaseMatchesEntry=false;
    bool support=false,coupled=false,shadow=false;
    std::uint64_t pair=0,generation=0;
    bool weaponBinding=false,weaponBindingMatchesEntry=false,weaponSupport=false;
    std::uintptr_t weaponPlayer=0,weaponItem=0,weaponRoot=0,weaponModel=0;
    std::uint32_t weaponReference=0;int weaponSide=-1;
    std::uint64_t weaponPair=0,weaponGeneration=0;
    NativeCameraMath::CameraPose weaponSource{},weaponTracked{};
};

template<class Reader> bool Capture(std::uintptr_t image,std::uintptr_t state,
    std::uintptr_t package,std::uintptr_t technique,unsigned pass,unsigned depth,
    Reader read,Context& output) {
    using namespace NativeWeaponOwnership;
    Context next;next.image=image;next.state=state;next.package=package;
    next.technique=technique;next.pass=pass;next.techniqueDepth=depth;
    if(depth&&depth<=8&&package&&state)next.evidence|=TechniqueScope;
    if(!(next.evidence&TechniqueScope)){output=next;return false;}
    // One bounded state read, rather than a syscall for each field. Matrices
    // are kept even if non-orthogonal or non-finite: this is diagnostic data.
    std::array<unsigned char,0x440> bytes{};
    if(Address(state,bytes.size())&&read(state,bytes.data(),bytes.size())) {
        next.evidence|=StateRead;
        std::uint32_t record=0,palette=0,light=0;
        std::memcpy(&record,bytes.data()+0x14,4);next.record=record;
        std::memcpy(&next.world,bytes.data()+0x8c,sizeof(next.world));
        std::memcpy(&next.view,bytes.data()+0xbc,sizeof(next.view));
        std::memcpy(next.projection.data(),bytes.data()+0x3f8,64);
        std::memcpy(&palette,bytes.data()+0x3bc,4);next.palette=palette;
        std::memcpy(&light,bytes.data()+0x3c0,4);next.lightPalette=light;
        std::memcpy(&next.paletteIndex,bytes.data()+0x3c4,4);
    }
    // A current material provider exists only on the native48F0A0 stack.
    // Without that scope, state+14 can be a stale record: do not decode it.
    if((next.evidence&TechniqueScope)&&Address(next.record,sizeof(next.recordWords))&&
       read(next.record,next.recordWords,sizeof(next.recordWords))) {
        next.evidence|=RecordRead;next.resource=next.recordWords[1];next.entry=next.recordWords[3];
        std::uint32_t vt=0,binder=0;
        if(Read(next.entry,vt,read)) {
            next.entryVtable=vt;Read(std::uintptr_t(vt)+0x18,binder,read);next.entryBinder=binder;
            next.evidence|=ProviderRead;
            if(Address(next.entry,sizeof(next.entryWords))&&read(next.entry,next.entryWords,sizeof(next.entryWords))) {
                next.evidence|=EntryHeaderRead;next.geometryCandidate=next.entryWords[0xf4/4];
            }
            const auto rva=std::uintptr_t(vt)-image;
            if(rva==0x540ee0||rva==0x541034||rva==0x541070||rva==0x540ff8) {
                std::uint32_t geometry=0;
                if(Read(next.entry+0xf4,geometry,read)&&geometry) {
                    next.geometry=geometry;next.evidence|=KnownGeometryLayout;
                }
                if(read(next.entry+0x10,next.inlineWorld,sizeof(next.inlineWorld)))next.evidence|=InlineWorldRead;
                if(rva==0x541034) {
                    std::uint32_t cache=0,count=0;
                    if(Read(next.entry+0x108,cache,read)&&Read(next.entry+0x114,count,read)) {
                        next.paletteCache=cache;next.bones=count;next.evidence|=SkinFieldsRead;
                    }
                }
            }
        }
    }
    std::uint32_t data=0,player=0,playerVtable=0,root=0,model=0;
    if(Read(image+0x68760c,data,read)&&Read(std::uintptr_t(data)+0xa2c,player,read)&&
       Read(player,playerVtable,read)&&playerVtable==image+0x56582c&&
       Read(std::uintptr_t(player)+4,root,read)&&Read(std::uintptr_t(player)+0x70,model,read)) {
        next.localPlayer=player;next.playerRoot=root;next.playerModel=model;next.evidence|=LocalPlayerRead;
        bool both=true;
        for(unsigned side=0;side<2;++side) {
            const auto hand=std::uintptr_t(player)+(side?0x358:0x2d8);std::uint32_t vt=0;
            const bool ok=Read(hand,vt,read)&&vt==image+0x5508b4&&ReadHandState(hand,read,next.hands[side])&&
                next.hands[side].enabled<=1&&next.hands[side].attacher==(side?31u:32u);
            next.handVtables[side]=vt;if(!ok){next.hands[side]={};both=false;}
        }
        if(both)next.evidence|=HandsRead;
    }
    Snapshot attachments;
    if(next.localPlayer&&NativeWeaponOwnership::Capture(image,next.localPlayer,read,attachments)) {
        next.evidence|=AttachmentsRead;
        // The authorizing helper deliberately skips wrist slots. Passive
        // diagnostics retain their generation-checked references as well.
        for(unsigned i=0;i<attachments.count;++i)if(!attachments.geometry[i]&&attachments.records[i].bone<128)
            ResolveGeometry(image,attachments.records[i].geometryRef,read,attachments.geometry[i]);
    }
    auto geometry=next.geometry;
    while(geometry&&next.ancestorCount<MaximumAncestors) {
        bool cycle=false;for(unsigned i=0;i<next.ancestorCount;++i)cycle=cycle||next.ancestors[i].geometry==geometry;
        if(cycle){next.evidence|=HierarchyCycle;break;}
        GeometryLinks links;
        if(!ReadGeometryLinks(geometry,read,links)){next.evidence|=HierarchyReadFailed;break;}
        auto& node=next.ancestors[next.ancestorCount++];node.geometry=geometry;node.parent=links.parent;node.owner=links.owner;
        std::uint32_t vt=0,ownerRoot=0,ownerModel=0,setter=0;
        if(Read(node.owner,vt,read))node.ownerVtable=vt;
        if(Read(node.owner+4,ownerRoot,read))node.ownerRoot=ownerRoot;
        if(node.ownerRoot==geometry)node.roles|=OwnerRootMatches;
        if(node.owner&&node.owner==next.localPlayer){node.roles|=LocalBody;node.ownerModel=next.playerModel;}
        for(unsigned side=0;side<2;++side)if(node.owner&&node.owner==next.hands[side].item) {
            node.roles|=side?RightHandItem:LeftHandItem;
            if(next.hands[side].enabled)node.roles|=side?ActiveRightHand:ActiveLeftHand;
        }
        if(vt&&Read(std::uintptr_t(vt)+0x2d4,setter,read)&&setter==image+0x11df40) {
            node.roles|=Firearm;if(Read(node.owner+0xc0,ownerModel,read))node.ownerModel=ownerModel;
        }
        for(unsigned i=0;i<attachments.count;++i)if(attachments.geometry[i]==geometry&&
            StillAttached(image,attachments,i,read)) {
            node.roles|=AttachedToLocalPlayer;node.attachmentIndex=i;node.attachmentBone=attachments.records[i].bone;
            if(node.attachmentBone==31||node.attachmentBone==32)node.roles|=AttachmentAtWrist;
            if((node.roles&(Firearm|OwnerRootMatches))==(Firearm|OwnerRootMatches)&&
               !(node.roles&(ActiveLeftHand|ActiveRightHand|AttachmentAtWrist)))node.roles|=StowedFirearm;
        }
        next.roles|=node.roles;geometry=node.parent;
    }
    if(geometry&&next.ancestorCount==MaximumAncestors)next.evidence|=HierarchyTruncated;
    std::uint32_t after=0,provider=0;
    if((next.evidence&RecordRead)&&Read(state+0x14,after,read)&&after==next.record&&
       Read(next.record+0xc,provider,read)&&provider==next.entry)next.evidence|=CoherentRecord;
    std::uint32_t gpu=2,uploaded=0;
    if(Read(image+0x7678a4,gpu,read))next.gpuIndex=gpu;
    if(Read(image+0x7893b0,uploaded,read))next.lastUploadedPalette=uploaded;
    output=next;return (next.evidence&TechniqueScope)!=0;
}
} // namespace NativeDrawContextKL
