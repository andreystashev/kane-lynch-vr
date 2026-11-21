# Kane & Lynch: Dead Men VR — 0.3.64

An unofficial VR conversion for the original **Kane & Lynch: Dead Men (2007)** on PC. Requires your own copy of the game. Local tests covered the original Steam edition (App 8080), GOG and a compatible legacy PC build.

The physical reload cycle was verified on Steam with the release profile and stock Games for Windows Live components.

## What's new

- **Physical reload in Settings.** Open **Modifiers → Physical reload**, save and restart the game after changing it. Fresh installs enable it for supported weapons. Updates enable it for profiles that never had a reload setting, and keep a saved on or off choice.
- **Performance.** Original Direct3D 9 captures MSAA frames faster. In a controlled 4K, 8× MSAA test, the game produced about 60 source eye frames per second, up from about 50, with matching pixels. The default DXVK profile uses direct GPU frame transfer where supported. The mod also avoids repeated DirectInput scans when devices stay connected.
- **Reload handling.** Physical reloads cover detachable magazines, shotgun tubes, revolver cylinders and individual rounds on supported weapons. Magazine placement, insertion, bolt and pump handling, and empty-chamber feedback have been refined. The Type 88 uses its original reload. Switching weapons no longer starts an automatic reload.
- **Original HUD in VR.** The game's ammo display, weapon icons and action prompts are adapted to VR. Hold **Y** to open the wrist-mounted map, squad panel and weapon wheel. The right stick selects weapons and right grip cycles squadmates. Long subtitles wrap across lines. Hold **B** to climb.

## Download

- **KaneLynchVR-0.3.64-Setup.exe** — recommended Windows installer, with game detection and Play/Settings shortcuts.
- **KaneLynchVR-0.3.64-Portable.zip** — extract completely into a separate writable folder, then open **KaneLynchVRSettings.cmd**.

Connect your headset, select the matching connection in Settings and choose **Save & play**. Python is not required for the mod. The original game and saved games are not included.

## Included

- Stereo rendering, positional head tracking, tracked native arms and controller-based aiming.
- Second-hand support for long guns and the original near-face sniper optic.
- VR controls for actions, reloads, grenades, weapon inventory and squad orders.
- Configurable laser, native reticles, cutscene presentation, turning and mounted mouse aiming.
- Meta, Virtual Desktop / VDXR and SteamVR / OpenXR connection profiles.

Fresh installations use DXVK, the handheld laser and the original mounted machine-gun crosshair. VR cutscenes, diagnostic logging and automatic recording start disabled.

## Read before playing

Performance varies by mission and renderer. Frame delivery can still pause in Virtual Desktop / VDXR while the game window keeps updating. D3D9On12 is experimental. Short rendering stalls still occurred in its Steam test, and a loading-related GPU timeout remains under investigation. See [known issues](KNOWN-ISSUES.md) for details.

**VR cutscenes are optional.** Scenes were authored for fixed shots and may have no animation outside the original camera view. Looking around can reveal strange or incomplete character movement. Leave **VR cutscenes** off for the original cinematic framing on a virtual screen.

**The late-game vehicle-mounted machine-gun sequence is difficult to control in VR.** Enable **Modifiers → Aim mounted guns with the mouse** to use a physical mouse while retaining the VR view.

**Co-op and multiplayer are untested and are not guaranteed to work.** Not every headset, controller or game edition has been validated. If security software blocks the launcher, stop and report the detection; it has not been established as a false positive.

See the repository's **CONTROLS.md**, **INSTALLATION.md**, **KNOWN-ISSUES.md** and **TROUBLESHOOTING.md** for the complete controls, modifiers, limitations and optional recorder setup. Feedback with a mission and reproducible steps is welcome.

This is an independent fan project, unaffiliated with the original game's owners.
