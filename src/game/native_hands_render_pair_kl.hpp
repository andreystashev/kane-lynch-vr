#pragma once
#include "hands_math.hpp"
#include <array>
#include <cstring>

// Numerical render admission only. This never authorizes simulation input,
// native ownership, a GPU palette lease or another image after the pair ends.
namespace NativeHandsRenderPairKL {
constexpr std::uint32_t MaximumPairAgeMs=250;
struct Key {std::array<std::uint64_t,26> words{};};
inline bool Same(const Key& a,const Key& b){return a.words==b.words;}
enum class Result {Rejected,Admitted,Reused,SecondEye,Expired,IdentityLost,ContextLost};
struct State {
    bool initialized=false,valid=false,blocked=false;
    // Render-only native stance policy, captured with this pair's input.
    bool neutralStanding=false;
    std::uint64_t pair=0,epoch=0;
    unsigned eye=0,eyes=0;
    std::uint32_t startedTick=0,admittedTick=0;
    Key key{};
    HandsIPC::Frame pose{},input{};
};
struct Request {
    bool context=false,identity=false,latestAvailable=false;
    bool neutralStanding=false;
    std::uint64_t pair=0,epoch=0;
    unsigned eye=0;
    std::uint32_t now=0,recenter=0;
    Key key{};
    HandsIPC::Frame latest{};
};
inline Result Block(State& state,Result reason){state.valid=false;state.blocked=true;return reason;}
inline Result Update(State& state,const Request& request){
    const bool context=request.context&&request.identity&&request.pair&&request.epoch&&request.eye<2;
    if(!state.initialized||state.pair!=request.pair){
        state={};state.initialized=true;state.pair=request.pair;state.epoch=request.epoch;state.eye=request.eye;state.key=request.key;
        if(!context)return Block(state,Result::ContextLost);
        if(request.eye!=0)return Block(state,Result::Rejected);
        if(!request.latestAvailable||!HandsMath::ValidFrame(request.latest,request.now,request.recenter))return Block(state,Result::Rejected);
        state.valid=true;state.eyes=1u<<request.eye;state.startedTick=state.admittedTick=request.now;
        state.pose=state.input=request.latest;state.neutralStanding=request.neutralStanding;return Result::Admitted;
    }
    if(state.blocked||!state.valid)return Result::Rejected;
    if(!context)return Block(state,Result::ContextLost);
    if(!Same(state.key,request.key))return Block(state,Result::IdentityLost);
    // Explicit host unmount/recenter is different from a temporarily absent
    // publication. An already admitted eye may finish while IPC is delayed.
    if(request.latestAvailable&&(!request.latest.present||!request.latest.mounted||request.latest.recenterAck!=request.recenter))
        return Block(state,Result::ContextLost);
    if(request.epoch==state.epoch&&request.eye==state.eye)return Result::Reused;
    // Only the other eye of this physical pair may enter another Present.
    // A repeated eye/new epoch cannot perpetually renew a stuck raw pairId.
    if(request.epoch!=state.epoch+1||request.eye==state.eye||(state.eyes&(1u<<request.eye)))return Block(state,Result::ContextLost);
    const auto age=std::int32_t(request.now-state.startedTick);
    if(age<0||std::uint32_t(age)>MaximumPairAgeMs)return Block(state,Result::Expired);
    if(!request.latestAvailable||!HandsMath::ValidFrame(request.latest,request.now,request.recenter))return Block(state,Result::Rejected);
    state.epoch=request.epoch;state.eye=request.eye;state.eyes|=1u<<request.eye;
    return Result::SecondEye;
}
inline bool Accepted(Result result){return result==Result::Admitted||result==Result::Reused||result==Result::SecondEye;}
}
