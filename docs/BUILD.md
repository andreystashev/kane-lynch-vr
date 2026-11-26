# Build from source

Build on 64-bit Windows with PowerShell and the **LLVM-MinGW UCRT** toolchain.
The tested toolchain release is [20261006](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006).
Obtain the [Oculus PC SDK 1.10.0](https://developers.meta.com/vr/downloads/package/oculus-sdk-for-windows/1.10.0/)
from Meta. The build uses its headers for the native Meta connection and
the shared host interface. Pass the SDK root or its `LibOVR/Include`
directory. The SDK is subject to its own terms.

The public tree has no Git submodules. DXVK and the OpenXR loader are
included with pinned provenance. The D3D9On12 selector builds from source.

## Runtime

From the checkout root in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-vr.ps1 -Toolchain 'C:\Tools\llvm-mingw' -OculusSdk 'C:\SDK\Oculus'
```

You can also set `VR_TOOLCHAIN` and `OCULUS_SDK`. If the cross-compilers are
on PATH, the script detects the toolchain. The default output is
`dist-kane-lynch-vr-<version>`. It contains the complete playable mod,
English help, dependency notices and a manifest of the built runtime hashes.
Open **KaneLynchVRSettings.cmd** there for the first launch. An installation can
also be checked without starting the game or changing its files:

```powershell
.\dist-kane-lynch-vr-0.3.67\LaunchVR.exe --check-installation 'C:\Games\Kane and Lynch Dead Men\kaneandlynch.exe'
```

The check verifies the executable and the native startup route. Steam and
GOG use their own original launchers. A supported older executable profile
can start directly with its original files.

## Tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-vr.ps1 -Toolchain 'C:\Tools\llvm-mingw' -OculusSdk 'C:\SDK\Oculus' -RunTests
```

The test suite uses Python 3.10+ and a Windows graphics device for the GPU
transport fixture. The core checks do not start the game or the headset.
Pass `-GameExe 'C:\Games\Kane and Lynch Dead Men\kaneandlynch.exe'` to include
read-only native integration checks. The test runner uses a separate copy
inside the build cache. Native fixture checks requiring the executable are
skipped when it is omitted. A physical headset is needed for gameplay testing.

## Installer and Portable ZIP

Install [Inno Setup](https://jrsoftware.org/isinfo.php), then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-installer.ps1 -PackageDirectory 'dist-kane-lynch-vr-0.3.67' -LocalBuild -Toolchain 'C:\Tools\llvm-mingw'
```

The compiler is found on PATH or in standard install locations. Use
`-Compiler 'C:\Path\To\ISCC.exe'` for other locations. `-LocalBuild` uses the
hashes in your build manifest and writes under `releases-installer-local`.
Without `-LocalBuild`, packaging requires the official pinned runtime hashes.
Use `-LocalBuild` for fresh builds from this checkout.
Use `-LocalBuild -Fixture` for install/update/uninstall tests with the fixture
application ID. See [installer tests](../installer/README.md).

For play you need your own supported copy of the game and a configured VR
runtime. The game executable is never packaged.
