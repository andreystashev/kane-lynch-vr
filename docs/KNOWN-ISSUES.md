# Known issues — 0.3.66

Local tests covered the original Steam and GOG releases and a compatible legacy PC build. Headset testing has focused on Quest hardware and Touch-style controllers. Results vary with mission, renderer and headset connection.

## Cutscenes and scripted interactions

**VR cutscenes** are optional and off by default. The original scenes were authored for a fixed camera; some animations do not exist outside that shot. Looking around in VR can reveal incomplete character movement, unusual poses or other visual problems. Turn the option off to watch the original framing on a virtual cinema screen.

Some scripted actions, camera transitions and body/weapon poses can still be awkward in first person. Menu and cutscene behavior may vary between sequences.

## Mounted machine guns

The vehicle-mounted machine-gun sequence near the end of the campaign is especially difficult to control in VR. Controller aiming and right-stick adjustment remain rough, particularly while the vehicle turns.

Enable **Options → VR Settings → Mounted Mouse Aim** and restart to use the game's mouse aiming while retaining the VR view.

## Performance and loading

- DirectInput device discovery now refreshes after device changes, reducing repeat scans during play. Other pauses in the game's render pipeline remain possible.
- Virtual Desktop / VDXR can briefly stop delivering frames to the headset while the game window keeps updating. Include the headset connection and whether the game window kept moving when reporting a freeze.
- The game renders the two eye views on alternating game frames. The rate of fresh stereo pairs and the rate reported by a desktop overlay measure different stages.
- D3D9On12 is experimental. A loading-related GPU timeout/system crash has been reported and remains under investigation. DXVK is the recommended starting choice; Original is another alternative.
- Mission loading can still fail when the original 32-bit game runs out of address space.

## Rendering and compatibility

- Some reflections, effects and body/weapon poses still show artifacts. An intermittent grey weapon silhouette near the Japanese-garden entrance and a reflective floor section on the Tokyo streets need more scene-specific testing.
- Physical reload is optional. Grip and insertion placement depend on the weapon and controller. Weapons without a supported mechanism use the original animation.
- Meta, Virtual Desktop / VDXR and SteamVR connection paths have been exercised, but testing across different devices is limited. A first-start stale-image issue was observed with SteamVR; later runs delivered fresh frames.
- Security software may block the launcher. If it does, stop and report the detection name, affected file and mod version. Detections have not been established as false positives. See [troubleshooting](TROUBLESHOOTING.md#security-software-blocks-a-file).
- Co-op and multiplayer have not been tested and are not guaranteed to work.

See [troubleshooting](TROUBLESHOOTING.md) for startup checks and optional diagnostic logging. Reproducible reports with a mission and exact steps are especially helpful.
