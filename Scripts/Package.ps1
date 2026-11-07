# Packages the Aedir project for Win64 with UAT BuildCookRun.
# Parameters: -EnginePath (UE install root), -Configuration (Development|Shipping), -OutputDir (archive output, relative to this script).

[CmdletBinding()]
param(
	[string]$EnginePath = "C:\Users\berka\Epic Games\UE_5.8",

	[ValidateSet("Development", "Shipping")]
	[string]$Configuration = "Development",

	[string]$OutputDir = "..\Build"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $repoRoot "Aedir\Aedir.uproject"
$runUat = Join-Path $EnginePath "Engine\Build\BatchFiles\RunUAT.bat"
$archiveDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputDir))

if (-not (Test-Path $projectFile)) {
	Write-Host "Project file not found: $projectFile" -ForegroundColor Red
	exit 1
}

if (-not (Test-Path $runUat)) {
	Write-Host "RunUAT.bat not found under engine path: $runUat" -ForegroundColor Red
	exit 1
}

New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null

$uatArgs = @(
	"BuildCookRun",
	"-project=$projectFile",
	"-noP4",
	"-platform=Win64",
	"-clientconfig=$Configuration",
	"-build",
	"-cook",
	"-stage",
	"-pak",
	"-archive",
	"-archivedirectory=$archiveDir",
	"-utf8output"
)

& $runUat @uatArgs

if ($LASTEXITCODE -ne 0) {
	Write-Host "Packaging failed with exit code $LASTEXITCODE." -ForegroundColor Red
	exit $LASTEXITCODE
}

Write-Host "Packaging succeeded. Archive directory: $archiveDir" -ForegroundColor Green
exit 0
