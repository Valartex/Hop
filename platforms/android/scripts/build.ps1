param(
    [switch]$ConfigureOnly,
    [switch]$Release,
    [switch]$Bundle,
    [string]$ApplicationId = 'io.github.valartex.hop',
    [string]$MavenCentralMirror = ''
)

$ErrorActionPreference = 'Stop'
$LRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$LTools = Join-Path $LRoot '.tools'
$LSdk = Join-Path $LTools 'android-sdk'
$LQt = Join-Path $LTools 'Qt/6.10.1/android_arm64_v8a'
$LHostQt = Join-Path $LTools 'Qt/6.10.1/msvc2022_64'
$LNdk = Join-Path $LSdk 'ndk/27.2.12479018'
$LPython = Join-Path $LTools 'build-python/Scripts'
$LCmake = Join-Path $LPython 'cmake.exe'
$LBuild = Join-Path $LRoot 'build/android/arm64-v8a'
$LTemporary = Join-Path $LTools 'javatmp'
foreach ($LRequired in @($LCmake, "$LQt/lib/cmake/Qt6/qt.toolchain.cmake", "$LNdk/build/cmake/android.toolchain.cmake", "$LTools/jdk17/bin/javac.exe")) {
    if (-not (Test-Path -LiteralPath $LRequired)) { throw "Missing Android dependency: $LRequired" }
}
New-Item -ItemType Directory -Path $LTemporary -Force | Out-Null
$env:JAVA_HOME = Join-Path $LTools 'jdk17'
$env:ANDROID_HOME = $LSdk
$env:ANDROID_SDK_ROOT = $LSdk
$env:ANDROID_NDK_ROOT = $LNdk
$env:PATH = "$LPython;$LTools/go/bin;$env:JAVA_HOME/bin;$LSdk/platform-tools;$env:PATH"
$env:CONAN_HOME = Join-Path $LTools 'conan'
$env:GRADLE_USER_HOME = Join-Path $LTools 'gradle'
$env:GOPATH = Join-Path $LTools 'go-work'
$env:GOMODCACHE = Join-Path $LTools 'go-mod-cache'
$env:GOCACHE = Join-Path $LTools 'go-build-cache'
$env:GOTOOLCHAIN = 'local'
$env:TEMP = $LTemporary
$env:TMP = $LTemporary
$LJavaTemporary = $LTemporary.Replace('\', '/')
$env:JAVA_TOOL_OPTIONS = "-Djdk.net.unixdomain.tmpdir=$LJavaTemporary -Djava.io.tmpdir=$LJavaTemporary -Djava.net.preferIPv4Stack=true"
$env:HOP_ANDROID_APPLICATION_ID = $ApplicationId
$env:HOP_ANDROID_BUILD_TYPE = if ($Release) { 'release' } else { 'debug' }
$env:HOP_ANDROID_FLAVOR = 'oss'
if ($MavenCentralMirror) { $env:HOP_MAVEN_CENTRAL_MIRROR = $MavenCentralMirror }
New-Item -ItemType Directory -Path $LBuild -Force | Out-Null
$LProfile = Join-Path $LBuild 'native-clang.profile'
foreach ($LName in @('LQt', 'LHostQt', 'LSdk', 'LNdk', 'LProfile')) {
    Set-Variable -Name $LName -Value ((Get-Variable -Name $LName -ValueOnly).Replace('\', '/'))
}
$LClang = (Join-Path $LNdk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe').Replace('\', '/')
$env:HOP_NDK_CLANG = $LClang
$env:HOP_ANDROID_TARGET = 'aarch64-linux-android28'
$LCompilerScript = (Join-Path $PSScriptRoot 'clang-response.sh').Replace('\', '/')
# Ответный файл обходит ограничения cmd и CreateProcess при линковке OpenSSL.
[IO.File]::WriteAllText($LProfile, "[buildenv]`nopenssl/*:CC=bash $LCompilerScript`n", [Text.UTF8Encoding]::new($false))
$LArguments = @(
    '-S', $LRoot, '-B', $LBuild, '-G', 'Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$LQt/lib/cmake/Qt6/qt.toolchain.cmake",
    '-DCMAKE_SYSTEM_NAME=Android', "-DCONAN_HOST_PROFILE=default;auto-cmake;$LProfile",
    "-DQT_HOST_PATH=$LHostQt", "-DCMAKE_PREFIX_PATH=$LQt",
    "-DANDROID_SDK_ROOT=$LSdk", "-DANDROID_NDK_ROOT=$LNdk", "-DCMAKE_ANDROID_NDK=$LNdk",
    '-DANDROID_ABI=arm64-v8a', '-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a',
    '-DANDROID_PLATFORM=android-28', '-DAPP_ANDROID_MIN_SDK=28',
    "-DCMAKE_BUILD_TYPE=$(if ($Release) { 'Release' } else { 'Debug' })",
    '-DCLIENT_TARGET_NAME=Hop', '-DCLIENT_APPLICATION_NAME=Hop',
    '-DCLIENT_ORGANIZATION_NAME=Hop', '-DCLIENT_KEYCHAIN_NAME=Hop-Keychain',
    '-DCLIENT_APP_INSTANCE_NAME=HopInstance', "-DCLIENT_ANDROID_PACKAGE=$ApplicationId",
    '-DCLIENT_ENABLE_APP_UPDATES=0', '-DANDROID_BUILD_PLAY=OFF'
)
& $LCmake @LArguments
if ($LASTEXITCODE -ne 0) { throw 'Android configure failed' }
if ($ConfigureOnly) { return }
& $LCmake --build $LBuild --parallel 4 --target $(if ($Bundle) { 'aab' } else { 'apk' })
if ($LASTEXITCODE -ne 0) { throw 'Android build failed' }
$LPackageRoot = Join-Path $LBuild 'client/android-build/build/outputs'
$LVersion = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim()
$LMode = if ($Release) { 'release' } else { 'debug' }
$LExtension = if ($Bundle) { 'aab' } else { 'apk' }
$LSource = if ($Bundle) {
    Join-Path $LPackageRoot "bundle/$LMode/android-build-$LMode.aab"
} else {
    Join-Path $LPackageRoot "apk/android-build-$LMode$(if ($Release) { '-unsigned' }).apk"
}
if (-not (Test-Path -LiteralPath $LSource)) { throw "Android output not found: $LSource" }
$LArtifacts = Join-Path $LRoot 'build/android/artifacts'
New-Item -ItemType Directory -Path $LArtifacts -Force | Out-Null
$LSigning = if ($Release) { if ($env:QT_ANDROID_KEYSTORE_PATH) { '-signed' } else { '-unsigned' } } else { '' }
$LDestination = Join-Path $LArtifacts "Hop-$LVersion-arm64-v8a-$LMode$LSigning.$LExtension"
Copy-Item -LiteralPath $LSource -Destination $LDestination -Force
& (Join-Path $LPython 'python.exe') (Join-Path $PSScriptRoot 'verify-package.py') $LDestination
if ($LASTEXITCODE -ne 0) { throw 'Android package verification failed' }
Write-Output "Hop Android artifact: $LDestination"
