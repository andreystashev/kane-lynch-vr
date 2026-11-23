#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Passive queue evidence, independent of any projection certificate. The
// first32 and latest96 commands survive; every command contributes to counts.
namespace NativeCameraDiagnosticsKL {
constexpr unsigned MaximumPackets=128,PayloadPreviewWords=40;
struct Packet {
    std::uint32_t ordinal=0,header[6]{},payloadWords=0;
    std::uintptr_t address=0;
    std::uint32_t payload[PayloadPreviewWords]{};
};
struct ProducerBatchContext {
    std::uint32_t version=1,tick=0,thread=0,reason=0;
    std::uint64_t sequence=0,generation=0;
    std::uintptr_t sourceEntry=0,entryVtable=0,geometry=0,sourceCamera=0,cameraVtable=0;
    std::uint32_t cameraFlags=0;
    bool cameraFlagsRead=false,sourceVerified=false;
    std::uintptr_t arguments[5]{},queue=0,queueBegin=0,before=0,after=0,capacity=0;
    std::uint32_t commands=0,kindCounts[64]{},unknownKinds=0,passCounts[16]{},otherPasses=0;
    std::uint32_t packetsStored=0,packetsOmitted=0;
    bool complete=false;
    std::uintptr_t failureAt=0;
    Packet packets[MaximumPackets]{};
};
// Reason0 includes a valid empty append,1invalidbounds,2headerread,
//3foreignqueue,4payloadbounds,5previewread,6commandlimit,7producerread.
template<class Reader> bool TraceBatch(Reader read,ProducerBatchContext& out) {
    if(!out.queue||!out.queueBegin||out.before<out.queueBegin||out.after<out.before||
       out.after>out.capacity||out.after-out.before>0x1000000){out.reason=1;return false;}
    auto at=out.before;
    while(at<out.after){
        if(out.commands==65536){out.reason=6;out.failureAt=at;return false;}
        Packet next;next.ordinal=out.commands;next.address=at;
        if(out.after-at<24||!read(at,next.header,24)){out.reason=2;out.failureAt=at;return false;}
        if(next.header[2]!=out.queue){out.reason=3;out.failureAt=at;return false;}
        const auto words=next.header[5];
        if(std::uint64_t(words)*4>out.after-at-24){out.reason=4;out.failureAt=at;return false;}
        next.payloadWords=words<PayloadPreviewWords?words:PayloadPreviewWords;
        if(next.payloadWords&&!read(at+24,next.payload,next.payloadWords*4)){out.reason=5;out.failureAt=at;return false;}
        const unsigned slot=out.commands<32?out.commands:32+(out.commands-32)%(MaximumPackets-32);
        out.packets[slot]=next;++out.commands;
        if(out.commands<=MaximumPackets)++out.packetsStored;else ++out.packetsOmitted;
        const auto kind=next.header[0];if(kind<64)++out.kindCounts[kind];else ++out.unknownKinds;
        if(kind==0x12){const auto pass=next.header[4];if(pass<16)++out.passCounts[pass];else ++out.otherPasses;}
        at+=24+std::uintptr_t(words)*4;
    }
    out.complete=true;return true;
}
}
