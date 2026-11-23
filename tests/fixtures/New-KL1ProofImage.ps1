# Native PE proofs receive an immutable private copy, never the installed EXE.
# This also contains a mistaken fixture output argument inside the test cache.
function New-KL1ProofImage {
    param([Parameter(Mandatory=$true)][string]$GameExe, [Parameter(Mandatory=$true)][string]$CacheRoot)
    $source = Get-Item -LiteralPath $GameExe -ErrorAction Stop
    if ($source.PSIsContainer -or ($source.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $source.Length -le 0 -or $source.Length -gt 64MB) {
        throw 'Native proof requires a regular game image of at most 64 MiB.'
    }
    $digest = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $folder = [IO.Path]::GetFullPath((Join-Path (Join-Path $CacheRoot 'proof-images') $digest))
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    if ((Get-Item -LiteralPath $folder -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Proof image cache must not be a reparse point.' }
    $target = Join-Path $folder 'KANEANDLYNCH.EXE'
    if (-not (Test-Path -LiteralPath $target)) { [IO.File]::Copy($source.FullName, $target, $false) }
    $copy = Get-Item -LiteralPath $target -Force
    if ($copy.PSIsContainer -or ($copy.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $copy.Length -ne $source.Length -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest -or
        (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest) {
        throw 'Private proof image verification failed; installed game was not modified.'
    }
    $copy.IsReadOnly = $true
    return $copy.FullName
}
