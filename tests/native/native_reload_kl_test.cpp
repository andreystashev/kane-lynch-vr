#define main UnusedScopeReaderMain
#include "native_scope_kl_test.cpp"
#undef main
#include "native_reload_kl.hpp"
#include "native_visual_policy.hpp"
#include <thread>
int main(int argc,char** argv) {
    using namespace NativeReloadKL;
    {
        IntentArbiter token;
        const auto simulation=[&]{return token.Simulation(true,123,125,true);};
        Check(simulation()&&!simulation(),"a reload tap is consumed once");
        Check(!token.Simulation(true,122,125,true)&&!token.Simulation(false,124,125,true)&&!token.Simulation(true,0,125,true),
              "older, invalid-context and empty tokens cannot dispatch or roll back the authority");
        Check(token.Simulation(true,124,125,true)&&!token.Simulation(true,124,125,true),"next fresh tap dispatches exactly once");
        Check(!token.Simulation(true,126,125,true),"a future tick never authorizes an action");
        Check(!token.Simulation(true,126,700,true)&&!token.Simulation(true,126,700,true),"expired event is retired without action");
        Check(!token.Simulation(true,701,702,false)&&!token.Simulation(true,701,702,true),"an ineligible press cannot be replayed");
    }
    IntentArbiter wrapped;
    Check(wrapped.Simulation(true,0xfffffff0u,0xfffffff1u,true)&&wrapped.Simulation(true,10u,11u,true)&&!wrapped.Simulation(true,0xfffffff1u,11u,true),
          "clock wrap accepts a newer tick but rejects a delayed previous-epoch observation");
    Check(wrapped.AlreadySeen(0)&&wrapped.AlreadySeen(10)&&wrapped.AlreadySeen(0xfffffff1u),"advisory fast path recognizes empty, current, and old wrapped ticks");
    Check(!wrapped.AlreadySeen(11),"advisory fast path does not suppress a new tap");
    for(unsigned round=0;round<64;++round) {
        IntentArbiter token;std::atomic<bool> go{false};std::atomic<unsigned> actions{0};
        const auto wait=[&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();};
        std::thread simulation([&]{wait();if(token.Simulation(true,123,125,true))++actions;});
        std::thread duplicate([&]{wait();if(token.Simulation(true,123,125,true))++actions;});
        go.store(true,std::memory_order_release);simulation.join();duplicate.join();
        Check(actions==1,"simultaneous observations of the same press start one action");
    }
    if(argc<2)return 2;
    FileImage executable;Check(executable.Load(argv[1]),"actual supported executable opens read-only");
    const auto pe=[&](auto at,void* out,auto count){return executable.Read(at,out,count);};
    Check(Verify(0x400000,pe),"actual PE proves player reload entry, all three update callers, manual message and magazine getter");
    for(auto rva:{0x56582Cu+0x4C0u,0x1F7390u,0x1F5BFDu,0x1F5C2Du,0x1F6129u,0x12B324u,0x11DDB0u}) {
        const auto corrupt=[&](auto at,void* out,auto count){if(!pe(at,out,count))return false;
            if(at==0x400000+rva)static_cast<unsigned char*>(out)[0]^=1;return true;};
        Check(!Verify(0x400000,corrupt),"changed native request/caller/getter evidence fails open before hook installation");
    }
    for(std::uint32_t caller:{0x1F5C12u,0x1F5C42u,0x1F6136u}) {
        Check(Decide(true,true,true,false,caller,true,false)==Decision::Suppress,"a local automatic update cannot replace the actor action");
        Check(Decide(true,true,true,false,caller,true,true)==Decision::Manual,"explicit R permits manual reload");
        for(unsigned fail=0;fail<5;++fail)Check(Decide(fail!=0,fail!=1,fail!=2,fail==3,caller,fail!=4,false)==Decision::Pass,
            "disabled/UI/not-ready/cinematic/unowned state cannot change native reload eligibility");
    }
    for(auto caller:{0u,0x12B335u,0x38270Fu,0x1F5C11u})
        Check(Decide(true,true,true,false,caller,true,false)==Decision::Pass,"manual message, network and unknown callers retain original behavior");
    constexpr std::uint32_t image=0x400000,data=0x100000,player=0x110000,item=0x120000;
    Memory memory;memory.Put(image+0x68760C,data);memory.Put(data+0xA2C,player);memory.Put(player,image+0x56582C);
    memory.Put(player+0x2D8,image+0x5508B4);memory.Put(player+0x358,image+0x5508B4);
    NativeWeaponOwnership::HandState right;right.item=item;right.enabled=1;right.attacher=31;
    memory.Put(player+0x358+0x60,right);memory.Put(player+0x2D8+0x60,NativeWeaponOwnership::HandState{});
    memory.Put(item,image+0x577B74);memory.Put(image+0x577B74+0x308,image+0x11DDB0);
    memory.Put(image+0x577B74+0x2D4,image+0x11DF40);memory.Put(item+0x58,std::uint32_t(23));
    memory.Put(item+0xD8,7u^0xBEEBBEEBu);memory.Put(image+0x873680,std::uint32_t(0));
    const auto read=[&](auto at,void* out,auto count){return memory.Read(at,out,count);};
    Observation observed;
    Check(Capture(image,player,item,read,observed)&&observed.magazine==7&&observed.side==1,"exact current active local hand proves the real encoded magazine");
    for(unsigned fault=0;fault<10;++fault) {
        auto bad=memory;
        switch(fault) {
        case 0:bad.Put(data+0xA2C,player+4);break;case 1:bad.Put(player,image+0x56477C);break;
        case 2:bad.Put(player+0x358+0x60,item+4);break;case 3:bad.Put(player+0x358+0x64,std::uint8_t(0));break;
        case 4:bad.Put(player+0x358+0x68,std::uint32_t(32));break;case 5:bad.Put(item,image+0x1234);break;
        case 6:bad.Put(image+0x577B74+0x308,image+0x1234);break;case 7:bad.Put(item+0xD8,10001u^0xBEEBBEEBu);break;
        case 8:bad.bytes.erase(item+0x58);break;default:bad.Put(image+0x577B74+0x2D4,image+0x1234);break;
        }
        Check(!Capture(image,player,item,[&](auto at,void* out,auto count){return bad.Read(at,out,count);},observed),
            "foreign, stale, inactive, non-firearm and unreadable native state cannot suppress a reload");
    }
    unsigned references=0;
    const auto changed=[&](auto at,void* out,auto count){if(!read(at,out,count))return false;
        if(at==item+0x58&&++references==2)++*static_cast<std::uint32_t*>(out);return true;};
    Check(!Capture(image,player,item,changed,observed),"late same-address item generation change fails open");
    memory.Put(image+0x873680,std::uint32_t(1));memory.Put(item+0xD8,0u^0xBEEBBEEBu);
    Check(Capture(image,player,item,read,observed)&&observed.magazine==999,"native infinite-ammunition getter override is retained");
    using M=NativeActionKL::Movement;using A=NativeActionKL::Action;
    struct NativeGesture {std::uintptr_t constructorOperand,vtable,start;A kind;};
    for(const auto& gesture:{NativeGesture{0x1FDBFC,0x566690,0x1FF000,A::PutBack},
        NativeGesture{0x1FDC2C,0x5666C4,0x1FF110,A::GetBack},NativeGesture{0x1FDC61,0x5666F8,0x1FF1F0,A::Command}}) {
        std::uint32_t actualVtable=0,actualStart=0,actualEnd=0;
        Check(pe(0x400000+gesture.constructorOperand,&actualVtable,4)&&actualVtable==0x400000+gesture.vtable&&
              pe(actualVtable+8,&actualStart,4)&&actualStart==0x400000+gesture.start&&
              pe(actualVtable+0x14,&actualEnd,4)&&actualEnd==0x754480&&
              NativeActionKL::ClassifyAction(actualVtable-0x400000)==gesture.kind,
              "actual native constructor and Start/empty-End vtable prove only exact weapon-transition/command action identity");
        for(auto movement:{M::Stand,M::BaseMove,M::Cover,M::Vehicle,M::Climb,M::Fall,M::Rappel,M::Mounted,M::Die,M::Unknown}) {
            NativeActionKL::State action;action.valid=true;action.player=player;action.movementKind=movement;action.action=item;action.actionKind=gesture.kind;
            const bool tracked=movement==M::Stand||movement==M::BaseMove||movement==M::Cover||movement==M::Vehicle;
            const auto policy=NativeVisualPolicy::ForAction(action,player);
            Check(action.PreserveRig()&&action.TrackedWeaponGesture()==tracked&&action.WeaponTrackingAllowed()==tracked&&
                  policy.nativeArms==!tracked&&policy.nativeBody==(!tracked||movement==M::Cover||movement==M::Vehicle),
                  "only exact owned gesture/weapon transition in ordinary/cover/seat movement keeps tracked arms and preserves native body/item lifecycle");
            action.actionKind=A::Other;
            Check(!action.TrackedWeaponGesture()&&!action.WeaponTrackingAllowed(),"unknown/interactive action cannot inherit cosmetic tracked-arms authorization");
        }
    }
    for(auto movement:{M::Stand,M::BaseMove,M::Cover,M::Vehicle,M::Climb,M::Fall,M::Rappel,M::Mounted,M::Die,M::Unknown}) {
        NativeActionKL::State action;action.valid=true;action.player=player;action.movementKind=movement;action.action=item;action.actionKind=A::Reload;
        const bool tracked=movement==M::Stand||movement==M::BaseMove||movement==M::Cover||movement==M::Vehicle;
        const auto policy=NativeVisualPolicy::ForAction(action,player);
        Check(action.PreserveRig()&&action.TrackedReload()==tracked&&action.WeaponTrackingAllowed()==tracked&&policy.nativeArms,
            "manual/empty reload retains original native arms while request filtering remains independent");
        action.actionKind=A::Throw;Check(!action.TrackedReload()&&!action.WeaponTrackingAllowed(),"throw action never inherits reload-only tracked weapon authorization");
    }
    std::printf("KL1 reload request and tracked pose: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
