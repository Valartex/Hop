param()

$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopVBox = Join-Path $env:ProgramFiles 'Oracle\VirtualBox\VBoxManage.exe'
$hopVmId = 'b79c0594-8e7d-42f6-85b6-3b9784efdb1c'
$hopPackage = Join-Path $hopRoot 'build\windows-test'
$hopExchange = Join-Path $hopRoot '.research\vm-exchange'
if (-not (Test-Path -LiteralPath (Join-Path $hopPackage 'Hop-VM-Test.cmd'))) { throw 'Stage the VM package first' }
if (-not (Test-Path -LiteralPath $hopVBox)) { throw 'VirtualBox is not installed at the expected path' }
$hopInfo = & $hopVBox showvminfo $hopVmId --machinereadable
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the test VM configuration' }
$hopStateLine = $hopInfo | Where-Object { $_ -match '^VMState=' }
if ($hopStateLine -match '^VMState="(poweroff|saved|aborted)"$') {
    & $hopVBox startvm $hopVmId --type gui
    if ($LASTEXITCODE -ne 0) { throw 'Cannot start the test VM' }
} elseif ($hopStateLine -ne 'VMState="running"') {
    throw "Unsupported VM state: $hopStateLine"
}
New-Item -ItemType Directory -Path $hopExchange -Force | Out-Null
$hopInfo = & $hopVBox showvminfo $hopVmId --machinereadable
foreach ($hopShare in @(@{ Name = 'HopBuild'; Path = $hopPackage; ReadOnly = $true },
                        @{ Name = 'HopTestResults'; Path = $hopExchange; ReadOnly = $false })) {
    if ($hopInfo -match ('^SharedFolderNameTransientMapping\d+="' + [regex]::Escape($hopShare.Name) + '"$')) {
        & $hopVBox sharedfolder remove $hopVmId --name $hopShare.Name --transient
        if ($LASTEXITCODE -ne 0) { throw "Cannot refresh test share $($hopShare.Name)" }
    }
    $hopShareArguments = @('sharedfolder', 'add', $hopVmId, '--name', $hopShare.Name, '--hostpath', $hopShare.Path, '--transient')
    if ($hopShare.ReadOnly) { $hopShareArguments += '--readonly' }
    & $hopVBox @hopShareArguments
    if ($LASTEXITCODE -ne 0) { throw "Cannot attach test share $($hopShare.Name)" }
}
Write-Output 'Inside the guest, open \\VBOXSVR\HopBuild and run Hop-VM-Test.cmd as administrator.'
Write-Output 'This host script does not install services, change network settings, or execute commands inside the guest.'
