#pragma once
#include "frame_pacing.hpp"
#include <cstdint>
#include <algorithm>
namespace FrameDelivery {
class Metrics {
    bool active=false,havePair=false,submittedPair=false;
    std::uint64_t pair=0,submitted=0;
    unsigned epoch=0,submittedEpoch=0,crossed=0;
    std::uint32_t pairTick=0;
public:
    FramePacing::Samples freshPairIntervals;
    std::uint64_t freshPairs=0,sceneSubmits=0,repeatedSceneSubmits=0,sourcePairIdGaps=0;
    std::uint64_t holds50=0,holds100=0,holds250=0;
    std::uint32_t maxRepeatAgeMs=0;
    void SetActive(bool value){if(!value){havePair=submittedPair=false;crossed=0;}active=value;}
    bool NewPair(std::uint64_t frame,unsigned generation,std::uint32_t now){
        if(!active||!generation||!frame||(frame&1))return false;
        if(havePair&&epoch==generation&&pair==frame)return false;
        if(havePair&&epoch==generation&&frame>pair){
            freshPairIntervals.Add(std::uint64_t(std::uint32_t(now-pairTick))*1000);
            sourcePairIdGaps+=(frame-pair)/2-1;
        }
        pair=frame;epoch=generation;pairTick=now;havePair=true;crossed=0;++freshPairs;return true;
    }
    void SceneSubmit(std::uint32_t now){
        if(!active||!havePair)return;
        ++sceneSubmits;
        if(submittedPair&&submitted==pair&&submittedEpoch==epoch){
            ++repeatedSceneSubmits;const auto age=std::uint32_t(now-pairTick);
            maxRepeatAgeMs=std::max(maxRepeatAgeMs,age);
            if(age>=50&&!(crossed&1)){++holds50;crossed|=1;}
            if(age>=100&&!(crossed&2)){++holds100;crossed|=2;}
            if(age>=250&&!(crossed&4)){++holds250;crossed|=4;}
        }
        submitted=pair;submittedEpoch=epoch;submittedPair=true;
    }
    std::uint32_t LastPairAge(std::uint32_t now)const{return active&&havePair?now-pairTick:0;}
};
}
