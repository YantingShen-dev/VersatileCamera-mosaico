# Install the verified release without installing the ESP-IDF toolchain.
param([string]$DeviceId = '', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$bundlePath = Join-Path $repoRoot 'firmware/face_mosaico-1.0.0.irisfw'
$manifest = Get-Content (Join-Path $repoRoot 'firmware/release.json') -Raw | ConvertFrom-Json
$actualHash = (Get-FileHash -LiteralPath $bundlePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $manifest.sha256) { throw 'Release bundle checksum mismatch.' }
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$arguments = @((Join-Path $repoRoot 'mosaico.py'), 'iris', 'system-update', '--bundle', $bundlePath)
if ($DeviceId) { $arguments += @('--device-id', $DeviceId) }
Push-Location -LiteralPath (Join-Path $repoRoot 'projects/face_mosaico')
try {
    & $Python @arguments
    if ($LASTEXITCODE) { throw 'Install failed. Inspect the CLI operation before retrying.' }
} finally { Pop-Location }
