$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopStart = [Diagnostics.ProcessStartInfo]::new()
$hopStart.FileName = Join-Path $hopRoot '.tools\build-python\Scripts\python.exe'
$hopStart.ArgumentList.Add((Join-Path $hopRoot 'hop\tools\lanHttpFixture.py'))
$hopStart.UseShellExecute = $false
$hopStart.CreateNoWindow = $true
$hopStart.RedirectStandardOutput = $true
$hopStart.RedirectStandardError = $true
$hopFixture = [Diagnostics.Process]::Start($hopStart)
$hopKey = $null
try {
    $hopLine = $hopFixture.StandardOutput.ReadLineAsync()
    if (-not $hopLine.Wait(10000) -or -not $hopLine.Result) { throw 'LAN fixture did not start' }
    $hopPort = ($hopLine.Result | ConvertFrom-Json).port
    if ($hopPort -lt 1024 -or $hopPort -gt 65535) { throw 'Unexpected fixture port' }
    $hopKey = ConvertTo-SecureString ("[Interface]`n# HopLanGuiTest`n# HopLanPort=$hopPort`n") -AsPlainText -Force
    $hopJob = & (Join-Path $PSScriptRoot 'send-vm-test-key.ps1') -Key $hopKey -GuardFault | ConvertFrom-Json
    if ($hopJob.id -notmatch '^[a-f0-9]{32}$') { throw 'Unexpected VM job identifier' }
    $hopJobs = Join-Path $hopRoot '.research\vm-exchange\HopJobs'
    $hopResult = Join-Path $hopJobs ($hopJob.id + '.json')
    $hopUntil = [DateTime]::UtcNow.AddSeconds(150)
    while (-not (Test-Path -LiteralPath $hopResult)) {
        if ([DateTime]::UtcNow -gt $hopUntil) { throw 'VM LAN test timed out' }
        Start-Sleep -Milliseconds 300
    }
    $hopArchive = Join-Path $hopRoot ('.research\vm-runs\' + $hopJob.id)
    New-Item -ItemType Directory -Force -Path $hopArchive | Out-Null
    Get-ChildItem -LiteralPath $hopJobs -Filter ($hopJob.id + '*') | Copy-Item -Destination $hopArchive
    Copy-Item -LiteralPath (Join-Path $hopRoot 'build\windows-test\BUILD-MANIFEST.json') -Destination $hopArchive
    Get-Content -LiteralPath $hopResult -Raw
} finally {
    if ($hopKey) { $hopKey.Dispose() }
    if (-not $hopFixture.HasExited) { $hopFixture.Kill(); $hopFixture.WaitForExit() }
    $hopFixture.Dispose()
}
