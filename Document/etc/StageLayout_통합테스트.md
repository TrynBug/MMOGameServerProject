# StageLayout 통합 테스트 절차 — EventArea / Waypoint / Spawner / SpawnPoint

> 목적: 에디터 마커 배치 → export → 서버 로드 → Lua 스크립트 → 클라 연동까지 **전 파이프라인**을 한 번에 검증한다.
> 대상 4종: **SpawnPoint, Waypoint, Spawner, EventArea(일반 + secure).**
> 테스트 스크립트: `Server/OUTPUT/Map/StageScript/test_layout.lua` (이미 생성됨).

---

## 0. 전제 (이미 되어 있는 것)

- 서버 빌드 완료(GameServer), proto/게임데이터 재생성 완료.
- 클라 컴파일 완료(마커 컴포넌트 4종 + EventAreaDetector + Export 툴).
- `test_layout.lua` 가 `Server/OUTPUT/Map/StageScript/` 에 있음.

이 테스트는 **Town(stageDataKey=100)** 을 테스트 무대로 쓴다고 가정한다(다른 스테이지를 써도 됨 — 키만 맞추면 됨).

---

## 1. 셋업

### 1-1. 마커 배치 (Town 씬)

Town 씬(= 런타임 프리팹의 원본)에 빈 GameObject 를 만들고 아래 컴포넌트를 붙인다. 좌표는 **플레이어 스폰 근처의 walkable(NavMesh 위) 지점**으로 — 걸어서 영역에 들어갈 수 있어야 한다. (값은 예시. 씬에 맞게 조정, 기즈모로 위치 확인.)

| GameObject | 컴포넌트 | 필드 | 비고 |
|---|---|---|---|
| `SP_1` | `SpawnPointMarker` | Key=1 | yaw 는 이 오브젝트 회전(Y) 사용 |
| `WP_1` | `WaypointMarker` | Key=1 | **자식** 빈 GameObject 3개를 경로 순서대로 배치(= 경로 점) |
| `Spawner_1001` | `SpawnerMarker` | Key=1001, Radius=20 | 밀도존. GameData_Spawner 1001 동작파라미터와 결합 |
| `EA_1001` | `EventAreaMarker` | EventKey=1001, Shape=Sphere, Radius=6, **Secure=false** | 일반(클라 보고) |
| `EA_1002` | `EventAreaMarker` | EventKey=1002, Shape=Sphere, Radius=6, **Secure=true** | secure(서버 폴링). EA_1001 과 떨어뜨려 배치 |
| `Prop_2001` | `PropMarker` | Key=2001, InteractRange=2 | 상호작용 prop(문/레버/NPC). 근처에서 F 키로 상호작용 |

> **중요 — EventArea·Prop 은 프리팹에도 있어야 한다.** 클라 `EventAreaDetector`/`PropInteractor` 는 런타임에 로드된 **스테이지 프리팹**에서 `EventAreaMarker`/`PropMarker` 를 찾는다. 마커를 씬에 배치한 뒤 **Town 프리팹(`Resources/Prefabs/Stages/Town`)을 갱신/저장**해서 런타임 프리팹에 포함되게 한다. (Spawner/SpawnPoint/Waypoint 마커는 서버 전용이라 프리팹에 있어도 런타임 동작엔 영향 없음.)

### 1-2. Export (마커 → 서버 json)

Town 씬을 연 상태에서:

- 메뉴 **`Tools/StageLayout/Export Active Scene`** 실행. (또는 `ai_command.txt` 에 `EXPORT_STAGELAYOUT:Town`)
- 결과 다이얼로그에 `spawners=1, spawnPoints=1, eventAreas=2, waypoints=1, props=1` 처럼 카운트가 떠야 한다.
- 출력 파일: `Server/OUTPUT/Map/StageLayout/Town.json` 생성 확인.

> export 는 `<씬이름>.json` 을 **덮어쓴다.** Town 씬이면 `Town.json`.

### 1-3. 게임데이터

`GameData_Stage` 의 Town(100) 행:

- `ScriptName1` = **`test_layout`** (테스트 동안 기존 `town` 대신. 끝나면 원복.)
- `StageLayoutFileName` = **`Town`** (= 씬 이름 = export 파일명. 비어있으면 레이아웃 로드 안 됨, 명시했는데 파일 없으면 서버 시작 실패.)

재사용(신규 행 불필요): 스포너 `1001`, 몬스터 `50`/`51` 은 기존 데이터에 이미 있음(town.lua 가 사용 중). 게임데이터 변경 후 **GameDataGenerator 실행 → 서버 GameDataLib 빌드**.

---

## 2. 실행

1. **서버 시작.** 시작 로그에서 확인:
   - `StageLayout loaded. file=Town spawners=1 spawnPoints=1 waypoints=1 eventAreas=2 props=1`
   - (만약 `StageLayout file not found. file=Town` 이 뜨면 export/파일명 불일치 → 1-2/1-3 재확인.)
