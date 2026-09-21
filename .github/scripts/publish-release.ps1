#Requires -Version 5.1
<#
.SYNOPSIS
    Publishes or updates a GitHub Release with installer binaries.
#>

[CmdletBinding()]
param(
    [string]$Tag = "",
    [string]$ArtifactsPattern = "Installer/out/*.exe"
)

$ErrorActionPreference = "Stop"

# 1. Determine tag/version if not explicitly provided
if (-not $Tag) {
    if ($env:GITHUB_REF_TYPE -eq 'tag' -and $env:GITHUB_REF_NAME) {
        $Tag = $env:GITHUB_REF_NAME
    } else {
        $rootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        $rcPath = Join-Path $rootDir "CLCL.rc"
        if (Test-Path $rcPath) {
            $rcText = [System.IO.File]::ReadAllText($rcPath, [System.Text.Encoding]::UTF8)
            $m = [regex]::Match($rcText, 'FILEVERSION\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)')
            if ($m.Success) {
                $Tag = "v$($m.Groups[1].Value).$($m.Groups[2].Value).$($m.Groups[3].Value)"
            }
        }
        if (-not $Tag) {
            $Tag = "v2.2.0"
        }
    }
}

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "       GitHub Release Publisher          " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Target Release Tag: $Tag"

$files = Get-ChildItem -Path $ArtifactsPattern -ErrorAction SilentlyContinue
if (-not $files) {
    throw "No installer artifacts found matching: $ArtifactsPattern"
}

Write-Host "Found $($files.Count) installer artifact(s) to publish:"
foreach ($f in $files) {
    Write-Host ("  - {0} ({1:N0} bytes)" -f $f.Name, $f.Length) -ForegroundColor Gray
}

# Check if gh CLI is available
$ghCmd = Get-Command gh -ErrorAction SilentlyContinue
if (-not $ghCmd) {
    Write-Warning "GitHub CLI (gh) not found in PATH. Skipping release upload."
    exit 0
}

# Check if release already exists
$releaseExists = $false
try {
    & gh release view $Tag > $null 2>&1
    if ($LASTEXITCODE -eq 0) {
        $releaseExists = $true
    }
} catch {
    $releaseExists = $false
}

if ($releaseExists) {
    Write-Host "Release '$Tag' already exists. Updating/overwriting installer assets..." -ForegroundColor Yellow
    & gh release upload $Tag ($files.FullName) --clobber
} else {
    Write-Host "Creating new GitHub Release '$Tag'..." -ForegroundColor Green
    & gh release create $Tag ($files.FullName) --title "CLCL $Tag" --generate-notes
}

if ($LASTEXITCODE -ne 0) {
    throw "Failed to publish GitHub Release for $Tag."
}

Write-Host "`nGitHub Release '$Tag' published successfully!" -ForegroundColor Green
