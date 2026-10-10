$ErrorActionPreference = 'Stop'
$cache = Join-Path $PSScriptRoot '..\upstream-research\toolchain'
$name = 'llvm-mingw-20261006-ucrt-x86_64'
$digest = '317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a'
$archive = Join-Path $cache "$name.zip"
New-Item -ItemType Directory -Force -Path $cache | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri "https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/$name.zip" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest) {
    throw 'Compiler SHA-256 mismatch. The archive was not executed or extracted.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $cache -Force
Write-Output "Verified portable compiler: $(Join-Path $cache $name)"
