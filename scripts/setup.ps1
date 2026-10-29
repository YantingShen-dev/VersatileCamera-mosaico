param(
    [string]$EnvironmentRoot = '',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $EnvironmentRoot) { $EnvironmentRoot = Join-Path (Split-Path $repoRoot -Parent) 'mosaico-env' }
$EnvironmentRoot = [IO.Path]::GetFullPath($EnvironmentRoot)
if ($repoRoot -match '[^\x00-\x7F]' -or $EnvironmentRoot -match '[^\x00-\x7F]') {
    throw 'Use short ASCII paths, for example C:\mosaico\camera and C:\mosaico-env.'
}
& $Python -c 'import sys; assert sys.version_info >= (3,10), "Python 3.10+ required; Python 3.12 recommended"'
if ($LASTEXITCODE) { throw 'Python check failed.' }
$versions = Get-Content (Join-Path $repoRoot 'versions.json') -Raw | ConvertFrom-Json
git -C $repoRoot submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp
if ($LASTEXITCODE) { throw 'Mosaico dependency checkout failed.' }
New-Item -ItemType Directory -Path $EnvironmentRoot -Force | Out-Null
$sdkRoot = Join-Path $EnvironmentRoot 'esp-idf'
if (-not (Test-Path -LiteralPath $sdkRoot)) {
    git clone --no-checkout $versions.esp_idf.url $sdkRoot
    if ($LASTEXITCODE) { throw 'ESP-IDF clone failed.' }
    git -C $sdkRoot checkout --detach $versions.esp_idf.commit
    if ($LASTEXITCODE) { throw 'Pinned ESP-IDF checkout failed.' }
}
$actualRevision = git -C $sdkRoot rev-parse HEAD
if ($LASTEXITCODE -or $actualRevision -ne $versions.esp_idf.commit) {
    throw 'Existing SDK is not the pinned revision. Use a new EnvironmentRoot; do not overwrite another SDK.'
}
git -C $sdkRoot submodule update --init --recursive
if ($LASTEXITCODE) { throw 'ESP-IDF submodules failed.' }
$env:IDF_PATH = $sdkRoot
$env:IDF_TOOLS_PATH = Join-Path $EnvironmentRoot 'tools'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
& $Python (Join-Path $sdkRoot 'tools/idf_tools.py') install --targets=esp32s31
if ($LASTEXITCODE) { throw 'Toolchain installation failed.' }
& $Python (Join-Path $sdkRoot 'tools/idf_tools.py') install-python-env --features=core
if ($LASTEXITCODE) { throw 'IDF Python environment installation failed.' }
@{ environment_root = $EnvironmentRoot } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $repoRoot 'workspace.local.json') -Encoding utf8
& (Join-Path $repoRoot 'build.ps1') -Action doctor
if ($LASTEXITCODE) { throw 'Environment doctor failed.' }
Write-Host 'Setup complete. Run .\build.ps1 to build the face Idea.'
