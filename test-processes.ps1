param([string]$Exe=(Join-Path $PSScriptRoot 'dist\RiumKeys.exe'))
$ErrorActionPreference='Stop'
$Exe=(Resolve-Path -LiteralPath $Exe).Path
if(Get-Process -Name RiumKeys -ErrorAction SilentlyContinue){throw 'Close the running utility before process tests.'}
function ChildOf([int]$ParentId){Get-CimInstance Win32_Process -Filter "Name='RiumKeys.exe'" | Where-Object ParentProcessId -eq $ParentId}
function WaitChild([int]$ParentId,[int]$Except=0){
    $until=(Get-Date).AddSeconds(10)
    do {Start-Sleep -Milliseconds 200; $child=ChildOf $ParentId | Where-Object ProcessId -ne $Except} until($child -or (Get-Date) -gt $until)
    if(@($child).Count -ne 1){throw 'Expected one worker'}
    return $child
}
$parent=$null
try {
    $parent=Start-Process -FilePath $Exe -ArgumentList '--paused' -WindowStyle Hidden -PassThru
    $child=WaitChild $parent.Id
    $parent.Refresh()
    if($parent.MainWindowHandle -ne 0){throw 'Unexpected visible window'}
    Write-Output 'PASS background startup and single worker'
    Stop-Process -Id $child.ProcessId
    $child=WaitChild $parent.Id $child.ProcessId
    Write-Output 'PASS killed worker recovery'
    Start-Process -FilePath $Exe -ArgumentList '--exit' -WindowStyle Hidden -Wait
    if(!$parent.WaitForExit(4000)){throw 'Graceful exit timeout'}
    Start-Sleep -Milliseconds 400
    if(Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue){throw 'Orphan after normal exit'}
    Write-Output 'PASS normal parent and worker exit'
    $parent=Start-Process -FilePath $Exe -ArgumentList '--paused' -WindowStyle Hidden -PassThru
    $child=WaitChild $parent.Id
    Stop-Process -Id $parent.Id
    $until=(Get-Date).AddSeconds(5)
    do{Start-Sleep -Milliseconds 200; $orphan=Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue}until(!$orphan -or (Get-Date) -gt $until)
    if($orphan){throw 'Orphan after parent termination'}
    Write-Output 'PASS worker exits after abrupt parent termination'
} finally {
    if($parent -and (Get-Process -Id $parent.Id -ErrorAction SilentlyContinue)){Stop-Process -Id $parent.Id}
}

