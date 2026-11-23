param(
    [Parameter(Mandatory=$true)][string]$SdkDirectory,
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [Parameter(Mandatory=$true)][string]$CMake,
    [Parameter(Mandatory=$true)][string]$Ninja
)
$ErrorActionPreference = 'Stop'
$expectedCommit = '24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8'
$sdkRoot = (Resolve-Path -LiteralPath $SdkDirectory).Path
$compilerRoot = (Resolve-Path -LiteralPath $Toolchain).Path
$cmakeExe = (Resolve-Path -LiteralPath $CMake).Path
$ninjaExe = (Resolve-Path -LiteralPath $Ninja).Path
$actualCommit = (& git -C $sdkRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) { throw "Expected official OpenXR-SDK commit $expectedCommit" }
$sourceChanges = & git -C $sdkRoot status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $sourceChanges) { throw 'Use a clean pinned SDK source checkout.' }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildRoot = Join-Path $repoRoot ('build\openxr-loader-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + $PID)
if (Test-Path -LiteralPath $buildRoot) { throw "Build folder already exists: $buildRoot" }
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$sourceCopy = Join-Path $buildRoot 'sdk'
Copy-Item -LiteralPath $sdkRoot -Destination $sourceCopy -Recurse
$loaderCMake = Join-Path $sourceCopy 'src\loader\CMakeLists.txt'
$cmakeText = [IO.File]::ReadAllText($loaderCMake)
$needle = '    else()' + "`n" + '        set_target_properties(' + "`n" + '            openxr_loader' + "`n" + '            PROPERTIES' + "`n" + '                LINK_FLAGS' + "`n" + '                "-Wl,--version-script='
$cmakeText = $cmakeText.Replace("`r`n", "`n")
if (-not $cmakeText.Contains($needle)) { throw 'Pinned SDK CMake compatibility location changed.' }
$cmakeText = $cmakeText.Replace($needle, $needle.Replace('    else()', '    elseif(NOT WIN32)'))
$cmakeText += @'

if(MINGW)
  target_sources(openxr_loader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/openxr-loader.def")
  target_link_options(openxr_loader PRIVATE -static)
  set_target_properties(openxr_loader PROPERTIES PREFIX "")
endif()
'@
[IO.File]::WriteAllText($loaderCMake, $cmakeText, [Text.UTF8Encoding]::new($false))
$binaryRoot = Join-Path $buildRoot 'binary'
function CMakePath([string]$Value) { return $Value.Replace('\', '/') }
$common = @('-S', $sourceCopy, '-B', $binaryRoot, '-G', 'Ninja',
    ('-DCMAKE_MAKE_PROGRAM=' + (CMakePath $ninjaExe)),
    ('-DCMAKE_C_COMPILER=' + (CMakePath (Join-Path $compilerRoot 'bin\x86_64-w64-mingw32-clang.exe'))),
    ('-DCMAKE_CXX_COMPILER=' + (CMakePath (Join-Path $compilerRoot 'bin\x86_64-w64-mingw32-clang++.exe'))),
    ('-DCMAKE_RC_COMPILER=' + (CMakePath (Join-Path $compilerRoot 'bin\x86_64-w64-mingw32-windres.exe'))),
    '-DCMAKE_BUILD_TYPE=Release', '-DDYNAMIC_LOADER=ON', '-DBUILD_API_LAYERS=OFF', '-DBUILD_TESTS=OFF',
    '-DBUILD_CONFORMANCE_TESTS=OFF', '-DBUILD_SDK_TESTS=OFF', '-DBUILD_CONFORMANCE_CLI=OFF')
& $cmakeExe @common
if ($LASTEXITCODE -ne 0) { throw 'OpenXR loader configure failed.' }
& $cmakeExe --build $binaryRoot --target openxr_loader -j 4
if ($LASTEXITCODE -ne 0) { throw 'OpenXR loader build failed.' }
$loader = Join-Path $binaryRoot 'src\loader\openxr_loader.dll'
Copy-Item -LiteralPath $loader -Destination (Join-Path $repoRoot 'vendor\openxr_loader.dll')
foreach ($header in @('openxr.h', 'openxr_platform.h', 'openxr_platform_defines.h')) {
    Copy-Item -LiteralPath (Join-Path $sourceCopy ('include\openxr\' + $header)) -Destination (Join-Path $PSScriptRoot $header)
}
Copy-Item -LiteralPath (Join-Path $sourceCopy 'LICENSE') -Destination (Join-Path $repoRoot 'vendor\OPENXR-LICENSE')
Copy-Item -LiteralPath (Join-Path $sourceCopy 'src\external\jsoncpp\LICENSE') -Destination (Join-Path $repoRoot 'vendor\JSONCPP-LICENSE')
Get-FileHash -LiteralPath (Join-Path $repoRoot 'vendor\openxr_loader.dll') -Algorithm SHA256
