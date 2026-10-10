param(
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [switch]$Installable,
    [switch]$Diagnostic,
    [string]$Toolchain = (Join-Path $PSScriptRoot '..\upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64'),
    [string]$SourceRoot = (Join-Path $PSScriptRoot 'third_party\jamotong')
)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$target = if ($Architecture -eq 'x64') { 'x86_64' } else { 'i686' }
$compiler = Join-Path $Toolchain "bin\$target-w64-mingw32-clang.exe"
$windres = Join-Path $Toolchain "bin\$target-w64-mingw32-windres.exe"
if (!(Test-Path -LiteralPath $compiler)) { throw 'Run bootstrap-toolchain.ps1, or provide -Toolchain.' }
$output = Join-Path $PSScriptRoot $(if($Installable){"out\installable\$Architecture"}else{"out\$Architecture"})
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sourceLine = Get-Content -LiteralPath (Join-Path $SourceRoot 'Makefile') | Where-Object { $_ -match '^SRCS = ' }
if (@($sourceLine).Count -ne 1) { throw 'Expected one upstream DLL source list.' }
$sources = (($sourceLine -replace '^SRCS = ','') -split ' ') | ForEach-Object { Join-Path $SourceRoot $_ }
$resource = Join-Path $output 'ime-resource.o'
$dll = Join-Path $output 'RiumKeysInput.dll'
$rcArgs = @('-I', (Join-Path $SourceRoot 'src'))
if ($Architecture -eq 'x86') { $rcArgs += '-DJAMOTONG_X86' }
$rcArgs += @('-I',$PSScriptRoot,(Join-Path $PSScriptRoot 'rium.rc'), '-O', 'coff', '-o', $resource)
& $windres @rcArgs
if ($LASTEXITCODE) { throw 'IME resource compilation failed.' }
$baseArgs = @('-Wall','-Wextra','-std=c2x','-D_UNICODE','-DUNICODE','-O2',
    '-include', (Join-Path $PSScriptRoot 'win32-compat.h'))
if($Installable){$baseArgs+='-DRIUM_INSTALLABLE'}else{$baseArgs+='-DRIUM_FIXTURE_ONLY'}
if($Diagnostic){
    if($Installable){throw 'Text diagnostics are restricted to isolated fixture builds.'}
    $baseArgs+='-DJAMO_DIAG'
}
$argsDll = $baseArgs + @('-o',$dll) + $sources + @((Join-Path $SourceRoot 'src\jamotong.def'),$resource,
    '-shared','-static','-s','-lole32','-loleaut32','-luuid','-luiautomationcore',
    '-lcomctl32','-lcomdlg32','-lgdi32','-limm32','-ladvapi32')
& $compiler @argsDll
if ($LASTEXITCODE) { throw 'IME compilation failed.' }
$argsTest = $baseArgs + @('-municode','-I',(Join-Path $SourceRoot 'src'),
    (Join-Path $PSScriptRoot 'routing-tests.c'),'-o',(Join-Path $output 'RoutingTests.exe'),
    '-static','-lole32','-loleaut32','-luuid')
& $compiler @argsTest
if ($LASTEXITCODE) { throw 'Routing test compilation failed.' }
$engineSources = @('fsm.c','layout.c','hangul_layout.c','comp_path.c','transition.c') |
    ForEach-Object { Join-Path $SourceRoot "src\$_" }
$engineArgs = $baseArgs + @('-I',(Join-Path $SourceRoot 'src'),
    (Join-Path $PSScriptRoot 'engine-tests.c')) + $engineSources +
    @('-o',(Join-Path $output 'EngineTests.exe'),'-static')
& $compiler @engineArgs
if ($LASTEXITCODE) { throw 'Engine test compilation failed.' }
$inlineArgs = $baseArgs + @('-I',(Join-Path $SourceRoot 'src'),
    (Join-Path $PSScriptRoot 'inline-tests.c')) + $engineSources +
    @('-o',(Join-Path $output 'InlineTests.exe'),'-static','-lole32','-loleaut32','-luuid')
& $compiler @inlineArgs
if ($LASTEXITCODE) { throw 'Inline composition test compilation failed.' }
Write-Output "Built $dll (local preview; Installable=$Installable; not registered by build)."
