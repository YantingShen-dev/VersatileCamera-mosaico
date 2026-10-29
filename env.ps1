# Dot-source: . .\env.ps1
param([string]$EnvironmentRoot = '')
$repoRoot = $PSScriptRoot
if (-not $EnvironmentRoot) {
    $settingsPath = Join-Path $repoRoot 'workspace.local.json'
    if (Test-Path -LiteralPath $settingsPath) {
        $EnvironmentRoot = (Get-Content $settingsPath -Raw | ConvertFrom-Json).environment_root
    } else {
        $EnvironmentRoot = Join-Path (Split-Path $repoRoot -Parent) 'mosaico-env'
    }
}
$env:IDF_PATH = Join-Path ([IO.Path]::GetFullPath($EnvironmentRoot)) 'esp-idf'
$env:IDF_TOOLS_PATH = Join-Path ([IO.Path]::GetFullPath($EnvironmentRoot)) 'tools'
$versions = Get-Content (Join-Path $repoRoot 'versions.json') -Raw | ConvertFrom-Json
$actualRevision = git -C $env:IDF_PATH rev-parse HEAD
if ($LASTEXITCODE -or $actualRevision -ne $versions.esp_idf.commit) {
    throw 'Pinned ESP-IDF not found. Run scripts/setup.ps1 first.'
}
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_CONFIGDEP_ENABLE = '0'
$env:CCACHE_DIR = Join-Path $EnvironmentRoot 'ccache'
# Do not inherit imported-library overrides from a different project.
$env:MOSAICO_MANAGED_COMPONENTS_PATH = Join-Path $repoRoot 'projects/face_mosaico/managed_components'
. (Join-Path $env:IDF_PATH 'export.ps1')
if ($LASTEXITCODE) { throw 'ESP-IDF activation failed.' }
Set-Location -LiteralPath $repoRoot
