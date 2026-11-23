#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#if !defined(FRAME_COPY_DISABLE_STREAMING) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2))
#include <emmintrin.h>
#define FRAME_COPY_STREAMING_SSE2 1
#endif

namespace FrameCopy {
struct Extent {std::uint32_t width=0,height=0;};
inline Extent Fit(std::uint32_t width,std::uint32_t height,std::uint32_t maxWidth,std::uint32_t maxHeight){
    if(!width||!height||!maxWidth||!maxHeight)return {};
    double scale=1.;if(width>maxWidth)scale=double(maxWidth)/width;
    if(double(height)*scale>maxHeight)scale=double(maxHeight)/height;
    return {std::uint32_t(double(width)*scale),std::uint32_t(double(height)*scale)};
}
inline bool ValidLayout(std::uint32_t width,std::uint32_t height,std::size_t stride) {
    if(!width||!height)return false;
    const std::size_t row=std::size_t(width)*4;
    if(row/4!=width)return false; // multiplication overflow on a 32-bit producer
    return stride>=row&&stride<=std::numeric_limits<std::size_t>::max()/height;
}
namespace Detail {
constexpr std::size_t StreamingMinimumBytes=16u*1024u*1024u;
inline bool Separate(const std::uint8_t* destination,std::size_t destinationBytes,
                     const std::uint8_t* source,std::size_t sourceBytes) {
    const auto dst=reinterpret_cast<std::uintptr_t>(destination),src=reinterpret_cast<std::uintptr_t>(source);
    return dst>=src?dst-src>=sourceBytes:src-dst>=destinationBytes;
}
#if defined(FRAME_COPY_STREAMING_SSE2)
inline void OpaqueStreaming(std::uint8_t* destination,const std::uint8_t* source,
                            std::uint32_t height,std::size_t row,std::size_t stride) {
    const auto alpha=_mm_set1_epi32(-16777216);
    for(std::uint32_t y=0;y<height;++y) {
        auto* out=destination+std::size_t(y)*row;
        const auto* in=source+std::size_t(y)*stride;
        std::size_t offset=0;
        // A pixel-aligned destination may start four or eight bytes into a
        // cache line. Only aligned 16-byte addresses receive streaming stores.
        for(;offset<row&&(reinterpret_cast<std::uintptr_t>(out+offset)&15u);offset+=4) {
            std::uint32_t pixel;std::memcpy(&pixel,in+offset,4);pixel|=0xff000000u;std::memcpy(out+offset,&pixel,4);
        }
        for(;row-offset>=64;offset+=64) {
            _mm_stream_si128(reinterpret_cast<__m128i*>(out+offset),_mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+offset)),alpha));
            _mm_stream_si128(reinterpret_cast<__m128i*>(out+offset+16),_mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+offset+16)),alpha));
            _mm_stream_si128(reinterpret_cast<__m128i*>(out+offset+32),_mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+offset+32)),alpha));
            _mm_stream_si128(reinterpret_cast<__m128i*>(out+offset+48),_mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+offset+48)),alpha));
        }
        for(;offset<row;offset+=4) {
            std::uint32_t pixel;std::memcpy(&pixel,in+offset,4);pixel|=0xff000000u;std::memcpy(out+offset,&pixel,4);
        }
    }
    // The writer publishes metadata after this function returns. Streaming
    // writes must reach the coherency domain before that publication.
    _mm_sfence();
}
#endif
}
// Source is borrowed and never mutated. Destination is tightly packed BGRA8.
// Normalize undefined X8/transparent A8 alpha while publishing, rather than
// materializing an extra full image in the capture module.
inline bool BGRA(std::uint8_t* dest,const std::uint8_t* source,
                 std::uint32_t width,std::uint32_t height,std::size_t stride,bool forceOpaque) {
    if(!dest||!source||!ValidLayout(width,height,stride))return false;
    const std::size_t row=std::size_t(width)*4;
    if(!forceOpaque&&stride==row){std::memcpy(dest,source,row*height);return true;}
#if defined(FRAME_COPY_STREAMING_SSE2)
    // Large interprocess images exceed the useful cache working set. Avoid
    // write-allocate/RFO traffic while combining the required alpha fix with
    // the one existing copy. Small, overlapping and unaligned layouts retain
    // the portable path; there is no extra image or resolution change.
    if(forceOpaque&&row>=64&&row*height>=Detail::StreamingMinimumBytes&&
       !(reinterpret_cast<std::uintptr_t>(dest)&3u)&&
       Detail::Separate(dest,row*height,source,std::size_t(height-1)*stride+row)) {
        Detail::OpaqueStreaming(dest,source,height,row,stride);return true;
    }
#endif
    for(std::uint32_t y=0;y<height;++y){
        auto* out=dest+std::size_t(y)*row;
        const auto* in=source+std::size_t(y)*stride;
        if(!forceOpaque){std::memcpy(out,in,row);continue;}
        for(std::uint32_t x=0;x<width;++x){
            const auto offset=std::size_t(x)*4;
            std::uint32_t pixel;std::memcpy(&pixel,in+offset,4);
            pixel|=0xff000000u;std::memcpy(out+offset,&pixel,4);
        }
    }
    return true;
}
}
#if defined(FRAME_COPY_STREAMING_SSE2)
#undef FRAME_COPY_STREAMING_SSE2
#endif
