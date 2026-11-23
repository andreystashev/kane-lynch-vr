function Resolve-Toolchain([string]$Directory) {
    if (-not $Directory) { $Directory = $env:VR_TOOLCHAIN }
    if (-not $Directory) {
        $compiler = Get-Command i686-w64-mingw32-clang++.exe -ErrorAction SilentlyContinue
        if ($compiler) { $Directory = Split-Path (Split-Path $compiler.Source) }
    }
    if (-not $Directory) { throw 'Pass -Toolchain with the root of an LLVM-MinGW installation.' }
    $bin = Join-Path (Resolve-Path -LiteralPath $Directory).Path 'bin'
    foreach ($compiler in @('i686-w64-mingw32-clang++.exe', 'x86_64-w64-mingw32-clang++.exe',
                           'i686-w64-mingw32-clang.exe', 'x86_64-w64-mingw32-clang.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $compiler) -PathType Leaf)) {
            throw "Compiler not found: $compiler in $bin"
        }
    }
    return $bin
}

function Resolve-OculusHeaders([string]$Directory) {
    if (-not $Directory) { $Directory = $env:OCULUS_SDK }
    if (-not $Directory) { throw 'Pass -OculusSdk with Oculus PC SDK 1.10.0 or its LibOVR/Include folder.' }
    $candidate = (Resolve-Path -LiteralPath $Directory).Path
    $include = Join-Path $candidate 'LibOVR/Include'
    if (Test-Path -LiteralPath $include -PathType Container) { $candidate = $include }
    foreach ($header in @('OVR_CAPI.h', 'OVR_CAPI_D3D.h', 'OVR_CAPI_Keys.h', 'OVR_ErrorCode.h', 'OVR_Version.h')) {
        if (-not (Test-Path -LiteralPath (Join-Path $candidate $header) -PathType Leaf)) {
            throw "Oculus PC SDK header not found: $header in $candidate"
        }
    }
    return $candidate
}

function Get-SourceIncludeFlags([string]$Root, [string]$OculusHeaders) {
    foreach ($relative in @('src/audio', 'src/common', 'src/game', 'src/launcher', 'src/render',
                            'src/vr', 'vendor', 'vendor/vr-mod-shared/include')) {
        '-I'
        Join-Path $Root $relative
    }
    if ($OculusHeaders) {
        '-I'
        $OculusHeaders
    }
}
