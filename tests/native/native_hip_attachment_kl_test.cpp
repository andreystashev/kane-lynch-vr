#include "native_weapon_ownership.hpp"
#include "native_weapon_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>
namespace {
unsigned checks=0;
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Put(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i)bytes[at+i]=p[i];}
    bool Read(std::uintptr_t at,void* out,std::size_t n)const{auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){const auto it=bytes.find(at+i);if(it==bytes.end())return false;p[i]=it->second;}return true;}
};
struct PE {
    struct Section {std::uint32_t rva=0,rawSize=0,raw=0;};std::vector<unsigned char> bytes;std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& value)const{if(at>bytes.size()||sizeof(value)>bytes.size()-at)return false;std::memcpy(&value,bytes.data()+at,sizeof(value));return true;}
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);const auto n=f.tellg();if(n<=0||n>64*1024*1024)return false;bytes.resize(std::size_t(n));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),n);std::uint32_t off=0,sig=0;std::uint16_t count=0,opt=0;if(!Get(0x3c,off)||!Get(off,sig)||sig!=0x4550||!Get(off+6,count)||!Get(off+20,opt)||count>96)return false;for(unsigned i=0;i<count;++i){Section s;const auto at=off+24+opt+i*40;if(!Get(at+12,s.rva)||!Get(at+16,s.rawSize)||!Get(at+20,s.raw))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t n)const{if(at<0x400000)return false;const auto rva=at-0x400000;for(auto s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize){const auto off=s.raw+rva-s.rva;if(n>s.rawSize-(rva-s.rva)||off>bytes.size()||n>bytes.size()-off)return false;std::memcpy(out,bytes.data()+off,n);return true;}return false;}
};
}
int main(int argc,char** argv) {
    using namespace NativeWeaponOwnership;Memory memory;
    constexpr std::uintptr_t image=0x400000,player=0x220000,component=0x230000,model=0x240000,lookup=0x250000;
    constexpr std::uint32_t manager=0x200000,pool=0x210000,data=0x300000,generations=0x310000,group=0x600000,playerRoot=0x700000;
    constexpr std::uint32_t modelResource=0x410000,parts=0x420000,header=0x430000,definitions=0x440000,primitives=0x450000;
    memory.Put(image+0x6C99B8,manager);memory.Put(manager+0xC,pool);memory.Put(pool+4,std::uint32_t(0x70));memory.Put(pool+8,std::uint32_t(8));
    memory.Put(pool+0xC,data);memory.Put(pool+0x10,generations);for(unsigned i=0;i<8;++i)memory.Put(generations+i*2,std::uint16_t(3));
    memory.Put(player,std::uint32_t(image+0x56582C));memory.Put(player+4,playerRoot);memory.Put(player+0x70,std::uint32_t(model));memory.Put(player+0x88,std::uint32_t(component));
    memory.Put(model+0xC,std::uint32_t(lookup));memory.Put(model+0x4A0,std::uint32_t(20));memory.Put(lookup+63,std::uint8_t(6));
    memory.Put(playerRoot+0x68,std::uint32_t(10));
    const std::uint32_t resources[]={modelResource,parts,header,definitions,primitives};for(unsigned i=0;i<5;++i)memory.Put(image+0x71F308+(10+i)*4,resources[i]);
    memory.Put(modelResource+2,std::uint16_t(7));memory.Put(modelResource+0xC,std::uint8_t(1));memory.Put(modelResource+0x10,std::uint32_t(11));memory.Put(parts+4,std::uint32_t(12));
    memory.Put(header,std::uint32_t(20));memory.Put(header+4,std::uint32_t(13));memory.Put(header+0x14,std::uint32_t(14));memory.Put(primitives+35,std::uint8_t(6));
    const char name[16]="hip attacher";memory.Put(definitions+6*64+0x1C,name);
    Record record;record.geometryRef=3*8+6+1;record.bone=35;memory.Put(component+0x14,std::uint32_t(1));memory.Put(component+0x18,record);
    const auto geometry=data+6*0x70;memory.Put(geometry+0x60,group);memory.Put(group,std::uint32_t(image+0x5462CC));memory.Put(group+4,geometry);
    const auto read=[&](auto a,auto* out,auto n){return memory.Read(a,out,n);};Snapshot snapshot;
    Check(Capture(image,player,read,snapshot)&&snapshot.geometry[0]==geometry,"hip slot uses exact generation-checked actor attachment geometry");
    const auto before=memory.bytes;std::uintptr_t owner=0;
    Check(HipGroup(image,snapshot,0,read,owner)&&owner==group,"native hip primitive resolves to the costume's standard63 and named hip attacher GROUP");
    Check(memory.bytes==before,"hip ownership proof leaves actor, resource, geometry and physics memory untouched");
    memory.Put(lookup+63,std::uint8_t(7));Check(!HipGroup(image,snapshot,0,read,owner),"primitive and standard mapping mismatch cannot authorize belt equipment");memory.Put(lookup+63,std::uint8_t(6));
    const char handName[16]="LEFT_HAND";memory.Put(definitions+6*64+0x1C,handName);Check(!HipGroup(image,snapshot,0,read,owner),"matching bone index with a hand name is not a hip");memory.Put(definitions+6*64+0x1C,name);
    char unterminated[16];std::memset(unterminated,'x',sizeof(unterminated));memory.Put(definitions+6*64+0x1C,unterminated);Check(!HipGroup(image,snapshot,0,read,owner),"unterminated name fails the bounded exact-name proof");memory.Put(definitions+6*64+0x1C,name);
    memory.Put(group,std::uint32_t(image+0x57A79C));Check(!HipGroup(image,snapshot,0,read,owner),"actual rope class cannot enter the hip GROUP scope");memory.Put(group,std::uint32_t(image+0x5462CC));
    memory.Put(group+4,std::uint32_t(geometry+0x70));Check(!HipGroup(image,snapshot,0,read,owner),"foreign GROUP root backpointer fails");memory.Put(group+4,geometry);
    memory.Put(generations+6*2,std::uint16_t(4));Check(!HipGroup(image,snapshot,0,read,owner),"recycled geometry reference immediately retires belt authority");memory.Put(generations+6*2,std::uint16_t(3));
    auto changed=record;changed.bone=25;memory.Put(component+0x18,changed);Check(!HipGroup(image,snapshot,0,read,owner),"reassigning the native slot to a hand retires its hip snapshot");memory.Put(component+0x18,record);
    memory.Put(player,std::uint32_t(image+0x552684));Check(!HipGroup(image,snapshot,0,read,owner),"another actor type cannot borrow a local hero hip slot");memory.Put(player,std::uint32_t(image+0x56582C));
    memory.Put(modelResource+2,std::uint16_t(8));Check(!HipGroup(image,snapshot,0,read,owner),"unexpected model resource type is retained native");memory.Put(modelResource+2,std::uint16_t(7));
    memory.Put(header,std::uint32_t(6));Check(!HipGroup(image,snapshot,0,read,owner),"bone must be within the resource count");memory.Put(header,std::uint32_t(20));
    const auto late=[&](std::uintptr_t at,void* out,std::size_t n){const bool okay=memory.Read(at,out,n);if(at==definitions+6*64+0x1C)memory.Put(geometry+0x60,std::uint32_t(group+4));return okay;};
    Check(!HipGroup(image,snapshot,0,late,owner),"late GROUP owner replacement cannot reuse an earlier name proof");memory.Put(geometry+0x60,group);
    memory.Put(component+0x14,std::uint32_t(0));owner=0xABCDEF;Check(!HipGroup(image,snapshot,0,read,owner)&&owner==0xABCDEF,"detached slot fails without mutating caller outputs");memory.Put(component+0x14,std::uint32_t(1));
    Check(!HipGroup(image,snapshot,1,read,owner),"out-of-range snapshot index rejected");
    constexpr std::uint32_t entry=0x710000,extra=0x720000,partAt=0x730000,partResource=0x740000,cache=0x750000;
    memory.Put(entry,std::uint32_t(image+0x541070));memory.Put(entry+0xF4,playerRoot);memory.Put(entry+0x108,cache);
    memory.Put(playerRoot+0x60,std::uint32_t(player));memory.Put(extra+4,partAt);memory.Put(partAt+0x14,std::uint32_t(15));
    memory.Put(image+0x71F308+15*4,partResource);memory.Put(partResource+0x3C,std::uint32_t(6));
    HipRigidIdentity rigid;unsigned observed=255;
    memory.Put(component+0x14,std::uint32_t(0));
    Check(HipRigid(image,player,entry,extra,read,rigid,&observed)&&rigid.bone==6&&observed==6,"rigid costume hip bone is proved without any actor attachment slot");
    const auto rigidBefore=memory.bytes;
    Check(HipRigid(image,player,entry,extra,read,rigid)&&memory.bytes==rigidBefore,"native rigid resource/cache/geometry remain read-only");
    memory.Put(partResource+0x3C,std::uint32_t(7));observed=255;
    Check(!HipRigid(image,player,entry,extra,read,rigid,&observed)&&observed==7,"another rigid body bone does not borrow hip authority");memory.Put(partResource+0x3C,std::uint32_t(6));
    memory.Put(playerRoot+0x60,std::uint32_t(player+4));Check(!HipRigid(image,player,entry,extra,read,rigid),"a foreign rigid model never receives the local body's delta");memory.Put(playerRoot+0x60,std::uint32_t(player));
    memory.Put(entry,std::uint32_t(image+0x541034));Check(!HipRigid(image,player,entry,extra,read,rigid),"skinned body entries are never double-remapped as rigid hip parts");memory.Put(entry,std::uint32_t(image+0x541070));
    memory.Put(player+4,std::uint32_t(geometry));Check(!HipRigid(image,player,entry,extra,read,rigid),"the world cable or GROUP attachment cannot impersonate the actor's root model");memory.Put(player+4,playerRoot);
    const auto rigidLate=[&](std::uintptr_t at,void* out,std::size_t n){const bool okay=memory.Read(at,out,n);if(at==definitions+6*64+0x1C)memory.Put(entry+0x108,std::uint32_t(cache+48));return okay;};
    Check(!HipRigid(image,player,entry,extra,rigidLate,rigid),"late palette-cache recycling rejects a previously named rigid hip");memory.Put(entry+0x108,cache);
    const auto invalidRigid=rigid;
    Check(!HipRigid(image,player,entry,0,read,rigid)&&!std::memcmp(&rigid,&invalidRigid,sizeof(rigid)),"missing native rigid material argument cannot mutate proof output");
    memory.Put(lookup,std::uint8_t(1));const char pelvisName[16]="PELVIS";memory.Put(definitions+64+0x1C,pelvisName);memory.Put(partResource+0x3C,std::uint32_t(1));
    Check(HipRigid(image,player,entry,extra,read,rigid)&&rigid.standard==0&&rigid.bone==1,"exact current costume PELVIS rigid part uses the same proved hero/root path without requiring hip attacher");
    memory.Put(definitions+64+0x1C,name);Check(!HipRigid(image,player,entry,extra,read,rigid),"a matching pelvis index with another name cannot authorize a belt part");memory.Put(definitions+64+0x1C,pelvisName);
    memory.Put(lookup,std::uint8_t(2));Check(!HipRigid(image,player,entry,extra,read,rigid),"PELVIS name cannot bypass its current costume's standard0 mapping");memory.Put(lookup,std::uint8_t(1));
    memory.Put(partResource+0x3C,std::uint32_t(0));Check(!HipRigid(image,player,entry,extra,read,rigid),"GROUND or any other model bone remains native");memory.Put(partResource+0x3C,std::uint32_t(6));
    changed=record;changed.bone=2;memory.Put(component+0x14,std::uint32_t(1));memory.Put(component+0x18,changed);memory.Put(primitives+2,std::uint8_t(1));
    Check(Capture(image,player,read,snapshot)&&HipGroup(image,snapshot,0,read,owner)&&owner==group,"native primitive2 PELVIS GROUP has an exact generation-checked current body attachment proof");
    memory.Put(definitions+64+0x1C,name);Check(!HipGroup(image,snapshot,0,read,owner),"PELVIS group cannot use a different bone name");memory.Put(definitions+64+0x1C,pelvisName);
    constexpr std::uint32_t dataState=0x760000,bodyChild=0x770000,bodyControl=0x780000;
    memory.Put(image+0x68760C,dataState);memory.Put(dataState+0xA2C,std::uint32_t(player));
    GeometryLinks childLinks;childLinks.parent=playerRoot;childLinks.owner=bodyControl;memory.Put(bodyChild+0x50,childLinks);memory.Put(bodyControl,std::uint32_t(image+0x5462CC));memory.Put(bodyControl+4,bodyChild);
    GeometryLinks actorLinks;actorLinks.owner=player;memory.Put(playerRoot+0x50,actorLinks);BodyChild childProof;
    Check(CaptureBodyChild(image,player,bodyChild,read,childProof)&&childProof.count==2&&StillBodyChild(image,childProof,read),"static model child is proved by actual current actor-root ancestry without slots or rigid bone indices");
    Check(!CaptureBodyChild(image,player,playerRoot,read,childProof),"main body root cannot be double-carried as a costume accessory");
    memory.Put(bodyControl,std::uint32_t(image+0x57A79C));Check(!CaptureBodyChild(image,player,bodyChild,read,childProof),"actual world cable class is excluded even if its geometry is parented to the hero");memory.Put(bodyControl,std::uint32_t(image+0x5462CC));
    childLinks.parent=bodyChild;memory.Put(bodyChild+0x50,childLinks);Check(!CaptureBodyChild(image,player,bodyChild,read,childProof),"cyclic scene geometry does not establish ownership");childLinks.parent=playerRoot;memory.Put(bodyChild+0x50,childLinks);
    memory.Put(dataState+0xA2C,std::uint32_t(player+4));Check(!CaptureBodyChild(image,player,bodyChild,read,childProof),"another current actor cannot borrow retained scene-parent links");memory.Put(dataState+0xA2C,std::uint32_t(player));
    const auto childLate=[&](std::uintptr_t at,void* out,std::size_t n){const bool okay=memory.Read(at,out,n);if(at==playerRoot+0x50)memory.Put(bodyChild+0x50,GeometryLinks{});return okay;};
    Check(!CaptureBodyChild(image,player,bodyChild,childLate,childProof),"late native child detachment is rechecked across the full ancestry");memory.Put(bodyChild+0x50,childLinks);
    constexpr std::uint32_t parentTable=0x790000;memory.Put(model+0x10,parentTable);
    std::uint8_t hierarchy[20]{};hierarchy[2]=1;hierarchy[3]=2;hierarchy[4]=2;hierarchy[5]=2;hierarchy[6]=1;memory.Put(parentTable,hierarchy);
    memory.Put(lookup+6,std::uint8_t(3));memory.Put(lookup+16,std::uint8_t(4));memory.Put(lookup+22,std::uint8_t(5));
    struct NameEntry {std::uint32_t value,name;};NameEntry nativeNames[32]{};nativeNames[29]={34,std::uint32_t(image+0x5385C8)};memory.Put(image+0x6318B0,nativeNames);
    const char spineName[32]="spine1 attacher";memory.Put(image+0x5385C8,spineName);memory.Put(definitions+2*64+0x1C,spineName);memory.Put(primitives+34,std::uint8_t(2));
    changed.bone=34;memory.Put(component+0x18,changed);
    Check(Capture(image,player,read,snapshot)&&BodyGroup(image,snapshot,0,read,owner)&&owner==group,"body GROUP uses the actual sparse primitive descriptor and costume topology beyond hip whitelist");
    memory.Put(primitives+34,std::uint8_t(4));Check(!BodyGroup(image,snapshot,0,read,owner),"an arm subtree cannot borrow a torso-only body-root correction");memory.Put(primitives+34,std::uint8_t(3));
    Check(!BodyGroup(image,snapshot,0,read,owner),"head subtree remains owned by the head-mask adapter");memory.Put(primitives+34,std::uint8_t(2));
    const char wrongBodyName[32]="SPINE";memory.Put(definitions+2*64+0x1C,wrongBodyName);Check(!BodyGroup(image,snapshot,0,read,owner),"matching body bone number without its native semantic name cannot establish a GROUP attachment");memory.Put(definitions+2*64+0x1C,spineName);
    memory.Put(group,std::uint32_t(image+0x57A79C));Check(!BodyGroup(image,snapshot,0,read,owner),"world rope never enters animated body GROUP scope");memory.Put(group,std::uint32_t(image+0x5462CC));
    using namespace NativeCameraMath;CameraPose raw,render,part,output;
    raw.basis={{0,0,1},{0,1,0},{1,0,0}};raw.position={300,160,-900};render=raw;
    const Quat q{0,std::sin(.7f),0,std::cos(.7f)};render.basis={Rotate(q,raw.basis.forward),Rotate(q,raw.basis.up),Rotate(q,raw.basis.right)};render.position=Add(raw.position,{12,-8,5});
    part=NativeRigMath::ToWorld(raw,{raw.basis,{20,90,-10}});
    Check(NativeWeaponMath::Remap(raw,render,part,output)&&NativeArmMath::Length(Subtract(NativeRigMath::ToModel(render,output).position,NativeRigMath::ToModel(raw,part).position))<.001f,"body yaw/translation carries the original native hip offset with the rendered torso");
    if(argc>1){PE pe;Check(pe.Load(argv[1]),"load pinned EXE without launching it");const auto actual=[&](auto a,auto* out,auto n){return pe.Read(a,out,n);};Check(VerifyHipImage(image,actual),"actual EXE primitive35 has exact native hip-attacher metadata");Check(VerifyHipRigidImage(image,actual),"actual EXE rigid binder selects the material's model bone/cache and writes the derived WORLD inline");}
    std::printf("Native named hip attachment: %u checks PASS; read-only private fixtures.\n",checks);return 0;
}
