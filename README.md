# Kane & Lynch: Dead Men VR

**[Download 0.3.67 — Windows installer](https://github.com/andreystashev/kane-lynch-vr/releases/download/v0.3.67/KaneLynchVR-0.3.67-Setup.exe)** · [Portable ZIP](https://github.com/andreystashev/kane-lynch-vr/releases/download/v0.3.67/KaneLynchVR-0.3.67-Portable.zip) · [All releases](https://github.com/andreystashev/kane-lynch-vr/releases)

An unofficial VR mod for the original **Kane & Lynch: Dead Men (2007)** on PC, with stereo rendering, positional head tracking, motion-controller aiming and tracked native arms.

Play the original campaign with VR controls for shooting, interaction, squad orders and inventory. Missions and progression use the original game's systems.

You need your own copy of the original PC game. Local tests covered the original Steam edition (App 8080), GOG and a compatible legacy PC build. Steam startup supports compatible Microsoft Games for Windows Live builds, including 3.5.0092.0 and 3.5.0095.0. This is an independent fan project, unaffiliated with the original game's owners.

## What the mod adds

- First-person stereo views and head tracking, including physical head movement for looking around cover.
- Controller-driven arms and weapon aiming, with second-hand support for long guns.
- The sniper rifle's original scope, activated by bringing the right controller near your face.
- Controller access to actions, reloads, grenades, weapon selection and squad commands.
- The weapon wheel, map and team panel open together while you hold Y. Native action prompts appear near your right hand.
- Optional laser and native aiming graphics, including the original mounted machine-gun crosshair.
- Optional VR cutscenes, with the original cinematic framing on a virtual screen as the default.
- Headset audio routing and a separate optional gameplay recorder.

## Install and play

1. Install the original game and connect your PC VR headset.
2. Run **KaneLynchVR-0.3.67-Setup.exe**, confirm the detected game executable and choose a separate folder for the mod.
3. Open **Kane & Lynch Dead Men VR Settings**, select your headset connection and choose **Save & play**.

The default installation does not require administrator rights or Python. The Play shortcut uses your saved settings. A Portable ZIP is available if you prefer to extract the complete mod into a writable folder.

[Installation](docs/INSTALLATION.md) · [Controller controls](docs/CONTROLS.md) · [Troubleshooting and recording](docs/TROUBLESHOOTING.md) · [Known issues](docs/KNOWN-ISSUES.md)

## Settings

Open **Options → VR Settings** from the main menu or pause menu to change
physical reload, cutscenes, aiming graphics and game hints. **Game Hints** are on by default.

Use the desktop Settings app for game selection, VR connection, renderers,
comfort controls and diagnostic logging. Fresh installs use **DXVK**.
**Original** and experimental **D3D9On12** are also available. See the
[renderer notes](docs/INSTALLATION.md#renderer-and-graphics).

## Source and builds

See [Build from source](docs/BUILD.md) for toolchain setup, tests and
installer packaging. [Architecture](docs/ARCHITECTURE.md) maps the source tree.

The installer and Portable ZIP are built from the [v0.3.67 source](https://github.com/andreystashev/kane-lynch-vr/tree/v0.3.67).
The project code is under the [MIT license](LICENSE).
Third-party components are covered by their own [terms](docs/THIRD-PARTY-NOTICES.md).

## Known limitations

- **VR cutscenes can look strange.** Original shots were made for a fixed camera, and animation may be missing outside that shot. Looking around can reveal incomplete or unnatural character movement. Leave **VR cutscenes** off to watch the original framing on a cinema screen.
- **The vehicle-mounted machine-gun sequence near the end of the game is difficult to control in VR.** Controller aiming and right-stick adjustment remain rough. Enable **Mounted Mouse Aim** in VR Settings to aim with a physical mouse while keeping the VR view.
- **Performance and stability are still uneven.** Expect scene-dependent frame-rate drops, stutters and possible loading crashes. D3D9On12 has an unresolved report of a loading-related GPU timeout/system crash; DXVK or Original are alternatives.
- Some body/weapon poses, reflections and effects can still show visual artifacts. Not every mission, headset or controller has been validated.
- **Co-op and multiplayer have not been tested and are not guaranteed to work.**

Meta Quest Link / Air Link, Virtual Desktop / VDXR and SteamVR / OpenXR connection profiles are included. Testing has focused on Quest hardware and Touch-style controllers; support for other setups needs more feedback.

## Feedback

Please report reproducible problems in [Issues](https://github.com/andreystashev/kane-lynch-vr/issues), including the mod version, mission, renderer, connection, headset and steps to reproduce the problem. State whether it affects the game window, the headset or both.

Logs are optional: enable **Diagnostics → Enable diagnostic logs**, restart the game, reproduce the issue and run **Collect-KL1-Diagnostics.cmd**. Turn logging off again afterward. See [troubleshooting](docs/TROUBLESHOOTING.md) before sharing diagnostic files.

[Third-party notices](docs/THIRD-PARTY-NOTICES.md) and license texts accompany the download.
