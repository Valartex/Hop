param([switch]$TestsOnly)

$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopGo = if ($env:HOP_GO_EXE) { $env:HOP_GO_EXE } else { Join-Path $hopRoot '.tools\go\bin\go.exe' }
$hopZig = if ($env:HOP_ZIG_EXE) { $env:HOP_ZIG_EXE } else { Join-Path $hopRoot '.tools\zig-x86_64-windows-0.17.0\zig.exe' }
foreach ($hopTool in @($hopGo, $hopZig)) { if (-not (Test-Path -LiteralPath $hopTool)) { throw "Missing tool: $hopTool" } }
$env:GOPATH = Join-Path $hopRoot '.tools\go-work'
$env:GOMODCACHE = Join-Path $hopRoot '.tools\go-mod-cache'
$env:GOCACHE = Join-Path $hopRoot '.tools\go-build-cache'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $hopRoot '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $hopRoot 'build\zig-cache'
$env:GOTOOLCHAIN = 'local'
$env:GOMAXPROCS = '4'
$env:CGO_ENABLED = '1'
$env:CC = "$hopZig cc -target x86_64-windows-gnu"
$hopModule = Join-Path $hopRoot 'shared\native\xraybindings'
& $hopGo -C $hopModule test -p 4 ./...
if ($LASTEXITCODE -ne 0) { throw 'Xray Direct socket tests failed' }
if ($TestsOnly) { return }
$hopOutput = Join-Path $hopRoot 'build\hop-xray\amnezia_xray.dll'
& $hopGo -C $hopModule build -p 4 -trimpath -buildmode=c-shared -ldflags '-s -w -buildid=' -o $hopOutput .
if ($LASTEXITCODE -ne 0) { throw 'Hop Xray bindings build failed' }
Write-Output "Hop Xray bindings: $hopOutput"
