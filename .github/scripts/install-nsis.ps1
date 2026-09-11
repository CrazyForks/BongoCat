[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageVersion,
    [Parameter(Mandatory = $true)][string]$CompilerVersion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Install the concrete package from the public feed, even on preloaded runners.
choco upgrade nsis.install --version $PackageVersion `
    --source https://community.chocolatey.org/api/v2/ `
    --force --allow-downgrade --fail-on-unfound --no-progress --yes
if ($LASTEXITCODE -ne 0) {
    throw "Pinned NSIS installation failed (exit $LASTEXITCODE)"
}

$candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe'),
    (Join-Path $env:ProgramFiles 'NSIS/makensis.exe')
)
$command = Get-Command makensis.exe -ErrorAction SilentlyContinue
if ($command) { $candidates += $command.Source }
$found = @()
foreach ($candidate in ($candidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $version = (& $candidate /VERSION | Out-String).Trim()
    $status = $LASTEXITCODE
    $found += "$candidate ($version, exit $status)"
    if ($status -eq 0 -and $version -eq $CompilerVersion) {
        $root = Split-Path $candidate
        $env:PATH = "$root;$env:PATH"
        $root | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8
        Write-Host "NSIS compiler: $candidate ($version)"
        return
    }
}
throw "NSIS $CompilerVersion was not installed. Candidates: $($found -join '; ')"
