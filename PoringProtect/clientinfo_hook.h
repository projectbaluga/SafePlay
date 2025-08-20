#pragma once
#include <windows.h>

// Initialize hooks that serve embedded clientinfo.xml.
// Returns true on success.
bool InitClientInfoHooks(HMODULE module);

// Remove any installed hooks.
void UninitClientInfoHooks();
