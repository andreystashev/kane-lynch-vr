#pragma once
#include <cstdint>

// Meta SubmitFrame can block for compositor pacing. Its time is part of the
// interval between submissions, so an unchanged image must not wait for a
// second full display interval after the call returns. Fresh images retain
// priority and are submitted immediately, as before.
namespace MetaSubmitPacing {
class Cadence {
    std::uint32_t previousStart=0;
    bool started=false;
public:
    bool Due(bool updated,std::uint32_t now,std::uint32_t interval)const{
        return updated||!started||now-previousStart>=interval;
    }
    void Begin(std::uint32_t now){previousStart=now;started=true;}
};
}
