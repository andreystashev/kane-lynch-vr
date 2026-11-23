#pragma once
#include <cstdint>
#include <type_traits>

namespace TeamCommandIPC {
enum class Command : std::uint32_t { None,Recall,Move,Attack };
enum class ResultCode : std::uint32_t { None,Accepted,Rejected,NoTarget,SelectionChanged,Stale,Unsupported };
struct ReceiverCertificate {
    std::uint32_t player=0,control=0,receiver=0,nativeId=0,tick=0,recenter=0;
    bool valid=false,all=false;
};
struct Request {
    std::uint32_t serial=0,tick=0,recenter=0;
    Command command=Command::None;
    ReceiverCertificate selected{};
};
struct Result {
    std::uint32_t serial=0,tick=0;
    ResultCode code=ResultCode::None;
    Command command=Command::None;
    std::uint32_t nativeId=0,targetActor=0;
    float point[3]{};
};
inline bool Known(Command command){return command>=Command::Recall&&command<=Command::Attack;}
inline bool Known(ResultCode code){return code>=ResultCode::None&&code<=ResultCode::Unsupported;}
inline bool SameReceiver(const ReceiverCertificate& a,const ReceiverCertificate& b) {
    return a.valid&&b.valid&&a.player==b.player&&a.control==b.control&&a.receiver==b.receiver&&
        a.nativeId==b.nativeId&&a.all==b.all&&a.recenter==b.recenter;
}
inline bool Fresh(const ReceiverCertificate& receiver,std::uint32_t now,std::uint32_t recenter) {
    return receiver.valid&&receiver.player&&receiver.control&&receiver.tick&&receiver.recenter==recenter&&
        std::int32_t(now-receiver.tick)>=0&&now-receiver.tick<=200&&
        (receiver.all?receiver.receiver==0&&receiver.nativeId==0:receiver.receiver!=0);
}
static_assert(sizeof(ReceiverCertificate)==28&&sizeof(Request)==44&&sizeof(Result)==36,"Team command data ABI changed");
static_assert(std::is_trivially_copyable<Request>::value&&std::is_trivially_copyable<Result>::value,"Team command payload must copy atomically under seqlock");
}
