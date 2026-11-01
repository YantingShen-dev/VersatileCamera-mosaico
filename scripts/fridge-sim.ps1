param([switch]$ServiceOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$python = Join-Path $repoRoot '.venv-sim/Scripts/python.exe'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
Push-Location -LiteralPath $repoRoot
try {
    New-Item -ItemType Directory -Path '.mosaico-state/fridge' -Force | Out-Null
    New-Item -ItemType Directory -Path 'evidence' -Force | Out-Null
    if (-not (Test-Path -LiteralPath '.mosaico-state/fridge/.env')) {
        throw 'Configure .mosaico-state/fridge/.env first; see docs/fridge-simulator.md.'
    }
    if ($ServiceOnly) {
        & $python projects/fridge_mosaico/tools/service.py
    } else {
        foreach ($port in @(3222, 3223)) {
            $probe = New-Object System.Net.Sockets.TcpClient
            try {
                $connection = $probe.ConnectAsync('127.0.0.1', $port)
                if ($connection.Wait(300) -and $probe.Connected) {
                    throw "Port $port is already in use. Open http://127.0.0.1:3223/ or stop the existing simulator first."
                }
            } catch [System.AggregateException] {
                # Connection refused means the port is available.
            } finally { $probe.Dispose() }
        }
        $service = Start-Process -FilePath $python -ArgumentList 'projects/fridge_mosaico/tools/service.py' -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput 'evidence/fridge-service.stdout.log' -RedirectStandardError 'evidence/fridge-service.stderr.log'
        try { & "$PSScriptRoot/sim.ps1" -Project fridge_mosaico }
        finally { if (-not $service.HasExited) { Stop-Process -Id $service.Id } }
    }
} finally { Pop-Location }
