# Installer

The per-user installer stages the mod outside the game folder, verifies
the selected game, and creates Play and Settings shortcuts. Upgrades use
the payload manifest and preserve user settings and unrelated files.

See [Build from source](../docs/BUILD.md#installer-and-portable-zip) for
Setup and Portable commands. Build a separate fixture before testing the
full installer workflow:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-installer.ps1 -PackageDirectory 'dist-kane-lynch-vr-0.3.66' -LocalBuild -Fixture -Toolchain 'C:\Tools\llvm-mingw'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\installer\test-installer-workflow.ps1 -BuildReport '<installer-build.json>' -GameExecutable '<kaneandlynch.exe>'
```

The fixture uses its own application ID and a separate copy of the game
executable. Pass `-PreviousPortable` with a Portable ZIP from a verified release
asset folder to test an upgrade from those exact bytes. The test exercises
fresh installation, upgrading, relocation
and uninstallation. Runtime and package hashes are checked before setup,
and the published asset verifier rejects fixture builds by default.
