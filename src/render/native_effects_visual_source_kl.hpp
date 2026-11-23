#pragma once
#include <cstdint>
#include <cstddef>
namespace NativeEffectsKL {
using VisualTraceActive=bool(*)();
struct SpriteProducerEvidence {
    bool valid=false,transformKnown=false,positionKnown=false;
    std::uint64_t sequence=0;
    std::uint32_t callerRva=0,tick=0,slot=2,stamp=0,emitType=0,flags=0,header[5]{};
    std::uintptr_t entry=0,manager=0,textureHandle=0,argGeometry=0,storedGeometry=0;
    float transform[9]{},position[3]{};
};
struct VisualSourceDiagnostic {
    bool valid=false,viewKnown=false,rawBeforeKnown=false,rawAfterKnown=false,recordsKnown=false,rootKnown=false;
    unsigned kind=0,type=0,flags=~0u,slot=2,stamp=0,mode=~0u,writer=0,reason=0;
    unsigned allocatedQuads=0,liveQuads=0,sourceCount=0,firstRecord=0,recordStride=0,recordCount=0;
    std::uint32_t tick=0;
    std::uint64_t sequence=0,frame=0,drawScopeId=0;
    std::uintptr_t entry=0,entryVtable=0,state=0,geometry=0,records=0,instance=0,object=0,descriptor=0,descriptorHandle=0;
    SpriteProducerEvidence producer;
    std::uint32_t materialHeader[5]{};
    // Raw source records are an allocation prefix, not a claimed live-particle
    // correspondence. In contrast, rawQuad* is the actual written live quad.
    std::uint32_t rawQuadBefore[24]{},rawQuadAfter[24]{},rawRecords[4][29]{};
    float world[12]{},nativeView[12]{},eye[12]{},reference[12]{},projection[16]{},rootOriginal[12]{},rootBound[12]{};
};
void ConfigureVisualSourceDiagnostics(VisualTraceActive active);
bool SnapshotVisualSource(std::uintptr_t entry,std::uintptr_t state,VisualSourceDiagnostic& output);
}
