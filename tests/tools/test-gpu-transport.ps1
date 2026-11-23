param(
    [string]$OutputDirectory = '.cache-build/gpu-transport',
    [ValidateSet('D3D9On12','DXVK')][string]$Renderer = 'D3D9On12',
    [Parameter(Mandatory=$true)][string]$ToolchainBin,
    [Parameter(Mandatory=$true)][string]$OculusHeaders
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $root 'scripts/build-env.ps1')
$includeFlags = Get-SourceIncludeFlags -Root $root -OculusHeaders $OculusHeaders
$bin = $ToolchainBin
$cache=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))}
New-Item -ItemType Directory -Force $cache | Out-Null
foreach($arch in @('i686','x86_64')) {
    $role=if($arch -eq 'i686'){'producer'}else{'consumer'}
    $libs=if($arch -eq 'i686'){@('-ld3d9','-luser32','-ladvapi32','-lbcrypt')}else{@('-ld3d11','-ldxgi','-luser32','-ladvapi32','-lbcrypt')}
    & "$bin\$arch-w64-mingw32-clang++.exe" -std=c++17 -O2 -static -municode @includeFlags "$root\tests\fixtures\kl1_gpu_transport_fixture.cpp" -o "$cache\kl1-gpu-$role.exe" @libs
    if($LASTEXITCODE -ne 0){throw "GPU fixture compile failed: $role"}
}
Push-Location $cache
try {
    $rendererArgs=if($Renderer -eq 'DXVK'){@('--dxvk',(Join-Path $root 'vendor\dxvk-x86\d3d9.dll'))}else{@()}
    & "$cache\kl1-gpu-producer.exe" "$cache\kl1-gpu-consumer.exe" @rendererArgs
    if($LASTEXITCODE -ne 0){throw 'Cross-architecture direct GPU transport test failed'}
    & "$cache\kl1-gpu-producer.exe" "$cache\kl1-gpu-consumer.exe" baseline @rendererArgs
    if($LASTEXITCODE -ne 0){throw 'Cross-architecture baseline GPU transport test failed'}
} finally {
    Pop-Location
}
