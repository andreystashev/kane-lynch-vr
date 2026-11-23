#include "native_carried_camera_math.hpp"
#include "stereo_render_math.hpp"
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
using namespace NativeCameraMath;
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
bool Near(Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.0001f;}
bool Same(const Basis& a,const Basis& b){return Near(a.forward,b.forward)&&Near(a.up,b.up)&&Near(a.right,b.right);}
Basis Turn(Basis b,float yaw,float pitch=0){Quat y{0,std::sin(yaw/2),0,std::cos(yaw/2)},p{std::sin(pitch/2),0,0,std::cos(pitch/2)};const auto q=Multiply(y,p);return {Rotate(q,b.forward),Rotate(q,b.up),Rotate(q,b.right)};}
constexpr std::uintptr_t Image=0x400000,Data=0x10000000,Player=0x11000000,Move=0x12000000,
    Dispatcher=0x13000000,Entity=0x14000000,Record=0x15000000,Controller=0x16000000,
    Geometry=0x17000000,Parent=0x18000000,Gun=0x19000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t trigger=0,target=0;std::uint32_t replacement=0;
    template<class T>void Put(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(T);++i)bytes[at+i]=p[i];}
    void U32(std::uintptr_t at,std::uint32_t value){Put(at,value);}
    bool Read(std::uintptr_t at,void* out,std::size_t size){for(std::size_t i=0;i<size;++i){auto it=bytes.find(at+i);if(it==bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}
        if(trigger==at){trigger=0;U32(target,replacement);}return true;}
    Memory(){
        U32(Image+0x68760C,Data);U32(Data+0xA2C,Player);U32(Player,Image+0x56582C);
        U32(Player+0x67C,Move);U32(Move,Image+0x566F54);U32(Move+0x38,Player);U32(Move+0x44,13);U32(Player+0x5F8+13*4,Move);
        U32(Player+0x194,Dispatcher);U32(Dispatcher,Image+0x54DE4C);U32(Dispatcher+8,Player);U32(Dispatcher+4,0);
        U32(Player+0xB04,Record);U32(Player+0xB10,Controller);U32(Record+0x3C,Player);U32(Record+0x40,Controller);
        U32(Controller+0xF0,Record);U32(Controller+0x4C,Entity);U32(Controller+0x20,Gun);Put(Controller+0x44,std::uint8_t(1));U32(Controller+0x48,Player);U32(Controller+0x24,0);
        U32(Entity,Image+0x57F7EC);U32(Entity+0x7AC,Record);U32(Entity+0x7B0,Record+0x44);U32(Entity+4,Geometry);
        NativePlayerPose::Geometry geom;geom.pose.basis={{0,0,1},{0,1,0},{1,0,0}};geom.pose.position={12,34,56};geom.parent=Parent;Put(Geometry,geom);U32(Geometry+0x60,Entity);
        geom.pose.basis=Turn(geom.pose.basis,.7f,.3f);geom.pose.position={1000,2000,3000};geom.parent=0;Put(Parent,geom);
    }
    auto Reader(){return [this](auto at,auto* out,auto size){return Read(at,out,size);};}
    NativeActionKL::State Action(){return NativeActionKL::Snapshot(Image,Reader());}
};
void CarrierIdentity(){
    using namespace NativeCarriedCamera;
    Memory memory;Reference result;Check(MountedReference(Image,memory.Action(),memory.Reader(),result)&&result.kind==2&&result.identity==Geometry,"native mounted record/controller/entity reciprocal graph selects the actual base geometry");
    CameraPose expected;Check(NativePlayerPose::World(Geometry,memory.Reader(),expected)&&Near(result.root.position,expected.position)&&Same(result.root.basis,expected.basis),"carrier reference follows full geometry WORLD ancestry, not local +4F7900 copy");
    memory.U32(Controller+0x20,Gun+0x100);Reference changed;Check(MountedReference(Image,memory.Action(),memory.Reader(),changed)&&Same(result.root.basis,changed.root.basis),"moving barrel object cannot rotate the camera's base");
    Memory car;car.U32(Entity,Image+0x582D14);car.U32(Entity+0x838,Record);car.U32(Entity+0x83C,Record+0x44);
    car.U32(Entity+0x7AC,0);car.U32(Entity+0x7B0,0);
    Check(MountedReference(Image,car.Action(),car.Reader(),changed)&&changed.kind==2&&changed.identity==Geometry&&Near(changed.root.position,result.root.position),
        "vehicle mounted gun selects the actual HavokCar base through its distinct proved record vector");
    memory.Put(Controller+0x44,std::uint8_t(0));memory.U32(Controller+0x48,0);Check(MountedReference(Image,memory.Action(),memory.Reader(),changed),"camera base remains valid while native MG aim is temporarily automated");
    const std::uintptr_t breaks[]={Player+0xB04,Player+0xB10,Record+0x3C,Record+0x40,Controller+0xF0,Controller+0x4C,Entity,Entity+4,Entity+0x7AC,Geometry+0x60};
    for(auto at:breaks){Memory bad;bad.U32(at,0);Reference untouched=result;Check(!MountedReference(Image,bad.Action(),bad.Reader(),untouched)&&Near(untouched.root.position,result.root.position),"missing or foreign mount ownership fails without returning an old reference");}
    Memory sparse;sparse.U32(Entity+0x7B0,Record+0x45);Check(!MountedReference(Image,sparse.Action(),sparse.Reader(),changed),"unaligned native record vector rejected");
    Memory late;late.trigger=Geometry;late.target=Record+0x40;late.replacement=Controller+4;Check(!MountedReference(Image,late.Action(),late.Reader(),changed)&&!late.trigger,"record replacement during world walk retires base proof");
    Memory cycle;cycle.U32(Parent+0x50,Geometry);Check(!MountedReference(Image,cycle.Action(),cycle.Reader(),changed),"carrier hierarchy cycle rejected without native calls");
    Memory wrong;auto action=wrong.Action();action.movementKind=NativeActionKL::Movement::Vehicle;Check(!MountedReference(Image,action,wrong.Reader(),changed),"passenger movement cannot impersonate a mounted MG carrier");
}
void Movement(){
    using namespace NativeCarriedCamera;
    CameraPose root;root.basis={{0,0,1},{0,1,0},{1,0,0}};root.position={100,200,300};
    CameraPose facing=root;Check(AnatomicalForward(root,facing)&&Near(facing.basis.forward,Scale(root.basis.forward,-1)),
        "seated camera uses proved human/model -Z facing rather than arbitrary neck-bone bind axes");
    CameraPose head{Turn(root.basis,.2f),{170,320,350}},out;Reference carrier{root,77,2};State state;
    Check(state.Select(carrier,head,1,2,3,out)&&Same(out.basis,head.basis)&&Near(out.position,head.position),"carried entry preserves authored anatomical position and direction");const auto entry=out;
    head.basis=Turn(head.basis,1.f,.5f);head.position={220,325,370};
    Check(state.Select(carrier,head,1,2,3,out)&&Same(out.basis,entry.basis)&&Near(out.position,entry.position),"native fire/neck motion cannot move or rotate the held seat reference");
    carrier.root.basis=Turn(root.basis,.8f,.2f);carrier.root.position={600,100,700};
    const Basis expected{World(carrier.root.basis,state.local.forward),World(carrier.root.basis,state.local.up),World(carrier.root.basis,state.local.right)};
    Check(state.Select(carrier,head,1,2,3,out)&&Same(out.basis,expected)&&Near(out.position,Add(carrier.root.position,World(carrier.root.basis,state.localPosition))),"actual carrier turn/roll transports the same local seat position");
    StereoRenderMath::CinematicLook look;Quat q;Check(look.SelectFree({},2,10,q),"carried physical look owns a separate recenter reference");
    CameraPose eye;Check(Compose(out,q,{},{-.032f,0,0},100,eye),"left physical eye composes from current seat");
    const auto before=out;Check(look.SelectFree({0,.3f,0,.9539392f},2,11,q)&&state.Select(carrier,head,1,2,3,out)&&Near(before.position,out.position)&&Same(before.basis,out.basis),"recenter changes physical reference without recapturing or shifting carrier baseline");
    const auto previous=out;head.position.x+=100;carrier.root.position.x+=50;CameraPose selected;
    Check(state.Select(carrier,head,1,2,3,out)&&EyeBase(true,true,out,previous,root,selected)&&Near(selected.position,Add(previous.position,{50,0,0})),"same-pair moving carrier updates the eye without borrowing animated head displacement");
    Check(EyeBase(false,true,out,previous,root,selected)&&Near(selected.position,previous.position),"ordinary stable stereo keeps existing first-eye centre reuse");
    Check(EyeBase(false,false,out,previous,root,selected)&&Near(selected.position,root.position),"ordinary fresh pair keeps its native camera base");
    auto bad=out;bad.position.x=std::numeric_limits<float>::quiet_NaN();const auto saved=selected;
    Check(!EyeBase(true,true,bad,previous,root,selected)&&Near(selected.position,saved.position),"invalid current seat cannot fall back to a stale first-eye position");
    head.basis=Turn(root.basis,-1);carrier.identity=88;Check(state.Select(carrier,head,1,2,3,out)&&Same(out.basis,head.basis),"new exact carrier recaptures reference");
    head.basis=Turn(root.basis,.5f);carrier.kind=1;Check(state.Select(carrier,head,1,2,3,out)&&Same(out.basis,head.basis),"reference kind change recaptures even when an address is reused");
    const auto actorEntry=out;
    head.position=Add(head.position,{20,-130,50});head.basis=Turn(head.basis,.8f,.3f);
    Check(state.Select(carrier,head,1,2,3,out)&&Near(out.position,head.position)&&Same(out.basis,actorEntry.basis),
        "van actor fallback follows standing-to-seated anatomical translation without retaining entry camera on the roof");
    carrier.root.position=Add(carrier.root.position,{700,-20,300});head.position=Add(head.position,{700,-20,300});
    Check(state.Select(carrier,head,1,2,3,out)&&Near(out.position,head.position)&&Same(out.basis,actorEntry.basis),
        "actor fallback still follows the current passenger position as van travels while holding independent seat heading");
    State paired;CameraPose first,second;
    Check(paired.Select(carrier,head,1,2,3,first,77),"actor seat captures its first source for a stereo pair");
    auto alternate=head;alternate.position=Add(head.position,{60,20,-40});
    carrier.root.position=Add(carrier.root.position,{30,0,50});
    Check(paired.Select(carrier,alternate,1,2,3,second,77)&&Near(second.position,Add(first.position,{30,0,50})),
        "same-pair parent-to-fallback source change cannot shift the eyes while current carrier translation is retained");
    Check(paired.Select(carrier,alternate,1,2,3,second,78)&&Near(second.position,alternate.position),
        "next stereo pair resumes current seating animation, never permanently locking an entry position");
    auto replacementHead=alternate;replacementHead.position.x+=31;
    Check(paired.Select(carrier,replacementHead,9,2,3,second,78)&&Near(second.position,replacementHead.position),
        "new player cannot inherit another player's pair-local position");
    replacementHead.position.z+=21;
    Check(paired.Select(carrier,replacementHead,9,2,3,second,78,42)&&Near(second.position,replacementHead.position),
        "a changed native action cannot borrow the previous same-pair position");
    for(unsigned which=0;which<3;++which){State copy=state;auto current=head;current.basis=Turn(head.basis,float(which+1)*.2f);Check(copy.Select(carrier,current,which==0?8:1,which==1?8:2,which==2?8:3,out)&&Same(out.basis,current.basis),"player camera or movement replacement recaptures reference once");}
    const auto old=out;carrier.root.basis.forward={};Check(!state.Select(carrier,head,1,2,3,out)&&Near(old.position,out.position),"invalid current root fails without stale output publication");
    state.Reset();Check(!state.valid,"carried action exit clears only independent carried state");
}
void EntryDoorClearance(){
    using namespace NativeCarriedCamera;
    EntryClearance state;EntryKey key{1,2,3,4};CameraPose seat;seat.basis={{0,0,1},{0,1,0},{1,0,0}};seat.position={100,200,300};CameraPose result;
    Check(state.Select(key,true,true,true,seat,100,result)&&state.active&&Near(result.position,{100,200,288})&&Same(result.basis,seat.basis),
        "exact blocked entry moves only twelve centimetres inward along current anatomical seat, without camera yaw/height changes");
    auto moving=seat;moving.basis=Turn(seat.basis,1.f);moving.position={700,400,1000};
    Check(state.Select(key,true,true,true,moving,100,result)&&Near(result.position,Subtract(moving.position,Scale(moving.basis.forward,12))),
        "entry clearance follows moving/rotated van using current seat, never a cached world position");
    Check(state.Select(key,true,true,false,moving,100,result)&&!state.active&&state.released&&Near(result.position,moving.position)&&Same(result.basis,moving.basis),
        "first native control release restores accepted seated camera exactly");
    Check(state.Select(key,true,true,true,moving,100,result)&&!state.active&&Near(result.position,moving.position),
        "later pause/menu/native block cannot reactivate intro clearance in the same entry");
    Check(state.Select(key,true,false,true,moving,100,result)&&state.released&&Near(result.position,moving.position),
        "missing late graph/controller read keeps accepted gameplay seat and released latch");
    EntryClearance interrupted;Check(interrupted.Select(key,true,true,true,seat,100,result)&&interrupted.active,
        "a separate proved blocked entry initially owns its clearance");interrupted.Unavailable();
    Check(interrupted.valid&&interrupted.released&&!interrupted.active&&interrupted.Select(key,true,true,true,seat,100,result)&&Near(result.position,seat.position),
        "late unreadable graph retires correction without resetting its entry or allowing later blocked rearm");
    EntryClearance unknown;Check(unknown.Select(key,true,false,true,seat,100,result)&&unknown.released&&Near(result.position,seat.position)&&
        unknown.Select(key,true,true,true,seat,100,result)&&!unknown.active&&Near(result.position,seat.position),
        "unknown first observation fails closed and later readable blocked state cannot invent another intro");
    EntryClearance skipped;Check(skipped.Select(key,true,true,false,seat,100,result)&&skipped.Select(key,true,true,true,seat,100,result)&&!skipped.active,
        "already-open/skipped intro starts at accepted seat and never shifts later");
    EntryClearance other;Check(other.Select(key,false,true,true,seat,100,result)&&!other.active&&other.Select(key,true,true,true,seat,100,result)&&!other.active,
        "unproved or unrelated carrier cannot become an entry correction later in its lifetime");
    auto replacement=key;++replacement.record;Check(state.Select(replacement,true,true,true,seat,100,result)&&state.active,
        "new exact native vehicle entry may independently receive its bounded initial clearance");
    state.Reset();Check(!state.valid&&!state.active&&state.Select(key,true,true,true,seat,100,result)&&state.active,
        "proved Vehicle exit resets the entry latch for actual reentry");
    const auto preserved=result;Check(!state.Select(key,true,true,true,seat,0,result)&&Near(result.position,preserved.position),
        "invalid world scale cannot publish partial clearance");
    Check(state.Select({},false,false,false,seat,100,result)&&!state.valid&&Near(result.position,seat.position),
        "retired native entry returns its current accepted source without a stale offset");
}
}
int main(){CarrierIdentity();Movement();EntryDoorClearance();std::printf("Native carried camera: %u checks PASS.\n",checks);}
