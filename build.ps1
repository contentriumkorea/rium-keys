param([string]$Version='1.0.0')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    foreach($project in @('tests/Tests.csproj','native-tests/NativeTests.csproj','update-tests/UpdateTests.csproj')) {
        dotnet run --project $project
        if($LASTEXITCODE -ne 0){throw "Tests failed: $project"}
    }
    dotnet publish src/AdobeKoreanShortcuts.csproj -c Release -r win-x64 --self-contained true -p:Version=$Version -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=None -o dist
    if($LASTEXITCODE -ne 0){throw 'Publish failed'}
    New-Item -ItemType Directory -Force release | Out-Null
    & ./tools/nsis-3.13/makensis.exe "/DAPP_VERSION=$Version" installer/RiumKeys.nsi
    if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
} finally {Pop-Location}
