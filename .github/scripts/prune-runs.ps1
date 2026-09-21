#Requires -Version 5.1
<#
.SYNOPSIS
    Prunes previous GitHub Actions workflow runs to ensure only 1 run record remains in history.
#>

[CmdletBinding()]
param(
    [string]$CurrentRunId = $env:GITHUB_RUN_ID,
    [string]$Repository = $env:GITHUB_REPOSITORY,
    [string]$Token = $env:GH_TOKEN
)

$ErrorActionPreference = "Stop"

if (-not $Token) {
    $Token = $env:GITHUB_TOKEN
}

if (-not $Token) {
    Write-Warning "No GitHub token provided (GH_TOKEN or GITHUB_TOKEN). Skipping run history pruning."
    exit 0
}

if (-not $Repository) {
    Write-Warning "GITHUB_REPOSITORY environment variable not found. Skipping run history pruning."
    exit 0
}

if (-not $CurrentRunId) {
    Write-Warning "GITHUB_RUN_ID environment variable not found. Skipping run history pruning."
    exit 0
}

$headers = @{
    "Authorization"        = "Bearer $Token"
    "Accept"               = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "     GitHub Actions Run History Pruner   " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Target Repository: $Repository"
Write-Host "Preserved Run ID:  $CurrentRunId"

$attempted = @{}
$deletedCount = 0

while ($true) {
    $uri = "https://api.github.com/repos/$Repository/actions/runs?per_page=100"
    try {
        $response = Invoke-RestMethod -Uri $uri -Method Get -Headers $headers
    } catch {
        Write-Warning "Failed to query workflow runs: $_"
        break
    }

    $runs = $response.workflow_runs
    if ($null -eq $runs -or $runs.Count -eq 0) {
        break
    }

    $runsToDelete = @($runs | Where-Object {
        [string]$_.id -ne [string]$CurrentRunId -and -not $attempted.ContainsKey([string]$_.id)
    })

    if ($runsToDelete.Count -eq 0) {
        break
    }

    foreach ($run in $runsToDelete) {
        $runId = [string]$run.id
        $attempted[$runId] = $true
        Write-Host "Pruning run $runId ('$($run.name)' - status: $($run.status))..."

        # If active/queued, attempt cancellation before deletion
        if ($run.status -ne "completed") {
            try {
                Invoke-RestMethod -Uri "https://api.github.com/repos/$Repository/actions/runs/$runId/cancel" -Method Post -Headers $headers -ErrorAction SilentlyContinue
                Start-Sleep -Milliseconds 500
            } catch {}
        }

        try {
            Invoke-RestMethod -Uri "https://api.github.com/repos/$Repository/actions/runs/$runId" -Method Delete -Headers $headers
            $deletedCount++
            Write-Host "  -> Successfully deleted run $runId" -ForegroundColor Green
        } catch {
            Write-Warning "  -> Could not delete run $($runId): $_"
        }
    }

    if ($runs.Count -lt 100) {
        break
    }
}

Write-Host "`nPruning complete. Deleted $deletedCount previous run(s)." -ForegroundColor Green
Write-Host "Actions history now retains only run record $CurrentRunId." -ForegroundColor Green
