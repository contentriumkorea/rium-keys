param(
    [ValidateSet('x64','x86')][string]$Architecture='x64',
    [string]$OutputDirectory,
    [switch]$BuildOnly,
    [string]$Toolchain=(Join-Path $PSScriptRoot '..\upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64')
)
$ErrorActionPreference='Stop'
$source=Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'third_party/jamotong/src/text_service.c')
$start=$source.IndexOf('#define RESEND_DELAY_MS')
$end=$source.IndexOf('static UINT_PTR g_chordTimer',$start)
$schedule=$source.IndexOf('static void ScheduleKeyResend(')
$scheduleEnd=$source.IndexOf('static wchar_t ToFullWidth',$schedule)
if($start -lt 0 -or $end -lt $start -or $schedule -lt 0 -or $scheduleEnd -lt $schedule){throw 'Actual resend source extraction boundaries missing'}
$out=if($OutputDirectory){$OutputDirectory}else{Join-Path $PSScriptRoot "out\guard-tests\$Architecture"}
[IO.Directory]::CreateDirectory($out)|Out-Null
[IO.File]::WriteAllText((Join-Path $out 'resend-actual.inc'),$source.Substring($start,$end-$start)+$source.Substring($schedule,$scheduleEnd-$schedule),[Text.UTF8Encoding]::new($false))
$prefix=if($Architecture -eq 'x64'){'x86_64'}else{'i686'}
$cc=Join-Path $Toolchain "bin/$prefix-w64-mingw32-clang.exe"
$exe=Join-Path $out 'ResendTests.exe'
& $cc -Wall -Wextra -std=c2x -O2 -I $out -include (Join-Path $PSScriptRoot 'win32-compat.h') (Join-Path $PSScriptRoot 'resend-tests.c') (Join-Path $PSScriptRoot 'third_party/jamotong/src/transition.c') -o $exe -static -lole32 -luuid
if($LASTEXITCODE -ne 0){throw 'Resend fixture compile failed'}
if(!$BuildOnly){
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Resend fixture assertions failed'}
}
