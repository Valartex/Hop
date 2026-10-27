$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopTools = Join-Path $hopRoot '.tools'
$hopPythonScripts = if ($env:HOP_PYTHON_SCRIPTS) { $env:HOP_PYTHON_SCRIPTS } else { Join-Path $hopTools 'build-python\Scripts' }
$hopQt = if ($env:HOP_QT_ROOT) { $env:HOP_QT_ROOT } else { Join-Path $hopTools 'Qt\6.10.1\msvc2022_64' }
$hopVcVars = if ($env:HOP_VCVARS) { $env:HOP_VCVARS } else { Join-Path $hopTools 'vs2022\VC\Auxiliary\Build\vcvarsall.bat' }
foreach ($hopRequired in @($hopVcVars, (Join-Path $hopPythonScripts 'cmake.exe'), (Join-Path $hopQt 'bin\qmake.exe'))) {
    if (-not (Test-Path -LiteralPath $hopRequired)) { throw "Missing build dependency: $hopRequired" }
}
# Изменения окружения действуют только в текущем процессе.
$hopEnvironment = & $env:ComSpec /d /s /c "`"$hopVcVars`" x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed' }
foreach ($hopLine in $hopEnvironment) {
    if ($hopLine -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:PATH = "$hopPythonScripts;$hopQt\bin;$env:PATH"
$env:CONAN_HOME = Join-Path $hopTools 'conan'
$env:LC_ALL = 'C'
$env:LC_CTYPE = 'C'
$env:LANG = 'C'
