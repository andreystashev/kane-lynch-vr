# Source layout

| Directory | Responsibility |
| --- | --- |
| [src/game](../src/game) | Game-module entry point, native camera and player pose, controller actions, weapons, HUD and in-game settings. |
| [src/render](../src/render) | D3D9 capture, stereo rendering, effects, DXVK interop and the D3D9On12 selector. |
| [src/vr](../src/vr) | x64 OpenXR and native Meta hosts, controller sampling, frame submission and recording mirror. |
| [src/audio](../src/audio) | Early x86 audio bootstrap and per-application headset routing. |
| [src/launcher](../src/launcher) | Game discovery and executable qualification, desktop settings, runtime selection and the session monitor. |
| [src/common](../src/common) | Shared state, frame and process communication, and diagnostics. |
| [installer](../installer) | Inno Setup recipe, payload validation and upgrade handling. |
| [scripts](../scripts) and [tools](../tools) | Launch, support, recording and build tools. |
| [tests](../tests) | Native, launcher, tooling and installer tests with isolated fixtures. |
| [vendor](../vendor) | Vendored headers and pinned redistributable components. |

The x86 launcher prepares the renderer and starts the supported game edition.
The x86 game DLL provides the VR camera, input and menu integration in the
game process. The game DLL and x64 VR host exchange frames, tracking and
controller state through shared memory and Windows graphics resources.

The desktop app sets the renderer, headset connection and comfort options.
In-game **Options → VR Settings** manages gameplay modifiers through the
same `vr.ini`. The release preset is `vr-release.ini`. Local profiles belong
in an installed or built package.
