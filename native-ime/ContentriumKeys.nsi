Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!ifndef APP_VERSION
!error "APP_VERSION is required"
!endif
Name "CONTENTRIUM Keys"
OutFile "${OUTPUT}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "..\src\rium-keys.ico"
VIProductVersion "${FILE_VERSION}"
VIAddVersionKey "ProductName" "CONTENTRIUM Keys"
VIAddVersionKey "FileDescription" "CONTENTRIUM Keys"
VIAddVersionKey "CompanyName" "Contentrium"
VIAddVersionKey "LegalCopyright" "Contentrium; Jamotong contributors (MIT)"
VIAddVersionKey "FileVersion" "${FILE_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
!define MUI_WELCOMEPAGE_TITLE "CONTENTRIUM Keys"
!define MUI_WELCOMEPAGE_TEXT "Windows 한글 입력기 ${APP_VERSION}$\r$\n$\r$\n작업 중인 문서를 저장하고 앱을 종료한 뒤 진행하세요.$\r$\n$\r$\nWindows 관리자 확인창에서 예를 누르면 입력기를 설치하고 자동으로 확인합니다.$\r$\n$\r$\n지원 프로그램과 업데이트 안내는 GitHub 저장소에서 확인할 수 있습니다."
!define MUI_FINISHPAGE_TITLE "CONTENTRIUM Keys"
!define MUI_FINISHPAGE_TEXT "설치된 파일을 확인했습니다.$\r$\n$\r$\n열려 있던 앱을 다시 실행하세요. 이전 아이콘이 남으면 Windows에서 로그아웃한 뒤 다시 로그인하세요.$\r$\n$\r$\n입력기 선택: Windows + Space → CONTENTRIUM Keys$\r$\n한글: 가 / 영어·직접 입력: A$\r$\n$\r$\n사용 중에는 별도 설정 창이나 트레이 프로그램을 실행할 필요가 없습니다."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "third_party\jamotong\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPreflightFinish
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "Korean"
Var LockHandle
Var Preflight
Function SkipPreflightFinish
  ${If} $Preflight != ""
    Abort
  ${EndIf}
FunctionEnd
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Windows x64에서만 설치할 수 있습니다."
    SetErrorLevel 1
    Quit
  ${EndIf}
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\CONTENTRIUM.Keys.Setup") p .r0 ?e'
  Pop $1
  StrCpy $LockHandle $0
  ${If} $1 == 183
    SetErrorLevel 10
    Quit
  ${EndIf}
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/PREFLIGHT" $1
  IfErrors normal preflight
preflight:
  StrCpy $Preflight "-Preflight"
normal:
FunctionEnd
Section
  SetOutPath "$PLUGINSDIR\payload"
  File /r "${PAYLOAD}\*"
  ${DisableX64FSRedirection}
  nsExec::ExecToLog '"$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\payload\setup.ps1" $Preflight'
  Pop $0
  ${EnableX64FSRedirection}
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "설치를 완료하지 못했습니다. 관리자 승인 취소, 자동 점검 오류 또는 기존 버전 상태를 확인하세요.$\r$\n$\r$\n진단 로그: %LOCALAPPDATA%\Contentrium\KeysSetup$\r$\n설치 안내: GitHub 저장소의 README" /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetErrorLevel 0
SectionEnd
