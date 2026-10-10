Unicode true
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!ifndef APP_VERSION
!define APP_VERSION "1.1.0"
!endif
Name "CONTENTRIUM Keys"
!ifndef APP_EXE
!define APP_EXE "..\dist\RiumKeys.exe"
!endif
!ifndef OUTPUT
!define OUTPUT "..\release\RIUM-Keys-Setup.exe"
!endif
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\RIUM Keys"
InstallDirRegKey HKCU "Software\Contentrium\RiumKeys" "InstallDir"
RequestExecutionLevel user
SilentInstall silent
SilentUnInstall silent
SetCompressor /SOLID lzma
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "CONTENTRIUM Keys"
VIAddVersionKey "FileDescription" "CONTENTRIUM Keys"
VIAddVersionKey "CompanyName" "Contentrium"
VIAddVersionKey "LegalCopyright" "Contentrium"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
Var WasInstalled
Var LockHandle
!macro AcquireLock FUNCTIONNAME
Function ${FUNCTIONNAME}
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\RiumKeys.Installer") p .r0 ?e'
  Pop $1
  StrCpy $LockHandle $0
  ${If} $1 == 183
    SetErrorLevel 10
    Quit
  ${EndIf}
FunctionEnd
!macroend
!insertmacro AcquireLock .onInit
!insertmacro AcquireLock un.onInit
!macro WaitStopped
  StrCpy $3 0
  ${Do}
    System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\AdobeKoreanShortcuts") p .r0'
    ${If} $0 == 0
      ${ExitDo}
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p r0)'
    Sleep 250
    IntOp $3 $3 + 1
    ${If} $3 >= 40
      SetErrorLevel 11
      Quit
    ${EndIf}
  ${Loop}
!macroend
Section
  ReadRegStr $WasInstalled HKCU "Software\Contentrium\RiumKeys" "InstallDir"
  IfFileExists "$INSTDIR\RiumKeys.exe" 0 stopped
  ExecWait '"$INSTDIR\RiumKeys.exe" --exit'
  !insertmacro WaitStopped
  StrCpy $2 0
wait:
  Sleep 250
  ClearErrors
  Delete "$INSTDIR\RiumKeys.previous.exe"
  Rename "$INSTDIR\RiumKeys.exe" "$INSTDIR\RiumKeys.previous.exe"
  IfErrors 0 stopped
  IntOp $2 $2 + 1
  ${If} $2 < 40
    Goto wait
  ${EndIf}
  SetErrorLevel 11
  Quit
stopped:
  SetOutPath "$INSTDIR"
  ClearErrors
  SetOverwrite on
  File "${APP_EXE}"
  IfErrors rollback
  ClearErrors
  ExecWait '"$INSTDIR\RiumKeys.exe" --health-check' $0
  IfErrors rollback
  ${If} $0 != 0
    Goto rollback
  ${EndIf}
  WriteRegStr HKCU "Software\Contentrium\RiumKeys" "InstallDir" "$INSTDIR"
  ${If} $WasInstalled == ""
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RiumKeys" '"$INSTDIR\RiumKeys.exe"'
  ${EndIf}
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  Delete "$SMPROGRAMS\RIUM Keys.lnk"
  CreateShortcut "$SMPROGRAMS\CONTENTRIUM Keys.lnk" "$INSTDIR\RiumKeys.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "DisplayName" "CONTENTRIUM Keys"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "Publisher" "Contentrium"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "DisplayIcon" "$INSTDIR\RiumKeys.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys" "NoRepair" 1
  Delete "$INSTDIR\RiumKeys.previous.exe"
  Exec '"$INSTDIR\RiumKeys.exe"'
  SetErrorLevel 0
  Goto done
rollback:
  Delete "$INSTDIR\RiumKeys.exe"
  Rename "$INSTDIR\RiumKeys.previous.exe" "$INSTDIR\RiumKeys.exe"
  Exec '"$INSTDIR\RiumKeys.exe"'
  SetErrorLevel 12
done:
SectionEnd
Section "Uninstall"
  ExecWait '"$INSTDIR\RiumKeys.exe" --exit'
  !insertmacro WaitStopped
  StrCpy $2 0
unwait:
  Sleep 250
  ClearErrors
  Delete "$INSTDIR\RiumKeys.exe"
  IfErrors 0 unclean
  IntOp $2 $2 + 1
  ${If} $2 < 40
    Goto unwait
  ${EndIf}
  SetErrorLevel 11
  Quit
unclean:
  Delete "$INSTDIR\RiumKeys.previous.exe"
  Delete "$SMPROGRAMS\RIUM Keys.lnk"
  Delete "$SMPROGRAMS\CONTENTRIUM Keys.lnk"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RiumKeys"
  DeleteRegValue HKCU "Software\Contentrium\RiumKeys" "InstallDir"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
