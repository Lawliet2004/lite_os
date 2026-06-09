# LiteNix CI-style boot verification wrapper for PowerShell.
#
# Mirrors scripts/ci-boot.sh: runs the positive + negative boot verification
# matrix. Use this from plain PowerShell, GitHub Actions runners, or
# Azure DevOps Windows agents. The Make targets do the actual work and
# the PowerShell wrapper just calls them and propagates the exit code.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\ci-boot.ps1

$ErrorActionPreference = 'Stop'

# Ensure MSYS2/Clang64/UCRT64 tools are in the PATH
$env:PATH = "C:\msys64\usr\bin;C:\msys64\ucrt64\bin;C:\msys64\clang64\bin;" + $env:PATH

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RootDir = Resolve-Path (Join-Path $ScriptDir '..')
Set-Location $RootDir

$Timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
Write-Host "[ci-boot] starting boot verification matrix at $Timestamp"

$logPath = "$RootDir\ci-boot-temp.log"
New-Item -ItemType Directory -Path (Split-Path $logPath) -Force | Out-Null

# Run each verification mode separately so we can show a clean per-mode status.
# The Makefile already chains the three modes via `verify-boot-all`, but we
# call them individually to keep the PowerShell output readable and so a
# single failure doesn't hide the rest of the matrix.

$failures = @()

function Invoke-BootCheck {
    param([string]$Label, [string]$Target)
    Write-Host "[ci-boot] -> $Label"
    Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    $proc = Start-Process -FilePath 'make' -ArgumentList @($Target) `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$logPath-$Target.out" `
        -RedirectStandardError "$logPath-$Target.err"
    if ($proc.ExitCode -ne 0) {
        Write-Host "[ci-boot] $Label FAILED (exit=$($proc.ExitCode))" -ForegroundColor Red
        $script:failures += $Label
        Get-Content "$logPath-$Target.err" -ErrorAction SilentlyContinue | Select-Object -Last 20
    } else {
        Write-Host "[ci-boot] $Label OK" -ForegroundColor Green
    }
}

Invoke-BootCheck -Label 'verify-boot'      -Target 'verify-boot'
Invoke-BootCheck -Label 'verify-boot-vmm'  -Target 'verify-boot-vmm'
Invoke-BootCheck -Label 'verify-boot-heap' -Target 'verify-boot-heap'

# Cleanup any leftover QEMU process at exit
Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue

$EndTs = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
if ($failures.Count -gt 0) {
    Write-Host "[ci-boot] FAILED ($($failures -join ', ')) at $EndTs" -ForegroundColor Red
    exit 1
}

Write-Host "[ci-boot] all boot verifications passed at $EndTs" -ForegroundColor Green
exit 0
