# PoringProtect

`poringprotect.dll` is a Win32 (x86) anti-cheat module for Ragnarok Online. It embeds
`clientinfo.xml` and uses MinHook to serve the file directly from memory while
continuing to scan for common cheat tools.

## Building

1. Add MinHook sources to the project and build as a Visual Studio Win32/x86 DLL.
2. Ensure `poringprotect.rc` and `resource.h` are included so `clientinfo.xml` is embedded as `IDR_CLIENTINFO`.

## Testing

1. Rename or remove any on-disk `clientinfo.xml`.
2. Run the client with [DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview).
3. Look for `[PP]` messages showing CreateFile*/GetFileAttributes* interceptions and verify that launching known cheat tools triggers the anti-cheat alert.
4. If no messages appear, the client may read `clientinfo.xml` from a GRF archive, which is outside the scope of this DLL.
