param(
    [string]$CandidateRoot = (Join-Path $PSScriptRoot 'out\installable'),
    [string]$ReportPath = (Join-Path $PSScriptRoot 'out\package-verification.json')
)
$ErrorActionPreference = 'Stop'
$CandidateRoot = (Resolve-Path -LiteralPath $CandidateRoot).Path
$checks = @()
foreach ($candidateArchitecture in @('x64', 'x86')) {
    $candidateDirectory = Join-Path $CandidateRoot $candidateArchitecture
    $candidateDll = Join-Path $candidateDirectory 'RiumKeysInput.dll'
    $candidateHash = (Get-FileHash -LiteralPath $candidateDll -Algorithm SHA256).Hash
    $candidateSuites=@('EngineTests', 'InlineTests', 'EditSessionTests', 'RoutingTests',
        'InputOwnerTests', 'PendingTests', 'ResendTests', 'RuntimeTests')
    if($candidateArchitecture -eq 'x64'){$candidateSuites+=@('CclOwnerTests','DvaOwnerTests','DvaMonitorTests','DvaSnapshotTests','DvaCaptureTests','AeOwnerTests','AeLifecycleTests')}
    foreach ($candidateSuite in $candidateSuites) {
        $candidateExecutable = Join-Path $candidateDirectory ($candidateSuite + '.exe')
        if (-not (Test-Path -LiteralPath $candidateExecutable -PathType Leaf)) {
            throw "Missing $candidateArchitecture/$candidateSuite. Rebuild both installable architectures before packaging."
        }
        # Routing must exercise this exact candidate DLL. Never pass the
        # diagnostic --expect-known-workspace-bug switch to a package gate.
        if ($candidateSuite -eq 'RoutingTests') {
            $candidateOutput = @(& $candidateExecutable $candidateDll 2>&1)
        } else {
            $candidateOutput = @(& $candidateExecutable 2>&1)
        }
        $candidateExit = $LASTEXITCODE
        $checks += [pscustomobject]@{
            Architecture = $candidateArchitecture
            Suite = $candidateSuite
            ExitCode = $candidateExit
            DllSha256 = $candidateHash
            Output = @($candidateOutput | ForEach-Object { $_.ToString() })
        }
        Write-Output "$candidateArchitecture/$candidateSuite exit=$candidateExit"
    }
    if ((Get-FileHash -LiteralPath $candidateDll -Algorithm SHA256).Hash -ne $candidateHash) {
        throw "The $candidateArchitecture candidate changed during verification. Rebuild and rerun."
    }
}
$failedChecks = @($checks | Where-Object ExitCode -ne 0)
$report = [ordered]@{
    VerifiedAt = [DateTime]::UtcNow.ToString('o')
    CandidateRoot = $CandidateRoot
    ContractChecksPassed = $failedChecks.Count -eq 0
    Scope = 'Native automated contracts only; not physical app, installer, or all-application approval.'
    Checks = $checks
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ReportPath) | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
if ($failedChecks.Count) {
    throw "Package blocked: $($failedChecks.Count) native contract suite(s) failed. Report: $ReportPath"
}
Write-Output "Native contract checks passed. Physical app and installation verification are still required. Report: $ReportPath"
