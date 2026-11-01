# MMOGameServerProject 작업 지침

이 파일은 저장소 전체에 적용된다. 하위 폴더에 더 가까운 `AGENTS.md`가 있으면 그 지침을 우선한다.

## 언어와 작업 방식

- 사용자 대화, 새 문서는 기본적으로 한국어로 작성한다. 코드의 주석은 기본적으로는 한글로 하는데 단어는 영어를 섞어서 사용해도 됨. 기존 코드/문서의 언어와 용어는 존중한다. 
- 요청 범위만 변경한다. 이미 존재하는 사용자 변경이나 관련 없는 파일은 되돌리거나 정리하지 않는다.
- 코드·데이터 동작을 바꾸면 관련 문서도 같은 작업에서 갱신한다.
- 원인 분석 요청은 진단과 근거를 먼저 제공하고, 명시적 요청 없이 구현을 추가하지 않는다.
- 파일 수정은 작은 단위의 patch로 수행하고, 변경 후 가능한 범위에서 빌드·정적 검사·diff 검사를 실행한다.

## 먼저 읽을 문서
Document 폴더 안에는 현재 설계/구현 내용들이 들어있다. Document/etc 폴더는 신경쓰지 않아도 된다.

| 주제 | 문서 |
|---|---|
| 클라이언트 구조·Unity 에셋 규칙 | `Document/클라이언트.md` |
| 게임서버 구조 | `Document/게임서버.md` |
| 스킬·프리팹 앵커·VFX 위치 규약 | `Document/스킬시스템_정리.md` |
| 서버-클라이언트 패킷/동기화 | `Document/서버클라연동.md` |
| 게임데이터·코드 생성 | `Document/게임데이터.md` |
| 코드 스타일 | `Document/CodeConvention.md` |

## 프로젝트 구조

- `Development/Client`: Unity 클라이언트. 런타임 데이터는 `Assets/StreamingAssets/GameData`, 경로 기반 로드 프리팹은 `Assets/Resources` 아래에 둔다.
- `Development/Server`: C++ 게임서버와 서버 출력물. 서버가 읽는 Unity export는 `Development/Server/OUTPUT/Map`에 둔다.
- `Development/Common`: xlsx 원본, enum, 코드 생성 관련 공용 데이터.
- `Document`: 설계와 현재 진행 상황의 기준 문서.

## 게임데이터와 생성물

- 게임 규칙 데이터의 원본은 `Development/Common/GameData/*.xlsx`와 enum xlsx다.
- 컬럼/enum 변경 시 xlsx, 생성된 서버·클라이언트 코드, 각 런타임 CSV를 함께 갱신한다. 생성 파일만 직접 고쳐 원본과 불일치하게 만들지 않는다.
- `Skill.csv`의 VFX 경로는 `Resources.Load` 경로다. 해당 프리팹 파일은 반드시 `Assets/Resources` 아래에 있어야 한다.

## 검증과 인계

- 서버 변경은 가능하면 `Development/Server/MMOGameServerProject.sln`의 `Debug|x64` 빌드를 사용해 확인한다.
- Unity 런타임 확인이 필요하지만 자동 실행할 수 없으면, 실행하지 못한 사실과 사용자가 확인할 구체적 항목을 명시한다.
- git 작업 트리가 이미 더러울 수 있다. 관련 파일만 확인하고, `reset --hard`, 강제 checkout, 무단 파일 삭제를 사용하지 않는다.
