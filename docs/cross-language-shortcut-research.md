# 다른 언어 입력기의 단축키 충돌과 RIUM 적용 실험

조사·실측일: 2026-10-10. 중국어·일본어에서도 같은 문제가 확인된다.
조사한 공개 구현은 실제 편집 상태를 전달하는 앱/프레임워크 계약에
의존한다. 이 문서는 해결판 릴리스 노트가 아니다.

## 확인한 사례

| 사례 | 실제 방식 | RIUM 적용 판단 |
| --- | --- | --- |
| 중국어 Rime/Weasel | 중국어 입력칸을 나간 뒤 Chrome의 단독 r/t 단축키가 작동하지 않는 [997번 문제](https://github.com/rime/weasel/issues/997)를 [1277번 수정](https://github.com/rime/weasel/pull/1277)으로 처리. 키 처리 때 입력 금지를 다시 확인. | RIUM `Compart_ReadContextDisabled`도 preview/실제 keydown에서 재검사한다. 이미 있는 원칙이며 새 Adobe 해결책이라고 할 수 없다. |
| 러시아어 배열과 VS Code | [회귀 테스트](https://github.com/microsoft/vscode/blob/959031245ebb1fe077e0d512e1397c0ae82006e4/src/vs/workbench/services/keybinding/test/node/macLinuxKeyboardMapper.test.ts#L1678-L1712)는 Ctrl+S를 생성 문자 대신 `ctrl+[KeyS]`로 해석. [공식 문서](https://code.visualstudio.com/docs/configure/keybindings#_keyboard-layoutindependent-bindings)도 물리 키 바인딩을 지원. | 키의 정체성을 보존하는 방법이며 입력 소유권 판정은 별도로 필요하다. 한국어 조합 키를 항상 재전송하는 근거는 아니다. |
| 일본어 Mozc | 현재 선택 범위의 [InputScope](https://github.com/google/mozc/blob/18e76511af9371a9a4409b1d13c3e30884b53e9c/src/win32/tip/tip_range_util.cc#L193-L220)를 읽고 [포커스 때 유효 모드](https://github.com/google/mozc/blob/18e76511af9371a9a4409b1d13c3e30884b53e9c/src/win32/tip/tip_edit_session.cc#L139-L160)를 갱신. | 독립 진단으로 구현해 실측했다. Premiere의 검색·자막·타임라인에서는 모두 비어 있었다. 숫자·암호·반각 제약과 명령 영역은 다르며, 없음/default/실패를 비텍스트로 판정하면 안 된다. |
| Fcitx5/IBus와 Qt | [포커스 객체의 ImEnabled 질의](https://github.com/fcitx/fcitx5-qt/blob/0285a5d18367d8f3af80dab7e4819d5555982340/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp#L289-L311) 후 [입력 불가면 엔진 호출 생략](https://github.com/fcitx/fcitx5-qt/blob/0285a5d18367d8f3af80dab7e4819d5555982340/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp#L1066-L1087). | 공통 UI 기반의 의미 상태 연결. [Windows Qt도 같은 상태로 IME 연결을 관리](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/windows/qwindowsinputcontext.cpp#L253-L285)한다. 정상 Qt 앱은 자체 처리할 수 있으며 Adobe가 Qt라고 가정하지 않는다. |
| 중국 개발자의 Blender IME Helper | [입력칸을 나온 뒤 G 단축키가 작동하지 않는 증상](https://github.com/Arius-Cr/wire_ext_blender_fix_ime/blob/b07ec4491874fbf70113334346f7b2f83e6e78fb/README.md)을 대상으로 [내부 활성 텍스트 버튼](https://github.com/Arius-Cr/wire_ext_blender_fix_ime/blob/b07ec4491874fbf70113334346f7b2f83e6e78fb/src/native/blender.c#L296-L430)과 편집 상태를 읽음. | 실제 편집 수명을 연결하는 선례. 원본은 IME 연결 변경, 버전별 내부 구조, 일부 키 재전송·텍스트 상태 쓰기에도 의존하므로 전체 구현을 복사하지 않는다. |
| 일본 AutoCAD AutoIME v2.1 | [공개 구현](https://note.com/chiko_root/n/n97bcfe7b6606)은 텍스트 명령 시작·종료·취소 reactor에서 IME를 제어. | 편집 수명 신호는 유용하다. IME on/off 전환과 지연 뒤 foreground 재조회는 RIUM의 한글 유지·첫 키 요구와 다르다. |
| 일본 Premiere용 半角コパイロット | [제작자](https://booth.pm/ja/items/7859277)는 같은 문제와 앱별 특수 검출, 오검출 가능성을 명시. | 공개 검출 소스는 찾지 못했고 이 PC에서 실행하지 않았다. 제품 설명을 범용 해결의 검증으로 간주하지 않는다. |

Fcitx의 Qt6 파일은 Qt5 파일을 가리키는 심볼릭 링크이므로 실제 파일에
연결했다. 해외 구현의 원본 코드를 RIUM에 복사한 것은 아니다.

## 선택한 방향: 공통 UI 기반의 입력 상태 연결

표준 TSF 제약 검사와 한글 조합 엔진에, 프레임워크가 제공하는 현재 텍스트
소유권을 같은 UI 스레드에서 확인하는 연결 계층을 더하는 방향을 검증한다.
사용자가 프로그램 이름이나 패널 좌표를 등록하는 방식으로 확장하지 않는다.
키의 정체성과 입력 소유권은 분리한다. 명령 경로가 확인되면 원래 물리 키와
수정키를 보존하며, 한글로 변환된 문자에서 단축키를 역추정하지 않는다.
러시아어의 Ctrl+S 배열 문제와 CJK 조합 중 단독 V가 흡수되는 문제는
같은 계층의 결함이 아니다. 스캔 코드만 고쳐서는 후자를 해결할 수 없다.

| 기반 | 후보 계약 | 적용 조건 |
| --- | --- | --- |
| Qt | `QInputMethod::queryFocusObject(Qt::ImEnabled, ...)` | 올바른 focus object, 성공·값 타입, 버전별 ABI, GUI 스레드. Qt5/6의 인자 ABI도 같다고 가정하지 않는다. |
| SDL3 | `SDL_TextInputActive(SDL_GetKeyboardFocus())` | 호스트가 Start/StopTextInput을 올바르게 관리하는 경우. SDL2 기본 활성 상태를 같은 의미로 쓰지 않는다. |
| CCL | `IDesktop::isInMode(kTextInputMode)` 및 현재 창/포커스 소유권 | 설치본 ABI와 대상 소유권 검증. 아래 전역 카운터만으로 명령 허용을 결정하지 않는다. |
| TSF | disabled/empty/read-only와 선택 범위 InputScope | 제약 정보는 존중하지만 제약 없음이 편집 허용의 증거는 아니다. |

소스: [Qt 질의](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/kernel/qinputmethod.cpp#L386-L411),
[SDL3 조회](https://github.com/libsdl-org/SDL/blob/release-3.2.20/src/video/SDL_video.c#L5529-L5545),
[SDL2 초기 활성화](https://github.com/libsdl-org/SDL/blob/release-2.30.11/src/video/SDL_video.c#L580-L594),
[CCL Desktop 구현](https://github.com/cclsoftware/ccl-framework/blob/ad5876b1a7b64aa247f4378c86a7862e7c284ba3/ccl/gui/windows/desktop.cpp).

결과는 텍스트 소유권 확인 / 명령 대상 확인 / 알 수 없음으로 구분한다.
알 수 없음을 자동으로 명령 영역으로 바꾸거나 문자를 취소하지 않는다.
앱의 원래 키 처리 경로를 보존하며, 뒤늦게 명령을 되돌리는 방식은 사용하지
않는다. 후보 계약의 존재와 검증된 제품 지원은 구분한다.

## 이 PC의 CCL 실측

Studio One 6.6.4.102451의 `cclgui.dll` 4.0.3을 분석했다. 공개된 현재 헤더와
설치본의 vtable 슬롯은 같지 않다. 추측한 함수를 호출하지 않고 검증된
getter가 읽는 카운터만 외부에서 읽었다.

`native-ime/experiments/framework-input-state/ccl-readonly-probe.py`는 파일 SHA,
로드된 getter 코드, vtable과 메서드 주소를 확인한 뒤 정수 하나를 읽는다.
프로세스 쓰기·함수 호출·훅·입력·한영 전환은 수행하지 않는다.

| 실제 관찰 상태 | 편집 카운터 |
| --- | --- |
| 빈 QA 곡의 작업 영역 | 0 |
| Add Tracks 이름 입력칸 포커스 | 1 |
| Add Tracks 취소 후 작업 영역 | 0 |
| 브라우저 검색 입력칸 포커스 | 1 |
| 검색칸에서 작업 영역 클릭 | 0 |

기록: `native-ime/out/ccl-input-state-live1.jsonl`. 최초 메뉴 선택은 화면 밖
좌표 오류로 실패했으므로 뒤따른 `add-track-name-auto-focus` 표본은 입력칸
진입 증거에서 제외한다. 위 표는 이후 화면·포커스를 확인한
`add-tracks-dialog-open-confirmed` 표본을 사용한다.

두 입력 경로에서 구별되는 새 신호를 확인했다. 전역 카운터는 다른 창이나
외부 플러그인의 입력 소유권을 보장하지 않는다. 약 20–22ms의 표본 시간은
모듈·파일 검사를 포함하며 키 지연 측정이 아니다. 외부 진단을 상시 키 경로에
연결하지 않는다. CCL이 없는 Premiere에서는 false 대신 unknown을 반환했다.

## Mozc의 InputScope 경로를 적용한 실측

`native-ime/experiments/framework-input-state/input-scope-*`는 이미 활성화된
Windows TSF 서비스에서 현재 선택 범위의 `GUID_PROP_INPUTSCOPE`를 읽는 별도
진단이다. 같은 GUI 스레드의 실제 포커스 HWND와 TSF 문서·context 동일성을
전후 확인하고 동기 읽기 세션만 사용한다. 문자열·키·문서 쓰기·한영 전환을
요청하지 않는다. 입력 서비스 설치나 기존 라우팅 변경도 없다.

자체 Windows TSF 문서 검사 45개와 자체 스레드의 실제 메시지 훅 정리 검사
4개를 통과했다. 숨김 HWND의 실제 포커스 시험은 `SKIP77`이며 성공으로 세지
않는다. 비활성 TSF를 진단이 새로 켜지 않도록 검사하며, 중첩 활성화의
`S_FALSE`도 정확히 해제하고 전후 서비스 플래그가 같은지 확인한다.

이 PC의 Premiere 26.5.2 (파일 버전 26.5.2.5)에 이미 열린
`AdobeShortcutTest.prproj`에서 다음 순서로 관측했다.

| 화면에서 확인한 상태 | 포커스 HWND | 속성 조회 결과 |
| --- | --- | --- |
| 타임라인 작업 영역 | `0x009C0D46` | `S_OK`, `VT_EMPTY`, enum 없음 |
| 프로젝트 검색 입력칸 | `0x00320E04` | `S_OK`, `VT_EMPTY`, enum 없음 |
| Program Monitor의 자막 편집, 글자 선택 표시 | `0x000F07E2` | `S_OK`, `VT_EMPTY`, enum 없음 |
| 자막 편집 종료 후 타임라인 복귀 | `0x009C0D46` | `S_OK`, `VT_EMPTY`, enum 없음 |

각 관측에서 실제 provider의 동기 읽기 콜백이 한 번 실행됐다. 모두
`ownerMatched=1`, `unhooked=1`, `writers=0`, `activationBalanced=1`, `clean=1`이고
서비스 플래그는 전후 `0x80000000`이었다. 이 값들은 진단 실행·정리 증거이며
단축키 성공 증거가 아니다. `known=0/unknown=1` 결과를 그대로 유지한다.
검색어·자막을 입력하지 않았고 저장하지 않았다. 선택 도구와 타임라인으로
복귀했으며 앱은 계속 응답했다.
종료 후 진단 실행 프로세스와 Premiere에 로드된 진단 DLL이 없음을 확인했다.

결론: 이 Premiere 버전의 관측한 세 영역은 InputScope만으로 구분할 수 없다.
이 경로를 제품의 명령 판정에 연결하지 않는다. 속성을 제공하는 앱의 필드
제약을 존중하는 용도와, CCL처럼 실제 편집 수명을 확인하는 용도는 구분한다.

진단 DLL SHA-256: `A81069FFF50036FB8664E8A8BDBBBCC8EE5A8DFE0EE0EDEBDF55EB943B2CC940`.
원시 기록은 `native-ime/out/input-scope-probe/premiere-*.log`에 보존한다.

## 제품 적용과 배포 조건

현재 창·UI 스레드·소유권 세대를 연결하고 첫 글자/첫 단축키/연속 조합/
포커스 왕복/플러그인 입력/버전 불일치를 실기로 검증해야 한다. 이 결과는
공통 기반부터 지원을 넓히는 근거이며 모든 프로그램 해결의 증거가 아니다.

설치본과 공개 릴리스는 바꾸지 않았다. 기존 transitory workspace 회귀 실패도
유지한다. 제품 빌드·설치기·자동 업데이트에 진단 도구를 연결하지 않았으며
업체 문의도 보내지 않았다.
