param([ValidateSet('doctor','build')][string]$Action = 'build')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot 'env.ps1')
$stageRoot = Join-Path (Split-Path $env:IDF_PATH -Parent) 'fridge-build-workspace'
# GNU objdump cannot open this workspace's Unicode path on Windows. Build a
# disposable source copy under the ASCII SDK directory; keep editing the repo.
if ($stageRoot -match '[^\x00-\x7f]') { throw 'The firmware build cache must use an ASCII path.' }
# Robocopy preserves source timestamps. An edit made while the preceding build
# was running can otherwise look older than its object file after staging.
$changedInputs = [System.Collections.Generic.List[string]]::new()
$appSource = Join-Path $repoRoot 'projects/fridge_mosaico'
$appInputs = @(Get-ChildItem -LiteralPath $appSource -File | Where-Object Name -NotLike '.env*')
foreach ($folder in @('components', 'main')) {
    $appInputs += Get-ChildItem -LiteralPath (Join-Path $appSource $folder) -File -Recurse
}
foreach ($inputFile in $appInputs) {
    $relativePath = $inputFile.FullName.Substring($repoRoot.Length + 1)
    $stagedFile = Join-Path $stageRoot $relativePath
    if (-not (Test-Path -LiteralPath $stagedFile) -or
        (Get-FileHash -LiteralPath $inputFile.FullName).Hash -ne (Get-FileHash -LiteralPath $stagedFile).Hash) {
        $changedInputs.Add($stagedFile)
    }
}
foreach ($relative in @('projects/fridge_mosaico', 'submodule/esp-mosaico-bsp', 'submodule/esp-mosaico-utils')) {
    $source = Join-Path $repoRoot $relative
    $destination = Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    & robocopy $source $destination /E /XJ /NFL /NDL /NJH /NJS /NP /R:1 /W:1 /XD .git .gateway .codex-runs build build-sim __pycache__ .venv .venv-sim /XF .git '.env*' | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Failed to stage $relative" }
}
foreach ($stagedFile in $changedInputs) {
    (Get-Item -LiteralPath $stagedFile).LastWriteTimeUtc = [DateTime]::UtcNow
}
$stageProject = Join-Path $stageRoot 'projects/fridge_mosaico'
$env:MOSAICO_MANAGED_COMPONENTS_PATH = Join-Path $stageProject 'managed_components'
$env:GSPC_EXECUTABLE = Join-Path $repoRoot '.tools/gspc/gspc.exe'
$runner = Join-Path $repoRoot 'submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py'
python $runner --project $stageProject --log-dir (Join-Path $repoRoot 'projects/fridge_mosaico/.codex-runs/idf-low-noise-build') $Action
if ($LASTEXITCODE) { throw 'Fridge build/doctor failed. Inspect the reported log.' }
