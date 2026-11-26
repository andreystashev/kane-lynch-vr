#pragma once

#include "owned_patch_audit_win.hpp"
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace Sdk35Family {
// These identity windows cover the cache's owner lookup and lock, protected
// table reader, module record lookup, and the page cache entry points.
struct CodeRegion {
    DWORD rva, size;
    const char* sha256;
    std::initializer_list<DWORD> relocations;
};
inline constexpr CodeRegion code[] = {
    {0xee55a, 82, "b134ae02bfc30213b8716d2309b200822a09ce50c4d4045ba9042715aedffaa0", {2, 33}},
    {0xf0354, 198, "99db28d635bbe731963f5fc37203c16739e72c872fa47c40bab08b552db675ab", {}},
    {0xf07c9, 195, "4ddb1d00dcd5e16c6808a3381e4d249a01f558db8051ce9d436af121531cd9d9", {36, 144, 153, 159, 183}},
    {0xf15b4, 188, "322ffcfd69f411bd259d40357fcc43d80762b7d566d3129ed6be29e710dc7900", {}},
    {0xf2f40, 32, "bd8bc425c59d846a4162d51e72534416caba490d52346221c98224b04f2c01ac", {}},
    {0x1648b3, 355, "267b5e849cec01db51f973851e10fb0ab41ba1115985f6a556c5b79c05cd24b2", {55, 136}},
};

inline constexpr DWORD fingerprintImageBase = 0x400000;
inline bool MatchesLiveCode(std::uintptr_t base) {
    // The loader can record the relocated base in the live PE header.
    const DWORD displacement = static_cast<DWORD>(base) - fingerprintImageBase;
    for (const auto& region : code) {
        std::vector<unsigned char> bytes(region.size);
        if (!KL1OwnedPatchAudit::ReadSelf(base + region.rva, bytes.data(), bytes.size()))
            return false;
        for (const auto offset : region.relocations) {
            DWORD operand = 0;
            std::memcpy(&operand, bytes.data() + offset, sizeof(operand));
            operand -= displacement;
            std::memcpy(bytes.data() + offset, &operand, sizeof(operand));
        }
        std::array<unsigned char, 32> digest{};
        if (!KL1OwnedPatchAudit::Sha256(bytes, digest))
            return false;
        const char* hex = "0123456789abcdef";
        for (std::size_t i = 0; i < digest.size(); ++i) {
            if (region.sha256[2 * i] != hex[digest[i] >> 4] ||
                region.sha256[2 * i + 1] != hex[digest[i] & 15])
                return false;
        }
    }
    return true;
}

inline bool IsMicrosoftGfwlSigner(HANDLE state) {
    auto* provider = WTHelperProvDataFromStateData(state);
    const auto* signer = provider
        ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)
        : nullptr;
    if (!signer || !signer->pChainContext || !signer->csCertChain ||
        !signer->pasCertChain || !signer->pasCertChain[0].pCert)
        return false;

    wchar_t name[96]{};
    if (!CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_ATTR_TYPE, 0,
                            const_cast<char*>(szOID_COMMON_NAME), name, std::size(name)) ||
        std::wcscmp(name, L"Microsoft LIVE Gaming for Windows"))
        return false;

    CERT_CHAIN_POLICY_PARA policy{};
    policy.cbSize = sizeof(policy);
    CERT_CHAIN_POLICY_STATUS result{};
    result.cbSize = sizeof(result);
    return CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT,
                                            signer->pChainContext, &policy, &result) &&
           result.dwError == ERROR_SUCCESS;
}

inline bool VerifyMicrosoftFile(const wchar_t* path, const char* digest) {
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    struct FileHandle { HANDLE value; ~FileHandle() { CloseHandle(value); } } closeFile{file};

    // Bind the signature decision to the digest checked by the startup caller.
    char current[65]{};
    if (!HashFile(path, current) || std::strcmp(current, digest))
        return false;

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path;
    fileInfo.hFile = file;
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &fileInfo;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const bool verified = WinVerifyTrustEx(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust) == ERROR_SUCCESS &&
                          IsMicrosoftGfwlSigner(trust.hWVTStateData);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrustEx(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);
    return verified;
}

inline bool TrustedFile(const wchar_t* path, const char* digest) {
    // The game's xlive.dll is loaded for the lifetime of this process. Avoid
    // reevaluating trust while the SDK's owner lock is held.
    static const std::pair<std::string, bool> checked{digest, VerifyMicrosoftFile(path, digest)};
    return checked.second && checked.first == digest;
}
} // namespace Sdk35Family
