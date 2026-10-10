param([ValidateSet('x64','x86')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdk='C:\Program Files (x86)\Windows Kits\10'
$version='10.0.22621.0'
$env:PATH="$sdk\bin\$version\x64;$env:PATH"
$env:INCLUDE="$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um;$sdk\Include\$version\winrt"
$env:LIB="$vc\lib\$Architecture;$sdk\Lib\$version\ucrt\$Architecture;$sdk\Lib\$version\um\$Architecture"
$output=Join-Path $PSScriptRoot "out\$Architecture"
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    & "$vc\bin\Hostx64\$Architecture\cl.exe" /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /DRIUM_REUSED_ENGINE "$PSScriptRoot\..\input-probe\Controller.cpp" /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /OUT:RiumImeFixture.exe ole32.lib oleaut32.lib uuid.lib user32.lib advapi32.lib imm32.lib shell32.lib gdi32.lib
    if($LASTEXITCODE){throw 'Native fixture build failed'}
} finally {Pop-Location}
