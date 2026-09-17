[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$PackageName
)
$ErrorActionPreference = 'Stop'
$iscc = & "$PSScriptRoot/find-inno.ps1"
# A fresh staging directory avoids shipping files left by an older build.
$stage = Join-Path $BuildDir ('inno-stage-' + [Guid]::NewGuid().ToString('N'))
try {
    & cmake --install $BuildDir --config $Configuration --component Runtime --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw 'Could not stage the installer payload.' }
    & $iscc "/DPayloadDir=$stage" (Join-Path $BuildDir 'BongoCat.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup packaging failed.' }
    $output = Join-Path $BuildDir "dist/$PackageName-setup.exe"
    $hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $PackageName-setup.exe" | Set-Content -LiteralPath "$output.sha256" -Encoding ascii
} finally {
    # Only remove the unique stage directory directly below the build directory.
    $stageFull = [IO.Path]::GetFullPath($stage)
    $buildFull = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
    if ((Split-Path $stageFull -Parent) -eq $buildFull -and
        (Split-Path $stageFull -Leaf) -match '^inno-stage-[0-9a-f]{32}$' -and
        (Test-Path -LiteralPath $stageFull)) {
        Remove-Item -LiteralPath $stageFull -Recurse -Force
    }
}
