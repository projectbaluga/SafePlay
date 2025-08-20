#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum MH_STATUS {
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;

MH_STATUS WINAPI MH_Initialize(void);
MH_STATUS WINAPI MH_Uninitialize(void);
MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget);
MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_ApplyQueued(void);
const char* WINAPI MH_StatusToString(MH_STATUS status);

#ifndef MH_ALL_HOOKS
#define MH_ALL_HOOKS NULL
#endif

#ifdef __cplusplus
}
#endif
