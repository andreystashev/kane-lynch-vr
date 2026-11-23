/* Internal read-only metadata companion; not a public MinHook API. */
#pragma once
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KL1_MH_OWNED_PATCH {
    uintptr_t target;
    uintptr_t detour;
    uintptr_t patchAddress;
    unsigned int patchLength;
    unsigned int patchAbove;
    unsigned char backup[8];
    unsigned char installed[8];
} KL1_MH_OWNED_PATCH;

typedef enum KL1_MH_AUDIT_STATUS {
    KL1_MH_AUDIT_OK = 0,
    KL1_MH_AUDIT_NOT_INITIALIZED,
    KL1_MH_AUDIT_INVALID_ARGUMENT,
    KL1_MH_AUDIT_CAPACITY,
    KL1_MH_AUDIT_UNREADABLE,
    KL1_MH_AUDIT_LIVE_MISMATCH
} KL1_MH_AUDIT_STATUS;

/* All-or-nothing snapshot of this MinHook instance's enabled entries, under
   its existing mutation lock. No memory writes, callbacks or SDK operations.
   requiredCount is always set when non-null. CAPACITY never publishes records.
   This is an observation, not a lease against subsequent mutation. */
KL1_MH_AUDIT_STATUS WINAPI KL1_MH_AuditEnabledPatches(
    KL1_MH_OWNED_PATCH* records, unsigned int capacity, unsigned int* requiredCount);

#ifdef __cplusplus
}
#endif
