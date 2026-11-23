// Actual production ownership/scope/launcher callbacks plus the unchanged
// authenticated native 6020B4 release tail in a private CPU image. No game,
// detour, GPU, input injection, saves or runtime are opened.
#define main UnusedThrowbackWeaponFixtureMain
#include "native_scope_fire_test.cpp"
#undef main
#include <fstream>
#include <iterator>
#include <vector>
namespace ThrowbackFixture {
using namespace NativeCameraMath;
struct FileImage {
    std::vector<unsigned char> bytes;
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;bytes.assign(std::istreambuf_iterator<char>(f),{});return bytes.size()>0x1000;}
    bool Read(std::uintptr_t at,void* out,std::size_t count)const {
        if(at<0x400000||bytes.size()<0x40)return false;std::uint32_t pe=0;std::memcpy(&pe,bytes.data()+0x3C,4);
        if(pe>bytes.size()-24)return false;std::uint16_t sections=0,optional=0;std::memcpy(&sections,bytes.data()+pe+6,2);std::memcpy(&optional,bytes.data()+pe+20,2);
        for(unsigned i=0;i<sections;++i){const auto h=pe+24+optional+40*i;if(h>bytes.size()-40)return false;std::uint32_t size=0,rva=0,raw=0;
            std::memcpy(&size,bytes.data()+h+16,4);std::memcpy(&rva,bytes.data()+h+12,4);std::memcpy(&raw,bytes.data()+h+20,4);
            const auto offset=at-0x400000;if(offset>=rva&&offset-rva<size){const auto delta=offset-rva;
                if(count>size-delta||raw+delta>bytes.size()||count>bytes.size()-raw-delta)return false;
                std::memcpy(out,bytes.data()+raw+delta,count);return true;}}
        return false;
    }
};
using Tail=void(__stdcall*)(void*,const Vec3*,const Vec3*);
Tail tail=nullptr;
unsigned launches=0;Vec3 input{0,350,1200},spin{11,22,33},captured{},capturedSpin{};
std::uintptr_t selected=0;bool mutateAction=false;
void __thiscall Launch(void* object,const Vec3* linear,const Vec3* angular) {
    ++launches;Fixture::Check(reinterpret_cast<std::uintptr_t>(object)==selected,"native launcher receives the exact incoming projectile");
    captured=*linear;capturedSpin=*angular;
}
bool __thiscall Update(void* action) {
    if(mutateAction)Fixture::actionState.action=0;
    tail(action,&input,&spin);return true;
}
}
int main(int argc,char** argv) {
    using namespace NativeWeaponKL;using namespace ThrowbackFixture;using Fixture::Check;using Fixture::Put;
    if(argc!=2)return 2;FileImage exe;Check(exe.Load(argv[1]),"supported EXE opened read-only");
    wchar_t path[MAX_PATH]{};char digest[65]{};
    Check(MultiByteToWideChar(CP_ACP,0,argv[1],-1,path,MAX_PATH)>0&&HashFile(path,digest)&&IsSupportedExecutableHash(digest),"supported EXE hash authenticates the copied release before execution");
    if(Fixture::failures)return 1;
    const auto fileRead=[&](std::uintptr_t at,void* out,std::size_t bytes){return exe.Read(at,out,bytes);};
    Check(NativeThrowbackKL::Verify(0x400000,fileRead),"actual EXE proves constructor+68/+6C, exact throwback update, release and linear/angular launcher ABI");
    constexpr std::uintptr_t base=0x31000000;
    auto* allocation=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    Check(allocation==reinterpret_cast<void*>(base),"private native throwback image allocated");if(!allocation||Fixture::failures)return 1;
    image=base;const auto actor=base+0x10000,action=base+0x20000,projectile=base+0x30000;
    Put(actor,static_cast<std::uint32_t>(base+0x56582C));Put(action,static_cast<std::uint32_t>(base+NativeThrowbackKL::Vtable));
    Put(action+0x68,static_cast<std::uint32_t>(actor));Put(action+0x6C,static_cast<std::uint32_t>(projectile));
    Fixture::player=actor;selected=projectile;Fixture::actionState={};Fixture::actionState.valid=true;Fixture::actionState.player=actor;
    Fixture::actionState.action=action;Fixture::actionState.actionVtable=base+NativeThrowbackKL::Vtable;Fixture::actionState.actionKind=NativeActionKL::Action::Throw;
    Fixture::ready=true;Fixture::teamMenu=false;SetScopeGestureContext(false);SetThrowbackContext(true);Fixture::recenter=5;
    HeadGaze gaze;gaze.player=actor;gaze.sourceCamera=base+0x40000;gaze.pairId=7;gaze.tick=Fixture::fixtureTick;
    gaze.recenterAck=5;gaze.mounted=true;gaze.unitsPerMetre=100;gaze.direction={1,0,0};PublishHeadGaze(gaze);
    NativeThrowbackKL::Owner owner;
    Check(NativeThrowbackKL::Capture(base,Fixture::actionState,action,ReadBytes,owner)&&owner.projectile==projectile,"incoming projectile is owned by exact active action without held inventory ownership");
    GrenadeScope heldScope;Check(!GrenadeItem(actor,projectile,heldScope),"incoming projectile does not gain the ordinary held grenade capability");
    // Bridge only prepares native stack locals; 6020B4..6020CC itself is copied
    // byte-exact, including its real CALL return address 6020C6 and +88 write.
    const unsigned char bridge[]={0x56,0x81,0xEC,0x80,0,0,0,0x8B,0xB4,0x24,0x88,0,0,0,
        0x8B,0x84,0x24,0x8C,0,0,0,0x0F,0x10,0,0x0F,0x11,0x44,0x24,0x0C,
        0x8B,0x84,0x24,0x90,0,0,0,0x0F,0x10,0,0x0F,0x11,0x44,0x24,0x30,0xE9,0,0,0,0};
    constexpr auto bridgeAt=base+0x202070;std::memcpy(reinterpret_cast<void*>(bridgeAt),bridge,sizeof(bridge));
    Put(bridgeAt+sizeof(bridge)-4,static_cast<std::int32_t>((base+0x2020B4)-(bridgeAt+sizeof(bridge))));
    Check(exe.Read(0x6020B4,reinterpret_cast<void*>(base+0x2020B4),0x19),"original native release tail copied unchanged");
    const unsigned char epilogue[]={0x81,0xC4,0x80,0,0,0,0x5E,0xC2,0x0C,0};std::memcpy(reinterpret_cast<void*>(base+0x2020CD),epilogue,sizeof(epilogue));
    const unsigned char jump[]={0x68,0,0,0,0,0xC3};std::memcpy(reinterpret_cast<void*>(base+NativeThrowbackKL::Launch),jump,sizeof(jump));
    Put(base+NativeThrowbackKL::Launch+1,reinterpret_cast<std::uint32_t>(&LaunchProjectileHook));
    tail=reinterpret_cast<Tail>(bridgeAt);originalLaunchProjectile=Launch;originalThrowbackUpdate=Update;
    Check(ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr),"actual update wrapper preserves native result");
    Check(launches==1&&throwbackApplied==1&&std::fabs(captured.x-1200)<.01f&&std::fabs(captured.y-350)<.01f&&std::fabs(captured.z)<.01f,
          "unchanged native CALL follows fresh VR +X even with deliberately hidden scope/firearm visuals, retaining native loft and speed");
    const Vec3 originalInput{0,350,1200};Check(!std::memcmp(&capturedSpin,&spin,sizeof(spin))&&!std::memcmp(&input,&originalInput,sizeof(input)),"native spin and supplied velocity are unchanged");
    Check(!throwbackScope.action&&*reinterpret_cast<const std::uint8_t*>(action+0x88)==1,"scope retires and original release flag executes");
    gaze.direction={0,0,-1};PublishHeadGaze(gaze);ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);
    Check(throwbackApplied==2&&captured.z<-1199&&std::fabs(captured.x)<.01f,"turning around uses current VR gaze rather than last native body heading");
    Fixture::fixtureTick+=251;ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);
    Check(throwbackRejected==1&&!std::memcmp(&captured,&input,sizeof(input)),"stale gaze falls through to byte-identical native linear velocity");
    gaze.tick=Fixture::fixtureTick;PublishHeadGaze(gaze);Fixture::teamMenu=true;ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);Fixture::teamMenu=false;
    Check(throwbackRejected==2&&!std::memcmp(&captured,&input,sizeof(input)),"open UI cannot authorize a redirected incoming grenade");
    mutateAction=true;ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);mutateAction=false;Fixture::actionState.action=action;
    Check(throwbackRejected==3&&!std::memcmp(&captured,&input,sizeof(input)),"retired action between update and launch fails closed");
    const auto count=throwbackApplied.load();LaunchThrowback(reinterpret_cast<void*>(projectile),&input,&spin,base+NativeThrowbackKL::LaunchReturn);
    Check(throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"same caller without owned native update scope has no capability");
    throwbackScope={action,actor,projectile};const auto attempts=throwbackLaunches.load();
    LaunchThrowback(reinterpret_cast<void*>(projectile),&input,&spin,base+NativeThrowbackKL::LaunchReturn+1);
    Check(throwbackLaunches==attempts&&throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"a different native call site cannot borrow the throw-back scope");throwbackScope={};
    Fixture::recenter=6;const auto rejected=throwbackRejected.load();ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);Fixture::recenter=5;
    Check(throwbackRejected==rejected+1&&throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"recenter acknowledgement change rejects the former head frame");
    const auto updates=throwbackOwnedUpdates.load();Fixture::actionState.action=action+4;
    ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);Fixture::actionState.action=action;
    Check(throwbackOwnedUpdates==updates&&throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"inactive/foreign action update cannot grant a selected-projectile capability");
    gaze.player=actor+4;PublishHeadGaze(gaze);ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);
    Check(throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"another player's fresh head gaze is never used for local throw-back");gaze.player=actor;PublishHeadGaze(gaze);
    SetThrowbackContext(false);ThrowbackUpdateHook(reinterpret_cast<void*>(action),nullptr);SetThrowbackContext(true);
    Check(throwbackApplied==count&&!std::memcmp(&captured,&input,sizeof(input)),"retired gameplay context blocks throw-back independently of scope/visual state");
    Vec3 vertical;gaze.direction={0,1,0};
    Check(NativeThrowbackKL::Velocity(input,gaze,{1,0,0},vertical)&&vertical.y>1199&&std::fabs(vertical.x+350)<.02f,
          "vertical VR gaze uses the proved horizontal fallback and retains native ballistic loft");
    const auto beforeInvalid=vertical;
    Check(!NativeThrowbackKL::Velocity({0,10,0},gaze,{1,0,0},vertical)&&!std::memcmp(&beforeInvalid,&vertical,sizeof(vertical)),"degenerate native horizontal direction rejects without modifying output");
    for(unsigned i=0;i<36;++i){const float a=i*.174532925f;gaze.direction={std::cos(a),-.25f,std::sin(a)};NativeGrenadeKLMath::Unit(gaze.direction,gaze.direction);
        Vec3 mapped;Check(NativeThrowbackKL::Velocity(input,gaze,{gaze.direction.x,0,gaze.direction.z},mapped)&&std::fabs(Dot(mapped,mapped)-Dot(input,input))<1,
            "gravity-aligned remap preserves native kinetic speed over complete VR yaw sweep");}
    std::printf("native throwback production: %u checks, %u failures\n",Fixture::checks,Fixture::failures);VirtualFree(allocation,0,MEM_RELEASE);return Fixture::failures?1:0;
}
