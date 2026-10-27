param([Parameter(Mandatory)][ValidateSet('InstallerInstall','InstallerVerify','InstallerGuiVpn','InstallerRemove','InstallerInspect','InstallerUpgrade','InstallerRollback','InstallerDowngrade','InstallerServiceStart','RuntimeStage','GuiCatalog','GuiApiHealth','GuiApiVpn','GuiApiFreshVpn','GuiVpn','GuiAllFastest','GuiMonitoring','GuiIdleConnected','GuiIdleDisconnected','VlessPolicy','SmbRouting','Datasets','IpcScope','RouteRecovery','PacketMonitorInventory','PacketMonitorControl','BootInspect','WfpInventory','GuardScmRestart','BootDiagnostics')][string]$Scenario)
$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopMarkers = @{ InstallerInstall='HopInstallerPhase=install'; InstallerVerify='HopInstallerPhase=verify'; InstallerGuiVpn='HopInstallerPhase=gui-vpn'; InstallerRemove='HopInstallerPhase=remove'; InstallerInspect='HopInstallerInspect'; VlessPolicy='HopVlessPolicyTest'; Datasets='HopDatasetTest'; IpcScope='HopIpcScopeTest' }
$hopMarkers['SmbRouting'] = 'HopSmbRoutingTest'
$hopMarkers['RouteRecovery'] = 'HopRouteRecoveryTest'
$hopMarkers['PacketMonitorInventory'] = 'HopPacketMonitorInventory'
$hopMarkers['PacketMonitorControl'] = 'HopPacketMonitorControl'
$hopMarkers['BootInspect'] = 'HopBootInspectTest'
$hopMarkers['WfpInventory'] = 'HopWfpInventoryTest'
$hopMarkers['GuardScmRestart'] = 'HopGuardScmRestartTest'
$hopMarkers['BootDiagnostics'] = 'HopBootDiagnosticsTest'
$hopMarkers['InstallerUpgrade'] = 'HopInstallerPhase=upgrade'
$hopMarkers['InstallerRollback'] = 'HopInstallerPhase=rollback'
$hopMarkers['InstallerDowngrade'] = 'HopInstallerPhase=downgrade'
$hopMarkers['InstallerServiceStart'] = 'HopInstallerPhase=start-service'
$hopMarkers['RuntimeStage'] = 'HopRuntimeStageTest'
$hopMarkers['GuiCatalog'] = 'HopReadOnlyCatalogTest'
$hopMarkers['GuiApiHealth'] = 'HopReadOnlyApiHealthTest'
$hopMarkers['GuiApiVpn'] = 'HopPersistentApiVpnTest'
$hopMarkers['GuiApiFreshVpn'] = 'HopPersistentApiFreshVpnTest'
$hopMarkers['GuiVpn'] = 'HopInstallerPhase=gui-vpn'
$hopMarkers['GuiAllFastest'] = 'HopGuiAllFastest'
$hopMarkers['GuiMonitoring'] = 'HopGuiMonitoring'
$hopMarkers['GuiIdleConnected'] = 'HopGuiIdleConnected'
$hopMarkers['GuiIdleDisconnected'] = 'HopGuiIdleDisconnected'
$hopFault = $Scenario.StartsWith('Installer') -or $Scenario.StartsWith('Gui') -or $Scenario -in @('RouteRecovery','RuntimeStage')
$hopKey = ConvertTo-SecureString ("[Interface]`n# " + $hopMarkers[$Scenario] + "`n") -AsPlainText -Force
try {
    $hopJob = & (Join-Path $PSScriptRoot 'send-vm-test-key.ps1') -Key $hopKey -GuardFault:$hopFault | ConvertFrom-Json
    if ($hopJob.id -notmatch '^[a-f0-9]{32}$') { throw 'Unexpected VM job identifier' }
    $hopJobs = Join-Path $hopRoot '.research\vm-exchange\HopJobs'
    $hopResultPath = Join-Path $hopJobs ($hopJob.id + '.json')
    $hopUntil = [DateTime]::UtcNow.AddSeconds(150)
    while (-not (Test-Path -LiteralPath $hopResultPath)) {
        if ([DateTime]::UtcNow -gt $hopUntil) { throw 'The bounded VM scenario did not report a result' }
        Start-Sleep -Milliseconds 300
    }
    $hopArchive = Join-Path $hopRoot ('.research\vm-runs\' + $hopJob.id)
    New-Item -ItemType Directory -Force -Path $hopArchive | Out-Null
    Get-ChildItem -LiteralPath $hopJobs -Filter ($hopJob.id + '*') | Copy-Item -Destination $hopArchive
    Copy-Item -LiteralPath (Join-Path $hopRoot 'build\windows-test\BUILD-MANIFEST.json') -Destination $hopArchive
    $hopMsiName = switch ($Scenario) { 'InstallerRollback' { 'Hop-Rollback-Test.msi' }; 'InstallerDowngrade' { 'Hop-Previous.msi' }; default { 'Hop-Setup.msi' } }
    $hopMsiPath = Join-Path $hopRoot ('build\windows-test\' + $hopMsiName)
    if ($Scenario.StartsWith('Installer') -and (Test-Path -LiteralPath $hopMsiPath)) {
        Get-FileHash -LiteralPath $hopMsiPath -Algorithm SHA256 |
            Select-Object Hash, @{ Name='package'; Expression={ $hopMsiName } } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $hopArchive 'MSI-HASH.json')
    }
    Get-Content -LiteralPath $hopResultPath -Raw
} finally { $hopKey.Dispose() }
