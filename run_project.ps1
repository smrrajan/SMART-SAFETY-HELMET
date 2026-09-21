$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$pythonExe = Join-Path $env:LOCALAPPDATA "Python\bin\python.exe"
$mplConfig = Join-Path $projectRoot "run\mplconfig"

if (-not (Test-Path $pythonExe)) {
    Write-Error "Python executable not found at $pythonExe"
}

New-Item -ItemType Directory -Force -Path $mplConfig | Out-Null
$env:MPLCONFIGDIR = $mplConfig

Set-Location $projectRoot
& $pythonExe app.py
