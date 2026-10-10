param(
    [switch]$Test,
    [string]$SdkVersion = '10.0.22621.0'
)
$ErrorActionPreference = 'Stop'
$probeRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$probeCompiler = Join-Path $probeRepo 'upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
$probeSdkLibrary = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Lib\$SdkVersion\um\x64\ComCtl32.Lib"
$probeOutput = Join-Path $probeRepo 'native-ime\out\inline-preedit-probe'
foreach ($requiredFile in @($probeCompiler, $probeSdkLibrary)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) { throw "Missing build dependency: $requiredFile" }
}
New-Item -ItemType Directory -Path $probeOutput -Force | Out-Null
$probeFlags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static')
$probeCore = Join-Path $PSScriptRoot 'inline-preedit-core.cpp'
$probeWrapper = Join-Path $PSScriptRoot 'inline-preedit-experiment.cpp'
$probeDll = Join-Path $probeOutput 'inline-preedit-experiment.dll'
$probeController = Join-Path $probeOutput 'inline-preedit-experiment.exe'
$probeFixture = Join-Path $probeOutput 'inline-preedit-fixture.exe'
$probeLoader = Join-Path $probeOutput 'inline-preedit-load-check.exe'
& $probeCompiler @probeFlags '-DEXPERIMENT_DLL' '-shared' $probeCore $probeWrapper $probeSdkLibrary '-limm32' '-luser32' '-o' $probeDll
if ($LASTEXITCODE -ne 0) { throw 'Experiment DLL build failed.' }
& $probeCompiler @probeFlags '-municode' $probeWrapper '-luser32' '-o' $probeController
if ($LASTEXITCODE -ne 0) { throw 'Controller build failed.' }
& $probeCompiler @probeFlags $probeCore (Join-Path $PSScriptRoot 'inline-preedit-fixture.cpp') $probeSdkLibrary '-limm32' '-luser32' '-o' $probeFixture
if ($LASTEXITCODE -ne 0) { throw 'Fixture build failed.' }
& $probeCompiler @probeFlags '-municode' (Join-Path $PSScriptRoot 'inline-preedit-load-check.cpp') '-o' $probeLoader
if ($LASTEXITCODE -ne 0) { throw 'DLL loader check build failed.' }
if ($Test) {
    & $probeFixture
    if ($LASTEXITCODE -ne 0) { throw 'Hidden fixture failed.' }
    & $probeLoader $probeDll
    if ($LASTEXITCODE -ne 0) { throw 'DLL load/export/unload check failed.' }
}
Write-Output "Built disposable probe in $probeOutput. No app hook or installation was started."
