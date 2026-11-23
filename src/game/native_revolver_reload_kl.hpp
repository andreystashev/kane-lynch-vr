
#pragma once
#include "native_weapon_math.hpp"
#include "physical_reload_runtime_kl.hpp"
#include "reload_contact_geometry_kl.hpp"
#include "reload_parts_kl.hpp"

namespace NativeRevolverReloadKL {
namespace G = ReloadContactGeometryKL;
namespace M = PhysicalReloadRuntimeKL;
using namespace NativeCameraMath;

constexpr unsigned ChamberCount = 6;
enum class PartKind { Other, Crane, Cylinder, Round };
struct PartId { PartKind kind=PartKind::Other; unsigned round=0; };

inline PartId Classify(const ReloadPartsKL::Part& part) {
    if(!part.boundsValid || !part.bindValid)return {};
    if(!std::strcmp(part.name,"w_bone_slide") &&
       std::fabs(part.half.x-.863997f)<.01f && std::fabs(part.half.y-2.271004f)<.01f &&
       std::fabs(part.half.z-3.396448f)<.01f)return {PartKind::Crane,0};
    if(!std::strcmp(part.name,"w_bone_cylinder") &&
       std::fabs(part.half.x-2.781131f)<.01f && std::fabs(part.half.y-2.800678f)<.01f &&
       std::fabs(part.half.z-2.843664f)<.01f)return {PartKind::Cylinder,0};
    constexpr char prefix[]="w_bone_bullet_0";
    constexpr unsigned digit=sizeof(prefix)-1;
    if(!std::strncmp(part.name,prefix,digit) && part.name[digit]>='1' &&
       part.name[digit]<='6' && !part.name[digit+1] &&
       std::fabs(part.half.x-.88f)<.01f && std::fabs(part.half.z-2.135867f)<.01f)
        return {PartKind::Round,unsigned(part.name[digit]-'1')};
    return {};
}

struct Contacts {
    bool haveCrane=false,haveCylinder=false;
    std::uintptr_t catalogModel=0;
    ReloadPartsKL::Part crane{},cylinder{};
    CameraPose craneLocal{},cylinderLocal{};
    ReloadPartsKL::Part rounds[ChamberCount]{};
};

template<class Reader>
bool LoadCatalog(Contacts& contacts,std::uintptr_t image,std::uintptr_t resourceModel,Reader read) {
    if(!resourceModel)return false;
    if(contacts.catalogModel==resourceModel)return true;
    ReloadPartsKL::Catalog catalog;
    if(!ReloadPartsKL::ModelCatalog(image,resourceModel,read,catalog))return false;
    unsigned roundMask=0;
    ReloadPartsKL::Part foundRounds[ChamberCount]{},crane{},cylinder{};
    for(unsigned i=0;i<catalog.count;++i){
        ReloadPartsKL::Part bone;
        if(!ReloadPartsKL::Bone(catalog,i,read,bone))return false;
        const auto part=Classify(bone);
        if(part.kind==PartKind::Crane)crane=bone;
        if(part.kind==PartKind::Cylinder)cylinder=bone;
        if(part.kind==PartKind::Round){
            if(roundMask&(1u<<part.round))return false;
            roundMask|=1u<<part.round;
            foundRounds[part.round]=bone;
        }
    }
    // The Taurus "slide" is the crane and the round group is its descendant.
    // The zero-size parent w_bone_bullets cannot stand in for the six meshes.
    const auto parent=[&](unsigned child,unsigned& id){
        return NativeWeaponOwnership::Read(catalog.definitions+child*64+12,id,read);
    };
    unsigned group=0,owner=0;
    if(!crane.model || !cylinder.model || roundMask!=(1u<<ChamberCount)-1u ||
       !parent(cylinder.bone,owner) || owner!=crane.bone ||
       !parent(foundRounds[0].bone,group) || group>=catalog.count ||
       !parent(group,owner) || owner!=cylinder.bone)return false;
    for(const auto& round:foundRounds)if(!parent(round.bone,owner) || owner!=group)return false;
    std::copy(foundRounds,foundRounds+ChamberCount,contacts.rounds);
    contacts.catalogModel=resourceModel;
    return true;
}

inline bool Observe(Contacts& contacts,const ReloadPartsKL::Part& part,const CameraPose& local) {
    if(!ValidatePose(local,.05f))return false;
    switch(Classify(part).kind){
        case PartKind::Crane:
            contacts.crane=part;
            contacts.craneLocal=local;
            contacts.haveCrane=true;
            return true;
        case PartKind::Cylinder:
            contacts.cylinder=part;
            contacts.cylinderLocal=local;
            contacts.haveCylinder=true;
            return true;
        default:
            return false;
    }
}

struct Geometry {
    std::uintptr_t model=0;
    Vec3 pivot{},axis{};
    float angle=0;
    CameraPose closedTip{},openTip{},mouth{};
    Vec3 gripHalf{};
    float feedLength=0;
};

inline CameraPose Swing(const Geometry& geometry,const CameraPose& input,float opening){
    const float amount=geometry.angle*std::clamp(opening,0.f,1.f)*.5f;
    const auto axis=geometry.axis;
    const float sine=std::sin(amount);
    const Quat turn{axis.x*sine,axis.y*sine,axis.z*sine,std::cos(amount)};
    return {{Rotate(turn,input.basis.forward),Rotate(turn,input.basis.up),Rotate(turn,input.basis.right)},
            Add(geometry.pivot,Rotate(turn,Subtract(input.position,geometry.pivot)))};
}

inline bool Build(const Contacts& contacts,const CameraPose& wrist,const CameraPose& gun,float units,Geometry& result){
    if(!contacts.haveCrane || !contacts.haveCylinder || !contacts.catalogModel ||
       contacts.crane.model!=contacts.catalogModel || contacts.cylinder.model!=contacts.catalogModel ||
       !ValidatePose(wrist,.05f) || !ValidatePose(gun,.05f) || !std::isfinite(units) || units<1.f || units>1000.f)
        return false;

    const auto crane=ReloadMathKL::ToWorld(wrist,contacts.craneLocal);
    const auto cylinder=ReloadMathKL::ToWorld(wrist,contacts.cylinderLocal);
    Geometry next;
    next.model=contacts.catalogModel;
    next.pivot=crane.position;
    Vec3 centers[ChamberCount]{};
    G::Bounds boxes[ChamberCount]{};
    for(unsigned i=0;i<ChamberCount;++i){
        const auto& round=contacts.rounds[i];
        if(round.model!=next.model || Classify(round).kind!=PartKind::Round)return false;
        const auto bone=ReloadMathKL::ToWorld(cylinder,ReloadMathKL::ToLocal(contacts.cylinder.bind,round.bind));
        if(!G::BuildBounds(bone,round.centre,round.half,boxes[i]))return false;
        centers[i]=boxes[i].world.position;
        next.closedTip.position=Add(next.closedTip.position,Scale(centers[i],1.f/ChamberCount));
    }

    // The round meshes distinguish their long dimension from both chamber
    // axes. Use that native line, and let the current muzzle select its sign.
    next.axis=boxes[0].world.basis.forward;
    const float aimAgreement=Dot(next.axis,gun.basis.forward);
    if(std::fabs(aimAgreement)<.75f)return false;
    if(aimAgreement<0.f)next.axis=Scale(next.axis,-1.f);
    for(const auto& box:boxes)if(std::fabs(Dot(box.world.basis.forward,next.axis))<.985f)return false;

    const auto clusterCenter=next.closedTip.position;
    const auto radius=Subtract(clusterCenter,next.pivot);
    const auto radial=Subtract(radius,Scale(next.axis,Dot(radius,next.axis)));
    const float distance2=Dot(radial,radial);
    if(distance2<.010f*.010f*units*units || distance2>.075f*.075f*units*units)return false;
    // Use the authored hinge position. An 80 degree swing keeps the virtual
    // loader clear of the receiver. Its sign places the chambers on the
    // support-hand side of the weapon regardless of the player's wrist roll.
    const float sign=Dot(Cross(next.axis,radial),gun.basis.right)<0.f?1.f:-1.f;
    next.angle=sign*1.3962634f;

    Vec3 up;
    auto radialUp=Subtract(centers[0],clusterCenter);
    radialUp=Subtract(radialUp,Scale(next.axis,Dot(radialUp,next.axis)));
    if(!ReloadPresentationKL::Unit(radialUp,up))return false;
    next.closedTip.basis={next.axis,up,Cross(up,next.axis)};
    const auto grow=[&](Vec3 direction,const G::Bounds& box){
        return std::fabs(Dot(Subtract(box.world.position,clusterCenter),direction))+G::Extent(box,direction);
    };
    for(const auto& box:boxes){
        next.gripHalf.x=std::max(next.gripHalf.x,grow(next.closedTip.basis.right,box));
        next.gripHalf.y=std::max(next.gripHalf.y,grow(next.closedTip.basis.up,box));
        next.gripHalf.z=std::max(next.gripHalf.z,grow(next.axis,box));
    }
    if(!Finite(next.gripHalf) || next.gripHalf.z<.014f*units || next.gripHalf.z>.065f*units)return false;
    next.feedLength=next.gripHalf.z;
    next.closedTip.position=Add(clusterCenter,Scale(next.axis,next.feedLength));
    next.openTip=Swing(next,next.closedTip,1.f);
    next.mouth=next.openTip;
    next.mouth.position=Subtract(next.openTip.position,Scale(next.axis,2.f*next.feedLength));
    if(!ValidatePose(next.closedTip,.05f) || !ValidatePose(next.mouth,.05f) || !ValidatePose(next.openTip,.05f))
        return false;
    result=next;
    return true;
}

inline void Frame(const Geometry& geometry,M::Frame& frame){
    frame.slideAvailable=false;
    frame.readyOnInsert=true;
    frame.receiver=geometry.mouth;
    frame.seatedReceiver=geometry.openTip;
    frame.seatedReceiverValid=true;
    frame.magazine=ReloadPresentationKL::HeldGrip(frame.palm,geometry.gripHalf,frame.units);
    frame.magazine.position=Add(frame.magazine.position,Scale(frame.magazine.basis.forward,geometry.feedLength));
    frame.magazineBodyLength=2.f*geometry.feedLength/frame.units;
    frame.magazineValid=true;
}

inline Geometry Rebase(const Geometry& geometry,const CameraPose& from,const CameraPose& to){
    auto copy=geometry;
    copy.pivot=Add(to.position,ReloadMathKL::Transform(to.basis,
        ReloadMathKL::Inverse(from.basis,Subtract(geometry.pivot,from.position))));
    copy.axis=ReloadMathKL::Transform(to.basis,ReloadMathKL::Inverse(from.basis,geometry.axis));
    copy.closedTip=ReloadMathKL::ToWorld(to,ReloadMathKL::ToLocal(from,geometry.closedTip));
    copy.openTip=ReloadMathKL::ToWorld(to,ReloadMathKL::ToLocal(from,geometry.openTip));
    copy.mouth=ReloadMathKL::ToWorld(to,ReloadMathKL::ToLocal(from,geometry.mouth));
    return copy;
}

enum class DrawDisposition { Unchanged, Moved, Hidden };
inline DrawDisposition PosePart(const Geometry& geometry,const ReloadPartsKL::Part& part,bool inserted,bool held,
                               unsigned cylinderRounds,const CameraPose& heldCluster,float opening,CameraPose& output){
    if(!geometry.model || part.model!=geometry.model || !std::isfinite(opening))return DrawDisposition::Unchanged;
    const auto id=Classify(part);
    if(id.kind==PartKind::Other)return DrawDisposition::Unchanged;
    if(id.kind==PartKind::Round && !inserted){
        if(!held || id.round>=cylinderRounds)return DrawDisposition::Hidden;
        CameraPose carried;
        if(NativeWeaponMath::Remap(geometry.closedTip,heldCluster,output,carried)){
            output=carried;
            return DrawDisposition::Moved;
        }
        return DrawDisposition::Unchanged;
    }
    if(opening>0.f){
        if(id.kind==PartKind::Round && id.round>=cylinderRounds)return DrawDisposition::Hidden;
        const auto moved=Swing(geometry,output,opening);
        if(ValidatePose(moved,.05f)){
            output=moved;
            return DrawDisposition::Moved;
        }
    }
    return DrawDisposition::Unchanged;
}
}
