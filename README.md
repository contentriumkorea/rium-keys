# CONTENTRIUM Keys

**한글 입력과 편집 단축키를 함께 쓰기 위한 Windows 입력기입니다.** 텍스트를 입력할 때는 한글을 조합하고, 확인된 편집 영역에서는 원래 키를 프로그램에 전달합니다. 기존 RIUM Keys 트레이 유틸리티에서 Windows 입력기 방식으로 전환했습니다.

현재 공개 버전은 **2.0.0-preview.10**입니다. 아직 모든 프로그램·버전을 지원하는 완성판은 아니며, 자동 업데이트는 제공하지 않는 수동 설치 Preview입니다.

**[Windows 설치 파일 다운로드](https://github.com/contentriumkorea/rium-keys/releases/download/v2.0.0-preview.10/CONTENTRIUM-Keys-Setup.exe)** · [변경 사항·SHA-256](https://github.com/contentriumkorea/rium-keys/releases/tag/v2.0.0-preview.10) · [자세한 설치·복구 안내](docs/installation.md)

## 설치하기

1. Windows x64에서 사용합니다. 한국어와 Microsoft 입력기가 설치되어 있어야 합니다. 새로 설치한다면 먼저 한국어 Microsoft 입력기를 선택하세요. **Microsoft 입력기는 삭제하지 마세요.**
2. 작업을 저장하고 편집 프로그램을 종료한 다음, 위의 `CONTENTRIUM-Keys-Setup.exe`를 **일반 실행**합니다. 별도로 ‘관리자 권한으로 실행’을 선택하지 않습니다.
3. 설치 안내를 진행하고, Windows 관리자 확인창이 뜨면 직접 **예**를 누릅니다.
4. **CONTENTRIUM Keys - 입력 확인** 창에서 아래 순서대로 시험합니다. 한/영 키는 누르지 않습니다.
   - 입력칸에 `한글`을 쓰고 **Space**를 누릅니다. 두벌식 물리 키는 **G K S R M F**입니다.
   - **단축키 확인 (V)** 버튼을 클릭하고 **V 키**를 누릅니다.
   - 입력칸의 글자 끝을 클릭하고 다시 `한글`과 **Space**를 입력합니다.
   - `한글 한글 `이 완성되면 시험 창이 자동으로 닫히고 설치가 마무리됩니다.
5. 편집 프로그램을 다시 실행합니다. **Windows + Space → CONTENTRIUM Keys**로 선택할 수 있습니다. 기존 아이콘이 계속 남으면 Windows에서 로그아웃 후 다시 로그인하세요.

설치 중 입력 확인 창은 설치 검증용입니다. 평소 사용 중에는 별도 창이나 백그라운드 트레이 앱을 실행할 필요가 없습니다. Windows가 선택된 입력기를 불러옵니다. Windows 설정에서 기본 입력기를 CONTENTRIUM Keys로 유지하면 로그인 후에도 사용할 수 있습니다.

## 우측 하단 표시

| 표시 | 의미 |
| --- | --- |
| **가** | 한글 입력 |
| **A** | 영어 입력. ‘입력기 사용’을 끈 직접 입력 상태도 A로 표시 |
| **CK** | CONTENTRIUM Keys 입력기 선택 아이콘 |

상태 문자는 흰색 **가 / A 하나만** 표시하며 `가CK`, `ACK`처럼 합치지 않습니다. Windows가 상태 아이콘과 입력기 선택 아이콘을 별도로 표시하므로 CK가 옆에 보일 수 있습니다. 프로그램이 두 개 실행된 것은 아닙니다. 흰색 CK 및 상태 아이콘은 투명 배경을 사용합니다.

`가 / A`를 클릭하면 한/영이 바뀝니다. 우클릭 메뉴에는 **한/영 전환**과 **입력기 사용**이 있습니다. 새 입력기에는 이전 유틸리티의 ‘자동 업데이트·Windows 시작 시 실행’ 메뉴가 없습니다.

## 확인된 범위

| 환경 | 실제 확인한 동작 | 아직 확인이 필요한 범위 |
| --- | --- | --- |
| 표준 Windows 입력칸 / 버튼 | 한글 → 원래 V 단축키 → 한글, 조합과 입력 상태 유지 | 모든 종류의 자체 제작 입력창 |
| Premiere Pro **26.5.2.5** | 검색·자막·오디오/비디오 타임라인·선택된 그래픽의 시험 경로, 입력칸 복귀 후 한글 조합 | 다른 버전, 추가 패널, 빠른 연속 입력 전반 |
| Studio One **6.6.4.102451** | 검색 → 작업 영역 C 단축키 → 검색, 앱을 오간 뒤의 시험 경로 | 다른 버전과 나머지 입력칸 |
| After Effects **26.5.0.89** | 한글 입력 일부와 내부 입력 위치 판별 | 실제 단축키 및 입력칸 복귀 검증 미완료 |

Premiere·Studio One 실기 결과는 같은 입력 처리 코드를 사용하는 **preview.8**에서 확인했습니다. preview.10은 상태 아이콘·설치 경로·설치 안내를 갱신합니다. 앱 업데이트 후에는 확인된 내부 구조가 달라져 해당 단축키 보완이 동작하지 않을 수 있습니다. 확인할 수 없는 영역에 대해서는 일반 입력 처리로 남기며, 모든 Windows 프로그램에서 단축키가 작동한다고 보장하지 않습니다. [검증 기록](docs/input-owner-validation.md) · [버전별 내부 지원 범위](native-ime/providers/README.md)

## 업데이트와 제거

- **RIUM Keys 1.1.1 유틸리티** 사용자는 위 설치 파일로 전환할 수 있습니다. 새 입력 확인이 통과한 뒤 기존 유틸리티를 제거합니다.
- **입력기 preview.9**는 같은 설치 파일로 preview.10으로 업데이트합니다. 현재 한/영 기본 입력기 설정과 원래 Microsoft 입력기 복구 정보를 보존합니다.
- **더 오래된 입력기 Preview**는 먼저 Windows 설정 → 앱 → 설치된 앱에서 기존 입력기를 제거한 뒤 설치하세요. 오류가 나면 반복 설치하지 말고 [복구 안내](docs/installation.md)를 확인하세요.
- **preview.10**이 이미 정상 설치되어 있으면 파일·등록 상태만 확인하고 중복 설치하지 않습니다.
- 제거: **Windows 설정 → 앱 → 설치된 앱 → CONTENTRIUM Keys → 제거**. 설치 전 입력기로 복구하며 개인 설정과 복구 기록은 남깁니다.

구버전 1.1.1의 자동 업데이트는 이 Preview를 설치하지 않습니다. 저장소 주소는 기존 링크 호환을 위해 `rium-keys`를 유지합니다.

## 개인정보와 라이선스

입력 내용이나 문서 내용을 서버에 전송하지 않습니다. 이 입력기는 업데이트 확인을 위한 네트워크 통신도 하지 않습니다. 설치 파일은 GitHub에서 직접 받습니다. 설정은 `%APPDATA%\RIUM Keys`와 사용자 레지스트리, 설치 복구 기록은 `%LOCALAPPDATA%\Contentrium\RIUM Keys\Recovery`에 보관합니다. 설치 진단 로그는 `%LOCALAPPDATA%\Contentrium\KeysSetup`에 저장됩니다.

[MIT 라이선스 Jamotong](native-ime/third_party/jamotong/LICENSE)의 한글 조합 엔진을 기반으로 만들었습니다. [저작권 안내](native-ime/third_party/jamotong/COPYRIGHT.md)를 포함합니다. macOS 구름 입력기와는 별개의 Windows 프로젝트입니다.

개발 및 빌드: [native-ime/README.md](native-ime/README.md) · [예전 1.1.1 설명](docs/legacy-tray-1.1.1.md)
