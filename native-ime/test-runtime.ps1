param(
    [ValidateSet('x64','x86','Both')][string]$Architecture='Both',
    [string]$OutputDirectory,
    [switch]$BuildOnly,
    [string]$Toolchain
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if(!$Toolchain){$Toolchain=Join-Path $repo 'upstream-research/toolchain/llvm-mingw-20261006-ucrt-x86_64/bin'}
if(Test-Path -LiteralPath (Join-Path $Toolchain 'bin')){$Toolchain=Join-Path $Toolchain 'bin'}
$source=Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'input-owner-runtime.c')
# Only provider declarations are replaced; tests compile the actual implementation.
$source=$source -replace '(?m)^#include "providers/[^"\r\n]+"\r?\n',''
$architectures=if($Architecture -eq 'Both'){@('x64','x86')}else{@($Architecture)}
foreach($arch in $architectures){
    $out=if($OutputDirectory){if($Architecture -eq 'Both'){Join-Path $OutputDirectory $arch}else{$OutputDirectory}}else{Join-Path $PSScriptRoot "out/guard-tests/$arch"}
    $out=[IO.Path]::GetFullPath($out)
    [IO.Directory]::CreateDirectory($out)|Out-Null
    [IO.File]::WriteAllText((Join-Path $out 'runtime-actual.inc'),$source,[Text.UTF8Encoding]::new($false))
    $prefix=if($arch -eq 'x64'){'x86_64'}else{'i686'}
    $cc=Join-Path $Toolchain "$prefix-w64-mingw32-clang.exe"
    $exe=Join-Path $out 'RuntimeTests.exe'
    & $cc -Wall -Wextra -std=c2x -O2 -I $PSScriptRoot -I $out (Join-Path $PSScriptRoot 'runtime-tests.c') -o $exe -static
    if($LASTEXITCODE -ne 0){throw "Runtime $arch fixture compilation failed"}
    if(!$BuildOnly){
        & $exe
        if($LASTEXITCODE -ne 0){throw "Runtime $arch fixture assertions failed"}
    }
}
