#pragma once
#include <windows.h>
#include <array>
#include <initializer_list>
#include "native_mirror_bump_programs_kl.hpp"

namespace NativeTokyoFloorProgramKL {
// ps_2_0. Keep the shipped Mirror Pixel0 normal decode, local lighting and
// diffuse sample. Keep the native reflection weight and substitute a broad
// cool ambient term with detail from the authored diffuse and normal maps.
inline constexpr auto Pixel = [] {
    using NativeMirrorBumpProgramsKL::Pixel0;
    std::array<DWORD, 144> code{};
    std::size_t next = 0;
    const auto copy = [&](std::size_t begin, std::size_t end) {
        while (begin < end) code[next++] = Pixel0[begin++];
    };
    const auto emit = [&](std::initializer_list<DWORD> words) {
        for (DWORD word : words) code[next++] = word;
    };

    copy(0, 7);   // ps_2_0; def c3, 2, -1, 1, 0
    emit({0x05000051, 0xa00f0004, 0x40400000, 0x3f000000, 0x3e4ccccd, 0x3f428f5c}); // def c4, 3, .5, .20, .76
    emit({0x05000051, 0xa00f0005, 0x3f1eb852, 0x3f3d70a4, 0x3f51eb85, 0x3e0f5c29}); // def c5, .62, .74, .82, .14
    copy(7, 13);  // dcl v0.rgb and t0.xy
    copy(16, 28); // dcl t2.xyz, t5.xyz and 2D samplers s0/s1
    copy(31, 64); // native normal decode and dp3_sat r2.x, t2, r0
    emit({0x03000002, 0x80010003, 0x80000000, 0x81550000});             // add r3.x, r0.x, -r0.y
    emit({0x04000004, 0x80110003, 0x80000003, 0xa0000004, 0xa0550004}); // mad_sat r3.x, r3.x, c4.x, c4.y
    copy(68, 80); // native local lighting in r2.rgb
    copy(92, 96); // texld r1, t0, s0 (authored stone diffuse and alpha)
    copy(96, 99); // mov r3.y, c3.z (one distinct c# read per ps_2_0 instruction)
    emit({0x03000002, 0x80020003, 0x80550003, 0xa1000002});             // add r3.y, r3.y, -c2.x
    emit({0x04000004, 0x80120003, 0x81ff0001, 0x80550003, 0xa0aa0003}); // mad_sat r3.y, -r1.w, r3.y, c3.z
    emit({0x03000005, 0x80010003, 0x80000003, 0xa0ff0005});             // mul r3.x, r3.x, c5.w
    emit({0x03000002, 0x80010003, 0x80000003, 0xa0ff0004});             // add r3.x, r3.x, c4.w
    emit({0x04000004, 0x80070004, 0x80e40001, 0xa0aa0004, 0x80000003}); // mad r4.rgb, r1, c4.z, r3.x
    emit({0x03000005, 0x80070004, 0x80e40004, 0xa0e40005});             // mul r4.rgb, r4, c5
    emit({0x03000005, 0x80070000, 0x80e40001, 0x80e40002});             // mul r0.rgb, r1, r2
    emit({0x03000002, 0x80070004, 0x80e40004, 0x81e40000});             // add r4.rgb, r4, -r0
    emit({0x04000004, 0x80070000, 0x80e40004, 0x80550003, 0x80e40000}); // mad r0.rgb, r4, r3.y, r0
    copy(117, 124); // native opaque output and end
    return code;
}();
static_assert(Pixel.back() == 0x0000ffff, "Tokyo floor PS2.0 must end at its final word");

// Exact ps_2_0 bytecode, assembled with D3DX. The original diffuse/light
// path contributes the base. c6/c7 project local X/Z normal displacement
// using the same matrix as t1. c8 is the certified texture viewport and
// c9 blends out samples at and behind the producing camera.
inline constexpr DWORD GlossyPixel[] = {
    0xffff0200,                                                                 // ps_2_0
    0x05000051, 0xa00f0003, 0x40000000, 0xbf800000, 0x3f800000, 0x00000000,     // def c3, 2, -1, 1, 0
    0x05000051, 0xa00f0004, 0x3e570a3d, 0x3e8f5c29, 0x3ea8f5c3, 0x3e6147ae,     // def c4, .21, .28, .33, .22
    0x05000051, 0xa00f0005, 0x3f47ae14, 0x3fcccccd, 0x3f47ae14, 0x41000000,     // def c5, .78, 1.6, .78, 8
    0x0200001f, 0x80000000, 0x90070000,                                         // dcl v0.rgb
    0x0200001f, 0x80000000, 0xb0030000,                                         // dcl t0.xy
    0x0200001f, 0x80000000, 0xb00f0001,                                         // dcl t1
    0x0200001f, 0x80000000, 0xb0070002,                                         // dcl t2.xyz
    0x0200001f, 0x80000000, 0xb0070005,                                         // dcl t5.xyz
    0x0200001f, 0x90000000, 0xa00f0800,                                         // dcl_2d s0
    0x0200001f, 0x90000000, 0xa00f0801,                                         // dcl_2d s1
    0x0200001f, 0x90000000, 0xa00f0802,                                         // dcl_2d s2
    0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40801,                             // texld r0, t0, s1
    0x04000004, 0x80010000, 0xa0000003, 0x80ff0000, 0xa0550003,                 // mad r0.x, c3.x, r0.a, c3.y
    0x04000004, 0x80020000, 0xa0000003, 0x80550000, 0xa0550003,                 // mad r0.y, c3.x, r0.g, c3.y
    0x03000005, 0x80030000, 0x80e40000, 0xa0e40000,                             // mul r0.xy, r0, c0
    0x0400005a, 0x80080000, 0x80e40000, 0x81e40000, 0xa0aa0003,                 // dp2add r0.w, r0, -r0, c3.z
    0x02000007, 0x80080000, 0x80ff0000,                                         // rsq r0.w, r0.w
    0x02000006, 0x80040000, 0x80ff0000,                                         // rcp r0.z, r0.w
    0x03000008, 0x80110002, 0xb0e40002, 0x80e40000,                             // dp3_sat r2.x, t2, r0
    0x02000001, 0x800f0001, 0xb0e40001,                                         // mov r1, t1
    0x0300000b, 0x80080002, 0x80ff0001, 0xa0000009,                             // max r2.w, r1.w, c9.x
    0x02000006, 0x80080002, 0x80ff0002,                                         // rcp r2.w, r2.w
    0x03000005, 0x80030004, 0x80e40001, 0x80ff0002,                             // mul r4.xy, r1, r2.w
    // Visibility follows the unperturbed world point. The normal map still
    // distorts the sampled colour within the current reflector viewport.
    0x03000002, 0x80030003, 0x80e40004, 0xa1e40008,                             // add r3.xy, r4, -c8
    0x03000002, 0x80040003, 0xa0aa0008, 0x81000004,                             // add r3.z, c8.z, -r4.x
    0x03000002, 0x80080003, 0xa0ff0008, 0x81550004,                             // add r3.w, c8.w, -r4.y
    0x0300000a, 0x80010003, 0x80000003, 0x80aa0003,                             // min r3.x, r3.x, r3.z
    0x0300000a, 0x80020003, 0x80550003, 0x80ff0003,                             // min r3.y, r3.y, r3.w
    0x0300000a, 0x80010003, 0x80000003, 0x80550003,                             // min r3.x, r3.x, r3.y
    0x03000005, 0x80110003, 0x80000003, 0xa0ff0005,                             // mul_sat r3.x, r3.x, c5.w
    0x03000002, 0x80020003, 0x80ff0001, 0xa1000009,                             // add r3.y, r1.w, -c9.x
    0x03000005, 0x80120003, 0x80550003, 0xa0550009,                             // mul_sat r3.y, r3.y, c9.y
    0x03000005, 0x80010003, 0x80000003, 0x80550003,                             // mul r3.x, r3.x, r3.y
    0x03000005, 0x80010003, 0x80000003, 0xa0aa0009,                             // mul r3.x, r3.x, c9.z
    0x04000004, 0x800b0001, 0x80000000, 0xa0e40006, 0x80e40001,                 // mad r1.xyw, r0.x, c6, r1
    0x04000004, 0x800b0001, 0x80550000, 0xa0e40007, 0x80e40001,                 // mad r1.xyw, r0.y, c7, r1
    0x0300000b, 0x80080002, 0x80ff0001, 0xa0000009,                             // max r2.w, r1.w, c9.x
    0x02000006, 0x80080002, 0x80ff0002,                                         // rcp r2.w, r2.w
    0x03000005, 0x80030001, 0x80e40001, 0x80ff0002,                             // mul r1.xy, r1, r2.w
    0x0300000b, 0x80030001, 0x80e40001, 0xa0e40008,                             // max r1.xy, r1, c8
    0x0300000a, 0x80010001, 0x80000001, 0xa0aa0008,                             // min r1.x, r1.x, c8.z
    0x0300000a, 0x80020001, 0x80550001, 0xa0ff0008,                             // min r1.y, r1.y, c8.w
    0x03000042, 0x800f0005, 0x80e40001, 0xa0e40802,                             // texld r5, r1, s2
    0x03000002, 0x80020003, 0x80000000, 0x81550000,                             // add r3.y, r0.x, -r0.y
    0x02000001, 0x80080005, 0xa0ff0004,                                         // mov r5.w, c4.w
    0x04000004, 0x80120003, 0x80550003, 0xa0550005, 0x80ff0005,                 // mad_sat r3.y, r3.y, c5.y, r5.w
    0x04000004, 0x80020003, 0x80550003, 0x80ff0005, 0xa0000005,                 // mad r3.y, r3.y, r5.w, c5.x
    0x03000005, 0x80070006, 0x80550003, 0xa0e40004,                             // mul r6.rgb, r3.y, c4
    0x02000001, 0x80070000, 0x90e40000,                                         // mov r0.rgb, v0
    0x04000004, 0x80070000, 0x80000002, 0x80e40000, 0xb0e40005,                 // mad r0.rgb, r2.x, r0, t5
    0x03000002, 0x80070002, 0x80e40000, 0x80e40000,                             // add r2.rgb, r0, r0
    0x03000042, 0x800f0001, 0xb0e40000, 0xa0e40800,                             // texld r1, t0, s0
    0x03000005, 0x80070004, 0x80e40002, 0x80e40001,                             // mul r4.rgb, r2, r1
    0x04000004, 0x80070006, 0x80e40006, 0xa0aa0005, 0x80e40004,                 // mad r6.rgb, r6, c5.z, r4
    0x03000002, 0x80070005, 0x80e40005, 0x81e40006,                             // add r5.rgb, r5, -r6
    0x04000004, 0x80070005, 0x80e40005, 0x80000003, 0x80e40006,                 // mad r5.rgb, r5, r3.x, r6
    0x02000001, 0x80020003, 0xa0aa0003,                                         // mov r3.y, c3.z
    0x03000002, 0x80020003, 0x80550003, 0xa1000002,                             // add r3.y, r3.y, -c2.x
    0x04000004, 0x80120003, 0x81ff0001, 0x80550003, 0xa0aa0003,                 // mad_sat r3.y, -r1.w, r3.y, c3.z
    0x03000002, 0x80070005, 0x80e40005, 0x81e40004,                             // add r5.rgb, r5, -r4
    0x04000004, 0x80070000, 0x80e40005, 0x80550003, 0x80e40004,                 // mad r0.rgb, r5, r3.y, r4
    0x02000001, 0x80080000, 0xa0aa0003,                                         // mov r0.a, c3.z
    0x02000001, 0x800f0800, 0x80e40000,                                         // mov oC0, r0
    0x0000ffff,                                                                 // end
};

// Same material at zero coverage. No projected inputs or sampler s2 are
// required before the first certified reflector draw or after scene reset.
inline constexpr DWORD GlossyFallbackPixel[] = {
    0xffff0200,                                                                 // ps_2_0
    0x05000051, 0xa00f0003, 0x40000000, 0xbf800000, 0x3f800000, 0x00000000,     // def c3, 2, -1, 1, 0
    0x05000051, 0xa00f0004, 0x3e570a3d, 0x3e8f5c29, 0x3ea8f5c3, 0x3e6147ae,     // def c4, .21, .28, .33, .22
    0x05000051, 0xa00f0005, 0x3f47ae14, 0x3fcccccd, 0x3f47ae14, 0x41700000,     // def c5, .78, 1.6, .78, 15
    0x0200001f, 0x80000000, 0x90070000,                                         // dcl v0.rgb
    0x0200001f, 0x80000000, 0xb0030000,                                         // dcl t0.xy
    0x0200001f, 0x80000000, 0xb0070002,                                         // dcl t2.xyz
    0x0200001f, 0x80000000, 0xb0070005,                                         // dcl t5.xyz
    0x0200001f, 0x90000000, 0xa00f0800,                                         // dcl_2d s0
    0x0200001f, 0x90000000, 0xa00f0801,                                         // dcl_2d s1
    0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40801,                             // texld r0, t0, s1
    0x04000004, 0x80010000, 0xa0000003, 0x80ff0000, 0xa0550003,                 // mad r0.x, c3.x, r0.a, c3.y
    0x04000004, 0x80020000, 0xa0000003, 0x80550000, 0xa0550003,                 // mad r0.y, c3.x, r0.g, c3.y
    0x03000005, 0x80030000, 0x80e40000, 0xa0e40000,                             // mul r0.xy, r0, c0
    0x0400005a, 0x80080000, 0x80e40000, 0x81e40000, 0xa0aa0003,                 // dp2add r0.w, r0, -r0, c3.z
    0x02000007, 0x80080000, 0x80ff0000,                                         // rsq r0.w, r0.w
    0x02000006, 0x80040000, 0x80ff0000,                                         // rcp r0.z, r0.w
    0x03000008, 0x80110002, 0xb0e40002, 0x80e40000,                             // dp3_sat r2.x, t2, r0
    0x02000001, 0x80010003, 0xa0ff0003,                                         // mov r3.x, c3.w
    0x02000001, 0x800f0005, 0xa0ff0003,                                         // mov r5, c3.w
    0x03000002, 0x80020003, 0x80000000, 0x81550000,                             // add r3.y, r0.x, -r0.y
    0x02000001, 0x80080005, 0xa0ff0004,                                         // mov r5.w, c4.w
    0x04000004, 0x80120003, 0x80550003, 0xa0550005, 0x80ff0005,                 // mad_sat r3.y, r3.y, c5.y, r5.w
    0x04000004, 0x80020003, 0x80550003, 0x80ff0005, 0xa0000005,                 // mad r3.y, r3.y, r5.w, c5.x
    0x03000005, 0x80070006, 0x80550003, 0xa0e40004,                             // mul r6.rgb, r3.y, c4
    0x02000001, 0x80070000, 0x90e40000,                                         // mov r0.rgb, v0
    0x04000004, 0x80070000, 0x80000002, 0x80e40000, 0xb0e40005,                 // mad r0.rgb, r2.x, r0, t5
    0x03000002, 0x80070002, 0x80e40000, 0x80e40000,                             // add r2.rgb, r0, r0
    0x03000042, 0x800f0001, 0xb0e40000, 0xa0e40800,                             // texld r1, t0, s0
    0x03000005, 0x80070004, 0x80e40002, 0x80e40001,                             // mul r4.rgb, r2, r1
    0x04000004, 0x80070006, 0x80e40006, 0xa0aa0005, 0x80e40004,                 // mad r6.rgb, r6, c5.z, r4
    0x03000002, 0x80070005, 0x80e40005, 0x81e40006,                             // add r5.rgb, r5, -r6
    0x04000004, 0x80070005, 0x80e40005, 0x80000003, 0x80e40006,                 // mad r5.rgb, r5, r3.x, r6
    0x02000001, 0x80020003, 0xa0aa0003,                                         // mov r3.y, c3.z
    0x03000002, 0x80020003, 0x80550003, 0xa1000002,                             // add r3.y, r3.y, -c2.x
    0x04000004, 0x80120003, 0x81ff0001, 0x80550003, 0xa0aa0003,                 // mad_sat r3.y, -r1.w, r3.y, c3.z
    0x03000002, 0x80070005, 0x80e40005, 0x81e40004,                             // add r5.rgb, r5, -r4
    0x04000004, 0x80070000, 0x80e40005, 0x80550003, 0x80e40004,                 // mad r0.rgb, r5, r3.y, r4
    0x02000001, 0x80080000, 0xa0aa0003,                                         // mov r0.a, c3.z
    0x02000001, 0x800f0800, 0x80e40000,                                         // mov oC0, r0
    0x0000ffff,                                                                 // end
};

// The atrium's native reflector can stop producing a room image. The
// Mirror vertex program still emits its object-space eye and light-half
// vectors. Use those with the actual normal map for reflected light where
// no valid planar image covers the tile. No screen or history coordinates
// take part in this term.
template<bool lower=false,std::size_t N> constexpr auto AddAtriumPolish(const DWORD (&base)[N],bool planar) {
    constexpr DWORD inputs[]{
        0x0200001f,0x80000000,0xb0070003, // dcl t3.xyz, eye direction in tangent space
        0x0200001f,0x80000000,0xb0070004, // dcl t4.xyz, half vector in tangent space
    };
    const DWORD light[]{
        0x03000008,0x80110007,0x80e40000,0xb0e40004,             // dp3_sat r7.x, r0, t4
        0x03000005,0x80010007,0x80000007,0x80000007,             // mul r7.x, r7.x, r7.x
        0x03000005,0x80010007,0x80000007,0x80000007,             // mul r7.x, r7.x, r7.x
        0x03000008,0x80120007,0x80e40000,0xb0e40003,             // dp3_sat r7.y, r0, t3
        0x03000002,0x80020007,0xa0aa0003,0x81550007,             // add r7.y, c3.z, -r7.y
        0x03000005,0x80020007,0x80550007,0x80550007,             // mul r7.y, r7.y, r7.y
        0x04000004,0x80110007,0x80550007,0xa0ff0004,0x80000007, // mad_sat r7.x, r7.y, c4.w, r7.x
        0x03000005,0x80010007,0x80000007,lower?0xa0000003:0xa0aa0005, // strengthen the unprojected lower-floor highlight
    };
    constexpr DWORD mix[]{
        0x03000002,0x80010003,0xa0aa0003,0x81000003,             // add r3.x, c3.z, -r3.x (outside planar coverage)
        0x03000005,0x80010007,0x80000007,0x80000003,             // mul r7.x, r7.x, r3.x
        0x02000001,0x80070006,0x90e40000,                        // mov r6.rgb, v0
        0x03000002,0x80070006,0x80e40006,0xb0e40005,             // add r6.rgb, r6, t5
        0x04000004,0x80170000,0x80e40006,0x80000007,0x80e40000, // mad_sat r0.rgb, r6, r7.x, r0
    };
    constexpr DWORD ambient[]{0x03000002,0x80070006,0x80e40006,0xa0aa0004}; // add r6.rgb, r6, c4.zzzz
    const std::size_t samplers=planar?34:31,normalEnd=planar?76:70;
    std::array<DWORD,N+std::size(inputs)+std::size(light)+std::size(mix)+(lower?std::size(ambient):0)> code{};
    std::size_t next=0;
    for(std::size_t i=0;i<N;++i){
        if(i==samplers)for(auto value:inputs)code[next++]=value;
        if(i==normalEnd)for(auto value:light)code[next++]=value;
        if(i==N-7){
            for(std::size_t m=0;m<std::size(mix);++m){
                if constexpr(lower)
                    if(m==std::size(mix)-5)for(auto value:ambient)code[next++]=value;
                code[next++]=mix[m];
            }
        }
        code[next++]=base[i];
    }
    return code;
}
inline constexpr auto AtriumGloss=AddAtriumPolish(GlossyPixel,true);
inline constexpr auto AtriumFallback=AddAtriumPolish(GlossyFallbackPixel,false);
inline constexpr auto LowerGloss=AddAtriumPolish<true>(GlossyPixel,true);
inline constexpr auto LowerFallback=AddAtriumPolish<true>(GlossyFallbackPixel,false);
template<class Program> constexpr auto MaskedLowerFloor(const Program& base) {
    // Mirror's mask-only variant binds the base colour in s3 and keeps
    // the authored output alpha: colour.a * (1 - c2.x).
    constexpr auto N=sizeof(Program)/sizeof(DWORD);
    std::array<DWORD,N+2> code{};
    std::size_t out=0;
    for(std::size_t i=0;i<N-7;++i){
        DWORD word=base[i];
        if(word==0xa00f0800||word==0xa0e40800)word+=3;
        else if(word==0xa00f0801||word==0xa0e40801||word==0xa00f0802||word==0xa0e40802)--word;
        code[out++]=word;
    }
    constexpr DWORD alpha[]{0x04000004,0x80080000,0x81ff0001,0xa0000002,0x80ff0001}; // mad r0.a, -r1.a, c2.x, r1.a
    for(auto word:alpha)code[out++]=word;
    for(std::size_t i=N-4;i<N;++i)code[out++]=base[i];
    return code;
}
inline constexpr auto LowerMaskedGloss=MaskedLowerFloor(LowerGloss);
inline constexpr auto LowerMaskedFallback=MaskedLowerFloor(LowerFallback);
inline constexpr auto RingMaskedGloss=MaskedLowerFloor(GlossyPixel);
inline constexpr auto RingMaskedFallback=MaskedLowerFloor(GlossyFallbackPixel);
inline constexpr auto RingEnvironment(){
    constexpr DWORD material[]{0x05000051,0xa00f0006,0x3df5c28f,0x3ef0a3d7,0x3f800000,0x3ef5c28f}; // c6 = .12, .47, 1, .48
    constexpr DWORD rayInput[]{0x0200001f,0x80000000,0xb0070006}; // dcl t6.xyz
    constexpr DWORD cubeInput[]{0x0200001f,0x98000000,0xa00f0804}; // dcl_cube s4
    // t6 is the world-space vector from this floor point to the current
    // eye. Reflect its normalized direction about world Y, with subtle
    // distortion from the existing normal map and a grazing-angle blend.
    constexpr DWORD reflection[]{
        0x03000008,0x80080006,0xb0e40006,0xb0e40006,             // dp3 r6.w, t6, t6
        0x02000007,0x80080006,0x80ff0006,                        // rsq r6.w, r6.w
        0x03000005,0x80070005,0xb1e40006,0x80ff0006,             // mul r5.xyz, -t6, r6.w
        0x03000005,0x80020005,0xb0550006,0x80ff0006,             // mul r5.y, t6.y, r6.w
        0x04000004,0x80010005,0x80000000,0xa0000006,0x80000005,  // mad r5.x, r0.x, c6.x, r5.x
        0x04000004,0x80040005,0x80550000,0xa0000006,0x80aa0005,  // mad r5.z, r0.y, c6.x, r5.z
        0x03000002,0x80110003,0xa0aa0006,0x81550005,             // add_sat r3.x, c6.z, -r5.y
        0x03000005,0x80010003,0x80000003,0x80000003,             // mul r3.x, r3.x, r3.x
        0x04000004,0x80010003,0x80000003,0xa0550006,0xa0ff0006,  // mad r3.x, r3.x, c6.y, c6.w
        0x03000042,0x800f0005,0x80e40005,0xa0e40804,             // texld r5, r5, s4
        0x03000005,0x80170005,0x80e40005,0xa0000003,             // mul_sat r5.rgb, r5, c3.x
    };
    std::array<DWORD,std::size(GlossyFallbackPixel)+std::size(material)+std::size(rayInput)+
        std::size(cubeInput)+std::size(reflection)-6> code{};
    unsigned out=0;
    for(unsigned i=0;i<std::size(GlossyFallbackPixel);++i){
        if(i==19)for(auto word:material)code[out++]=word;
        if(i==25)for(auto word:rayInput)code[out++]=word;
        if(i==37)for(auto word:cubeInput)code[out++]=word;
        if(i==70){
            for(auto word:reflection)code[out++]=word;
            i=75;continue;
        }
        code[out++]=GlossyFallbackPixel[i];
    }
    return code;
}
inline constexpr auto RetomotoEnvironment=RingEnvironment();
inline constexpr auto RetomotoMaskedEnvironment=MaskedLowerFloor(RetomotoEnvironment);

inline constexpr auto RingProjectorVertex=[] {
    using NativeMirrorBumpProgramsKL::VertexPlain;
    // Preserve the native t1 projection. Put the unprojected world-space
    // eye ray in t6, using constants beyond the shipped program's range.
    constexpr DWORD ray[]{
        0x00000009,0xe0010006,0x90e40000,0xa0e40010, // dp4 oT6.x, v0, c16
        0x00000009,0xe0020006,0x90e40000,0xa0e40011, // dp4 oT6.y, v0, c17
        0x00000009,0xe0040006,0x90e40000,0xa0e40012, // dp4 oT6.z, v0, c18
    };
    std::array<DWORD,std::size(VertexPlain)+std::size(ray)> result{};
    unsigned out=0;
    for(unsigned i=0;i<std::size(VertexPlain)-1;++i)result[out++]=VertexPlain[i];
    for(const auto word:ray)result[out++]=word;
    result[out]=0xffff;
    return result;
}();

inline constexpr auto RingProjector=[] {
    constexpr DWORD rayInput[]{
        0x0200001f,0x80000000,0xb0070006, // dcl t6.xyz
        0x0200001f,0x98000000,0xa00f0804, // dcl_cube s4
    };
    constexpr DWORD material[]{0x05000051,0xa00f000a,0x3df5c28f,0x3ef0a3d7,0x3f800000,0x3ef5c28f}; // c10 = .12, .47, 1, .48
    constexpr DWORD blend[]{
        0x03000008,0x80080006,0xb0e40006,0xb0e40006,             // dp3 r6.w, t6, t6
        0x02000007,0x80080006,0x80ff0006,                        // rsq r6.w, r6.w
        0x03000005,0x80070007,0xb1e40006,0x80ff0006,             // mul r7.xyz, -t6, r6.w
        0x03000005,0x80020007,0xb0550006,0x80ff0006,             // mul r7.y, t6.y, r6.w
        0x04000004,0x80010007,0x80000000,0xa000000a,0x80000007,  // mad r7.x, r0.x, c10.x, r7.x
        0x04000004,0x80040007,0x80550000,0xa000000a,0x80aa0007,  // mad r7.z, r0.y, c10.x, r7.z
        0x03000002,0x80180006,0xa0aa000a,0x81550007,             // add_sat r6.w, c10.z, -r7.y
        0x03000005,0x80080006,0x80ff0006,0x80ff0006,             // mul r6.w, r6.w, r6.w
        0x04000004,0x80080006,0x80ff0006,0xa055000a,0xa0ff000a,  // mad r6.w, r6.w, c10.y, c10.w
        0x03000042,0x800f0007,0x80e40007,0xa0e40804,             // texld r7, r7, s4
        0x03000005,0x80170007,0x80e40007,0xa0000003,             // mul_sat r7.rgb, r7, c3.x
        0x04000012,0x80070005,0x80000003,0x80e40005,0x80e40007,  // lrp r5.rgb, r3.x, planar, cube
        0x04000012,0x80080007,0x80000003,0xa0aa0003,0x80ff0006,  // lrp r7.w, r3.x, 1, cube strength
    };
    std::array<DWORD,std::size(GlossyPixel)+std::size(rayInput)+std::size(material)+std::size(blend)> code{};
    unsigned out=0;
    bool sampled=false;
    for(unsigned i=0;i<std::size(GlossyPixel);++i){
        if(i==19)for(const auto word:material)code[out++]=word;
        if(i==34)for(const auto word:rayInput)code[out++]=word;
        const auto word=GlossyPixel[i];
        code[out++]=sampled&&word==0x80000003?0x80ff0007:word; // final reflection blend uses combined strength
        if(word==0xa0e40802){
            for(const auto instruction:blend)code[out++]=instruction;
            sampled=true;
        }
    }
    return code;
}();
inline constexpr auto RingMaskedProjector=MaskedLowerFloor(RingProjector);
static_assert(RingProjector.back()==0xffff&&RingMaskedProjector.back()==0xffff);
static_assert(AtriumGloss.back()==0xffff&&AtriumFallback.back()==0xffff);
static_assert(LowerMaskedGloss.back()==0xffff&&LowerMaskedFallback.back()==0xffff);
}