2. **클라 시작** → 로그인 → 캐릭터 선택 → Town 입장. (이미 다른 스테이지면 클라 치트 콘솔(백틱 `` ` ``)에서 `stage 100`.)
3. 입장 직후 **OnStageStart** 자동 실행 — 아래 (A)~(D) 가 한 번에 일어난다.
4. (선택) 서버 치트 `packet` 또는 `packetdetail` 토글 → 주고받는 패킷 이름/내용 콘솔 출력(EventAreaEnterReq, ObjectInteractReq, StageNoticeNtf 관찰용).
5. 캐릭터를 **마우스 좌클릭 이동**으로 EA_1001 → 밖 → EA_1002 순서로 걸어 들어갔다 나온다.
6. **Prop_2001 근처로 이동 후 F 키** → 상호작용. (범위 밖에서 F 누르면 아무 일도 안 일어남 = 정상.)

---

## 3. 검증 (기대 결과)

서버 로그는 `[TEST]` 로 grep. 화면은 배너/몬스터로 확인.

| # | 기능 | 트리거 | 기대 결과 |
|---|---|---|---|
| A | **SpawnPoint** | OnStageStart | 로그 `SpawnPoint(1) = (...)` + `SpawnMonsterAt(1, 51, 2) -> spawned 2`. 화면: SpawnPoint 1 위치에 몬스터 2마리 |
| B | **Waypoint** | OnStageStart | 로그 `Waypoint(1) points = 3` + 각 point 좌표 3줄. 화면: 경로 점마다 몬스터 1마리(총 3) |
| C | **Spawner** | OnStageStart `ActivateSpawner(1001)` | 잠시 후 스포너 1001 반경(20)에 몬스터 무리가 채워짐(밀도 유지). 화면 |
| D | **GetEventArea** | OnStageStart | 로그 `GetEventArea(1001) center=(...) shape=0 radius=6.0`, `GetEventArea(1002 secure) ...` |
| E | **EventArea 일반(1001)** | 영역 진입(걸어서) | 배너 "EventArea 1001 진입!" + 로그 `OnEnterEventArea key=1001` + 연계 스폰 3마리. 이탈 시 배너 "이탈" + `OnExitEventArea key=1001` |
| F | **EventArea secure(1002)** | 영역 진입 | 배너 "Secure EventArea 1002 진입!" + 로그 `OnEnterEventArea key=1002` (서버 폴링으로 발동) |
| G | **Object 상호작용(2001)** | prop 근처(range≤2)에서 **F** | 배너 "prop 2001 상호작용! (문이 열렸다)" + 로그 `OnObjectInteract markerKey=2001`. 범위 밖이면 무반응. `packet` 으로 `ObjectInteractReq` 관찰 |
| D2 | **GetProp** | OnStageStart | 로그 `GetProp(2001) pos=(...) type=0 range=2.0` |

### 패킷 레벨 확인 (`packet` 치트 켠 상태)

- 영역 진입 순간 클라→서버 **`EventAreaEnterReq`** 가 보임(영역 안에 머무는 동안 0.5초마다 재전송 — 정상, 서버가 중복 무시).
- 서버→클라 **`StageNoticeNtf`** 가 배너 직전에 보임.
- 1002(secure)는 클라가 `EventAreaEnterReq` 를 보내도 **서버가 무시**하고 폴링으로 발동 — 콜백은 정확히 1회.

---

## 4. 트러블슈팅

| 증상 | 원인/조치 |
|---|---|
| `StageLayout file not found. file=Town` (서버 시작 실패) | export 안 했거나 파일명≠StageLayoutFileName. `Town.json` 존재 + 게임데이터 `StageLayoutFileName=Town` 확인 |
| 로그에 `SpawnPoint(1) NOT FOUND` / `Waypoint(1) NOT FOUND` | 해당 마커 미배치 또는 export 누락. 씬에 마커 + 재-export |
| 스크립트 자체가 안 돎(OnStageStart 로그 없음) | `ScriptName1=test_layout` 아님, 또는 `test_layout.lua` 누락. 서버 시작 로그의 script 컴파일 확인 |
| EA 진입해도 배너/콜백 없음(1001) | ① EventAreaMarker 가 **런타임 프리팹**에 없음(1-1 주의 참조) ② 영역이 NavMesh 밖이라 못 걸어감 ③ 위치검증 실패 — 서버 로그에 `EventAreaEnterReq reported pos ...` 경고 확인(허용오차 `kLagSeconds`/`kAntiCheatMargin` 조정 가능) |
| 1002(secure)만 발동 안 함 | EventAreaMarker.Secure 체크 안 했거나 export 에 `"secure":true` 누락. `Town.json` 의 1002 항목 확인 |
| 진입 보고가 폭주 | 정상 — 영역 안에 있는 동안 0.5초 간격 재전송(서버 occupant 중복제거). 트리거는 1회만 |

---

## 5. 정리 (테스트 후)

- `GameData_Stage.Town.ScriptName1` 을 `town` 으로 원복(원하면).
- 테스트용 마커/`Town.json`/`test_layout.lua` 는 필요에 따라 유지 또는 제거.

---

## 부록: 한 줄 요약 흐름

```
[에디터] 마커 배치(SpawnPoint/Waypoint/Spawner/EventArea) + 프리팹 갱신
        │  Tools/StageLayout/Export Active Scene
        ▼
[서버] OUTPUT/Map/StageLayout/Town.json  ──load──►  StageLayout
        │                                            │ test_layout.lua: OnStageStart
        │                                            ├ SpawnMonsterAt(1)      (A SpawnPoint)
        │                                            ├ GetWaypointPath(1)     (B Waypoint)
        │                                            ├ ActivateSpawner(1001)  (C Spawner)
        │                                            └ GetEventArea(1001/2)   (D)
        ▼
[클라] Town 프리팹 로드(EventAreaMarker 포함) → EventAreaDetector
        │  영역 진입 → EventAreaEnterReq ─► [서버] 검증/폴링 → OnEnterEventArea (E/F)
        ▼
        StageNoticeNtf ─► 화면 배너
```
