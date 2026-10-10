$ErrorActionPreference='Stop'
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdk='C:\Program Files (x86)\Windows Kits\10'
$version='10.0.22621.0'
$env:INCLUDE="$vc\include;$sdk\Include\$version\ucrt"
$env:LIB="$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$output=Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $output
try {
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /MT "$PSScriptRoot\Tests.cpp" /Fe:HangulTests.exe
    if($LASTEXITCODE){throw 'Composition tests failed to compile'}
    & .\HangulTests.exe
    if($LASTEXITCODE){throw 'Composition tests failed'}
} finally {Pop-Location}
