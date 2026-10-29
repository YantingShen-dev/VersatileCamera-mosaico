param([ValidateSet('doctor','build')][string]$Action = 'build')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
$runner = Join-Path $PSScriptRoot 'submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py'
python $runner --project projects/face_mosaico $Action
if ($LASTEXITCODE) { throw 'Build/doctor failed. Inspect the reported log.' }
