// PRIVATE read-only byte ownership audit. No SDK APIs, writes, or hash cache.
#pragma once
#include "owned_patch_audit.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace KL1OwnedPatchAudit {
enum class Result { Okay, InvalidImage, InvalidRange, ForeignDetour, InvalidRecord,
                    BackupMismatch, MalformedBranch, Overlap, UnknownDifference };
struct Page {
    std::uint32_t pageRva = 0;
    std::uint32_t comparedOffset = 0;
    std::uint32_t comparedLength = 0;
    std::uint32_t ownedRecordCount = 0;
    std::uint32_t changedBytes = 0;
    std::vector<unsigned char> canonical;
    std::vector<unsigned char> live;
};
struct Bounds { std::uintptr_t base = 0; std::size_t size = 0; };
inline bool Contains(Bounds bounds, std::uintptr_t begin, std::size_t count) {
    return bounds.size && begin >= bounds.base && begin-bounds.base <= bounds.size &&
           count <= bounds.size-(begin-bounds.base) &&
           bounds.base <= std::numeric_limits<std::uintptr_t>::max()-bounds.size;
}

// Data-only worker for focused fixtures and the verified file wrapper. The
// caller supplies one complete .text snapshot; no partial comparison silently
// certifies an entire page. Output is published only after all bytes pass.
inline Result CompareText(const unsigned char* canonical, const unsigned char* live,
                          std::size_t textLength, std::uint32_t textRva,
                          Bounds game, Bounds self,
                          const KL1_MH_OWNED_PATCH* records, std::size_t count,
                          std::vector<Page>& output) {
    output.clear();
    if (!canonical || !live || !textLength || textLength > 64*1024*1024 ||
        !Contains(game, game.base+textRva, textLength) || !Contains(self,self.base,self.size) ||
        (count && !records) || count > 4096 || game.base+textRva < game.base)
        return Result::InvalidImage;
    const Bounds text{game.base+textRva,textLength};
    std::vector<unsigned char> expected(canonical,canonical+textLength);
    std::vector<unsigned char> owner(textLength,0);
    std::vector<const KL1_MH_OWNED_PATCH*> selected;
    for (std::size_t n=0;n<count;++n) {
        const auto& r=records[n];
        // Hooks fully outside the game image belong to other APIs and are not
        // certified as game changes. Any boundary-crossing game patch rejects.
        if (!Contains(game,r.patchAddress,1) && !Contains(game,r.target,1)) continue;
        if (!Contains(text,r.patchAddress,r.patchLength)) return Result::InvalidRange;
        if (!Contains(self,r.detour,1)) return Result::ForeignDetour;
        if (r.patchAbove>1 || r.patchLength!=(r.patchAbove?7u:5u) ||
            (r.patchAbove && r.target<5) ||
            r.patchAddress!=r.target-(r.patchAbove?5u:0u)) return Result::InvalidRecord;
        unsigned char branch[8]{}; branch[0]=0xe9;
        const std::uint32_t relative=static_cast<std::uint32_t>(r.detour-(r.patchAddress+5));
        std::memcpy(branch+1,&relative,4);
        if(r.patchAbove){branch[5]=0xeb;branch[6]=0xf9;}
        if(std::memcmp(branch,r.installed,r.patchLength)) return Result::MalformedBranch;
        const auto offset=r.patchAddress-text.base;
        if(std::memcmp(canonical+offset,r.backup,r.patchLength)) return Result::BackupMismatch;
        for(std::size_t i=0;i<r.patchLength;++i){
            if(owner[offset+i])return Result::Overlap;
            owner[offset+i]=1;expected[offset+i]=r.installed[i];
        }
        selected.push_back(&r);
    }
    if(std::memcmp(expected.data(),live,textLength))return Result::UnknownDifference;
    std::vector<Page> checked;
    const std::uint64_t endRva=static_cast<std::uint64_t>(textRva)+textLength;
    for(std::uint64_t page=textRva&~std::uint64_t{4095};page<endRva;page+=4096){
        const auto begin=std::max<std::uint64_t>(page,textRva),end=std::min<std::uint64_t>(page+4096,endRva);
        Page p;p.pageRva=static_cast<std::uint32_t>(page);p.comparedOffset=static_cast<std::uint32_t>(begin-page);
        p.comparedLength=static_cast<std::uint32_t>(end-begin);
        const auto at=static_cast<std::size_t>(begin-textRva);
        p.canonical.assign(canonical+at,canonical+at+p.comparedLength);
        p.live.assign(live+at,live+at+p.comparedLength);
        for(const auto* r:selected)if(r->patchAddress<game.base+end && r->patchAddress+r->patchLength>game.base+begin)++p.ownedRecordCount;
        for(std::size_t i=0;i<p.comparedLength;++i)p.changedBytes+=p.canonical[i]!=p.live[i];
        if(p.changedBytes)checked.push_back(std::move(p));
    }
    output=std::move(checked);
    return Result::Okay;
}
} // namespace KL1OwnedPatchAudit
