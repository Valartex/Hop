param([string]$Stage = 'build\windows-test')
$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopStage = [IO.Path]::GetFullPath((Join-Path $hopRoot $Stage))
$hopOutput = Join-Path $hopRoot 'build\installer-rollback'
$hopPython = Join-Path $hopRoot '.tools\build-python\Scripts\python.exe'
$hopVersion = [Version]((Get-Content -LiteralPath (Join-Path $hopRoot 'hop\packaging\windows-version.txt') -Raw).Trim())
if ($hopVersion.Build -ge 65535) { throw 'Choose a new base MSI version before building the rollback fixture' }
$hopTestVersion = '{0}.{1}.{2}' -f $hopVersion.Major, $hopVersion.Minor, ($hopVersion.Build + 1)
& $hopPython (Join-Path $hopRoot 'hop\packaging\build_msi_source.py') --stage $hopStage --output (Join-Path $hopOutput 'Hop-base.wxs') --version $hopTestVersion
if ($LASTEXITCODE -ne 0) { throw 'Rollback base generation failed' }
& $hopPython (Join-Path $hopRoot 'hop\packaging\build_rollback_fixture.py') --source (Join-Path $hopOutput 'Hop-base.wxs') --output (Join-Path $hopOutput 'Hop-failure.wxs')
if ($LASTEXITCODE -ne 0) { throw 'Rollback fixture generation failed' }
$env:DOTNET_ROLL_FORWARD = 'LatestMajor'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
& (Join-Path $hopRoot '.tools\wix\wix.exe') build (Join-Path $hopOutput 'Hop-failure.wxs') -arch x64 -culture ru-RU -ext (Join-Path $hopRoot '.tools\wix\.wix\extensions\WixToolset.UI.wixext\4.0.6\wixext4\WixToolset.UI.wixext.dll') -out (Join-Path $hopOutput 'Hop-Rollback-Test.msi')
if ($LASTEXITCODE -ne 0) { throw 'Rollback fixture compilation failed' }
Write-Output 'TEST ONLY: this MSI deliberately rolls back. Never distribute it as the Hop installer.'
Get-FileHash -LiteralPath (Join-Path $hopOutput 'Hop-Rollback-Test.msi') -Algorithm SHA256 | Select-Object Hash
