#pragma once
#include <cstdint>

// Steam's verified legacy bootstrap can exit 53 while a newly started client
// is still opening its IPC services. A living steam.exe is not that service's
// readiness contract. Retry only this observed pre-game failure; never retry a
// game crash, another native error, or an unqualified process candidate.
namespace LauncherSteamRetry {
constexpr std::uint32_t BudgetMs=45000,DelayMs=5000,MaximumRetries=8;
inline bool Eligible(bool steamRoute,bool nativeExited,std::uint32_t nativeExit,
                     unsigned rejected,bool hasGameHandle,unsigned completedRetries,
                     std::uint64_t elapsedMs) {
    return steamRoute&&nativeExited&&nativeExit==53&&!rejected&&!hasGameHandle&&
        completedRetries<MaximumRetries&&elapsedMs<=BudgetMs-DelayMs;
}
}
