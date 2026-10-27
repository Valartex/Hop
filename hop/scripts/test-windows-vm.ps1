param()

$ErrorActionPreference = 'Stop'
$hopProduct = (Get-CimInstance Win32_ComputerSystem).Model
if ($hopProduct -notmatch 'VirtualBox') { throw 'This test must run inside the VirtualBox guest, not on the host' }
$hopIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$hopPrincipal = [Security.Principal.WindowsPrincipal]::new($hopIdentity)
if (-not $hopPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the VM test launcher as administrator inside the guest'
}
$hopSource = '\\VBOXSVR\HopBuild'
$hopResults = '\\VBOXSVR\HopTestResults'
$hopInstall = Join-Path $env:ProgramFiles 'Hop Test'
New-Item -ItemType Directory -Path $hopInstall -Force | Out-Null
Get-ChildItem -LiteralPath $hopSource | Copy-Item -Destination $hopInstall -Recurse -Force
$hopService = $null
$hopProbe = $null
$hopProbeExit = 1
$env:QT_FORCE_STDERR_LOGGING = '1'
try {
    $hopService = Start-Process -FilePath (Join-Path $hopInstall 'Hop-service.exe') -ArgumentList '-exec' -WorkingDirectory $hopInstall -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $hopResults 'service-stdout.txt') -RedirectStandardError (Join-Path $hopResults 'service-diagnostics.txt')
    Start-Sleep -Seconds 3
    $hopService.Refresh()
    if ($hopService.HasExited) { throw 'The test service exited during startup' }
    $hopSecretUrl = Read-Host 'HTTPS subscription URL for the test' -AsSecureString
    $hopSecretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($hopSecretUrl)
    try {
        $hopStart = New-Object Diagnostics.ProcessStartInfo
        $hopStart.FileName = Join-Path $hopInstall 'hop_windows_vpn_probe.exe'
        $hopStart.WorkingDirectory = $hopInstall
        $hopStart.UseShellExecute = $false
        $hopStart.CreateNoWindow = $true
        $hopStart.RedirectStandardInput = $true
        $hopStart.RedirectStandardOutput = $true
        $hopStart.RedirectStandardError = $true
        $hopProbe = [Diagnostics.Process]::Start($hopStart)
        $hopOutput = $hopProbe.StandardOutput.ReadToEndAsync()
        $hopErrors = $hopProbe.StandardError.ReadToEndAsync()
        $hopProbe.StandardInput.WriteLine([Runtime.InteropServices.Marshal]::PtrToStringBSTR($hopSecretPointer))
        $hopProbe.StandardInput.Close()
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($hopSecretPointer)
    }
    if (-not $hopProbe.WaitForExit(90000)) { throw 'The VM test did not finish within 90 seconds' }
    $hopProbeExit = $hopProbe.ExitCode
    $hopReport = $hopOutput.Result + "`r`nProbe exit code: $hopProbeExit"
    $hopReport | Set-Content -LiteralPath (Join-Path $hopResults 'vpn-result.txt') -Encoding utf8
    $hopErrors.Result | Set-Content -LiteralPath (Join-Path $hopResults 'vpn-diagnostics.txt') -Encoding utf8
    Write-Output $hopReport
    Write-Output "Probe exit code: $($hopProbe.ExitCode)"
} finally {
    if ($hopProbe -and -not $hopProbe.HasExited) { $hopProbe.Kill() }
    if ($hopService) {
        Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq $hopService.Id } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
    }
    if ($hopService -and -not $hopService.HasExited) { Stop-Process -Id $hopService.Id }
}
exit $hopProbeExit
