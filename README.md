# PoringProtect

This project builds a Windows DLL that performs simple anti-cheat checks and now
also embeds `clientinfo.xml` into the binary. When a game attempts to open
`clientinfo.xml`, the DLL serves the embedded copy instead of reading from disk.

## Building

1. Build as a Win32/x86 DLL with Visual Studio.
2. Ensure `clientinfo.xml` is included in the project so the `.rc` files compile
   it as `IDR_CLIENTINFO`.

The DLL uses a small built-in import address table (IAT) hook, so no external
hooking libraries are required.

Since `RagnaPH.exe` already loads `poringprotect.dll`, no extra injection is
required. To test, remove `clientinfo.xml` from disk and confirm that login and
server selection still work.
