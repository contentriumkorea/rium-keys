param([switch]$Test)
$ErrorActionPreference = 'Stop'
$probeRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$probeCompiler = Join-Path $probeRepo 'upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
$probeOutput = Join-Path $probeRepo 'native-ime\out\focus-contract-probe'
$probeExecutable = Join-Path $probeOutput 'focus-contract-snapshot.exe'
if (-not (Test-Path -LiteralPath $probeCompiler -PathType Leaf)) { throw "Missing compiler: $probeCompiler" }
New-Item -ItemType Directory -Path $probeOutput -Force | Out-Null
& $probeCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-municode' `
    (Join-Path $PSScriptRoot 'focus-contract-snapshot.cpp') (Join-Path $PSScriptRoot 'uia-metadata.cpp') `
    '-loleacc' '-loleaut32' '-lole32' '-luuid' '-luser32' '-luiautomationcore' '-o' $probeExecutable
if ($LASTEXITCODE -ne 0) { throw 'Focus contract diagnostic build failed.' }
if ($Test) {
    & $probeExecutable '--self-test'
    if ($LASTEXITCODE -ne 0) { throw "Hidden control self-test failed: $LASTEXITCODE" }
    & $probeExecutable '--self-test-uia'
    if ($LASTEXITCODE -ne 0) { throw "Hidden UIA control self-test failed: $LASTEXITCODE" }
}
Write-Output "Built $probeExecutable. No real application was inspected."
