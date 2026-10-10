# 구름 입력기와 RIUM Keys 입력 경로 분석

검증일: 2026-10-10. 구름 소스 기준 커밋:
`46c62e51a311c89ee084ce14eb8071b6d81f765d`.
Windows 실측: Premiere Pro 2026, RIUM Keys `2.0.0-preview.4`.

## 결론

구름은 한글을 조합하는 모드와 앱이 사용할 기본 키보드 배열을 분리한다.
일반 Ctrl/Alt/Win 앱 단축키를 원본 이벤트로 전달하는 경로는 RIUM에도 있다.
입력기 자체 단축키와 Shift 조합은 별도로 처리하며, 현재 단독 V는 한글
자모 키로 소비되는 문제가 있다.
그러나 이것만으로 Windows의 모든 프로그램에서 단독 문자 단축키가
작동한다고 결론 내릴 수 없다. 이번 Premiere 시험에서 입력기 쪽 기본
상태 정보는 자막 편집과 타임라인을 구분하지 못했다.

한글 조합 엔진을 libhangul로 교체하거나 커서가 없는 곳에서 한글을
일괄 차단하는 방식은 이 문제의 검증된 해결책이 아니다. 현재 preview.4는
진단판이며 **Premiere 단축키 문제는 미해결**이다.

## 구름 소스에서 확인한 동작

| 항목 | 확인한 구현 | RIUM에 적용할 의미 |
| --- | --- | --- |
| 기본 키보드 | 설정 기본값은 `com.apple.keylayout.ABC`; 입력 모드 설정 시 클라이언트에 `overrideKeyboard` 호출 | 한글 선택 상태와 단축키용 키 배열을 분리한다. 키마다 영어 모드로 전환하는 구현이 아니다. |
| Command/Control | `InputReceiver.input2`에서 `processed: false`, 조합 확정 동작 반환 | 기존 이벤트를 앱으로 전달한다. 키를 삼킨 다음 재입력하지 않는다. |
| 일반 문자 | 물리 keyCode를 바탕으로 한글 조합기와 libhangul에 전달 | 입력기로 전달된 V 자체를 모든 앱의 단축키로 판별하는 테이블은 이 경로에 없다. |
| 입력 종료 | 마우스 이벤트, 입력 서비스 비활성화 시 조합 확정 | 이전 입력 대상의 미완성 글자를 새 패널로 넘기지 않아야 한다. |
| 앱 입력 연결 | `IMKInputController`와 클라이언트 입력 세션을 사용 | Windows의 TSF/IMM 호환 문맥과 동일한 계약으로 볼 수 없다. |

소스 근거:

