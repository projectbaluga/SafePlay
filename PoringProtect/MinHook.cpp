#include "MinHook.h"

MH_STATUS WINAPI MH_Initialize(void) {
    return MH_OK;
}

MH_STATUS WINAPI MH_Uninitialize(void) {
    return MH_OK;
}

MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal) {
    if (ppOriginal)
        *ppOriginal = pTarget;
    return MH_OK;
}

MH_STATUS WINAPI MH_RemoveHook(LPVOID) {
    return MH_OK;
}

MH_STATUS WINAPI MH_EnableHook(LPVOID) {
    return MH_OK;
}

MH_STATUS WINAPI MH_DisableHook(LPVOID) {
    return MH_OK;
}

MH_STATUS WINAPI MH_QueueEnableHook(LPVOID) {
    return MH_OK;
}

MH_STATUS WINAPI MH_QueueDisableHook(LPVOID) {
    return MH_OK;
}

MH_STATUS WINAPI MH_ApplyQueued(void) {
    return MH_OK;
}

const char* WINAPI MH_StatusToString(MH_STATUS) {
    return "MH_OK";
}
