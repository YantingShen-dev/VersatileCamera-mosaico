param(
    [switch]$Headless,
    [double]$Duration = 0,
    [string]$Project = 'face_mosaico'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$python = Join-Path $repoRoot '.venv-sim/Scripts/python.exe'
$component = Join-Path $repoRoot '.tools/esp-gsp-1.5.1/esp-gsp'
foreach ($required in @($python, "$component/tools/sim_bridge/run.py",
        "$repoRoot/.tools/gspc/gspc.exe", "$repoRoot/.tools/sim/sim.exe")) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing simulator dependency: $required. See docs/pc-simulator.md."
    }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ tools are required.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install the Visual Studio Desktop development with C++ workload.' }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:Path = "$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja;$env:Path"
$env:ESP_GSP_COMPONENT_DIR = $component
$env:GSPC_EXECUTABLE = Join-Path $repoRoot '.tools/gspc/gspc.exe'
$env:GSP_SIM_EXECUTABLE = Join-Path $repoRoot '.tools/sim/sim.exe'
$env:CMAKE_GENERATOR = 'Ninja'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
# Use an ASCII relative filename for the native C fixture on Windows.
$env:FACE_SIM_STATE = 'evidence/face-sim-input.txt'
$simArgs = @('project', 'sim', '--project', "projects/$Project")
if ($Headless) { $simArgs += '--headless' } else { $simArgs += '--interactive' }
if ($Duration -gt 0) { $simArgs += @('--duration', "$Duration") }
Push-Location -LiteralPath $repoRoot
try {
    New-Item -ItemType Directory -Path evidence -Force | Out-Null
    Set-Content -LiteralPath evidence/face-sim-input.txt -Value auto -Encoding ascii
    & $python mosaico.py @simArgs
    if ($LASTEXITCODE) { throw "Simulator exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
}
