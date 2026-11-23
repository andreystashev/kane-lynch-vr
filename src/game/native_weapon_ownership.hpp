#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only K&L geometry attachments. The native player attachment methods
// 0x524CB0/0x524D20 use player+0x88; 0x46A570/0x46A470 store/search these
// exact geometry references. Scene hierarchy, inventory membership and
// distance from the player do not establish attachment ownership.
namespace NativeWeaponOwnership {
constexpr unsigned MaximumAttachments=12;
struct Record {
    std::uint32_t geometryRef=0,bone=0;
    std::uint8_t localPose[48]{};
};
static_assert(sizeof(Record)==0x38,"Native attached-geometry record");
struct Snapshot {
    std::uintptr_t player=0,component=0;
    unsigned count=0;
    std::array<Record,MaximumAttachments> records{};
    std::array<std::uintptr_t,MaximumAttachments> geometry{};
};

inline bool Address(std::uintptr_t value,std::size_t bytes) {
    return value>=0x10000u&&value<=0xffffffffu-bytes;
}

struct DrawEntryFields {
    std::uint32_t vtable=0,geometry=0;
};

// Read the two x86 entry fields together only when the entire span is in
// one page. The caller keeps the scalar path for page boundaries and failed
// reads, including its existing vtable qualification before reading geometry.
template<class Reader> bool ReadDrawEntryOnPage(std::uintptr_t entry,std::size_t pageBytes,
                                                Reader read,DrawEntryFields& output) {
    constexpr std::size_t GeometryOffset=0xF4;
    constexpr std::size_t Span=GeometryOffset+sizeof(std::uint32_t);
    if(pageBytes<Span||(pageBytes&(pageBytes-1))||
       (entry&(pageBytes-1))>pageBytes-Span||!Address(entry,Span))return false;
    std::uint8_t bytes[Span];
    if(!read(entry,bytes,sizeof(bytes)))return false;
    std::memcpy(&output.vtable,bytes,sizeof(output.vtable));
    std::memcpy(&output.geometry,bytes+GeometryOffset,sizeof(output.geometry));
    return true;
}

// These pointer-free blocks are native x86 fields, also when a host-side
// fixture compiles this header as x64. Read adjacent fields once rather than
// issuing a self-ReadProcessMemory syscall for each DWORD/byte.
struct GeometryLinks {
    std::uint32_t parent=0,reserved[3]{},owner=0;
};
struct HandState {
    std::uint32_t item=0;
    std::uint8_t enabled=0,reserved[3]{};
    std::uint32_t attacher=0;
};
// Cache only a negative ownership decision for one exact render generation.
// A hit never authorizes a pose write. Current root links are read again,
// and held item/attachment generations participate in the identity.
struct NegativeDrawKey {
    std::uint64_t generation=0,signature=0;
    std::uintptr_t player=0,entry=0,geometry=0;
    GeometryLinks root{};
};
class NegativeDrawMemo {
public:
    constexpr NegativeDrawMemo()=default;
    constexpr explicit NegativeDrawMemo(bool geometryIdentity):geometryIdentity_(geometryIdentity){}
    bool Contains(const NegativeDrawKey& key)const {
        if(!key.generation||!key.player||!key.entry||!key.geometry)return false;
        const auto& item=items_[Slot(key)];
        return item.generation==key.generation&&item.signature==key.signature&&item.player==key.player&&
            (geometryIdentity_||item.entry==key.entry)&&item.geometry==key.geometry&&std::memcmp(&item.root,&key.root,sizeof(key.root))==0;
    }
    void Store(const NegativeDrawKey& key) {
        if(key.generation&&key.player&&key.entry&&key.geometry)items_[Slot(key)]=key;
    }
private:
    unsigned Slot(const NegativeDrawKey& key)const {
        if(!geometryIdentity_)return static_cast<unsigned>((key.entry>>4)^(key.geometry>>5)^key.generation)&1023u;
        auto hash=static_cast<std::uint32_t>(key.geometry>>4)^static_cast<std::uint32_t>(key.generation);
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;return (hash^(hash>>16))&1023u;
    }
    bool geometryIdentity_=false;
    std::array<NegativeDrawKey,1024> items_{};
};
inline std::uint64_t OwnershipSignature(const Snapshot& snapshot,const std::uintptr_t weapons[2],
                                      const std::uint32_t references[2]) {
    std::uint64_t hash=1469598103934665603ull;
    const auto add=[&](std::uint64_t value){hash^=value;hash*=1099511628211ull;};
    add(snapshot.player);add(snapshot.component);add(snapshot.count);
    for(unsigned side=0;side<2;++side){add(weapons[side]);add(references[side]);}
    for(unsigned i=0;i<snapshot.count&&i<MaximumAttachments;++i) {
        add(snapshot.records[i].geometryRef);add(snapshot.records[i].bone);add(snapshot.geometry[i]);
    }
    return hash;
}
static_assert(sizeof(GeometryLinks)==20&&offsetof(GeometryLinks,owner)==16,"Native geometry parent/owner span");
static_assert(sizeof(HandState)==12&&offsetof(HandState,enabled)==4&&offsetof(HandState,attacher)==8,"Native IKHAND item/state span");
template<class Reader> bool ReadGeometryLinks(std::uintptr_t geometry,Reader read,GeometryLinks& output) {
    if(!Address(geometry,0x50+sizeof(GeometryLinks)))return false;
    GeometryLinks next;
    if(!read(geometry+0x50,&next,sizeof(next)))return false;
    output=next;return true;
}
template<class Reader> bool ReadHandState(std::uintptr_t hand,Reader read,HandState& output) {
    if(!Address(hand,0x60+sizeof(HandState)))return false;
    HandState next;
    if(!read(hand+0x60,&next,sizeof(next)))return false;
    output=next;return true;
}
template<class T,class Reader> bool Read(std::uintptr_t address,T& value,Reader read) {
    return Address(address,sizeof(value))&&read(address,&value,sizeof(value));
}

// Native 0x45C650 resolves normal ZREFs with an index plus generation, not
// a raw pointer. Attachment creation 0x45C450 always stores this normal form;
// compressed internal offsets are deliberately not accepted here.
template<class Reader> bool ResolveGeometry(std::uintptr_t image,std::uint32_t reference,
                                           Reader read,std::uintptr_t& output) {
    if(!reference||(reference&0xc0000000u))return false;
    std::uint32_t manager=0,pool=0,stride=0,capacity=0,data=0,generations=0;
    if(!Read(image+0x6c99b8,manager,read)||!Read(manager+0xc,pool,read)||
       !Read(pool+4,stride,read)||stride!=0x70||!Read(pool+8,capacity,read)||
       !capacity||capacity>0x100000||!Read(pool+0xc,data,read)||
       !Read(pool+0x10,generations,read))return false;
    if(!Address(data,std::size_t(capacity)*stride)||!Address(generations,std::size_t(capacity)*2))return false;
    const std::uint32_t index=(reference-1)%capacity,generation=(reference-1)/capacity;
    std::uint16_t actual=0;
    if(generation>=0x8000u||!Read(generations+std::uintptr_t(index)*2,actual,read)||actual!=generation)return false;
    const auto geometry=data+std::uintptr_t(index)*stride;
    if(geometry<data||!Address(geometry,stride))return false;
    output=geometry;return true;
}

template<class Reader> bool Capture(std::uintptr_t image,std::uintptr_t player,
                                   Reader read,Snapshot& output) {
    Snapshot next;next.player=player;std::uint32_t component=0,count=0;
    if(!Read(player+0x88,component,read)||!component||!Read(component+0x14,count,read)||
       count>MaximumAttachments)return false;
    next.component=component;next.count=count;
    if(count&&(!Address(component+0x18,count*sizeof(Record))||
       !read(component+0x18,next.records.data(),count*sizeof(Record))))return false;
    for(unsigned i=0;i<count;++i) {
        // Hand attachments are handled by the native wrist remap. Carry only
        // body slots here, including spine and hip, when IKHAND is inactive.
        const auto& record=next.records[i];
        if(record.bone<128&&record.bone!=31&&record.bone!=32)
            ResolveGeometry(image,record.geometryRef,read,next.geometry[i]);
    }
    output=next;return true;
}

template<class Reader> bool StillAttached(std::uintptr_t image,const Snapshot& snapshot,
                                         unsigned index,Reader read) {
    if(index>=snapshot.count||index>=MaximumAttachments||!snapshot.geometry[index])return false;
    std::uint32_t component=0,count=0;Record current;std::uintptr_t geometry=0;
    return Read(snapshot.player+0x88,component,read)&&component==snapshot.component&&
        Read(component+0x14,count,read)&&count<=MaximumAttachments&&index<count&&
        Read(component+0x18+index*sizeof(Record),current,read)&&
        current.geometryRef==snapshot.records[index].geometryRef&&current.bone==snapshot.records[index].bone&&
        ResolveGeometry(image,current.geometryRef,read,geometry)&&geometry==snapshot.geometry[index];
}

// Native PrimControl::GetResource (0x467CA0) accepts a resource-table index
// or an internal tagged pointer. These are resource handles, not geometry
// ZREFs, whose separate generation-checked resolver is above.
template<class Reader> bool Resource(std::uintptr_t image,std::uint32_t handle,
                                    Reader read,std::uintptr_t& output) {
    if(!handle)return false;
    std::uint32_t pointer=handle&0x7fffffffu;
    if(!(handle&0x80000000u)) {
        const std::uint64_t slot=std::uint64_t(image)+0x71f308u+std::uint64_t(handle)*4;
        if(slot>0xffffffffu||!Read(static_cast<std::uintptr_t>(slot),pointer,read))return false;
    }
    if(!Address(pointer,4))return false;
    output=pointer;return true;
}

// An attachment's primitive index is not an IKHAND attacher ID. Prove its
// current costume resolves it to the named wrist, rather than trusting 20/25
// alone. This proves the slot/bone only, never the attached object's owner.
// Layout: PrimControl getters 0x484170/+BC and 0x4841C0/+C4 both use
// 0x468D50: type-7 model -> part table+4 -> skeletal header. Header+4 is
// BoneDefs, header+14 is primitive->model-bone lookup; BoneDefs stride64,
// name+1C. This path reads immutable model resources only.
template<class Reader> bool HandPrimitive(std::uintptr_t image,const Snapshot& snapshot,unsigned index,
                                         Reader read,int& side) {
    if(index>=snapshot.count||index>=MaximumAttachments)return false;
    const auto primitive=snapshot.records[index].bone;
    const int candidate=primitive==20?0:primitive==25?1:-1;
    if(candidate<0||!StillAttached(image,snapshot,index,read))return false;
    std::uint32_t playerVtable=0,model=0,lookup=0,modelCount=0,root=0,handle=0;
    if(!Read(snapshot.player,playerVtable,read)||playerVtable!=image+0x56582c||
       !Read(snapshot.player+0x70,model,read)||!model||!Read(model+0xc,lookup,read)||!lookup||
       !Read(model+0x4a0,modelCount,read)||modelCount<2||modelCount>256||
       !Read(snapshot.player+4,root,read)||!Read(root+0x68,handle,read))return false;
    std::uintptr_t resource=0,parts=0,header=0,definitions=0,primitiveTable=0;
    std::uint16_t type=0;std::uint8_t flags=0;
    std::uint32_t partsHandle=0,headerHandle=0,definitionsHandle=0,primitiveHandle=0,count=0;
    if(!Resource(image,handle,read,resource)||!Read(resource+2,type,read)||type!=7||
       !Read(resource+0xc,flags,read)||!(flags&1)||!Read(resource+0x10,partsHandle,read)||
       !Resource(image,partsHandle,read,parts)||!Read(parts+4,headerHandle,read)||
       !Resource(image,headerHandle,read,header)||!Read(header,count,read)||count<2||count>256||
       !Read(header+4,definitionsHandle,read)||!Resource(image,definitionsHandle,read,definitions)||
       !Read(header+0x14,primitiveHandle,read)||!Resource(image,primitiveHandle,read,primitiveTable))return false;
    std::uint8_t primitiveBone=255,standardBone=255;char name[16]{};
    if(!Read(primitiveTable+primitive,primitiveBone,read)||
       !Read(lookup+(candidate?24:18),standardBone,read)||primitiveBone!=standardBone||
       primitiveBone>=count||primitiveBone>=modelCount||
       !Read(definitions+std::uintptr_t(primitiveBone)*64+0x1c,name,read))return false;
    const char* expected=candidate?"RIGHT_HAND":"LEFT_HAND";
    if(std::memcmp(name,expected,std::strlen(expected)+1))return false;
    side=candidate;return true;
}

// A cuff/accessory GROUP is not an IKHAND-held firearm. In addition to the
// named wrist proof above, retain its exact GROUP owner and root checks.
template<class Reader> bool HandGroup(std::uintptr_t image,const Snapshot& snapshot,unsigned index,
                                     Reader read,int& side,std::uintptr_t& owner) {
    if(index>=snapshot.count||index>=MaximumAttachments||
       (snapshot.records[index].bone!=20&&snapshot.records[index].bone!=25)||
       !StillAttached(image,snapshot,index,read))return false;
    const auto geometry=snapshot.geometry[index];
    std::uint32_t group=0,vtable=0,root=0;
    if(!Read(geometry+0x60,group,read)||!Read(group,vtable,read)||vtable!=image+0x5462cc||
       !Read(group+4,root,read)||root!=geometry)return false;
    int candidate=-1;
    if(!HandPrimitive(image,snapshot,index,read,candidate))return false;
    side=candidate;owner=group;return true;
}

// A named hip GROUP is a native attachment, not the world cable. Some
// costumes instead contain a rigid model part (HipRigid below), without
// any record in the player's attachment component.
// The pinned primitive table names35 "hip attacher"; standard skeleton63
// has the same name in the independently observed character BoneDefs. Keep
// primitive/standard IDs separate and prove their current costume mapping.
template<class Reader> bool VerifyHipImage(std::uintptr_t image,Reader read) {
    std::uint32_t primitive=0,name=0;char text[13]{};
    return Read(image+0x6319A0,primitive,read)&&primitive==35&&
        Read(image+0x6319A4,name,read)&&name==image+0x5385B8&&
        Read(name,text,read)&&!std::memcmp(text,"hip attacher",sizeof(text))&&
        Read(image+0x6318C0,primitive,read)&&primitive==2&&
        Read(image+0x6318C4,name,read)&&name==image+0x53870C&&
        read(name,text,sizeof("PELVIS"))&&!std::memcmp(text,"PELVIS",sizeof("PELVIS"));
}
template<class Reader> bool HipGroup(std::uintptr_t image,const Snapshot& snapshot,unsigned index,
                                    Reader read,std::uintptr_t& owner) {
    if(index>=snapshot.count||index>=MaximumAttachments||(snapshot.records[index].bone!=35&&snapshot.records[index].bone!=2)||
       !StillAttached(image,snapshot,index,read))return false;
    std::uint32_t playerVtable=0,model=0,lookup=0,modelCount=0,root=0,handle=0;
    std::uint32_t group=0,groupVtable=0,groupRoot=0;
    const auto geometry=snapshot.geometry[index];
    if(!Read(snapshot.player,playerVtable,read)||playerVtable!=image+0x56582c||
       !Read(snapshot.player+0x70,model,read)||!model||!Read(model+0xc,lookup,read)||!lookup||
       !Read(model+0x4a0,modelCount,read)||modelCount<2||modelCount>256||
       !Read(snapshot.player+4,root,read)||!Read(root+0x68,handle,read)||
       !Read(geometry+0x60,group,read)||!Read(group,groupVtable,read)||groupVtable!=image+0x5462cc||
       !Read(group+4,groupRoot,read)||groupRoot!=geometry)return false;
    std::uintptr_t resource=0,parts=0,header=0,definitions=0,primitiveTable=0;
    std::uint16_t type=0;std::uint8_t flags=0;
    std::uint32_t partsHandle=0,headerHandle=0,definitionsHandle=0,primitiveHandle=0,count=0;
    if(!Resource(image,handle,read,resource)||!Read(resource+2,type,read)||type!=7||
       !Read(resource+0xc,flags,read)||!(flags&1)||!Read(resource+0x10,partsHandle,read)||
       !Resource(image,partsHandle,read,parts)||!Read(parts+4,headerHandle,read)||
       !Resource(image,headerHandle,read,header)||!Read(header,count,read)||count<2||count>256||
       !Read(header+4,definitionsHandle,read)||!Resource(image,definitionsHandle,read,definitions)||
       !Read(header+0x14,primitiveHandle,read)||!Resource(image,primitiveHandle,read,primitiveTable))return false;
    std::uint8_t primitiveBone=255,standardBone=255;char name[16]{};
    const auto primitive=snapshot.records[index].bone;
    const char* expectedName=primitive==35?"hip attacher":"PELVIS";
    if(!Read(primitiveTable+primitive,primitiveBone,read)||!Read(lookup+(primitive==35?63:0),standardBone,read)||primitiveBone!=standardBone||
       primitiveBone>=count||primitiveBone>=modelCount||
       !Read(definitions+std::uintptr_t(primitiveBone)*64+0x1c,name,read)||
       std::memcmp(name,expectedName,std::strlen(expectedName)+1))return false;
    // A generation/name check alone does not authorize a recycled GROUP.
    std::uint32_t afterOwner=0,afterRoot=0,afterModel=0,afterVtable=0;
    if(!StillAttached(image,snapshot,index,read)||!Read(geometry+0x60,afterOwner,read)||afterOwner!=group||
       !Read(group+4,afterRoot,read)||afterRoot!=geometry||!Read(group,afterVtable,read)||afterVtable!=image+0x5462CC||
       !Read(snapshot.player+0x70,afterModel,read)||afterModel!=model)return false;
    owner=group;return true;
}
struct HipRigidIdentity {
    std::uintptr_t player=0,geometry=0,model=0,part=0,resource=0,cache=0;
    std::uint32_t bone=0,standard=63;
};
template<class Reader> bool VerifyHipRigidImage(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[32]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char part[]={0x8B,0x48,4,0x8B,0x41,0x14};
    const unsigned char bone[]={0x8B,0x40,0x3C,0x83,0xC0,3,0x8D,0x34,0x40,0xC1,0xE6,4,0x03,0xB5,8,1,0,0};
    const unsigned char world[]={0x8B,0xFA,0xD9,0x44,0x24,0x20,0xB9,0x0C,0,0,0,0xD9,0x9C,0x24,0xA4,0,0,0,0x8D,0x74,0x24,0x78,0xF3,0xA5};
    return match(0x739EE,part)&&match(0x73A08,bone)&&match(0x73B17,world);
}
// Actual 4739E0 rigid binder: extra+4 -> resource handle at+14;
// resource+3C is the MODEL bone, and entry+108+(bone+3)*48 its palette.
// A rigid piece of the current player's own costume does not occupy +88.
// Require the exact current player root and the costume's named hip/PELVIS
// bone (belt meshes can be authored directly on the pelvis, without an
// extra hip-attacher joint). No other body bones are authorized here;
// unrelated actors, attached world cable, hand parts and guessed proximity
// are all insufficient. The native callback's original source is read-only.
template<class Reader> bool HipRigid(std::uintptr_t image,std::uintptr_t player,std::uintptr_t entry,
                                    std::uintptr_t extra,Reader read,HipRigidIdentity& output,
                                    unsigned* observedBone=nullptr) {
    std::uint32_t vt=0,geometry=0,owner=0,root=0,model=0,lookup=0,modelCount=0,cache=0,part=0,handle=0,bone=0;
    if(!Read(entry,vt,read)||vt!=image+0x541070||!Read(entry+0xF4,geometry,read)||
       !Read(geometry+0x60,owner,read)||owner!=player||!Read(player,vt,read)||vt!=image+0x56582C||
       !Read(player+4,root,read)||root!=geometry||!Read(player+0x70,model,read)||!model||
       !Read(model+0xC,lookup,read)||!lookup||!Read(model+0x4A0,modelCount,read)||modelCount<2||modelCount>256||
       !Read(entry+0x108,cache,read)||!cache||!Read(extra+4,part,read)||!part||!Read(part+0x14,handle,read))return false;
    std::uintptr_t resource=0;
    if(!Resource(image,handle,read,resource)||!Read(resource+0x3C,bone,read)||bone>=modelCount)return false;
    if(observedBone)*observedBone=bone;
    std::uint8_t standardBone=255;unsigned standard=63;
    if(!Read(lookup+63,standardBone,read))return false;
    if(bone!=standardBone) {
        standard=0;
        if(!Read(lookup,standardBone,read)||bone!=standardBone)return false;
    }
    std::uintptr_t modelResource=0,parts=0,header=0,definitions=0;
    std::uint32_t modelHandle=0,partsHandle=0,headerHandle=0,definitionsHandle=0,count=0;
    std::uint16_t type=0;std::uint8_t flags=0;
    if(!Read(root+0x68,modelHandle,read)||!Resource(image,modelHandle,read,modelResource)||
       !Read(modelResource+2,type,read)||type!=7||!Read(modelResource+0xC,flags,read)||!(flags&1)||
       !Read(modelResource+0x10,partsHandle,read)||!Resource(image,partsHandle,read,parts)||
       !Read(parts+4,headerHandle,read)||!Resource(image,headerHandle,read,header)||
       !Read(header,count,read)||count<2||count>256||bone>=count||
       !Read(header+4,definitionsHandle,read)||!Resource(image,definitionsHandle,read,definitions))return false;
    char name[16]{};
    const char* expectedName=standard?"hip attacher":"PELVIS";
    if(!Read(definitions+std::uintptr_t(bone)*64+0x1C,name,read)||std::memcmp(name,expectedName,std::strlen(expectedName)+1))return false;
    // Recheck every mutable ownership/cache/resource edge after the name.
    std::uint32_t after=0;
    if(!Read(entry+0xF4,after,read)||after!=geometry||!Read(geometry+0x60,after,read)||after!=player||
       !Read(player+4,after,read)||after!=geometry||!Read(player+0x70,after,read)||after!=model||
       !Read(entry+0x108,after,read)||after!=cache||!Read(extra+4,after,read)||after!=part||
       !Read(part+0x14,after,read)||after!=handle||!Read(resource+0x3C,after,read)||after!=bone)return false;
    output={player,geometry,model,part,resource,cache,bone,standard};return true;
}
// Static/model children can be linked directly beneath the actor's scene
// geometry instead of occupying an animated attachment slot. Carry only an
// explicit CURRENT local actor-root ancestry; the root body itself and all
// actual cable classes are excluded. No proximity or guessed asset name.
struct BodyChild {
    std::uintptr_t player=0,root=0,model=0;
    unsigned count=0;
    std::array<std::uintptr_t,16> nodes{};
    std::array<GeometryLinks,16> links{};
};
inline bool SameBodyChild(const BodyChild& a,const BodyChild& b) {
    if(a.player!=b.player||a.root!=b.root||a.model!=b.model||a.count!=b.count)return false;
    for(unsigned i=0;i<a.count;++i)if(a.nodes[i]!=b.nodes[i]||std::memcmp(&a.links[i],&b.links[i],sizeof(GeometryLinks)))return false;
    return true;
}
template<class Reader> bool CaptureBodyChild(std::uintptr_t image,std::uintptr_t player,std::uintptr_t geometry,
                                            Reader read,BodyChild& output) {
    BodyChild next;next.player=player;std::uint32_t data=0,current=0,vt=0,root=0,model=0,count=0;
    if(!Read(image+0x68760C,data,read)||!Read(data+0xA2C,current,read)||current!=player||
       !Read(player,vt,read)||vt!=image+0x56582C||!Read(player+4,root,read)||!root||geometry==root||
       !Read(player+0x70,model,read)||!model||!Read(model+0x4A0,count,read)||count<2||count>256)return false;
    next.root=root;next.model=model;
    bool reached=false;
    for(unsigned depth=0;geometry&&depth<next.nodes.size();++depth) {
        for(unsigned i=0;i<depth;++i)if(next.nodes[i]==geometry)return false;
        auto& links=next.links[depth];
        if(!ReadGeometryLinks(geometry,read,links))return false;
        if(links.owner&&(!Read(links.owner,vt,read)||vt<image+0x500000||vt>image+0x680000||vt==image+0x57A79C))return false;
        next.nodes[depth]=geometry;next.count=depth+1;
        if(geometry==root){if(links.owner!=player)return false;reached=true;break;}
        // A skinned body slice directly owned by the player already has its
        // own palette/root binder. It is not a separate costume accessory.
        if(links.owner==player)return false;
        geometry=links.parent;
    }
    if(!reached)return false;
    for(unsigned i=0;i<next.count;++i){GeometryLinks after;
        if(!ReadGeometryLinks(next.nodes[i],read,after)||std::memcmp(&after,&next.links[i],sizeof(after)))return false;}
    std::uint32_t after=0;
    if(!Read(image+0x68760C,after,read)||after!=data||!Read(data+0xA2C,after,read)||after!=player||
       !Read(player+4,after,read)||after!=root||!Read(player+0x70,after,read)||after!=model)return false;
    output=next;return true;
}
template<class Reader> bool StillBodyChild(std::uintptr_t image,const BodyChild& snapshot,Reader read) {
    BodyChild after;
    return snapshot.count>1&&CaptureBodyChild(image,snapshot.player,snapshot.nodes[0],read,after)&&SameBodyChild(snapshot,after);
}
// Native animated GROUP attachments need not be on primitive35. A torso,
// pelvis or lower-body GROUP remains driven by original bone animation; the
// GPU body correction then carries its actual WORLD. Arm/head subtrees are
// excluded because hand IK/head masking require their own specific adapters.
template<class Reader> bool BodyGroup(std::uintptr_t image,const Snapshot& snapshot,unsigned index,
                                     Reader read,std::uintptr_t& owner) {
    if(index>=snapshot.count||index>=MaximumAttachments||!StillAttached(image,snapshot,index,read))return false;
    const unsigned primitive=snapshot.records[index].bone;
    if(primitive<1||primitive>36)return false;
    const auto geometry=snapshot.geometry[index];std::uint32_t vt=0,model=0,lookup=0,parents=0,modelCount=0,root=0,handle=0,group=0,groupRoot=0;
    if(!Read(snapshot.player,vt,read)||vt!=image+0x56582C||!Read(snapshot.player+0x70,model,read)||!model||
       !Read(model+0xC,lookup,read)||!lookup||!Read(model+0x10,parents,read)||!parents||
       !Read(model+0x4A0,modelCount,read)||modelCount<2||modelCount>256||
       !Read(snapshot.player+4,root,read)||!Read(root+0x68,handle,read)||
       !Read(geometry+0x60,group,read)||!Read(group,vt,read)||vt!=image+0x5462CC||
       !Read(group+4,groupRoot,read)||groupRoot!=geometry)return false;
    std::uintptr_t resource=0,parts=0,header=0,definitions=0,primitiveTable=0;
    std::uint16_t type=0;std::uint8_t flags=0;
    std::uint32_t partsHandle=0,headerHandle=0,definitionsHandle=0,primitiveHandle=0,count=0;
    if(!Resource(image,handle,read,resource)||!Read(resource+2,type,read)||type!=7||
       !Read(resource+0xC,flags,read)||!(flags&1)||!Read(resource+0x10,partsHandle,read)||
       !Resource(image,partsHandle,read,parts)||!Read(parts+4,headerHandle,read)||!Resource(image,headerHandle,read,header)||
       !Read(header,count,read)||count<2||count>256||!Read(header+4,definitionsHandle,read)||
       !Resource(image,definitionsHandle,read,definitions)||!Read(header+0x14,primitiveHandle,read)||
       !Resource(image,primitiveHandle,read,primitiveTable))return false;
    std::uint8_t bone=255,pelvis=255,head=255,leftArm=255,rightArm=255;
    if(!Read(primitiveTable+primitive,bone,read)||bone>=count||bone>=modelCount||
       !Read(lookup,pelvis,read)||!Read(lookup+6,head,read)||!Read(lookup+16,leftArm,read)||!Read(lookup+22,rightArm,read)||
       !pelvis||!head||!leftArm||!rightArm||pelvis>=modelCount||head>=modelCount||leftArm>=modelCount||rightArm>=modelCount)return false;
    std::array<std::uint8_t,256> hierarchy{};
    if(!read(parents,hierarchy.data(),modelCount))return false;
    const auto descendant=[&](unsigned child,unsigned ancestor){
        for(unsigned depth=0;depth<modelCount;++depth){if(child==ancestor)return true;if(!child||hierarchy[child]>=child)return false;child=hierarchy[child];}return false;};
    if(!descendant(bone,pelvis)||descendant(bone,head)||descendant(bone,leftArm)||descendant(bone,rightArm))return false;
    // The primitive enum and model table are separate namespaces. Prove
    // their semantic match through both actual native names, not equal IDs.
    struct PrimitiveName {std::uint32_t value=0,name=0;};std::array<PrimitiveName,32> names{};
    // Native enum IDs are sparse (5 and27..30 are absent). Its descriptor
    // table is not an array indexed by primitive ID.
    if(!read(image+0x6318B0,names.data(),sizeof(names)))return false;
    std::uint32_t nativeName=0;for(const auto& item:names)if(item.value==primitive){nativeName=item.name;break;}
    char expected[32]{},actual[32]{};
    if(nativeName<image+0x500000||nativeName>=image+0x680000||
       !read(nativeName,expected,sizeof(expected))||!std::memchr(expected,0,sizeof(expected))||!expected[0]||
       !read(definitions+std::uintptr_t(bone)*64+0x1C,actual,sizeof(actual))||
       std::memcmp(actual,expected,std::strlen(expected)+1))return false;
    std::uint32_t after=0;
    if(!StillAttached(image,snapshot,index,read)||!Read(geometry+0x60,after,read)||after!=group||
       !Read(group+4,after,read)||after!=geometry||!Read(group,after,read)||after!=image+0x5462CC||
       !Read(snapshot.player+0x70,after,read)||after!=model)return false;
    owner=group;return true;
}
}
