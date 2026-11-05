# Runs the Aedir automation tests headlessly through UnrealEditor-Cmd.
# Parameter: -EnginePath (UE install root). Exits non-zero when the editor run fails.

[CmdletBinding()]
param(
	[string]$EnginePath = "C:\Program Files\Epic Games\UE_5.7"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $repoRoot "Aedir\Aedir.uproject"
$editorCmd = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

if (-not (Test-Path $projectFile)) {
	Write-Host "Project file not found: $projectFile" -ForegroundColor Red
	exit 1
}

if (-not (Test-Path $editorCmd)) {
	Write-Host "UnrealEditor-Cmd.exe not found under engine path: $editorCmd" -ForegroundColor Red
	exit 1
}

& $editorCmd $projectFile "-ExecCmds=Automation RunTests Aedir; Quit" -unattended -nullrhi -nosplash -log

if ($LASTEXITCODE -ne 0) {
	Write-Host "Automation tests failed with exit code $LASTEXITCODE." -ForegroundColor Red
	exit $LASTEXITCODE
}

Write-Host "Automation tests finished with exit code 0." -ForegroundColor Green
exit 0
