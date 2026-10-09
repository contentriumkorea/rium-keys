#Requires -Version 7.0
param([string]$Version='1.1.0')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security.Cryptography.ProtectedData
$installer=Join-Path $PSScriptRoot 'release/RIUM-Keys-Setup.exe'
if([Diagnostics.FileVersionInfo]::GetVersionInfo($installer).FileVersion -ne $Version){throw 'Installer version does not match requested signed version'}
$keyFile=Join-Path $env:LOCALAPPDATA 'RiumKeysPublisher/release-key.dpapi'
$plain=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($keyFile),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
$rsa=[Security.Cryptography.RSA]::Create()
try {
    $consumed=0
    $rsa.ImportRSAPrivateKey($plain,[ref]$consumed)
    $metadata=[ordered]@{Version=$Version;Url="https://github.com/contentriumkorea/rium-keys/releases/download/v$Version/RIUM-Keys-Setup.exe";Sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash;Size=(Get-Item -LiteralPath $installer).Length}
    $payload=[Text.Encoding]::UTF8.GetBytes(($metadata | ConvertTo-Json -Compress))
    $signature=$rsa.SignData($payload,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    [ordered]@{Payload=[Convert]::ToBase64String($payload);Signature=[Convert]::ToBase64String($signature)} | ConvertTo-Json -Compress | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'release/update.json')
} finally {[Array]::Clear($plain,0,$plain.Length);$rsa.Dispose()}
