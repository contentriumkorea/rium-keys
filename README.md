# CONTENTRIUM Keys

Windows용 한글 입력기. 입력칸에서는 한글을 조합하고, 지원되는 작업 영역에서는 한/영 전환 없이 원래 키보드 단축키를 사용할 수 있습니다.

**현재 버전 2.0.0 · Windows x64 · 두벌식 한글**

[설치 파일 다운로드](https://github.com/contentriumkorea/rium-keys/releases/download/v2.0.0/CONTENTRIUM-Keys-Setup.exe) · [배포 페이지](https://github.com/contentriumkorea/rium-keys/releases/tag/v2.0.0) · [설치·복구 안내](docs/installation.md)

## 설치하기

1. 한국어와 Microsoft 입력기가 설치된 Windows x64에서 사용합니다. 새 설치 전에는 기본 입력 방법과 현재 입력기를 **한국어 Microsoft 입력기**로 선택합니다. Microsoft 입력기는 유지하세요.
2. 작업을 저장하고 편집 프로그램을 종료한 다음, `CONTENTRIUM-Keys-Setup.exe`를 **일반 실행**합니다.
3. 다음 → 라이선스 확인 → 설치를 진행하고, Windows 관리자 확인창에서 **예**를 누릅니다.
4. 파일·등록·DLL 로딩을 자동 점검합니다. **직접 타이핑하거나 단축키 시험을 할 필요가 없습니다.** 완료 화면에서 마침을 누릅니다.
5. 작업 프로그램을 다시 실행합니다. **Windows + Space → CONTENTRIUM Keys**로 선택할 수 있습니다.

별도 백그라운드 트레이 앱 없이 Windows가 선택된 입력기를 불러옵니다. 기본 입력기를 CONTENTRIUM Keys로 유지하면 로그인 후에도 사용할 수 있습니다. 기존 아이콘이 남아 있으면 로그아웃 후 다시 로그인하세요.

## 우측 하단 표시

| 표시 | 의미 |
| --- | --- |
| **가** | 한글 입력 |
| **A** | 영어 입력 / 입력기를 끈 직접 입력 |
| **CK** | CONTENTRIUM Keys 입력기 선택 아이콘 |

상태는 흰색 **가 / A**로만 표시합니다. CK와 상태 문자는 투명 배경입니다. Windows가 두 아이콘을 나란히 표시할 수 있으며, 프로그램이 두 개 실행된 것은 아닙니다.

`가 / A`를 클릭하면 한/영이 바뀝니다. 우클릭 메뉴에서 **한/영 전환**, **입력기 사용**을 조절합니다. 별도 설정 창은 없습니다.

## 지원 범위

| 환경 | 확인된 동작 | 추가 검증이 필요한 범위 |
| --- | --- | --- |
| 표준 Windows 입력칸 / 버튼 | 한글 → 원래 V 단축키 → 한글, 조합과 입력 상태 유지 | 자체 제작 입력창 전반 |
| Premiere Pro **26.5.2.5** | 검색·자막·오디오/비디오 타임라인·선택된 그래픽의 시험 경로, 입력칸 복귀 후 한글 조합 | 다른 버전, 추가 패널, 빠른 연속 입력 전반 |
| Studio One **6.6.4.102451** | 검색 → 작업 영역 C 단축키 → 검색, 앱 전환 후의 시험 경로 | 다른 버전과 나머지 입력칸 |
| After Effects **26.5.0.89** | 한글 입력 일부와 내부 입력 위치 판별 | 실제 단축키 및 입력칸 복귀 |

Premiere·Studio One의 실기 결과는 같은 입력 처리 코드를 사용하는 개발 빌드 8에서 확인했습니다. 2.0.0은 제품 표시와 설치 과정을 정리한 버전입니다. 프로그램 업데이트로 내부 구조가 바뀌면 지원 범위도 달라질 수 있습니다. 모든 Windows 프로그램의 단축키 호환성을 보장하지 않습니다. [검증 기록](docs/input-owner-validation.md) · [버전별 내부 지원 범위](native-ime/providers/README.md)

## 업데이트와 제거

- **RIUM Keys 1.1.1 유틸리티**: 위 설치 파일로 전환합니다. 새 입력기 자동 점검이 통과하면 기존 유틸리티를 제거합니다.
- **직전 개발 빌드 10**: 같은 EXE로 2.0.0으로 업데이트합니다. 기본 입력기 설정과 원래 입력기로 돌아갈 복구 정보를 보존합니다.
- **그보다 오래된 입력기**: Windows 설정 → 앱 → 설치된 앱에서 기존 입력기를 제거한 뒤 설치합니다.
- **2.0.0이 이미 설치됨**: 파일·등록·로딩을 확인하고 중복 설치를 생략합니다.
- **제거**: Windows 설정 → 앱 → 설치된 앱 → CONTENTRIUM Keys → 제거. 설치 전 입력기로 복구하며 개인 설정과 복구 기록은 남깁니다.

업데이트는 GitHub에서 설치 파일을 받아 진행합니다. 2.0.0에는 자동 업데이트 기능이 없습니다. 구형 1.1.1의 업데이트 경로는 유지되며 새 입력기로 자동 전환하지 않습니다. 저장소 주소는 기존 링크 호환을 위해 `rium-keys`를 유지합니다.

## 개인정보와 라이선스

입력 내용이나 문서 내용을 서버에 전송하지 않습니다. 입력기 자체는 업데이트 확인을 위한 네트워크 통신도 하지 않습니다. 설정은 `%APPDATA%\RIUM Keys`와 사용자 레지스트리, 복구 기록은 `%LOCALAPPDATA%\Contentrium\RIUM Keys\Recovery`, 설치 로그는 `%LOCALAPPDATA%\Contentrium\KeysSetup`에 보관합니다.

[MIT 라이선스 Jamotong](native-ime/third_party/jamotong/LICENSE)의 한글 조합 엔진을 기반으로 만들었습니다. [저작권 안내](native-ime/third_party/jamotong/COPYRIGHT.md)를 포함합니다. macOS 구름 입력기와는 별개의 Windows 프로젝트입니다.

[개발 및 빌드](native-ime/README.md) · [2.0.0 배포 기록](docs/release-2.0.0.md) · [예전 1.1.1 설명](docs/legacy-tray-1.1.1.md)
