param([string]$Destination = 'build\windows-stage', [switch]$VmTests, [switch]$PolicyCore = $true)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'enter-build-env.ps1')
$hopStage = [IO.Path]::GetFullPath((Join-Path $hopRoot $Destination))
$hopBuildRoot = [IO.Path]::GetFullPath((Join-Path $hopRoot 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $hopStage.StartsWith($hopBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The staging directory must be inside the project build directory'
}
& cmake --install (Join-Path $hopRoot 'build\windows') --prefix $hopStage --component AmneziaVPN
if ($LASTEXITCODE -ne 0) { throw "Staging failed: $LASTEXITCODE" }

# Скрипты установки upstream используют идентичность Amnezia и здесь не применяются.
foreach ($hopScriptName in @('post_install.cmd', 'post_uninstall.cmd')) {
    $hopScriptPath = Join-Path $hopStage $hopScriptName
    if (Test-Path -LiteralPath $hopScriptPath) { Remove-Item -LiteralPath $hopScriptPath }
}
# Удаляем остатки прежней упаковки отключённого общего драйвера разделения приложений.
foreach ($hopDriverSuffix in @('cat', 'inf', 'pdb', 'sys')) {
    $hopUnusedDriver = Join-Path $hopStage "mullvad-split-tunnel.$hopDriverSuffix"
    if (Test-Path -LiteralPath $hopUnusedDriver) { Remove-Item -LiteralPath $hopUnusedDriver }
}
foreach ($hopLicense in @('LICENSE', 'THIRD_PARTY_LICENSES.md')) {
    Copy-Item -LiteralPath (Join-Path $hopRoot $hopLicense) -Destination (Join-Path $hopStage $hopLicense) -Force
}
if ($PolicyCore) {
    $hopPolicyDll = Join-Path $hopRoot 'build\hop-xray\amnezia_xray.dll'
    if (-not (Test-Path -LiteralPath $hopPolicyDll)) { throw 'Build Hop Xray bindings first' }
    Copy-Item -LiteralPath $hopPolicyDll -Destination (Join-Path $hopStage 'amnezia_xray.dll') -Force
    $hopPolicyNotices = Join-Path $hopStage 'licenses\hop-xray-bindings'
    New-Item -ItemType Directory -Force -Path $hopPolicyNotices | Out-Null
    foreach ($hopNotice in @('LICENSE', 'ORIGIN.md')) {
        Copy-Item -LiteralPath (Join-Path $hopRoot "shared\native\xraybindings\$hopNotice") -Destination $hopPolicyNotices -Force
    }
}
if ($VmTests) {
    Copy-Item -LiteralPath (Join-Path $hopRoot 'build\windows\client\hop_windows_vpn_probe.exe') -Destination $hopStage -Force
    Copy-Item -LiteralPath (Join-Path $hopRoot 'build\windows\client\hop_foreign_ipc_probe.exe') -Destination $hopStage -Force
    foreach ($hopVmScript in @('test-windows-vm.ps1', 'Hop-VM-Test.cmd', 'vm-test-agent.ps1', 'Hop-VM-Agent.cmd')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $hopVmScript) -Destination $hopStage -Force
    }
}
# Уведомления зависимостей отдельного AWG-измерителя поставляются вместе с бинарником.
$hopAwgNotices = Join-Path $hopStage 'licenses\hop-awg-probe'
New-Item -ItemType Directory -Force -Path $hopAwgNotices | Out-Null
Copy-Item -LiteralPath (Join-Path $hopRoot 'hop\native\awg-probe\ORIGIN.md') -Destination $hopAwgNotices -Force
foreach ($hopModule in @('github.com/amnezia-vpn/amneziawg-go/v3@v3.1.20260814', 'github.com/google/btree@v1.1.3',
                         'golang.org/x/crypto@v0.42.0', 'golang.org/x/net@v0.44.0', 'golang.org/x/sys@v0.36.0',
                         'golang.org/x/time@v0.9.0', 'golang.zx2c4.com/wintun@v0.0.0-20230126152724-0fa3db229ce2',
                         'gvisor.dev/gvisor@v0.0.0-20231202080848-1f7806d17489')) {
    $hopModulePath = Join-Path $hopRoot ('.tools\go-mod-cache\' + $hopModule)
    $hopModuleNotices = Join-Path $hopAwgNotices ($hopModule.Replace('/', '__'))
    New-Item -ItemType Directory -Force -Path $hopModuleNotices | Out-Null
    Get-ChildItem -LiteralPath $hopModulePath -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)' } |
        Copy-Item -Destination $hopModuleNotices -Force
}
@'
Hop development build

Run Hop.exe to inspect the native interface and import HTTPS VLESS subscriptions.
The VPN service and drivers are not registered by this staging script.
VLESS Reality HTTPS through TUN and deliberate disconnect have passed Windows VM tests.
Persistent WFP protection survives fault cleanup/service termination with Direct IPv4 available.
AWG 3.1 subscription HTTPS/IP, HTTPS/DNS and explicit UDP DNS have passed VM tests.
AWG fault protection with Direct IPv4 has passed the local VM fixture.
Installed ordinary GUI VLESS and AWG: connect/live domain edit/reconnect/Direct/disconnect passed in the VM.
Always-on protection before Hop starts or after full Windows reboot is outside the agreed scope.
Full IPv6 tunnel and long idle/battery measurements are not accepted by these VM tests.
Use the guarded VM test package for network tests; do not install on the host yet.
DAT download/import/catalog, VLESS HTTPS and AWG encrypted DNS candidate checks are available.
AWG candidate checks defer during an active VPN to avoid endpoint roaming with the same key.
AWG -> VLESS automatic failover and provider catalogs through retained protection passed VM tests.
Common domain/IP/DAT policy has passed native AWG and VLESS VM tests.
MSI install, ordinary GUI startup, SCM VLESS and clean uninstall passed in the VM.
This directory is a development artifact, not a release installer.
Source and build instructions: D:\Projects\Hop\HOP.md
'@ | Set-Content -LiteralPath (Join-Path $hopStage 'DEVELOPMENT.txt') -Encoding utf8
$hopHashes = [ordered]@{}
foreach ($hopBinary in @('Hop.exe', 'Hop-service.exe', 'hop_endpoint_probe.exe', 'hop_awg_probe.exe', 'hop_tunnel_probe.exe', 'hop_windows_vpn_probe.exe', 'hop_foreign_ipc_probe.exe', 'hop_guard_recover.exe', 'agw.dll', 'amnezia_xray.dll', 'tunnel.dll', 'tun2socks.exe', 'wintun.dll')) {
    $hopBinaryPath = Join-Path $hopStage $hopBinary
    if (Test-Path -LiteralPath $hopBinaryPath) { $hopHashes[$hopBinary] = (Get-FileHash -LiteralPath $hopBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$hopManifest = [ordered]@{
    application = 'Hop'; kind = 'development'; createdAtUtc = [DateTime]::UtcNow.ToString('o')
    sourceBase = (& git -C $hopRoot rev-parse HEAD).Trim(); uncommittedChanges = $true
    qt = '6.10.1'; msvc = '19.44'; xrayBindings = $(if ($PolicyCore) { 'shared/native/xraybindings' } else { 'upstream v1.4.0' }); hashes = $hopHashes
}
$hopSourceFingerprint = & (Join-Path $hopRoot '.tools\build-python\Scripts\python.exe') (Join-Path $hopRoot 'hop\packaging\source_fingerprint.py')
if ($LASTEXITCODE -ne 0) { throw 'Source fingerprint failed' }
$hopManifest.sourceFingerprint = $hopSourceFingerprint | ConvertFrom-Json
$hopManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $hopStage 'BUILD-MANIFEST.json') -Encoding utf8
Write-Output "Hop development files: $hopStage"
