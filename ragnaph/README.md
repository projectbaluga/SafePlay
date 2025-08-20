# ragnaph.dll

This project builds a Win32 (x86) DLL that serves `clientinfo.xml` directly from an embedded resource. When the Ragnarok Online client requests `clientinfo.xml`, the DLL intercepts file API calls and supplies the embedded bytes without touching disk.

## Build Steps
1. Open Visual Studio and create a **Win32 DLL** project targeting **x86**.
2. Add the files from this folder (`dllmain.cpp`, `resource.h`, `ragnaph.rc`) to the project.
3. Include `clientinfo.xml` in the project root and ensure it is referenced by `ragnaph.rc` as an `RCDATA` resource.
4. Add the [MinHook](https://github.com/TsudaKageyu/minhook) sources to the project and include its header path.
5. Compile with C++17 and warning level `/W4`.

## Deployment
Inject `ragnaph.dll` into the Ragnarok Online client (for example, by proxy DLL or an external launcher). The DLL loads the embedded `clientinfo.xml` and hooks the relevant Win32 APIs so the game reads from memory.

## Test Checklist
1. Remove any on-disk `clientinfo.xml` from the game directory.
2. Launch the game with `ragnaph.dll` injected.
3. Verify the server list appears, proving the XML was served from the DLL.
