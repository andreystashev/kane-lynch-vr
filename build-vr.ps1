param(
    [string]$Toolchain = '',
    [string]$OculusSdk = '',
    [string]$DistName = '',
    [string]$CacheName = '.cache-build',
    [string]$GameExe = '',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
if ($GameExe -and -not (Test-Path -LiteralPath $GameExe -PathType Leaf)) {
    throw 'Pass an existing game executable for the optional native checks.'
}
$root = $PSScriptRoot
. (Join-Path $root 'scripts/build-env.ps1')
$versionSource = Get-Content -LiteralPath (Join-Path $root 'src/common/release_version.hpp') -Raw
$versionMatch = [regex]::Match($versionSource, '(?m)^#define KL1_RELEASE_VERSION "(\d+(?:\.\d+){2,3})"\r?$')
if(!$versionMatch.Success){throw 'Cannot determine the runtime version.'}
$version = $versionMatch.Groups[1].Value
if(!$DistName){$DistName = "dist-kane-lynch-vr-$version"}
if ($DistName -notmatch '^dist-[A-Za-z0-9._-]+$') {
    throw 'DistName must be a local dist-* folder name.'
}
if ($CacheName -notmatch '^\.cache-[A-Za-z0-9._-]+$') {
    throw 'CacheName must be a local .cache-* folder name.'
}
$dist = Join-Path $root $DistName
$cache = Join-Path $root $CacheName
$bin = Resolve-Toolchain $Toolchain
$ovrHeaders = Resolve-OculusHeaders $OculusSdk
$includeFlags = Get-SourceIncludeFlags -Root $root -OculusHeaders $ovrHeaders
$generatedHeaders = Join-Path $cache "generated"
New-Item -ItemType Directory -Force $dist, "$cache\x86", "$cache\x64" | Out-Null
$clang32 = Join-Path $bin 'i686-w64-mingw32-clang++.exe'
$clang64 = Join-Path $bin 'x86_64-w64-mingw32-clang++.exe'
$cc32 = Join-Path $bin 'i686-w64-mingw32-clang.exe'
$cc64 = Join-Path $bin 'x86_64-w64-mingw32-clang.exe'
$mh = Join-Path $root 'vendor\minhook'
$mhInclude = Join-Path $mh 'include'

function CompileEach($cc, $sources, $objDir, $flags) {
    $objects = @()
    foreach($source in $sources){
        $obj = Join-Path $objDir (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
        & $cc @includeFlags @flags -c $source -o $obj 2>&1 | ForEach-Object { Write-Host "  $_" }
        if($LASTEXITCODE -ne 0){ throw "Compilation failed: $source" }
        $objects += $obj
    }
    return $objects
}

$mh32 = CompileEach $cc32 @("$mh\src\buffer.c","$mh\src\hook.c","$mh\src\trampoline.c","$mh\src\hde\hde32.c") "$cache\x86" @('-O2')

Write-Host '== D3D9On12 renderer selector (x86) =='
$dxvk = Join-Path $dist 'renderer-dxvk-x86'
$on12 = Join-Path $dist 'renderer-d3d9on12-x86'
New-Item -ItemType Directory -Force $dxvk, $on12 | Out-Null
foreach ($name in @('d3d9.dll', 'LICENSE', 'provenance.json')) {
    Copy-Item -LiteralPath (Join-Path "$root\vendor\dxvk-x86" $name) -Destination $dxvk -Force
}
foreach ($name in @('NOTICE.txt', 'MINHOOK-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path "$root\vendor\d3d9on12-x86" $name) -Destination $on12 -Force
}
$on12Sources = @("$root\src\render\on12\on12_selector.cpp", "$root\src\render\on12\on12_selector.def")
& $clang32 @includeFlags -I $mhInclude -std=c++17 -O2 -Wall -Wextra -Werror -static '-DKL2_ON12_MEMORY_POLICY=1' @on12Sources @mh32 -shared '-Wl,--kill-at,--no-insert-timestamp' -o "$on12\d3d9.dll"
if ($LASTEXITCODE) { throw 'D3D9On12 selector build failed.' }
$selectorProvenance = Get-Content -LiteralPath "$root\vendor\d3d9on12-x86\provenance.json" -Raw | ConvertFrom-Json
foreach ($source in $selectorProvenance.source) {
    $selectorProvenance.sourceSha256.$source = (Get-FileHash -LiteralPath (Join-Path $root $source)).Hash.ToLowerInvariant()
}
$selectorProvenance.dllSha256 = (Get-FileHash -LiteralPath "$on12\d3d9.dll").Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$on12\provenance.json", (($selectorProvenance | ConvertTo-Json -Depth 4) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))

# Compile the generated selector's exact hash into both launcher entry points.
New-Item -ItemType Directory -Force $generatedHeaders | Out-Null
$builtOn12Header = '#define KL1_BUILT_ON12_SHA256 "' + $selectorProvenance.dllSha256 + '"'
[IO.File]::WriteAllText((Join-Path $generatedHeaders 'kl1_built_on12.hpp'), $builtOn12Header + [Environment]::NewLine, [Text.Encoding]::ASCII)
$includeFlags += @('-I', $generatedHeaders)

Write-Host "== KaneLynchVR.dll (x86 game module) =="
$gameSources = @(
    "src/game/kane_lynch_vr.cpp"
    "src/render/d3d9_capture.cpp"
    "src/common/vr_bridge.cpp"
    "src/render/hands_renderer.cpp"
    "src/game/native_camera_kl.cpp"
    "src/game/native_player_anchor.cpp"
    "src/game/native_body_kl.cpp"
    "src/game/native_hands_kl.cpp"
    "src/game/native_weapon_kl.cpp"
    "src/game/native_aim_kl.cpp"
    "src/render/native_postprocess_kl.cpp"
    "src/game/native_hud_kl.cpp"
    "src/game/native_gameplay_hud_kl.cpp"
    "src/game/native_pip_render_kl.cpp"
    "src/game/native_team_hud_kl.cpp"
    "src/render/native_frame_wait_kl.cpp"
    "src/render/native_buffer_flush_kl.cpp"
    "src/render/native_effects_kl.cpp"
    "src/game/native_action_kl.cpp"
    "src/game/native_cover_kl.cpp"
    "src/game/native_team_commands_kl.cpp"
    "src/game/native_rope_kl.cpp"
    "src/game/native_grenade_visual_kl.cpp"
    "src/game/native_mounted_aim_kl.cpp"
    "src/game/native_passenger_actor_kl.cpp"
    "src/game/native_tutorial_kl.cpp"
    "src/game/native_objective_visibility_kl.cpp"
    "src/game/native_invulnerability_kl.cpp"
    "src/game/native_ledge_hint_kl.cpp"
    "src/game/native_passenger_collision_kl.cpp"
    "src/render/native_skin_lighting_kl.cpp"
    "src/render/native_mirror_bump_kl.cpp"
    "src/render/native_reflection_probe_kl.cpp"
    "src/render/native_postfilter_probe_kl.cpp"
    "src/game/native_visual_trace_kl.cpp"
    "src/game/native_tracer_visual_kl.cpp"
    "src/game/native_carried_person_visibility_kl.cpp"
    "src/game/native_vr_menu_kl.cpp"
) | ForEach-Object { Join-Path $root $_ }
$mod32 = CompileEach $clang32 $gameSources "$cache\x86" @('-std=c++17', '-O2', '-I', $mhInclude)
& $clang32 ($mod32 + $mh32) -shared -static -o "$dist\KaneLynchVR.dll" -ld3d9 -lbcrypt -lgdi32 -lgdiplus 2>&1 | ForEach-Object { Write-Host "  $_" }
if($LASTEXITCODE -ne 0){ throw "Link failed: KaneLynchVR.dll" }

Write-Host "== LaunchVR.exe (x86 launcher) =="
$launch32 = CompileEach $clang32 @("$root\src\launcher\launcher.cpp") "$cache\x86" @('-std=c++17','-O2','-municode')
& $clang32 $launch32 -static -municode -o "$dist\LaunchVR.exe" -lbcrypt -lole32 -luuid 2>&1 | ForEach-Object { Write-Host "  $_" }
if($LASTEXITCODE -ne 0){ throw "Link failed: LaunchVR.exe" }

Write-Host "== KaneLynchVRMonitor.exe (independent session observer) =="
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -municode "$root\src\launcher\session_monitor.cpp" -o "$dist\KaneLynchVRMonitor.exe" -lbcrypt -luser32
if($LASTEXITCODE -ne 0){throw 'Session monitor build failed'}

Write-Host "== KaneLynchVRAudio.dll (x86 early audio routing) =="
$audio32 = CompileEach $clang32 @("$root\src\audio\audio_bootstrap.cpp","$root\src\audio\audio_route.cpp") "$cache\x86" @('-std=c++17','-O2')
& $clang32 $audio32 -shared -static -o "$dist\KaneLynchVRAudio.dll" -lbcrypt -lole32 -luuid -ldsound 2>&1 | ForEach-Object { Write-Host "  $_" }
if($LASTEXITCODE -ne 0){ throw "Link failed: KaneLynchVRAudio.dll" }

Write-Host "== KaneLynchVRSettings.exe (graphical launcher) =="
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -mwindows "$root\src\launcher\settings_ui.cpp" -o "$dist\KaneLynchVRSettings.exe" -lbcrypt -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lgdi32 -luser32 -ladvapi32 -ldwmapi -luxtheme
if($LASTEXITCODE -ne 0){throw 'Settings UI build failed'}
Write-Host "== KaneLynchVRHost.exe (x64 Meta compositor) =="
$host64 = CompileEach $clang64 @("$root\src\vr\kane_lynch_vr_host.cpp") "$cache\x64" @('-std=c++17','-O2','-municode')
& $clang64 $host64 -static -municode -o "$dist\KaneLynchVRHost.exe" -ld3d11 -ldxgi -luser32 -ladvapi32 2>&1 | ForEach-Object { Write-Host "  $_" }
if($LASTEXITCODE -ne 0){ throw "Link failed: KaneLynchVRHost.exe" }
Write-Host "== KaneLynchOpenXRHost.exe (x64 OpenXR compositor) =="
& $clang64 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -DKL1_OPENXR_HOST=1 "$root\src\vr\kane_lynch_vr_host.cpp" -o "$dist\KaneLynchOpenXRHost.exe" -ld3d11 -ldxgi -luser32 -ladvapi32
if($LASTEXITCODE -ne 0){throw 'OpenXR host build failed'}
foreach($dependency in @('openxr_loader.dll','OPENXR-LICENSE','JSONCPP-LICENSE','OPENVR-LICENSE')){Copy-Item -LiteralPath (Join-Path "$root\vendor" $dependency) -Destination $dist -Force}


$settingsPath = Join-Path $dist 'vr.ini'
if (-not (Test-Path -LiteralPath $settingsPath)) {
    Copy-Item -LiteralPath "$root\vr-release.ini" -Destination $settingsPath
}
foreach ($name in @('Launch-KaneLynch-VR.cmd', 'KaneLynchVRSettings.cmd', 'Open-Logs.cmd',
                    'Collect-KL1-Diagnostics.cmd', 'Collect-KL1-Diagnostics.ps1',
                    'Record-KaneLynch-VR.cmd', 'Start-KL1-Recorder.ps1')) {
    Copy-Item -LiteralPath (Join-Path "$root\scripts" $name) -Destination $dist -Force
}
Copy-Item -LiteralPath "$root\licenses\THIRD-PARTY-AUDIO.txt" -Destination $dist -Force
Copy-Item -LiteralPath "$root\LICENSE" -Destination $dist -Force
$noticeDirectory = Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force $noticeDirectory | Out-Null
foreach ($name in @('MinHook-LICENSE.txt', 'Oculus-SDK-LICENSE.txt', 'LLVM-LICENSE.txt',
                    'libcxx-LICENSE.txt', 'libcxxabi-LICENSE.txt', 'libunwind-LICENSE.txt',
                    'compiler-rt-LICENSE.txt', 'MinGW-w64-runtime-LICENSE.txt', 'winpthreads-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path "$root\licenses" $name) -Destination $noticeDirectory -Force
}
$packageReadme = (Get-Content -LiteralPath "$root\docs\PACKAGE-README.md" -Raw -Encoding UTF8).Replace('{version}', $version)
[IO.File]::WriteAllText("$dist\README.md", $packageReadme, [Text.UTF8Encoding]::new($false))
foreach ($name in @('THIRD-PARTY-NOTICES.md', 'INSTALLATION.md', 'CONTROLS.md', 'TROUBLESHOOTING.md', 'KNOWN-ISSUES.md')) {
    Copy-Item -LiteralPath (Join-Path "$root\docs" $name) -Destination $dist -Force
}
New-Item -ItemType Directory -Force "$dist\tools-read" | Out-Null
foreach ($name in @('record-vr.py', 'decode_visual_trace.py')) {
    Copy-Item -LiteralPath (Join-Path "$root\tools" $name) -Destination "$dist\tools-read" -Force
}
Copy-Item -LiteralPath "$root\tools\analyze_kl1_performance.py" -Destination $dist -Force

if ($RunTests -or $GameExe) {
    . (Join-Path $root 'tests/run.ps1')
}

# Record the bytes in the complete runtime package.
$runtimeLock = Get-Content -LiteralPath (Join-Path $root 'installer/runtime-lock.json') -Raw | ConvertFrom-Json
$runtimeHashes = [ordered]@{}
foreach($file in $runtimeLock.files.PSObject.Properties.Name){
    $runtimeHashes[$file] = (Get-FileHash -LiteralPath (Join-Path $dist $file) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$buildManifest = [ordered]@{version=$version; files=$runtimeHashes}
$manifestPath = Join-Path $dist ('BUILD-'+$version.Replace('.','')+'.json')
[IO.File]::WriteAllText($manifestPath, ($buildManifest|ConvertTo-Json -Depth 3)+"`n", [Text.UTF8Encoding]::new($false))

Write-Host "== Package: $dist =="
Get-ChildItem -LiteralPath $dist | Format-Table Name, Length -AutoSize
