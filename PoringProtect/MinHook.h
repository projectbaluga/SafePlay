#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum MH_STATUS {
    MH_OK = 0,
} MH_STATUS;

static inline MH_STATUS MH_Initialize(void) { return MH_OK; }
static inline MH_STATUS MH_Uninitialize(void) { return MH_OK; }
static inline MH_STATUS MH_CreateHook(LPVOID target, LPVOID detour, LPVOID *original) {
    if (original) { *original = target; }
    return MH_OK;
}
static inline MH_STATUS MH_EnableHook(LPVOID) { return MH_OK; }
static inline MH_STATUS MH_DisableHook(LPVOID) { return MH_OK; }

#define MH_ALL_HOOKS NULL

#ifdef __cplusplus
}
#endif
