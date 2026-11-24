param([string]$Stage = 'build\windows-test', [string]$Version = '')
$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopStage = [IO.Path]::GetFullPath((Join-Path $hopRoot $Stage))
$hopOutput = Join-Path $hopRoot 'build\installer'
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $hopRoot 'hop\packaging\windows-version.txt') -Raw).Trim() }
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'MSI version requires exactly three numeric fields' }
$hopWix = Join-Path $hopRoot '.tools\wix\wix.exe'
$hopUi = Join-Path $hopRoot '.tools\wix\.wix\extensions\WixToolset.UI.wixext\4.0.6\wixext4\WixToolset.UI.wixext.dll'
if (-not (Test-Path -LiteralPath $hopWix) -or -not (Test-Path -LiteralPath $hopUi)) { throw 'Install the local WiX 4.0.6 CLI and UI extension first' }
$hopManifest = Get-Content -LiteralPath (Join-Path $hopStage 'BUILD-MANIFEST.json') -Raw | ConvertFrom-Json
if ($hopManifest.xrayBindings -ne 'shared/native/xraybindings') { throw 'The installer requires Hop policy bindings' }
foreach ($hopProperty in $hopManifest.hashes.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $hopStage $hopProperty.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hopProperty.Value) {
        throw "Stage manifest mismatch: $($hopProperty.Name)"
    }
}
. (Join-Path $PSScriptRoot 'enter-build-env.ps1')
New-Item -ItemType Directory -Force -Path $hopOutput | Out-Null
& cl.exe /nologo /LD /W4 /WX /O2 /EHsc /MT /std:c++17 /DUNICODE /D_UNICODE (Join-Path $hopRoot 'hop\native\installer-actions\installerActions.cpp') "/Fo$hopOutput\HopInstallerActions.obj" "/Fe$hopOutput\HopInstallerActions.dll" /link msi.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Installer rollback action compilation failed' }
& (Join-Path $hopRoot '.tools\build-python\Scripts\python.exe') (Join-Path $hopRoot 'hop\packaging\build_msi_source.py') --stage $hopStage --output (Join-Path $hopOutput 'Hop.wxs') --version $Version
if ($LASTEXITCODE -ne 0) { throw 'MSI source generation failed' }
$env:DOTNET_ROLL_FORWARD = 'LatestMajor'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
& $hopWix build (Join-Path $hopOutput 'Hop.wxs') -arch x64 -culture ru-RU -ext $hopUi -out (Join-Path $hopOutput "Hop-$Version-windows-x64.msi")
if ($LASTEXITCODE -ne 0) { throw 'MSI build failed' }
Copy-Item -LiteralPath (Join-Path $hopStage 'BUILD-MANIFEST.json') -Destination (Join-Path $hopOutput 'SOURCE-BUILD-MANIFEST.json') -Force
Get-FileHash -LiteralPath (Join-Path $hopOutput "Hop-$Version-windows-x64.msi") -Algorithm SHA256 | Select-Object Hash
