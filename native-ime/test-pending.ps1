param(
    [ValidateSet('x64','x86')][string]$Architecture='x64',
    [string]$OutputDirectory,
    [switch]$BuildOnly,
    [string]$Toolchain=(Join-Path $PSScriptRoot '..\upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64')
)
$ErrorActionPreference='Stop'
$source=Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'third_party/jamotong/src/text_service.c')
$start=$source.IndexOf('static void CompTarget_Remember(')
$end=$source.IndexOf('static bool RetryPendingAtOwnedFocus(',$start)
if($start -lt 0 -or $end -lt $start){throw 'Actual pending source extraction boundaries missing'}
$foldStart=$source.IndexOf('static void Jamotong_FoldInput(JamotongTextService *obj) {')
$foldEnd=$source.IndexOf('void Jamotong_FlushForExternalSwitch(',$foldStart)
$resetStart=$source.IndexOf('static void ResetComposition(JamotongTextService *obj) {')
$resetEnd=$source.IndexOf('void Jamotong_ClearCompositionState(',$resetStart)
$out=if($OutputDirectory){$OutputDirectory}else{Join-Path $PSScriptRoot "out\guard-tests\$Architecture"}
[IO.Directory]::CreateDirectory($out)|Out-Null
[IO.File]::WriteAllText((Join-Path $out 'pending-actual.inc'),($source.Substring($start,$end-$start)+$source.Substring($foldStart,$foldEnd-$foldStart)+$source.Substring($resetStart,$resetEnd-$resetStart)),[Text.UTF8Encoding]::new($false))
$prefix=if($Architecture -eq 'x64'){'x86_64'}else{'i686'}
$cc=Join-Path $Toolchain "bin/$prefix-w64-mingw32-clang.exe"
$exe=Join-Path $out 'PendingTests.exe'
$src=Join-Path $PSScriptRoot 'third_party/jamotong/src'
& $cc -Wall -Wextra -std=c2x -O2 -I $out -include (Join-Path $PSScriptRoot 'win32-compat.h') (Join-Path $PSScriptRoot 'pending-tests.c') "$src/transition.c" "$src/fsm.c" "$src/layout.c" "$src/hangul_layout.c" -o $exe -static -lole32 -luuid
if($LASTEXITCODE -ne 0){throw 'Pending fixture compile failed'}
if(!$BuildOnly){
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Pending fixture assertions failed'}
}
