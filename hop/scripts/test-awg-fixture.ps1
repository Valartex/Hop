param([switch]$GuardFault, [switch]$Selection, [switch]$CrossSelection, [switch]$Policy, [switch]$RoutedPolicy, [switch]$FullTunnel, [switch]$RoutedSelection, [switch]$InstalledGui, [switch]$NetworkRecovery, [switch]$EndpointHealth, [ValidateSet('','connected','disconnected')][string]$Idle = '')

$ErrorActionPreference = 'Stop'
if ($Idle -and $NetworkRecovery) { throw 'Run idle measurement and network interruption as separate bounded scenarios' }
if ($NetworkRecovery) { $InstalledGui = $true }
if ($Idle) { $InstalledGui = $true }
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopStart = [Diagnostics.ProcessStartInfo]::new()
$hopStart.FileName = Join-Path $hopRoot 'build\hop_awg_test_peer.exe'
$hopStart.UseShellExecute = $false
$hopStart.CreateNoWindow = $true
$hopStart.RedirectStandardOutput = $true
$hopStart.RedirectStandardError = $true
$hopPeer = [Diagnostics.Process]::Start($hopStart)
$hopKey = $null
try {
    $hopErrors = $hopPeer.StandardError.ReadToEndAsync()
    $hopFirstLine = $hopPeer.StandardOutput.ReadLineAsync()
    if (-not $hopFirstLine.Wait(20000) -or -not $hopFirstLine.Result) { throw 'The local AWG fixture did not become ready' }
    $hopProfile = $hopFirstLine.Result | ConvertFrom-Json
    if ($GuardFault -or $FullTunnel -or $InstalledGui) { $hopProfile.config = $hopProfile.config.Replace('AllowedIPs = 10.77.0.1/32', 'AllowedIPs = 0.0.0.0/0, ::/0') }
    if ($InstalledGui) { $hopProfile.config = $hopProfile.config.Replace('DNS = 1.1.1.1', 'DNS = 10.77.0.1') + "`n# HopInstalledAwgGuiTest`n" }
    if ($NetworkRecovery) { $hopProfile.config += "`n# HopInstalledNetworkTest`n" }
    if ($Idle -eq 'connected') { $hopProfile.config += "`n# HopGuiIdleConnected`n" }
    if ($Idle -eq 'disconnected') { $hopProfile.config += "`n# HopGuiIdleDisconnected`n" }
    if ($Selection) { $hopProfile.config = $hopProfile.config.Replace('DNS = 1.1.1.1', 'DNS = 10.77.0.1') + "`n# HopSelectionTest`n" }
    if ($CrossSelection) { $hopProfile.config = $hopProfile.config.Replace('DNS = 1.1.1.1', 'DNS = 10.77.0.1') + "`n# HopCrossSelectionTest`n" }
    if ($Policy) { $hopProfile.config += "`n# HopAwgPolicyTest`n# HopDirectPort=$($hopProfile.directPort)`n" }
    if ($RoutedPolicy) { $hopProfile.config += "`n# HopRoutedPolicyTest`n# HopDirectPort=$($hopProfile.directPort)`n" }
    if ($RoutedSelection) { $hopProfile.config = $hopProfile.config.Replace('DNS = 1.1.1.1', 'DNS = 10.77.0.1') + "`n# HopRoutingSelectionTest`n" }
    if ($EndpointHealth) { $hopProfile.config = $hopProfile.config.Replace('DNS = 1.1.1.1', 'DNS = 10.77.0.1') + "`n# HopAwgEndpointTest`n# HopDirectPort=$($hopProfile.directPort)`n" }
    $hopKey = ConvertTo-SecureString $hopProfile.config -AsPlainText -Force
    $hopJobJson = & (Join-Path $PSScriptRoot 'send-vm-test-key.ps1') -Key $hopKey -GuardFault:($GuardFault -or $InstalledGui)
    $hopJob = $hopJobJson | ConvertFrom-Json
    $hopResultPath = Join-Path $hopRoot ".research\vm-exchange\HopJobs\$($hopJob.id).json"
    $hopUntil = [DateTime]::UtcNow.AddSeconds(110)
    $hopResult = $null
    while (-not $hopResult) {
        if ([DateTime]::UtcNow -gt $hopUntil) { throw 'The guest AWG fixture test did not finish before its deadline' }
        if (Test-Path -LiteralPath $hopResultPath) {
            try { $hopResult = Get-Content -LiteralPath $hopResultPath -Raw | ConvertFrom-Json }
            catch { Start-Sleep -Milliseconds 100 }
        } else { Start-Sleep -Milliseconds 200 }
    }
    if ($hopJob.id -notmatch '^[a-f0-9]{32}$') { throw 'Unexpected VM job identifier' }
    $hopArchive = Join-Path $hopRoot ('.research\vm-runs\' + $hopJob.id)
    New-Item -ItemType Directory -Force -Path $hopArchive | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $hopRoot '.research\vm-exchange\HopJobs') -Filter ($hopJob.id + '*') | Copy-Item -Destination $hopArchive
    Copy-Item -LiteralPath (Join-Path $hopRoot 'build\windows-test\BUILD-MANIFEST.json') -Destination $hopArchive
    if ($InstalledGui) {
        Get-FileHash -LiteralPath (Join-Path $hopRoot 'build\windows-test\Hop.exe'), (Join-Path $hopRoot 'build\windows-test\Hop-service.exe') -Algorithm SHA256 |
            Select-Object Hash, @{ Name='name'; Expression={ Split-Path $_.Path -Leaf } } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $hopArchive 'RUNTIME-HASH.json')
    }
    $hopResult | Select-Object id,exitCode,recoveryExitCode,output | ConvertTo-Json -Depth 3
} finally {
    if ($hopKey) { $hopKey.Dispose() }
    if (-not $hopPeer.HasExited) { $hopPeer.Kill(); $hopPeer.WaitForExit(5000) | Out-Null }
    $hopPeer.Dispose()
    if ($hopErrors -and $hopErrors.Wait(3000)) {
        $hopStats = $hopErrors.Result.Trim().Split("`n") | Select-Object -Last 1
        if ($hopStats.StartsWith('{')) { Write-Output "Fixture counters: $hopStats" }
    }
}
