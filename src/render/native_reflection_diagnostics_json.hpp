#pragma once
#include "native_mirror_bump_kl.hpp"
#include "native_reflection_camera_json.hpp"
#include "native_reflection_probe_kl.hpp"
namespace NativeReflectionDiagnosticsJSON {
inline void Mirror(FILE* out,const NativeMirrorBumpKL::Stats& value) {
    std::fprintf(out,",\"mirrorBump\":{\"seen\":%llu,\"recognized\":%llu,\"applied\":%llu,\"restored\":%llu,\"viewRejected\":%llu,\"matrixRejected\":%llu,\"zeroPreserved\":%llu,\"failed\":%llu,\"restoreFailed\":%llu,\"viewportApplied\":%llu,\"targetCaptured\":%llu,\"targetRejected\":%llu,\"targetEligible\":%llu,\"targetApplied\":%llu,\"targetRestored\":%llu,\"cachedTargetApplied\":%llu,\"targetInvalidated\":%llu,\"historyInvalidationReasons\":[",
        static_cast<unsigned long long>(value.seen),static_cast<unsigned long long>(value.recognized),
        static_cast<unsigned long long>(value.applied),static_cast<unsigned long long>(value.restored),
        static_cast<unsigned long long>(value.viewRejected),static_cast<unsigned long long>(value.matrixRejected),
        static_cast<unsigned long long>(value.zeroPreserved),static_cast<unsigned long long>(value.failed),
        static_cast<unsigned long long>(value.restoreFailed),static_cast<unsigned long long>(value.viewportApplied),static_cast<unsigned long long>(value.targetCaptured),
        static_cast<unsigned long long>(value.targetRejected),static_cast<unsigned long long>(value.targetEligible),
        static_cast<unsigned long long>(value.targetApplied),static_cast<unsigned long long>(value.targetRestored),
        static_cast<unsigned long long>(value.cachedTargetApplied),static_cast<unsigned long long>(value.targetInvalidated));
    for(unsigned i=0;i<6;++i)
        std::fprintf(out,"%s%llu",i?",":"",static_cast<unsigned long long>(value.invalidationReasons[i]));
    std::fprintf(out,"],\"cachedOtherEyeApplied\":%llu,\"historyRetained\":%llu,\"historySceneRetires\":%llu,\"historyForeignWrites\":%llu,\"historyIneligible\":%llu",
        static_cast<unsigned long long>(value.cachedOtherEyeApplied),
        static_cast<unsigned long long>(value.historyRetained),static_cast<unsigned long long>(value.historySceneRetires),
        static_cast<unsigned long long>(value.historyForeignWrites),static_cast<unsigned long long>(value.historyIneligible));
    std::fprintf(out,",\"tokyoFloorEye\":[%llu,%llu],\"tokyoFloorUntracked\":%llu,\"tokyoFloorRestored\":%llu",
        static_cast<unsigned long long>(value.tokyoFloorEye[0]),
        static_cast<unsigned long long>(value.tokyoFloorEye[1]),
        static_cast<unsigned long long>(value.tokyoFloorUntracked),
        static_cast<unsigned long long>(value.tokyoFloorRestored));
    std::fprintf(out,",\"retomotoFloorLive\":%llu,\"retomotoFloorHistory\":%llu,\"retomotoFloorFallback\":%llu,\"retomotoFloorCopies\":%llu",
        static_cast<unsigned long long>(value.retomotoFloorLive),static_cast<unsigned long long>(value.retomotoFloorHistory),
        static_cast<unsigned long long>(value.retomotoFloorFallback),static_cast<unsigned long long>(value.retomotoFloorCopies));
    std::fprintf(out,",\"towerCubeMatched\":%llu,\"towerCubeDraws\":%llu",
        static_cast<unsigned long long>(value.towerCubeMatched),
        static_cast<unsigned long long>(value.towerCubeDraws));
    std::fprintf(out,",\"tokyoFloorLive\":%llu,\"tokyoFloorHistory\":%llu,\"tokyoFloorFallback\":%llu,\"tokyoFloorCopies\":%llu,\"tokyoFloorCopyFailed\":%llu,\"lastRejected\":",
        static_cast<unsigned long long>(value.tokyoFloorLive),static_cast<unsigned long long>(value.tokyoFloorHistory),
        static_cast<unsigned long long>(value.tokyoFloorFallback),static_cast<unsigned long long>(value.tokyoFloorCopies),
        static_cast<unsigned long long>(value.tokyoFloorCopyFailed));
    const auto& e=value.lastRejected;
    if(!e.valid){std::fputs("null}",out);return;}
    std::fprintf(out,"{\"read\":%s,\"mainMatch\":%s,\"viewportRead\":%s,\"tick\":%u,\"bumpRegister\":%u,\"eyeIndex\":%u,\"state\":%llu,\"generation\":%llu,\"viewport\":[",
        e.read?"true":"false",e.mainMatch?"true":"false",e.viewportRead?"true":"false",e.tick,e.bump,e.eyeIndex,
        static_cast<unsigned long long>(e.state),static_cast<unsigned long long>(e.frame));
    for(unsigned i=0;i<6;++i)std::fprintf(out,"%s%u",i?",":"",e.viewport[i]);std::fputc(']',out);
    NativeReflectionCameraJSON::WriteRows(out,"modelViewScaleAndDepthBias",e.modelAdjustment,2);
    NativeReflectionCameraJSON::WriteRows(out,"eyeRightUpForwardPosition",e.eye,12);
    NativeReflectionCameraJSON::WriteRows(out,"world",e.world,12);
    NativeReflectionCameraJSON::WriteRows(out,"projection",e.projection,16);
    NativeReflectionCameraJSON::WriteRows(out,"actual",e.actual,32);
    NativeReflectionCameraJSON::WriteRows(out,"expected",e.expected,32);
    NativeReflectionCameraJSON::WriteRows(out,"roundingBounds",e.bounds,32);
    std::fputs("}}",out);
}
// Append fields to a Sample object already opened by the status writer.
inline void Probe(FILE* out,const NativeReflectionProbeKL::Sample& value) {
    std::fprintf(out,",\"pixelAvailable\":%s,\"vertexAvailable\":%s,\"modelViewProjectionRegister\":[%u,%u],\"worldViewProjectionScaleRegister\":[%u,%u],\"contextResult\":%u,\"context\":",
        value.pixelAvailable?"true":"false",value.vertexAvailable?"true":"false",
        static_cast<unsigned>(value.vertex.modelViewProjection.start),static_cast<unsigned>(value.vertex.modelViewProjection.count),
        static_cast<unsigned>(value.vertex.worldViewProjectionScale.start),static_cast<unsigned>(value.vertex.worldViewProjectionScale.count),
        static_cast<unsigned>(value.contextResult));
    if(value.contextResult==NativeReflectionProbeKL::ContextResult::Available){
        std::fprintf(out,"{\"state\":%llu,\"generation\":%llu,\"eyeIndex\":%u",
            static_cast<unsigned long long>(value.context.state),static_cast<unsigned long long>(value.context.generation),value.context.eyeIndex);
        NativeReflectionCameraJSON::WriteRows(out,"world",value.context.world,12);
        NativeReflectionCameraJSON::WriteRows(out,"eyeRightUpForwardPosition",value.context.eye,12);
        NativeReflectionCameraJSON::WriteRows(out,"projection",value.context.projection,16);std::fputc('}',out);
    }else std::fputs("null",out);
    std::fprintf(out,",\"planarMatricesAttempted\":%s,\"planarMatricesRead\":%s",
        value.planarMatricesAttempted?"true":"false",value.planarMatricesRead?"true":"false");
    if(value.planarMatricesRead)NativeReflectionCameraJSON::WriteRows(out,"planarC0_7",value.planarMatrices,32);
    std::fprintf(out,",\"shadowMvpAttempted\":%s,\"shadowMvpRead\":%s",
        value.shadowModelViewProjectionAttempted?"true":"false",value.shadowModelViewProjectionRead?"true":"false");
    if(value.shadowModelViewProjectionRead)NativeReflectionCameraJSON::WriteRows(out,"shadowMvp",value.shadowModelViewProjection,16);
    std::fputs(",\"shadowMatrices\":[",out);bool first=true;
    for(unsigned i=0;i<NativeReflectionProbeKL::ShadowRangeCount;++i){const auto range=value.vertex.shadow[i];if(!range.count)continue;
        std::fprintf(out,"%s{\"name\":\"%s\",\"register\":[%u,%u],\"attempted\":%s,\"read\":%s",
            first?"":",",NativeReflectionProbeKL::ShadowRangeNames[i],static_cast<unsigned>(range.start),static_cast<unsigned>(range.count),
            value.shadowAttemptMask&(1u<<i)?"true":"false",value.shadowReadMask&(1u<<i)?"true":"false");
        if(value.shadowReadMask&(1u<<i))NativeReflectionCameraJSON::WriteRows(out,"values",value.shadowValues[i],range.count*4);
        std::fputc('}',out);first=false;
    }std::fputc(']',out);
}
}
