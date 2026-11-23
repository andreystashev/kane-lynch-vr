# OpenXR SDK dependency

Headers and the x64 Windows loader are built from the official Khronos
[OpenXR-SDK release 1.1.65](https://github.com/KhronosGroup/OpenXR-SDK/releases/tag/release-1.1.65),
commit `24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8`.

The loader is `../openxr_loader.dll`. It discovers the user's active OpenXR
runtime; it does not include a headset runtime or change the registry. The host
loads this file by absolute path beside its EXE. It does not use the old OpenVR
DLL as an OpenXR runtime.

License files that accompany redistribution:

- `../OPENXR-LICENSE`: Khronos Apache-2.0.
- `../JSONCPP-LICENSE`: bundled JsonCpp Public Domain/MIT license.

The loader was compiled with LLVM-MinGW 20261006, x86_64, Release, static C++
runtime. Its only imports are Windows/UCRT and ADVAPI32. No game code, Meta
runtime or SteamVR binary is bundled in this dependency.

To rebuild, clone that exact tag, install CMake/Ninja, then run:

```powershell
.\vendor\openxr\rebuild-loader.ps1 `
  -SdkDirectory C:\tools\OpenXR-SDK `
  -Toolchain C:\tools\llvm-mingw-20261006-ucrt-x86_64 `
  -CMake C:\tools\cmake\bin\cmake.exe `
  -Ninja C:\tools\ninja.exe
```

The script verifies the SDK commit and clean source tree, copies source to a
fresh build folder and applies a small CMake compatibility fix there: PE uses
the upstream `.def` exports instead of the ELF `--version-script` flag. No
loader C/C++ source is changed. The original SDK checkout remains untouched.
Headers/licenses are refreshed from the same pinned source with the DLL.

This is a locally built Khronos loader, not a signed binary from Microsoft or
Valve. Its hash is recorded by normal mod package/installer manifests.
