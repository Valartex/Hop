param(
    [switch]$ConfigureOnly,
    [switch]$Tests,
    [string[]]$Targets = @()
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'enter-build-env.ps1')
$hopBuild = Join-Path $hopRoot 'build\windows'

$hopCmake = Join-Path $hopPythonScripts 'cmake.exe'
$hopArguments = @(
    '-S', $hopRoot, '-B', $hopBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$hopQt",
    '-DCLIENT_TARGET_NAME=Hop', '-DCLIENT_APPLICATION_NAME=Hop',
    '-DCLIENT_SERVICE_NAME=Hop-service', '-DCLIENT_ORGANIZATION_NAME=Hop',
    '-DCLIENT_APP_INSTANCE_NAME=HopInstance', '-DCLIENT_KEYCHAIN_NAME=Hop-Keychain',
    '-DCLIENT_IPC_SERVICE_URL=local:HopVpnIpcInterface', '-DCLIENT_ENABLE_APP_UPDATES=0',
    '-DCLIENT_WINDOWS_TUNNEL_INTERFACE=Hop', '-DCLIENT_WINDOWS_XRAY_INTERFACE=Hop-TUN',
    '-DCLIENT_WINDOWS_DAEMON_PIPE=Hop-VpnDaemon',
    '-DCLIENT_WINDOWS_WFP_BASELINE_KEY=863eabcc-6df6-417a-9d8e-6b3b1ec0d10d',
    '-DCLIENT_WINDOWS_WFP_DNS_KEY=c8c9ce08-36c3-4598-a06e-662d5eab024e',
    '-DCLIENT_WINDOWS_APP_SPLIT_TUNNEL=0', '-DCLIENT_WINDOWS_SCOPED_IPV6_FILTERS=1',
    "-DAMNEZIA_BUILD_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })"
)
& $hopCmake @hopArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
if (-not $ConfigureOnly) {
    & (Join-Path $PSScriptRoot 'build-xray-bindings.ps1')
    $hopBuildArguments = @('--build', $hopBuild, '--parallel', '4')
    if ($Targets.Count -gt 0) { $hopBuildArguments += @('--target') + $Targets }
    & $hopCmake @hopBuildArguments
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
}
