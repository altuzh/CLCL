<#
.SYNOPSIS
    Builds and optionally deploys the custom CLCL build.

.DESCRIPTION
    Compiles CLCL using MSBuild (x86 Release) and deploys the binaries to C:\Users\al\000\clcl\.

.PARAMETER Deploy
    When set, deploys the compiled CLCL.exe and CLCLHook.dll to the target directory.

.PARAMETER Restart
    When set alongside -Deploy, restarts CLCL.exe after deploying.

.PARAMETER TargetDir
    The deployment directory (default: C:\Users\al\000\clcl).
#>

[CmdletBinding()]
param(
    [switch]$Deploy,
    [switch]$Restart,
    [string]$TargetDir = "C:\Users\al\000\clcl",
    [string]$Configuration = "Release",
    [string]$Platform = "x86",
    [string]$PlatformToolset = "v145"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$solutionPath = Join-Path $scriptDir "CLCL.sln"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "         CLCL Build & Deploy Harness      " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Locate MSBuild
$msbuild = $null
$candidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
)

foreach ($c in $candidates) {
    if (Test-Path $c) {
        $msbuild = $c
        break
    }
}

if (-not $msbuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $mb = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $mb) { $msbuild = $mb }
        }
    }
}

if (-not $msbuild) {
    throw "MSBuild.exe could not be located on this machine."
}

Write-Host "Using MSBuild: $msbuild" -ForegroundColor Green
Write-Host "Building $Configuration|$Platform with toolset $PlatformToolset..." -ForegroundColor Yellow

& $msbuild $solutionPath "-p:Configuration=$Configuration" "-p:Platform=$Platform" "-p:PlatformToolset=$PlatformToolset" "-v:m"
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$binDir = Join-Path $scriptDir "$Configuration"
$builtExe = Join-Path $binDir "CLCL.exe"
$builtDll = Join-Path $binDir "CLCLHook.dll"

if (-not (Test-Path $builtExe)) {
    throw "Expected build artifact not found: $builtExe"
}

Write-Host "`nBuild SUCCESSFUL!" -ForegroundColor Green
Write-Host "Built executable: $builtExe" -ForegroundColor Gray
Write-Host "Built hook DLL:   $builtDll" -ForegroundColor Gray

# 3. Deploy if requested
if ($Deploy) {
    Write-Host "`nDeploying to $TargetDir..." -ForegroundColor Cyan

    if (-not (Test-Path $TargetDir)) {
        throw "Target deployment directory '$TargetDir' does not exist."
    }

    # Stop running CLCL and CLCLSet processes if any
    $running = Get-Process -Name CLCL, CLCLSet -ErrorAction SilentlyContinue
    if ($running) {
        foreach ($p in $running) {
            Write-Host "Stopping running process $($p.ProcessName) (PID: $($p.Id))..." -ForegroundColor Yellow
            $p | Stop-Process -Force
        }
        Start-Sleep -Milliseconds 500
    }

    $destExe = Join-Path $TargetDir "CLCL.exe"
    $destDll = Join-Path $TargetDir "CLCLHook.dll"

    # Backup existing files
    if (Test-Path $destExe) {
        $backupExe = Join-Path $TargetDir "CLCL.exe.bak"
        $origBackup = Join-Path $TargetDir "CLCL.exe.orig_backup"
        if (-not (Test-Path $origBackup)) {
            Copy-Item -Path $destExe -Destination $origBackup -Force
            Write-Host "Saved original backup: $origBackup" -ForegroundColor Gray
        }
        Copy-Item -Path $destExe -Destination $backupExe -Force
        Write-Host "Saved backup: $backupExe" -ForegroundColor Gray
    }

    Copy-Item -Path $builtExe -Destination $destExe -Force
    Write-Host "Copied: $builtExe -> $destExe" -ForegroundColor Green

    if (Test-Path $builtDll) {
        Copy-Item -Path $builtDll -Destination $destDll -Force
        Write-Host "Copied: $builtDll -> $destDll" -ForegroundColor Green
    }

    $builtSet = Join-Path $binDir "CLCLSet.exe"
    $destSet = Join-Path $TargetDir "CLCLSet.exe"
    if (Test-Path $builtSet) {
        Copy-Item -Path $builtSet -Destination $destSet -Force
        Write-Host "Copied: $builtSet -> $destSet" -ForegroundColor Green
    }

    # Restart process if requested
    if ($Restart) {
        Write-Host "Starting updated CLCL.exe on interactive desktop..." -ForegroundColor Cyan
        $taskName = "LaunchCLCL_Deploy"
        cmd.exe /c "schtasks.exe /create /tn `"$taskName`" /tr `"\`"$destExe\`"`" /sc once /st 23:59 /it /f >nul 2>nul"
        cmd.exe /c "schtasks.exe /run /tn `"$taskName`" >nul 2>nul"
        Start-Sleep -Milliseconds 600
        cmd.exe /c "schtasks.exe /delete /tn `"$taskName`" /f >nul 2>nul"
        $newProc = Get-Process -Name CLCL -ErrorAction SilentlyContinue
        if ($newProc) {
            Write-Host "CLCL is now running! (PID: $($newProc.Id))" -ForegroundColor Green
        } else {
            Write-Host "Notice: Launched CLCL.exe." -ForegroundColor Yellow
        }
    }
}

Write-Host "`nAll done!" -ForegroundColor Green