- [ABC 기본값](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/Configuration.swift#L96),
  [기본 키보드 지정](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/InputReceiver.swift#L275).
- [Command/Control 전달](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/InputReceiver.swift#L35-L53),
  [물리 키 이벤트 전달](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/InputController.swift#L111-L153).
- [일반 문자 조합](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/HangulComposer.swift#L225-L264),
  [마우스/비활성화 조합 확정](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/OSXCore/InputController.swift#L191-L235).

Apple의 [키 이벤트 처리 설명](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/HandlingKeyEvents/HandlingKeyEvents.html)은
앱의 키 처리와 텍스트 입력 처리의 연결을 설명한다. 따라서 macOS 앱이
입력기로 보내기 전에 단축키를 처리하는 경로와 구름의 기본 배열 설정이
함께 작용한다는 해석이 가능하다. 이것은 프레임워크와 소스에 근거한
추론이며 **Mac의 Premiere에서 직접 추적한 결과는 아니다**.

검토한 [Command/Control 테스트](https://github.com/gureum/gureum/blob/46c62e51a311c89ee084ce14eb8071b6d81f765d/GureumTests/GureumTests.swift#L133-L144)는
모의 클라이언트의 qwerty 모드 시험이다. 이 테스트를 한글 모드의 실제
Premiere V/J/K 또는 모든 프로그램에 대한 통과 증거로 사용하지 않는다.
구름 코드를 RIUM 제품에 복사하지 않았고 Mac 실기 시험도 수행하지 않았다.

## 이 PC에서 재현한 두 경로

사용자가 저장/종료한 후 다시 연 자체 빈 시험 프로젝트에서만 조작했다.
Premiere 프로세스가 Program Files의 preview.4 DLL을 불러온 것을 확인했다.

1. Project 검색칸에 한글 입력 → 타임라인 클릭 → C: Razor 도구로
   바뀌지 않고 왼쪽 위 기본 IME 조합창에 `ㅊ` 표시.
2. Type 도구로 영상 위 텍스트 생성 → 물리 g/k/s로 `한` 조합 확인 →
   타임라인 클릭 → V: Selection 도구로 바뀌지 않고 `ㅍ` 표시.

검색과 자막 입력 모두에서 사용자가 설명한 실패를 재현했다. 시험 도중
초기 프로젝트 로딩이 응답 없음으로 보였으나 강제 종료하지 않았고 이후
회복했다. 기존 비정상 종료 알림만으로 이번 입력기를 원인으로 단정하지 않는다.

### 같은 문맥으로 나타난 다른 동작

수집된 샘플에서 동일한 TSF context와 HIMC 값이 관찰됐다. 로그는 100ms
간격 제한과 중복 생략을 사용하므로 사이의 모든 전환을 증명하지 않는다.
입력 텍스트를 읽지 않는 opt-in 메타데이터 로그로 아래 값을 관찰했다.

| 관찰 항목 | 검색 EDIT | 영상 위 자막 편집 | 타임라인 |
| --- | --- | --- | --- |
| TSF static flags | 4 (transitory) | 4 | 4 |
| TSF dynamic flags | `0x40000000` | `0x80000000` | `0x80000000` |
| 기존 restrictions 판정 | 허용 | 허용 | 허용 |
| 네이티브 caret | 있음 | 없음 | 없음 |
| Win32 class | Edit | DroverLord - Window Class | DroverLord - Window Class |
| IMM composition style | 2 | 0 | 0 |
| IMM candidate geometry | 제공 안 됨 | 제공 안 됨 | 제공 안 됨 |

자막과 타임라인의 HWND/크기는 달랐지만 의미를 보장하는 편집 여부 신호가
아니다. HWND, 크기, 패널명, 프로그램명을 규칙으로 외우지 않는다.
관찰한 상위 dynamic 비트는 공개된 편집 여부 계약으로 확인하지 못했으며,
자막/타임라인 값도 같으므로 분류 규칙으로 채택하지 않는다.
공개 정의는 [TS_STATUS](https://learn.microsoft.com/en-us/windows/win32/api/textstor/ns-textstor-ts_status)를 참고한다.

Windows 접근성 스냅샷도 이 시험의 자막 편집과 타임라인 모두에서 포커스를
`TopLevelWindow`로 보고했다. 이것만으로 모든 UIA 패턴이 없다고 단정할 수는
없지만, 최상위 포커스의 역할 하나로 분류하는 방식은 근거가 부족하다.

별도 진단 프로세스에서 문자 위치만 요청한 `IMR_QUERYCHARPOSITION` 한 번은
100ms 제한에서 timeout(1460)이었다. 실패를 비편집 상태로 해석하지 않는다.
이 결과는 앱의 해당 메시지 지원 여부나 TSF `GetTextExt`의 결과를 증명하지
않는다. 진단 뒤 앱 응답과 화면은 정상 확인했다. 키 처리 경로에 이 질의를
넣지 않았다.

### 호스트 스레드에서 추가 확인한 결과

같은 시험 프로젝트에서 입력 문자열을 읽지 않고 선택 상태와 좌표만 비교했다.
아래 값은 해당 시점의 관찰이며 앱의 모든 입력 상태를 포괄하지 않는다.

| 질의 | 영상 위 자막 편집 | 타임라인 |
| --- | --- | --- |
| `WM_GETDLGCODE` | 0 | 0 |
| `WM_IME_SETCONTEXT` 표시 플래그 | `0xc000000f` | `0xc000000f` |
| TSF context | `000002C700B6E040` | `000002C700B6E040` |
| TSF 읽기 세션 / 선택 / 좌표 HRESULT | 모두 `S_OK` | 모두 `S_OK` |
| 선택 개수 / interim / anchor | 1 / 1 / 0 | 1 / 1 / 0 |
| `GetTextExt` RECT | `1919,1031,1920,1031` | `1919,1031,1920,1031` |
| 좌표 clipped | 0 | 0 |

자막 편집에서 `IMR_QUERYCHARPOSITION`은 초기화한 `IMECHARPOSITION`
구조체(`dwSize=sizeof`, `dwCharPos=0`)를 전달했으며 0을 반환했다.
`IMR_DOCUMENTFEED`의 NULL 버퍼 크기 조회도 0이었다. QUERYCHARPOSITION을
NULL로 조회한 시험이 아니다. 두 항목을 타임라인과 구분되는 신호라고 주장하지 않는다.
TSF view HWND는 두 위치에서 달랐지만, 앞서 확인한 HWND 차이와 마찬가지로
편집 여부를 보장하지 않는다. 좌표는 양쪽 모두 높이가 0이었다.

TSF 비교는 기존 스레드 관리자를 `TF_GetThreadMgr`로 얻고, 진단 클라이언트의
`Activate`/`Deactivate`를 짝지은 동기 읽기 세션으로 실행했다. 두 호출 모두
완료와 참조 해제를 확인했다. 임시 관찰 훅도 해제됐으며 상주 진단 프로세스는
남아 있지 않다. 제어 프로세스의 대기 제한이 호스트 내부 동기 호출을 취소하거나
실행시간을 보장하는 것은 아니다. 이 진단 호출을 입력기의 키 처리에 넣지 않았다.

따라서 **이번에 확인한 공개 메타데이터에서는 자막 편집과 타임라인을 구분할
근거를 확보하지 못했다**. 모든 가능한 Windows 신호가 같거나 해결이 원리적으로
불가능하다는 증명은 아니다.

## 구현 경계와 다음 검증

- `OnTestKeyDown`/`OnKeyDown`에서 `pfEaten=FALSE`로 반환하면 원래 키를
  앱이 처리할 수 있다. [Microsoft 계약](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-keydown).
  현재 RIUM의 일반 Ctrl/Alt/Win 앱 단축키 전달 경로도 이 방식이다.
- writable 문맥과 restrictions 부재만으로 편집 중이라고 판단해서는 안 된다.
  반대로 transitory, caret 없음, metadata 질의 실패를 일괄 차단해도 안 된다.
- 실제 TSF 읽기 세션의 선택/텍스트 위치 비교도 위 두 상태를 구분하지 못했다.
  조합 전 자막, 조합 중 자막, 검색, 타임라인을 모두 구분한다는 근거를 확보하기
  전에는 제품 분기 조건으로 적용하지 않는다.
  읽기 세션의 선택/좌표 조회는 문서화된 진단 방법이지만 편집 의도를
  판별하는 계약은 아니다. `TS_E_NOLAYOUT`, 빈 좌표, 세션 거절은 비편집
  증거가 아니며, 비동기 결과가 첫 문자 전에 도착한다는 보장도 없다.
  동기 세션에는 실행시간 상한이 없고 `TF_ES_ASYNCDONTCARE`도 동기로
  실행될 수 있다. [세션 계약](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession),
  [좌표 계약](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextview-gettextext).
- 키 콜백에서 UIA 전체 탐색, 다른 스레드 응답 기다리기, 키 재주입,
  포커스마다 한영 전환을 추가하지 않는다. 특히 같은 스레드의
  [SendMessageTimeout](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)는
  timeout으로 실행 시간을 제한하지 못한다.
- 확보한 신호가 끝내 구별되지 않으면 앱 협력 없이 모든 레거시 앱을
  자동 지원한다는 약속은 성립하지 않는다. 앱별 규칙이나 수동 모드를
  사용자의 요구를 충족한 것처럼 대체하지 않는다.

검색/이름 변경 및 자막 입력 → 다른 패널/앱 → 첫 단축키 → 다시 한글 입력을
Premiere와 After Effects에서 실제로 통과하기 전에는 public release나
자동 업데이트로 배포하지 않는다. 현재 공개판 1.1.1과 로컬 preview.4의
검증 상태는 별개다.

## 별도 소스 수정의 범위

개발 소스에는 이전 조합과 지연 확정 문자를 원래 포커스 HWND에 묶는 보완을
추가했다. 같은 TSF 문맥이 다른 HWND에 재사용되어도 이전 글자를 새 입력창에
넣지 않으며, 세션 내부 삽입 실패를 요청 성공으로 오인하지 않도록 수정했다.
호출 중 포커스 변경, 실패한 지연 문자 유지, Escape 복구, 실제로 다른 문서로
이동했을 때 원래 문서 확정을 회귀 시험한다.

이 수정은 **단축키와 한글 입력의 자동 구분을 해결하지 않는다**. 아직 별도
개발 빌드에서만 시험했으며 설치·배포하지 않았다. 지원되지 않는 호스트에서
확정하지 못한 문자가 남으면 원래 입력 위치로 돌아오거나 Escape로 폐기할
때까지 새로운 한글 조합을 보류하는 한계도 있어 일반 배포 대상이 아니다.
로컬 설치본은 preview.4이고, 정상 모드의 단독 V 테스트 실패를 그대로 남겨
해결된 것으로 잘못 표시하지 않는다.

## 추가 해결 경로 조사 — 2026-10-10 15:22 KST

### 새로 배제한 방법: 문자 메시지만 골라 한글로 조합

같은 QA 프로젝트에서 입력기는 교체하지 않고 영문 모드의 원래 키 경로를
비교했다. 관찰기는 선택한 Premiere PID `197312`, UI thread `76728`에만
붙였고, `WH_GETMESSAGE`의 `PM_REMOVE`와 `WH_CALLWNDPROC`를 관찰했다.
키나 메시지를 변경·차단·재주입하지 않았다. 문자 본문 대신 메시지 종류와
시험 키 C/V와의 일치 여부만 기록했다. 임시 검색어만 입력했고 프로젝트의
영상·자막 내용이나 단축키 설정은 변경하지 않았다.

| 시험 | 화면에서 확인한 결과 | 큐에서 확인한 결과 |
| --- | --- | --- |
| 검색칸, 영문 C | `c`가 검색어에 추가됨 | `WM_KEYDOWN(C)` → `WM_CHAR(c)` → `WM_KEYUP(C)` |
| 타임라인, 영문 C | Razor 도구 선택 | 같은 세 메시지 |
| 타임라인, 영문 V | Selection 도구 선택 | `WM_KEYDOWN(V)` → `WM_CHAR(v)` → `WM_KEYUP(V)` |

이 표는 메시지 종류·시험 키가 같다는 의미다. HWND와 시각까지 같은 것은
아니다. 이번 관찰에서 `WM_CHAR` 유무는 문자 입력과 단축키를 구분하지
못했다. **따라서 키를 먼저 통과시킨 뒤 `WM_CHAR`가 발생한 경우에만 한글을
조합하는 안은 Premiere의 이 경로에 적용할 수 없다.** `WM_CHAR`가 큐에서
꺼내졌다는 사실은 앱이 그 문자를 텍스트로 받아들였다는 확인도 아니다.

관찰은 23행에서 종료했고 `queueUnhook=1`, `dispatchUnhook=1` 및 제어
프로세스 종료를 확인했다. Premiere도 응답 상태였다. 비교 시험 후 한글
모드에서 물리 C가 `ㅊ`으로 입력되는 것을 확인하고 임시 검색어를 지웠다.
Selection 도구와 타임라인 포커스로 복구했다. 이것은 원래의 한글 모드
단축키 문제를 고친 시험이 아니다.

로컬 임시 근거 폴더: `%TEMP%\rium-routing-research-20261010`.
`premiere-message-route.log`의 SHA-256:
`A08EB44DD9B8425BD7E274BD3273AB14B468FDABF5C91BCD3D07B4FFF52708DA`.
관찰 소스 `message-route-observer.c`의 SHA-256:
`73CBBA640E14DFA3DA2130F1BB3206F1A63B3EC25238A17A54031D757E606758`.
둘 다 임시 진단 자료이며 제품 빌드·설치 파일에 포함하지 않는다.

### 공식 계약으로 가능한 것과 아직 확보하지 못한 것

1. **앱이 단축키를 먼저 처리하도록 협력하는 경로는 존재한다.**
   [ImmGetVirtualKey](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetvirtualkey)는
   `WM_KEYDOWN(VK_PROCESSKEY)`의 원래 VK를 앱의 `TranslateMessage` 호출
   전에 얻는 API다. 이는 앱의 IMM 메시지 루프에서 사용해야 하는 계약이며,
   TIP이 키를 소비한 뒤 `pfEaten`만 바꾸면 복구된다는 뜻은 아니다.
   [TranslateAccelerator](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translateacceleratorw)는
   처리 여부를 반환하지만 앱의 accelerator table이 필요하다. Premiere의
   사용자 단축키가 이 테이블을 사용한다는 근거는 확보하지 못했다.
   [TranslateMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translatemessage)의
   반환값은 문자 입력 수락 여부가 아니므로 편집 판정에 쓰지 않는다.

2. **표준 접근성에는 더 구체적인 편집 상태를 확인할 후보가 있다.**
   [TextPattern2.GetCaretRange](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextpattern2-getcaretrange)의
   `isActive`는 해당 텍스트 컨트롤의 키보드 포커스를 뜻한다. 이것도 쓰기
   가능한 편집기라는 보장은 아니므로 read-only 속성 등과 함께 확인해야
   한다. MSAA의 focused child/role/state도 별도 후보다. 이전 트레이의
   ValuePattern/TextPattern·상위 역할 조회는 이미 구현돼 있었다. 이를
   다시 추가하는 것을 새 해결책이라고 부르지 않는다. 이 추가 패턴들이
   Premiere 자막에서 유효하다는 실측은 아직 없다. 미지원·지연·실패는
   비편집 증거가 아니다. 조회는 키 처리와 분리해야 한다.

3. **공개 UXP API만으로 Premiere 본체의 텍스트 포커스를 알 수 있다고
   약속할 근거는 찾지 못했다.**
   [공식 타입 선언](https://github.com/adobe/premierepro-types/blob/c8f108941197c1d987f08b9916c0d18a2e252699/src/premierepro.d.ts)의
   이벤트와 메서드를 조사했다. 프로젝트·시퀀스·선택 변경은 있지만 검색칸과
   영상 위 자막의 실제 편집 시작/종료를 알려주는 명시적 계약은 확인하지
   못했다. [UXP HTML의 focus/keydown](https://developer.adobe.com/premiere-pro/uxp/resources/recipes/html-events/)는
   확장 패널 자체의 DOM 이벤트이며 Premiere 본체 전체의 포커스 API로
   해석하면 안 된다. C++ SDK나 비공개 인터페이스가 전혀 없다는 증명은 아니다.

4. **다른 한글 조합 엔진으로 교체하는 것은 키 분배 문제의 해결 증거가
   아니다.** 조사한 [새나루의 TSF 키 처리](https://github.com/wkpark/saenaru/blob/3808b205be8463f2657a1c09ce94b448ba722de8/tip/keys.cpp#L708-L824)도
   한글 자모에 해당하면 키를 소비한다. [Weasel의 키 처리](https://github.com/rime/weasel/blob/6f9e0124bd8ad5b7e2af36280f6c5be8220301c6/WeaselTSF/KeyEventSink.cpp)는
   입력 활성 상태와 엔진 응답으로 소비 여부를 결정한다. 이 파일들에서
   Premiere의 편집 의도를 자동으로 아는 범용 방법을 발견하지 못했다.
   해당 입력기 전체가 모든 앱에서 실패한다는 주장이나 실기 비교 결과는 아니다.

같은 Windows/Korean/Japanese 단축키 증상은
[Adobe 커뮤니티의 장기 보고](https://community.adobe.com/bug-reports-728/shortcuts-cannot-be-used-when-entering-japanese-korean-on-windows-1330379/)에도
있다. 사용자 보고는 재현 범위를 이해하는 자료이며, 플랫폼상 해결 불가능을
증명하거나 최신 Premiere의 상태를 보증하는 자료로 쓰지 않는다.

### 개발 방향과 판정 기준

현재 권고는 **공통 TSF 입력 엔진을 유지하고, 실제 텍스트 입력 대상으로
연결되는 계층을 분리해 검증하는 것**이다. 프로그램명·단축키·화면 좌표를
계속 나열하는 설계는 채택하지 않는다. 표준 입력 컨트롤은 하나의 경로로
지원하고, 표준 정보를 제공하지 않는 프레임워크만 별도 연결 가능성을
조사한다. 사용자에게 앱별 규칙을 설정하도록 요구하는 방향은 아니다.
이는 설계 권고이며 모든 프레임워크에 그러한 연결점이 있다는 보장은 없다.

- 첫 실험: 자막 편집 전·중, 검색, 이름 변경, 타임라인에서 동일한 포커스
  세대에 속하는 표준 편집 이벤트/패턴을 수집한다. `TextPattern2.isActive`,
  쓰기 가능 여부, MSAA focused child를 기존 HWND·TSF 정보와 비교한다.
  첫 키 이전에 구분이 되지 않으면 이 경로를 제품 분기 조건으로 쓰지 않는다.
  같은 HWND 안의 전환도 포함하고, 오래된 비동기 결과를 새 포커스에 적용하지 않는다.
- 두 번째 후보는 **원래 키와 한글 조합을 각각 앱의 올바른 처리 경로에
  연결하는 호환 모듈**이다. `ImmGetVirtualKey`와 앱의 처리 결과를 이용할
  수 있는지 별도 시험해야 한다. 단순히 원래 키와 한글을 둘 다 보내면
  입력 중 단축키 실행, 영문 중복, 조합 취소가 생길 수 있다. 원래 키 복원
  API의 존재만으로 이 방식이 완성됐다고 주장하지 않는다. 실제 소비 결과를
  확인할 수 없으면 일반 배포하지 않으며, 임의 지연으로 해결하지 않는다.
- 두 경로 모두 신호를 확보하지 못하면 Adobe의 입력 처리 협력이 필요하다.
  UXP 플러그인 하나로 해결된다고 약속할 단계가 아니다. 사용자의 모든 앱
  자동 지원 요구를 만족했다고 표시하거나 영어 모드 고정을 대체안으로
  설치하지 않는다.

최초 통과 조건은 한글 모드를 바꾸지 않고 `검색/이름 변경/자막에 한글 입력`
→ `타임라인의 첫 C/V/J/K/L` → `다시 첫 한글 음절`이 모두 맞는 것이다.
문자 입력 중 명령 실행, 첫 키 누락, 영문 중복, 작업 영역의 조합창, 다른
문서로의 지연 삽입은 각각 실패다. 그 뒤 빠른 패널·앱 전환과 키 반복을
확장 시험한다. 키 콜백에서 UIA/IPC/동기 외부 호출을 기다리는 구현은 제외한다.

이번 추가 조사는 해결 후보와 탈락 기준을 좁혔으며 **범용 해결책 확보나
수정 설치 완료를 뜻하지 않는다**. 설치본·자동 업데이트·공개 배포는 변경하지 않았다.

## 원래 키 전달과 IME 메시지 위임 실험 — 2026-10-10 16:34 KST

같은 Premiere QA 프로젝트와 설치된 `2.0.0-preview.4`에서 세 경로를 비교했다.
임시 DLL은 선택한 UI 스레드에만 붙고 시간 제한 후 해제되며 제품에 포함하지
않는다. C/V만 시험했고 Ctrl/Alt/Win 조합은 건드리지 않았다.

| 경로 | 실제 관찰 | 판정 |
| --- | --- | --- |
| 꺼낸 `VK_PROCESSKEY`를 원래 VK로 변경 | 검색 C가 한글 대신 영문 `c`로 입력됨 | 탈락: 한글 조합 경로를 건너뜀 |
| `WM_IME_KEYDOWN`으로 원래 키 재전달 | 타임라인 Razor는 선택되지만 조합창이 남고, 검색 C는 `cㅊ` 중복 | 탈락: 재생성된 키가 영문 문자도 생성 |
| 원래 `WM_KEYDOWN`을 앱에 동기 전달, 큐의 `VK_PROCESSKEY`는 보존 | 검색 C는 `ㅊ`만 입력, 자막 V는 `ㅍ`만 입력되고 Type 도구 유지, 타임라인 V는 Selection 도구 선택 | 부분 성공: 타임라인에 `ㅍ` 조합창은 여전히 남음 |

마지막 실험은 원래 키를 앱의 문자 해석과 분리해 전달하면 일부 호스트가
자체 편집 상태에 맞게 처리한다는 실측이다. 첫 키·반복·다른 앱·커스텀 단축키
전체를 검증한 결과는 아니다. 특히 작업 영역의 숨은 조합을 그대로 두거나
창만 숨겨 해결됐다고 할 수 없다. 후속 확정이 잘못된 위치에 들어갈 위험을
따로 해소해야 한다. `WM_IME_KEYDOWN` 실험의 최초 검색 입력은 관찰기 준비
전에 발생했으므로 결과에서 제외했고, 이후 실제 로그가 있는 검색 입력만
위 표에 사용했다.

### 앱이 기본 IME 창으로 넘기는 실제 플래그

기존 진단은 앱으로 들어가는 `WM_IME_SETCONTEXT`만 봤다. 별도 읽기 전용
관찰기에 `WH_CALLWNDPROCRET`를 추가해 앱 처리 후 기본 IME 창과
`MSCTFIME UI`로 전달되는 메시지도 비교했다. 입력 본문은 읽거나 기록하지
않았다. 기본 창은 클래스 추정이 아닌 `ImmGetDefaultIMEWnd`와 대조했다.

| 대상 | 활성화 때 기본 IME 창에 전달한 표시 플래그 | 첫 한글 조합의 전달 |
| --- | --- | --- |
| 프로젝트 검색 `Edit` | `0x4000000f`: 기본 조합 UI 표시 비트 해제 | 검색칸에서 처리, 기본 IME 창으로 조합 위임 없음 |
| 영상 위 실제 자막 편집 | `0xc000000f`: 표시 비트 유지 | 자막에서 처리, 기본 IME 창으로 조합 위임 없음 |
| 타임라인 작업 공간 | `0xc000000f`: 표시 비트 유지 | `IME`와 `MSCTFIME UI`로 조합 전달, 왼쪽 위 팝업 |

따라서 **최종 `WM_IME_SETCONTEXT` 플래그도 자막과 타임라인을 구분하지
못했다.** 조합 메시지 위임 여부는 관찰한 두 상태에서 달랐지만, 첫 키 처리
이전 신호가 아니다. 정상적인 외부 조합창 방식의 편집기도 기본 IME 창을
사용할 수 있으므로 이를 모든 앱의 비편집 증거로 일반화하지 않는다.
[Microsoft의 표시 플래그 계약](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-setcontext)은
앱이 자체 조합 UI를 그릴 때 비트를 해제하도록 설명하지만, 이번 자막 경로는
그 비트만으로 입력 상태를 판단할 수 없음을 보여준다.

### Studio One 실기와 공통 프레임워크 조사

설치된 Studio One `6.6.4.102451`의 시작 화면 Recent Files 검색칸에서는
실제 Windows `Edit` HWND와 네이티브 caret이 생겼다. 바깥 작업 영역에서는
`CCLWindowClass`, caret 없음이었다. 빈 곡 `RIUM Input QA`는 저장 위치를
저장소 `qa/studio-one`으로 지정해 생성했다. 사용자 곡은 열지 않았다.
빈 편집 화면의 한글 C에서 왼쪽 위 `ㅊ` 조합창이 재현됐다. 이후 원래 키 전달
실험은 사용자 작업과 창 전환이 겹쳤고 로그가 영문 키 경로였으므로 **한글
상태에서 Studio One 단축키가 복구됐다는 증거로 사용하지 않는다.**

CCL 공개 소스는 일반 입력칸에 Win32 EDIT를 만들고 포커스를 주는 구조다.
설치본 전체가 공개 소스와 동일하다는 뜻은 아니지만 위 실측과 해당 경로가
일치한다. [CCL Windows 입력칸](https://github.com/cclsoftware/ccl-framework/blob/ad5876b1a7b64aa247f4378c86a7862e7c284ba3/ccl/platform/win/gui/textbox.win.cpp#L122-L155).
반면 `WCH_NONE` 키보드 배열로 영문 문자만 없애는 안은 CCL처럼 `ToUnicode`
결과로 문자 단축키를 조회하는 앱에서 실패할 수 있다.
[CCL 키 변환](https://github.com/cclsoftware/ccl-framework/blob/ad5876b1a7b64aa247f4378c86a7862e7c284ba3/ccl/platform/win/gui/keyevent.win.cpp#L225-L298).

### 정리와 근거

모든 임시 키 전달 훅은 해제됐다. 마지막 위임 관찰기는 256행,
`queueUnhook=1 dispatchUnhook=1 returnUnhook=1 dropped=0 writers=0`으로
종료했다. 시험 검색어를 비우고 자막에 추가한 한 글자는 Undo로 되돌린 뒤
Selection 도구로 복구했다. Premiere 프로젝트를 저장하거나 실제 작업
프로젝트를 편집하지 않았다. 설치본·기본 입력 프로필·공개 배포는 변경하지 않았다.

근거는 `%TEMP%/rium-routing-research-20261010`의 임시 파일이다.

| 파일 | SHA-256 |
| --- | --- |
| `bridge-mode1.log` | `74DFD39AC1BF58941788497EC571F7A1E506F6F2F7ADA230CDDECBE618ACFFE4` |
| `bridge-replay.log` | `D8C0E01314FF9A0A1C63C97EF8B8433FB993AE88DC105FA14A480AE1BBCEC500` |
| `bridge-direct.log` | `6A8FC4F2D4594099F7E491F5D805242A5A97A918BED80DC54C5E916C7B636CC3` |
| `premiere-delegation.log` | `33E11A4FAA7C6677036BCAD1EDBC0577D2866AB729CEF59B2981D3B0B965CDA9` |
| `message-direct-bridge.c` | `0E62441646E7180E7986F9F7888B06C928C876F66608D7ABEE95A15313D88CEA` |

### 후속 검증의 제한과 준비

TSF의 transitory parent document 연결도 별도로 읽는 관찰기를 만들었다.
이미 존재하는 스레드 매니저와 compartment만 조회하며, 활성화·편집 세션·
문서 텍스트 조회는 하지 않는다. Premiere가 비활성인 상태의 첫 실행에서는
`GetFocus=S_FALSE`, `docs=0`만 얻었으므로 검색·자막·타임라인을 구분하는
근거가 되지 않는다. 종료 로그는 `queueUnhook=1 returnUnhook=1`이었다.
실제 포커스가 있는 세 상태의 비교가 필요하다.

후속 임시 실험은 원래 키를 전달한 뒤 **첫 preedit 메시지만** 앱에 한 번
보내고, 그 동기 호출 중 기본 IME 창에 정확히 같은 메시지가 전달되는지
관찰한다. 결과 문자열이 포함된 메시지는 실험 대상으로 삼지 않는다.
`DefWindowProc`이 기본 IME 창에 전달하기 전에 결과를 `WM_IME_CHAR`로
이미 보낼 수 있기 때문이다.
[Microsoft 예제](https://github.com/microsoft/Windows-classic-samples/blob/434f6002bdf9cf9829406c3ff2b33387982d6168/Samples/Win7Samples/winui/input/tsf/tsfapps/tsfpad-step1/TextInputCtrl.cpp#L145-L159).

위임되지 않았다는 사실도 실제 입력 수락의 증명은 아니다. 텍스트 대상이
없어도 IME 메시지를 삼키는 호스트가 있으며, 정상적인 offspot 편집기는
기본 IME 창을 필요로 한다. 이 실험은 검증한 inline 호스트에서의 가능성을
확인하는 용도이며 범용 입력 상태 판별기로 취급하지 않는다.
[Chromium IMM 구현의 반례](https://github.com/chromium/chromium/blob/a2c7b5b9e8890b89397692007ff26e42784c3acf/ui/base/ime/input_method_win_imm32.cc#L350-L380).

Studio One 후속 실기는 빈 QA 곡에서 `C`의 Click(메트로놈) 상태를 확인한다.
`V`는 Copy Range to new Scratchpad이므로 무작정 같은 시험 키로 사용하지
않는다. 이 기본 매핑은 [PreSonus 공식 키 명령표](https://pae-web.presonusmusic.com/downloads/products/pdf/STUDIO_ONE_-_KEY_COMMANDS_SHEET.pdf)와
대조했으며, 설치본의 사용자 지정 여부는 실제 화면에서 별도로 확인한다.

원래 키를 `SendMessage(WM_KEYDOWN)`으로 전달하는 방법 자체에도 범위가
있다. 이는 같은 스레드의 창 프로시저를 직접 호출하므로 메시지 루프의
`TranslateAccelerator`나 MFC `PreTranslateMessage`는 거치지 않는다.
Premiere에서 얻은 결과는 창 프로시저에서 처리되는 명령의 실측이며, 별도
루프에서 명령을 소비하는 모든 앱의 단축키 복구 증거가 아니다.
[SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage),
[Windows accelerator loop](https://learn.microsoft.com/en-us/windows/win32/learnwin32/accelerator-tables).

임시 실험은 한 번의 preedit 이후 해제하며, 후속 RESULT 메시지를 광범위하게
버리지 않는다. 따라서 그 자동 시험의 통과는 늦은 결과 전체의 안전한 세대
분류나 반복 입력 지원을 입증하지 않는다. 실제 한 키의 취소 완료와 후속
포커스 이동을 확인한 뒤 반복·연속 조합 수명 관리를 별도로 검증해야 한다.

### 관찰 후 취소하는 격리 프로토타입

재현 가능한 소스를 `native-ime/experiments/inline-preedit`에 보관했다.
제품 빌드·설치기·자동 업데이트와 연결하지 않았다. `build.ps1 -Test`가
독립 출력 폴더에 빌드하고, 메시지 전용 자체 창을 사용하는 native fixture
34개 및 창/훅 없는 DLL 로드·export·해제 검사를 실행한다. 부모 실행에서
모두 통과했으며 fixture 자체 훅 해제도 확인했다.

최초 설계의 기본창 표시 억제는 폐기했다. 표시를 막은 뒤 포커스 변경,
subclass 제거 실패 또는 취소 실패가 생기면 조합만 보이지 않게 남을 수
있기 때문이다. 현재 버전은 기본 IME에 메시지를 항상 전달하고, 앱 호출이
끝나고 subclass가 제거된 뒤 소유권 조건이 유지될 때만 한 번 취소한다.
실패하면 일반 조합 상태가 그대로 보인다. 취소 후에는 내용을 읽지 않고
`GCS_COMPSTR` 바이트 길이만 기록한다.

컨트롤러는 명시적 실험 플래그, 선택한 PID/스레드/정확한 포커스 HWND,
최대 120초를 요구한다. C/V의 무수정 첫 키만 복원하고, 실행당 한 preedit
이후 재무장하지 않는다. 외부 앱에서의 실제 취소 효과, 첫 글자 보존,
늦은 결과 누출 여부 및 반복 입력은 **아직 검증 전**이다. 34개 fixture는
제어된 IMM 모델의 동작 시험이므로 실제 한글 입력기 동작으로 해석하지 않는다.

숨긴 별도 testhost를 이용한 외부 컨트롤러 smoke는, 활성화를 막은 상태에서
포커스가 성립하지 않아 실행하지 않았다. 그 testhost는 자체 창과 CBT 훅을
정리하고 종료했다. 따라서 DLL을 실제 대상 UI 스레드에 연결한 컨트롤러의
전체 검증까지 완료했다고 주장하지 않는다. 앞선 단독 화면 시험 시간은
종료했으며, 후속 Premiere/Studio One 실기는 사용자 작업과 겹치지 않는
추가 시험 시간을 확인한 뒤 진행한다.

### 후속 실제 앱 시험과 배포 판정

사용자가 검증·재설치·배포를 요청한 뒤, 같은 날 자체 시험 프로젝트에서
위 one-shot 컨트롤러를 실제로 연결했다. 직전 절의 미검증 항목 중 다음
범위는 확인했다. 설치된 입력기 자체의 개선으로 혼동하지 않는다.

| 실제 시험 | 관찰 결과 |
| --- | --- |
| Premiere 검색칸 C | 한글 모드 `open=1 conversion=1`, `ㅊ`만 입력 |
| Premiere 타임라인 C | Razor 선택, 기본 IME 위임 관찰, 취소 TRUE, 조합 길이 0 |
| 타임라인 시험 후 검색 복귀 | 검색칸이 비어 있음, 컨트롤러 종료 후 C는 `ㅊ`만 입력 |
| Premiere 영상 위 자막 V | Type 도구 유지, `ㅍ` 한 글자 입력; Undo로 원래 자막 복구 |
| Studio One 6.6.4 작업 영역 C | 한글 모드에서 Click/메트로놈 토글, 취소 TRUE, 조합 길이 0 |
| Studio One 같은 프로세스에서 3회 | 매번 별도 one-shot 재시작, 세 번 모두 위 결과; 상시 실행 검증은 아님 |
| Studio One 검색 EDIT C | 실험 연결·미연결 양쪽 모두 `ㅊ`만 입력, 잔여 작업 영역 글자 없음 |

최초 Studio One 시도는 영어 모드였으므로 한글 호환 성공에 포함하지 않았다.
한글 모드 전환 뒤의 기록에는 `VK_PROCESSKEY`와 `open=1 conversion=1`이
확인된다. 검색칸에서 발생한 `AbortReentry`는 취소를 중단하는 안전 분기다.
입력이 실제로 보존됐다는 판단은 화면 관찰에 근거하며, 그 분기 자체를
앱의 텍스트 수락 신호로 승격하지 않는다.

각 컨트롤러의 두 훅 해제, `writers=0`, 프로세스 종료를 확인했다. 모든 검색
시험 문자를 비우고 자막 시험 문자를 되돌렸다. 프로젝트 저장, 사용자 작업
프로젝트 편집, 입력 프로필 등록/변경, 입력기 설치, 공개 릴리스 변경은 하지 않았다.

다음은 `%TEMP%/rium-routing-research-20261010`의 메타데이터 로그 해시다.

| 파일 | SHA-256 |
| --- | --- |
| `live-search.log` | `F0EB07148B4667C98899195AAA92B187FE123DDF819D2FEC00B1A3977D9F31C7` |
| `live-timeline.log` | `0277114BAC349BA44378F869358B594DE228105A07533837B7E7587D2D96259A` |
| `live-caption.log` | `7E67F58C609BD17077E247EF5FAA7F571FDA06AE1C09990B43BEE9B4D04D3ADB` |
| `live-studio-korean-c.log` | `D422BADB02CD4403C525D18A3C48AE471D1CC15CDE6C5E4EC3CCBCB1BCBAB0E9` |
| `live-studio-repeat2.log` | `A4B40FC1CF6CDAE68070C7314D2A5DC758AB09BB7CA77AFA6262CAE9B89E24ED` |
| `live-studio-repeat3.log` | `B9C5065071185B9A16B015AAB0E64C34B5F6E52DC2C6A1B638503FC05DE80B3D` |
| `live-studio-search.log` | `AC8FD1B78789F60F7AE6D9BCE9325BACDA8656412F5E519A70865B66093FEAFC` |

#### 범용 정책의 반례와 설치 차단

같은 fixture에 별도 `--release-gate`를 추가했다. 이 검사는 실제 앱 재현이
아닌, 정상적인 Windows 메시지 처리 모델 두 가지에 대한 입력 보존 조건이다.
실험을 끈 대조군은 정상 입력을 확정한다. 실험을 켜면 다음 조건이 실패한다.

1. 별도 조합창을 사용하는 정상 편집기는 기본 IME 창에 preedit를 위임한다.
   이를 비텍스트 작업 영역으로 취급하면 진짜 입력할 글자가 취소된다.
2. raw 문자 키 명령과 IME 텍스트를 모두 처리하는 편집기는 한글 입력 중
   명령도 실행한다. 원래 키를 먼저 보낸 후 뒤늦게 취소를 포기해도 그 명령은
   되돌릴 수 없다.

따라서 HWND/HIMC·조합 세대를 정확하게 묶는 보강만으로 이 정책을 모든 앱에
적용할 수 없다. 창 클래스 이름이 같다는 이유만으로 같은 텍스트 계약이라고
가정하는 것도 반례를 해결하지 않는다. 이는 이 방식의 범용성 실패이며,
모든 가능한 Windows 구현의 불가능성을 증명한다는 뜻은 아니다.

별도로 최신 소스를 x64/x86 fixture 및 installable 후보로 다시 빌드했다.
양쪽 모두 engine 26, inline 76, edit-session 15개는 통과했으나 routing
86개 중 `transitory workspace without text focus passes first V` 한 개는
실패했다. 알려진 버그를 기대하는 진단 플래그는 배포 통과에 사용하지 않았다.

`native-ime/test-package.ps1`은 실제 installable 후보 DLL에 대해 위 검사를
실행하고 해시와 결과를 `out/package-verification.json`에 기록한다.
`prepare-package.ps1`은 이 검사를 패키지 변경보다 먼저 호출한다. 실제
실행에서 두 architecture의 routing 실패로 차단됐고, 기존 패키지 manifest
해시 `D2AC8269E33A3B74E1BE56983A12D369BF7338E51204AA659F24FF64F25EB426`은
변하지 않았다. 이 자동 검사는 실제 앱·설치 복구·자동 업데이트 검증의 대체가 아니다.

최종 읽기 확인에서 로컬 설치는 `2.0.0-preview.4`, 상태 `Installed`, manifest
7개 모두 해시 일치였다. GitHub 공개 버전은 `v1.1.1` 그대로다. 새 버전의
재설치와 공개 배포는 위 실패 때문에 진행하지 않았다.

### 표시·조합 수명 수정과 추가 입력 상태 검증

같은 날 후속 비교에서 활성 Premiere의 transitory parent를 실제로 읽었다.
검색 EDIT에는 `ITfDocumentMgr`로 질의 가능한 부모가 있었고 그 부모의
일반 컨텍스트 static flags는 8이었다. 자막과 타임라인의 부모는 같은 opaque
`VT_UNKNOWN` 값이며 `ITfDocumentMgr` 질의에 `E_NOINTERFACE`였다.
이 결과로 부모 인터페이스 질의 성공을 모든 CUAS의 조건으로 삼을 수 없다.
진단은 6개 스냅샷 후 두 훅 해제와 `writers=0`을 확인하고 종료했다.

표준 MSAA와 UIA의 더 구체적인 focus/caret 패턴도 추가 비교했다. 검색칸은
native EDIT와 caret를 노출했다. 반면 자막 편집과 타임라인은 둘 다 MSAA
role 10/state 1048580, UIA control type 50033과 keyboard focus만 보였고
TextPattern2 및 ValuePattern은 제공하지 않았다. 처음의 timeout과 포커스
전환 중 불일치 결과는 분류 근거에서 제외하고 안정된 스냅샷끼리 비교했다.
`native-ime/experiments/focus-contract`는 이 메타데이터 조회를 재현한다.
입력 문자열·Value·Name은 읽지 않으며 제품 키 처리 경로에 포함하지 않는다.
자체 hidden-control 검사 12개와 UIA 계약 검사 17개는 통과했다.

파란 선택 상자의 별도 원인은 확인했다. Chromium의 native TSF store도
`TRANSITORY | NOHIDDENTEXT`를 반환하므로 TRANSITORY 하나만으로 CUAS의
한 글자 interim selection을 적용하면 Electron에서도 글자가 선택된다.
현재 수정은 기존 document compartment 열거에서 transitory-extension parent를
찾고, 정확한 `S_OK`와 nonnull `VT_UNKNOWN` 값을 얻은 경우에만 interim을 쓴다.
그 외에는 조합을 유지하면서 선택을 접고 `fInterimChar=FALSE`로 둔다.
이 표시는 단축키 의도 판정과 무관하다.

- [Chromium TSF 상태 구현](https://github.com/chromium/chromium/blob/main/ui/base/ime/win/tsf_text_store.cc)
- [Windows transitory-parent compartment](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments)

표시 회귀 검사는 기존 코드에서 149개 중 23개 실패, 수정 뒤 x64/x86 각각
165개 통과였다. 이어 COM Release 콜백이 새로운 조합을 만드는 경우 이전
정리 코드가 그 조합과 컨텍스트까지 지우는 결함을 별도 재현했다. 현재는
이전 참조와 상태를 먼저 분리한 뒤 해제하며, 이전 종료 콜백은 새로운 조합의
음절·완료 대기 상태를 초기화하지 않는다. AddRef 도중의 종료·교체도 재검증한다.
이 추가 검사를 포함한 inline fixture는 양쪽 architecture에서 각각 178개 통과했다.
이는 소스 및 자체 fixture 검증이며 실제 Codex 화면의 표시 개선이나
Premiere 단독 문자 단축키 문제 해결까지 증명하지 않는다.

설치 스크립트는 버전·manifest·payload 경로를 공유하고 소스 checkout 없이
패키지 폴더에서 실행할 수 있도록 정리했다. 이전 입력기 선택과 버전별 DLL을
보존하고, 실패 시 실제 registry view별 복구 결과를 확인한다. 순수 스크립트
검사 23개는 PowerShell 5.1/7에서 통과했다. 새로 설치하거나 공개 업데이트를
발행한 상태는 아니며, 남은 routing 및 실제 앱 검증을 우회하지 않는다.

### Context key sink의 실제 호출 순서

별도 자체 문서에서 `ITfContextKeyEventSink`도 시험했다. 기존 입력 프로필은
바꾸지 않았으며, 사용자가 허용한 화면 시험에서 자체 창에만 포커스를 주었다.
F24의 네 가지 경로에서 실제 context callback과 앱 메시지 순서를 기록했다.

| 자체 문서의 처리 | 관찰 순서 |
| --- | --- |
| Test에서 거절 | context Test → 앱 WM_KEYDOWN; 앱 처리 뒤 KeyDown 콜백 없음 |
| Test/KeyDown 모두 소비 | context Test → context KeyDown; 앱 WM_KEYDOWN 없음 |
| Test 수락, KeyDown 거절 | context Test → context KeyDown → 앱 WM_KEYDOWN |
| 앱 accelerator를 먼저 처리 | 앱 명령만 실행; context 콜백 없음 |

이 시험에서 context sink는 앱이 단축키를 먼저 처리한 뒤 남은 키를 받는
후처리 지점이 아니었다. `TestKeyDown=FALSE` 뒤 `KeyDown`을 강제로 부르는
대안도 [Microsoft 호출 계약](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-testkeydown)에
맞지 않아 추가하지 않았다. 이 결과는 시험한 Windows 경로의 관찰이며,
모든 입력 경로에 대한 불가능성 증명은 아니다.

실제 포커스 실행에서는 callback 네 경로와 양성 대조가 실행됐으나, 정리
중 `AssociateFocus`의 출력 인자를 NULL로 넘긴 fixture 오류가 있었다.
해당 실행 전체를 통과로 집계하지 않는다. 출력 인자를 고친 뒤 숨김 대조에서
cleanup HRESULT와 참조 수는 정상 확인했으며, 포커스 없는 상태에서 콜백이
실행되지 않았다는 이유로 exit 3을 반환했다. 이 숨김 실행도 실기 통과가 아니다.

### 실제 Windows TSF 표시 검증용 자체 문서

`InlineFixture.cpp --chromium`은 Chromium과 같은 static flags를 내는 자체
문서에 실제 Windows TSF를 연결한다. DLL 경로·해시, 실제 조합 개수, TSF/ACP
선택 범위와 interim 표시, 한글 조합과 공백을 확인하도록 확장했다. 이것은
Chromium 앱 자체를 실행하는 시험이 아니며 성공하더라도 별도로 구분한다.

첫 실행은 Windows `PickerHost` 창이 클릭을 가려 포커스 대기 45초 만료로
종료됐다. 로그는 profile 활성화와 key 호출 이전의 실패를 명시했으며,
프로세스 종료도 확인했다. 이 실행을 실제 TSF 입력 성공으로 집계하지 않는다.
소스 검토에서 발견한 기존 입력 모드의 보존 순서도 수정했다.

후속 실행에서는 자체 문서 생성 전에 후보 DLL을 확인하던 초기화 순서 때문에
입력기 프로필이 선택되어도 서비스가 로드되지 않았다. Microsoft TSFPad 예제의
순서에 맞춰 ThreadMgr 활성화, 문서 생성·Push·AssociateFocus를 창 표시 전에
완료한 뒤 다시 실행했다. 실제 Windows는 자체 프로세스의 manifest를 통해
후보 DLL을 자동 로드했다. 별도 LoadLibrary나 수동 class factory 등록은 쓰지 않았다.

`native-ime/out/inline-chromium-live5.log`에서 x64 후보 해시
`87C429C4EDB3E9AA5EE5E666FE516FB04CE85B41B8DC606B314D0BA474BDCD53`와
실제 로드 경로가 일치했다. Windows가 미리 만든 parent compartment는 열거됐지만
값은 `S_FALSE / VT_EMPTY`여서 CUAS의 유효한 parent로 취급하지 않았다.
9개 키의 TestKeyDown과 KeyDown이 모두 성공·소비됐고, 한글 조합·받침 지우기·
음절 이동 동안 실제 조합이 유지되면서 모든 TSF/ACP 선택은 접힌 상태 및
`fInterimChar=FALSE`였다. 마지막 공백은 한 번만 입력되고 조합 수가 0이 됐다.
기존 프로필과 입력 모드는 복원 후 읽기 확인에서 모두 일치했고 Deactivate도
성공했다. 전체 결과는 `Inline failures: 0`이다.

이것은 실제 Windows TSF와 후보 DLL의 자체 문서 통합 시험 통과다. 실제
Chromium/Codex 화면, Premiere의 단축키 전달, 설치·업데이트 성공을 뜻하지 않는다.

### 공식 Premiere 26.0 C++ SDK 재검토

사용자가 Adobe Developer Console의 로그인 및 새 약관 처리를 직접 완료한
뒤, 2026년 2월 갱신된 Windows SDK를 공식 다운로드 버튼으로 받았다.
원본 ZIP의 SHA-256은
`A311CBC2613DED7842D79BD255B37A7D0A4D6E0331D0267E1F80E9EDFB2DD2B4`다.
자료는 ignored `upstream-research/premiere-sdk-26-official`에만 보관하며
제품 소스나 설치 파일에 복사하지 않았다.

150개 Headers 파일과 373쪽 PDF를 검토했다. SDK README가 문서화되지 않은
선언의 존재를 설명하므로 문서 검색뿐 아니라 선언도 확인했다.
`ADOBESDK_ControlSurfaceHostCommandSuite1`은 명령 열거와 실행을 제공하지만,
`ExecuteCommand`는 키의 handled/unhandled나 텍스트 편집 중 거절 여부를
돌려주는 계약이 아니다. 명령의 context ID도 현재 키보드 포커스 상태라는
뜻이 아니다. `Navigate`는 하드웨어 컨트롤러에서 호스트로 보내는 요청이며,
물리 키 선처리 콜백이 아니다. 플러그인의 Suspend/Resume 역시 앱 활성 상태다.

이 SDK에서 현재 자막·검색 입력의 소유자, 공통 DVA 텍스트 포커스, 또는
앱이 처리하지 않은 키를 안전하게 입력기로 넘겨주는 인터페이스는 찾지 못했다.
이는 검토한 SDK 범위의 결론이며 비공개 인터페이스의 부재까지 뜻하지 않는다.
명령을 실행할 수 있다는 사실만으로 한글 입력을 차단하는 분기를 추가하지 않는다.

### 접근성 이벤트 구독 실기

별도 `focus-contract/event-observer.cpp`로 Premiere 시험 프로젝트를 60초
관측했다. 검색칸, 문자 도구, 자막 편집과 커서 이동까지가 관측 범위다.
마지막 타임라인 복귀는 관측기가 종료된 뒤라 이벤트 비교 결과에 넣지 않았다.
문자열·이름·값은 수집하지 않았고, 원래 선택 도구로 복원했다.

| 구간 | 관찰 |
| --- | --- |
| 검색 | MSAA TEXT 역할 42와 FOCUSABLE·FOCUSED 상태, 실제 CARET 역할 7 이벤트 |
| 자막 | 사용자 정의 object ID 6044/6045의 focus 이벤트는 수신했으나 객체 조회가 E_FAIL |
| 자막의 일반 client 객체 | CLIENT 역할 10이며 문자 편집 의미 정보는 확인하지 못함 |
| UIA | main Window의 focus 2건, selection 0건; TextPattern2·ValuePattern은 S_OK와 null 객체 |
| IA2 | 조회한 33건에서 인터페이스 획득 실패(E_INVALIDARG); 프로그램 전체 미지원 판정은 아님 |

로그 `native-ime/out/focus-contract-probe/premiere-events-live1.jsonl`은 41개
이벤트와 `busy_dropped=19`를 기록했다. selection 0건은 미지원·비편집의
증거가 아니다. object 6044/6045는 PID 필터에 걸린 것이 아니라 객체 해석이
실패한 것이며, 비교적 짧은 지연으로 처리된 후속 이벤트에서도 같은 결과였다.

한 MSAA 이벤트 처리 구간에 7,516ms 간격이 있고 다음 이벤트는 발생 시각보다
7,922ms 늦게 관측됐다. 개별 호출의 시작·반환 시간은 기록하지 않아 지연을
특정 API에 귀속할 수 없다. 따라서 이 조회 경로를 매 키 입력의 동기 판정에
사용하지 않는다. 모든 구독 해제는 성공했고 handler 참조와 활성 callback은
0, cleanup 실패도 0이었다. 새로운 문자/명령 판별 근거는 확보하지 못했다.

### 현재 설계의 남은 조건

문자 입력과 명령 영역이 같은 공개 상태를 제공하면, 동일한 키에 대해 전자는
한글 조합과 명령 차단을, 후자는 원키 전달과 조합 차단을 요구한다. 관찰값이
같은 상태에 서로 다른 결정을 보장하는 정책은 만들 수 없다. 먼저 원키를
보내거나 조합 후 취소하는 방법도 실제 부작용을 되돌리는 보편적 계약이 없다.
이는 확인한 관측값에 대한 한계이며 모든 미탐색 API의 부재를 증명한 것은 아니다.

남은 해결 조건은 호스트·프레임워크가 현재 문자 입력 소유자나 처리하지 않은
키를 명시적으로 제공하는 것이다. 현재 Windows/Adobe 공개 경로와 실기에서
그 조건을 충족하지 못했으므로 범용 해결 또는 공개 배포 완료를 주장하지 않는다.
표시·조합 수명 수정은 자체 Windows TSF 검증을 통과했지만 routing gate의
`transitory workspace without text focus passes first V` 실패는 유지된다.
새 설치본과 자동 업데이트는 만들지 않았다.
