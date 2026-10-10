param([ValidateSet('x64','x86')][string]$Architecture='x64',[string]$Dll,[switch]$Test)
$ErrorActionPreference='Stop'
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdk='C:\Program Files (x86)\Windows Kits\10'
$version='10.0.22621.0'
$env:PATH="$sdk\bin\$version\x64;$env:PATH"
$env:INCLUDE="$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um;$sdk\Include\$version\winrt"
$env:LIB="$vc\lib\$Architecture;$sdk\Lib\$version\ucrt\$Architecture;$sdk\Lib\$version\um\$Architecture"
$output=Join-Path $PSScriptRoot "out\inline-$Architecture"
New-Item -ItemType Directory -Force -Path $output | Out-Null
if(!$Dll){$Dll=Join-Path $PSScriptRoot "out\installable\$Architecture\RiumKeysInput.dll"}
$Dll=(Resolve-Path -LiteralPath $Dll).Path
$sourceHash=(Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash
Copy-Item -LiteralPath $Dll -Destination (Join-Path $output 'RiumKeysInput.dll') -Force
$copiedHash=(Get-FileHash -LiteralPath (Join-Path $output 'RiumKeysInput.dll') -Algorithm SHA256).Hash
if($copiedHash -ne $sourceHash){throw 'Staged candidate hash mismatch'}
Set-Content -LiteralPath (Join-Path $output 'candidate.sha256') -Value $sourceHash -Encoding ascii
$processor=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
@"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
 <assemblyIdentity type="win32" name="Rium.InlineFixture" version="1.0.0.0" processorArchitecture="$processor"/>
 <file name="RiumKeysInput.dll"><comClass clsid="{E1985813-4FA4-4B93-8EF4-F8EE7777E291}" threadingModel="Apartment"/></file>
</assembly>
"@ | Set-Content -LiteralPath (Join-Path $output 'inline.manifest') -Encoding utf8
Push-Location $output
try {
 & "$vc\bin\Hostx64\$Architecture\cl.exe" /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /DRIUM_REUSED_ENGINE "$PSScriptRoot\InlineFixture.cpp" /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" /OUT:RiumImeFixture.exe ole32.lib oleaut32.lib uuid.lib user32.lib advapi32.lib imm32.lib gdi32.lib bcrypt.lib
 if($LASTEXITCODE){throw 'Inline fixture build failed'}
 if($Test){
  & .\RiumImeFixture.exe --self-test
  if($LASTEXITCODE){throw 'No-window fixture self-test failed'}
 }
} finally {Pop-Location}
if((Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash -ne $sourceHash){throw 'Source candidate changed during fixture build'}
Write-Output "Built own-document fixture; no registration, profile activation or window launch. Candidate=$Dll SHA256=$sourceHash"
