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
$cppCompiler = Join-Path $Toolchain "bin\$target-w64-mingw32-clang++.exe"
$windres = Join-Path $Toolchain "bin\$target-w64-mingw32-windres.exe"
if (!(Test-Path -LiteralPath $compiler)) { throw 'Run bootstrap-toolchain.ps1, or provide -Toolchain.' }
$output = Join-Path $PSScriptRoot $(if($Installable){"out\installable\$Architecture"}else{"out\$Architecture"})
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sourceLine = Get-Content -LiteralPath (Join-Path $SourceRoot 'Makefile') | Where-Object { $_ -match '^SRCS = ' }
if (@($sourceLine).Count -ne 1) { throw 'Expected one upstream DLL source list.' }
$sources = (($sourceLine -replace '^SRCS = ','') -split ' ') | ForEach-Object { Join-Path $SourceRoot $_ }
$sources += Join-Path $PSScriptRoot 'input-owner.c'
$sources += Join-Path $PSScriptRoot 'input-owner-runtime.c'
$providerObjects=@()
if($Architecture -eq 'x64'){
    $providerSources=@('ccl/ccl-owner-core.cpp','ccl/ccl-owner-reader.cpp',
        'dva/owner-snapshot-core.cpp','dva/owner-command-classifier.cpp','dva/owner-monitor-classifier.cpp',
        'dva/owner-runtime-policy.cpp','dva/owner-runtime.cpp',
        'ae/ae-owner-core.cpp','ae/ae-owner-runtime-policy.cpp','ae/ae-owner-runtime.cpp')
    foreach($providerSource in $providerSources){
        $providerObject=Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($providerSource))+'.o')
        & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -c (Join-Path $PSScriptRoot "providers/$providerSource") -o $providerObject
        if($LASTEXITCODE){throw "Provider compilation failed: $providerSource"}
        $providerObjects+=$providerObject
    }
}
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
$argsDll = $baseArgs + @('-o',$dll) + $sources + $providerObjects + @((Join-Path $SourceRoot 'src\jamotong.def'),$resource,
    '-shared','-static','-s','-lole32','-loleaut32','-luuid','-luiautomationcore',
    '-lcomctl32','-lcomdlg32','-lgdi32','-limm32','-ladvapi32')
if($Architecture -eq 'x64'){$argsDll+=@('-lbcrypt','-lc++','-lc++abi')}
& $compiler @argsDll
if ($LASTEXITCODE) { throw 'IME compilation failed.' }
$argsTest = $baseArgs + @('-municode','-I',(Join-Path $SourceRoot 'src'),
    (Join-Path $PSScriptRoot 'routing-tests.c'),'-o',(Join-Path $output 'RoutingTests.exe'),
    '-static','-lole32','-loleaut32','-luuid')
& $compiler @argsTest
if ($LASTEXITCODE) { throw 'Routing test compilation failed.' }
$langbarArgs=$baseArgs+@('-municode','-I',(Join-Path $SourceRoot 'src'),
    (Join-Path $PSScriptRoot 'langbar-tests.c'),(Join-Path $SourceRoot 'src\langbar.c'),(Join-Path $SourceRoot 'src\comp_state.c'),
    '-o',(Join-Path $output 'LangbarTests.exe'),'-static','-lole32','-loleaut32','-luuid','-lgdi32')
& $compiler @langbarArgs
if($LASTEXITCODE){throw 'Langbar test compilation failed.'}
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
$editArgs = $baseArgs + @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
    '-I',(Join-Path $SourceRoot 'src'),(Join-Path $PSScriptRoot 'edit-session-tests.c'),
    (Join-Path $SourceRoot 'src\edit_verdict.c'),(Join-Path $SourceRoot 'src\fsm.c'),
    (Join-Path $SourceRoot 'src\layout.c'),(Join-Path $SourceRoot 'src\hangul_layout.c'),
    '-o',(Join-Path $output 'EditSessionTests.exe'),'-static','-lole32','-loleaut32','-luuid')
& $compiler @editArgs
if ($LASTEXITCODE) { throw 'Edit-session test compilation failed.' }
$ownerArgs=$baseArgs+@((Join-Path $PSScriptRoot 'input-owner-tests.c'),'-o',(Join-Path $output 'InputOwnerTests.exe'),'-static')
& $compiler @ownerArgs
if($LASTEXITCODE){throw 'Input owner test compilation failed.'}
foreach($guardSuite in @('pending','resend','runtime')){
    & (Join-Path $PSScriptRoot "test-$guardSuite.ps1") -Architecture $Architecture -OutputDirectory $output -Toolchain $Toolchain -BuildOnly
}
if($Architecture -eq 'x64'){
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/ccl/ccl-owner-core.cpp') (Join-Path $PSScriptRoot 'providers/ccl/ccl-owner-fixture.cpp') -o (Join-Path $output 'CclOwnerTests.exe')
    if($LASTEXITCODE){throw 'CCL owner test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/dva/owner-runtime-fixture.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-runtime-policy.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-command-classifier.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-monitor-classifier.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-snapshot-core.cpp') -o (Join-Path $output 'DvaOwnerTests.exe')
    if($LASTEXITCODE){throw 'DVA owner test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/dva/owner-monitor-fixture.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-monitor-classifier.cpp') -o (Join-Path $output 'DvaMonitorTests.exe')
    if($LASTEXITCODE){throw 'DVA monitor test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/dva/owner-snapshot-fixture.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-snapshot-core.cpp') -o (Join-Path $output 'DvaSnapshotTests.exe')
    if($LASTEXITCODE){throw 'DVA snapshot test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/dva/owner-runtime-capture-fixture.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-runtime-policy.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-command-classifier.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-monitor-classifier.cpp') (Join-Path $PSScriptRoot 'providers/dva/owner-snapshot-core.cpp') -lbcrypt -o (Join-Path $output 'DvaCaptureTests.exe')
    if($LASTEXITCODE){throw 'DVA Capture coordinator test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/ae/ae-owner-core.cpp') (Join-Path $PSScriptRoot 'providers/ae/ae-owner-runtime-policy.cpp') (Join-Path $PSScriptRoot 'providers/ae/ae-owner-fixture.cpp') -o (Join-Path $output 'AeOwnerTests.exe')
    if($LASTEXITCODE){throw 'AE owner test compilation failed.'}
    & $cppCompiler -std=c++17 -O2 -Wall -Wextra -Werror -static (Join-Path $PSScriptRoot 'providers/ae/ae-owner-core.cpp') (Join-Path $PSScriptRoot 'providers/ae/ae-owner-runtime-policy.cpp') (Join-Path $PSScriptRoot 'providers/ae/ae-owner-runtime.cpp') (Join-Path $PSScriptRoot 'providers/ae/ae-owner-lifecycle-fixture.cpp') -lbcrypt -o (Join-Path $output 'AeLifecycleTests.exe')
    if($LASTEXITCODE){throw 'AE lifecycle test compilation failed.'}
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'providers/dva/owner-fixture-controls.manifest') -Destination $output
}
Write-Output "Built $dll (Installable=$Installable; not registered by build)."
