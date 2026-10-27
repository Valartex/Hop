param()

$ErrorActionPreference = 'Stop'
if ((Get-CimInstance Win32_ComputerSystem).Model -notmatch 'VirtualBox') { throw 'Run this agent inside the VirtualBox guest only' }
$hopPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $hopPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run the guest launcher as administrator' }
$hopSource = '\\VBOXSVR\HopBuild'
$hopJobs = '\\VBOXSVR\HopTestResults\HopJobs'
$hopInstall = Join-Path $env:ProgramFiles 'Hop Test'
New-Item -ItemType Directory -Path $hopJobs, $hopInstall -Force | Out-Null
$hopSecretUrl = Read-Host 'HTTPS subscription URL (kept in memory for this agent session)' -AsSecureString
$env:QT_FORCE_STDERR_LOGGING = '1'
$hopUntil = [DateTime]::UtcNow.AddHours(4)
$hopInbox = Join-Path $hopJobs 'inbox.json'
$hopStatus = Join-Path $hopJobs 'agent-status.json'
$hopSequence = 0
Write-Output 'Hop test agent ready. Keep this guest window open. Ctrl+C stops the agent.'
try {
    while ([DateTime]::UtcNow -lt $hopUntil) {
        @{ state = 'ready'; updatedAtUtc = [DateTime]::UtcNow.ToString('o'); sequence = $hopSequence } |
            ConvertTo-Json | Set-Content -LiteralPath $hopStatus -Encoding utf8
        if (-not (Test-Path -LiteralPath $hopInbox)) { Start-Sleep -Seconds 2; continue }
        $hopJob = Get-Content -LiteralPath $hopInbox -Raw | ConvertFrom-Json
        Remove-Item -LiteralPath $hopInbox
        if ($hopJob.id -notmatch '^[a-f0-9]{32}$' -or $hopJob.kind -notin @('baseline', 'guard-fault', 'stop')) {
            Write-Output 'Rejected an unsupported test job'; continue
        }
        if ($hopJob.kind -eq 'stop') { break }
        $hopService = $null
        $hopProbe = $null
        $hopResult = @{ id = $hopJob.id; kind = $hopJob.kind; exitCode = -1; output = ''; errors = ''; startedAtUtc = [DateTime]::UtcNow.ToString('o') }
        $hopServiceOut = Join-Path $hopJobs "$($hopJob.id)-service-out.txt"
        $hopServiceErr = Join-Path $hopJobs "$($hopJob.id)-service-err.txt"
        try {
            Get-ChildItem -LiteralPath $hopSource | Copy-Item -Destination $hopInstall -Recurse -Force
            $hopService = Start-Process -FilePath (Join-Path $hopInstall 'Hop-service.exe') -ArgumentList '-exec' -WorkingDirectory $hopInstall -WindowStyle Hidden -PassThru -RedirectStandardOutput $hopServiceOut -RedirectStandardError $hopServiceErr
            Start-Sleep -Seconds 2
            $hopService.Refresh()
            if ($hopService.HasExited) { throw 'The test service exited during startup' }
            $hopStart = [Diagnostics.ProcessStartInfo]::new()
            $hopStart.FileName = Join-Path $hopInstall 'hop_windows_vpn_probe.exe'
            $hopStart.WorkingDirectory = $hopInstall
            $hopStart.UseShellExecute = $false
            $hopStart.CreateNoWindow = $true
            $hopStart.RedirectStandardInput = $true
            $hopStart.RedirectStandardOutput = $true
            $hopStart.RedirectStandardError = $true
            if ($hopJob.kind -eq 'guard-fault') { $hopStart.Arguments = "--guard-fault --service-pid $($hopService.Id)" }
            $hopProbe = [Diagnostics.Process]::Start($hopStart)
            $hopOutput = $hopProbe.StandardOutput.ReadToEndAsync()
            $hopErrors = $hopProbe.StandardError.ReadToEndAsync()
            $hopPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($hopSecretUrl)
            try { $hopProbe.StandardInput.WriteLine([Runtime.InteropServices.Marshal]::PtrToStringBSTR($hopPointer)); $hopProbe.StandardInput.Close() }
            finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($hopPointer) }
            if (-not $hopProbe.WaitForExit(120000)) { throw 'The test exceeded its deadline' }
            $hopResult.exitCode = $hopProbe.ExitCode
            $hopResult.output = $hopOutput.Result
            $hopResult.errors = $hopErrors.Result
        } catch {
            $hopResult.errors = $_.Exception.Message
        } finally {
            if ($hopProbe -and -not $hopProbe.HasExited) { $hopProbe.Kill(); $hopProbe.WaitForExit(5000) | Out-Null }
            if ($hopService) {
                Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq $hopService.Id } |
                    ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
                if (-not $hopService.HasExited) { Stop-Process -Id $hopService.Id -ErrorAction SilentlyContinue }
            }
            $hopRecovery = Join-Path $hopInstall 'hop_guard_recover.exe'
            if (Test-Path -LiteralPath $hopRecovery) {
                & $hopRecovery --vm-test-only
                $hopResult.recoveryExitCode = $LASTEXITCODE
            }
        }
        $hopResult.completedAtUtc = [DateTime]::UtcNow.ToString('o')
        $hopResult | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $hopJobs "$($hopJob.id).json") -Encoding utf8
        $hopSequence++
        Write-Output "Test $hopSequence completed: $($hopJob.kind), exit $($hopResult.exitCode)"
    }
} finally {
    $hopSecretUrl.Dispose()
    @{ state = 'stopped'; updatedAtUtc = [DateTime]::UtcNow.ToString('o'); sequence = $hopSequence } |
        ConvertTo-Json | Set-Content -LiteralPath $hopStatus -Encoding utf8
}
