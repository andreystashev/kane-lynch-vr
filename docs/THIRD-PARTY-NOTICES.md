# Third-party notices

This is an unofficial fan project. It requires the original PC game, which is not included. Kane & Lynch names, characters and game content belong to their respective owners. The package contains no game executable, game data, saved games or replacement xlive.dll.

The MIT license covers original project code, including the
vendored project-owned launcher headers and D3D9On12 selector. The package
also uses or redistributes MinHook, the Khronos OpenXR loader and headers,
OpenVR interfaces, JsonCpp, DXVK, Vulkan headers and LLVM-MinGW runtime code.
Their notices are in the licenses directory and beside the relevant files under vendor in the source tree. Installed packages include the applicable notices.

The optional native Meta connection uses the Oculus PC SDK interface.
Obtain the SDK from Meta when building the mod. Its headers are kept outside
the source tree, and the plug-in loads the installed runtime at run time.
See the Oculus SDK terms in the licenses directory.

The OpenXR adapter and D3D9 capture build on earlier Hitman: Contracts VR
work by the project author. Windows audio routing uses interface references
documented in THIRD-PARTY-AUDIO.txt.

The D3D9On12 option selects the Windows system implementation through the project's own selector. Windows, Steam, Meta, Virtual Desktop and SteamVR are separate products; their applications and service terms are not supplied or replaced by this mod.

Optional recording uses separately installed Python, NumPy, SoundCard and FFmpeg. Those tools are not bundled with this release. Recording captures the selected playback output, not a microphone.
