# Stage 스크립트 시스템 설계 — Lua 기반 Stage 오케스트레이션

> **상태: 설계 v1 (미구현).** 현재 Stage 로직은 전부 C++ 하드코딩이다(`Town::OnStart` 의 테스트 스폰, `Field::OnStageUpdate` 빈 TODO). 이 문서는 **기획자가 스크립트로 Stage 로직(스폰 타이밍·이벤트·웨이브·목표)을 작성**할 수 있게 하는 레이어를 설계한다.

> 범위(v1): **Stage당 Lua VM + 생애주기 콜백 + 엔진 API(타이머/스폰/스포너/이벤트영역/조회) + 에디터 배치데이터 연동 + 안전 가드.** 풍부한 목표/UI 연출, 컷신, 비주얼 스크립팅은 **나중**.
> 전제: `Stage`(=`serverbase::Contents`)는 **단일 컨텐츠 스레드**에서 `OnUpdate(deltaMs)` → `OnStageUpdate(deltaMs)` 로 매 tick(50ms) 돈다(`Stage.h`). 단건 스폰(`Stage::SpawnMonster`)·디스폰·사망·시체·`MonsterSpawner`(`몬스터스폰.md`)·NavMesh(`Map/NavMesh` 일괄 로드)는 **유지·재사용**한다.
> 핵심 결론을 먼저: **층을 셋으로 분리한다 — 배치(유니티 에디터 export 데이터) / 메커니즘(C++) / 정책(Lua).** 스크립트는 좌표를 손으로 안 쓰고 에디터가 놓아둔 오브젝트를 **Key로 참조**한다. 스레드 안전은 **Stage당 lua_State 1개**로 락 없이 공짜로 얻는다(우리 스레드 모델과 정확히 맞음). 타이머·이벤트는 전부 컨텐츠 스레드 위에서 돈다. 폭주 스크립트가 스레드를 멈추지 않도록 **인스트럭션 가드 + pcall 격리**가 필수다. 스폰/오케스트레이션은 **신규 패킷 0**, 플레이어 대면 이벤트 UI만 최소 신규 패킷.

> **확정/제안 결정 (17장 요약):** ① 언어 = **Lua 5.4 + sol2**, ② VM = **Stage당 1개**(스레드 안전·인스턴스 상태 격리), ③ 좌표 = **유니티 에디터 배치 export**(스크립트는 Key 참조), ④ 타이머 = 컨텐츠 스레드 누적(OS/ServerBase 타이머 금지), ⑤ 매 tick Lua 노출 안 함(타이머 권장), ⑥ 폭주 가드 = `lua_sethook` 인스트럭션 상한 + 모든 진입 `pcall`, ⑦ 스폰 패킷 0 / 이벤트 UI = 최소 신규(`StageNoticeNtf`), ⑧ `MonsterSpawner`(`몬스터스폰.md`)는 스크립트가 `ActivateSpawner` 로 구동하는 메커니즘 레이어.

---

## 1. 왜 스크립트 레이어인가 — 3층 분리

Stage 로직(언제 어떤 몬스터를 깔고, 이벤트영역에 반응하고, 웨이브를 돌리고, 목표를 관리)을 C++에 하드코딩하면 기획 변경마다 재컴파일·재배포가 필요하고, 기획자가 직접 못 만진다. 그래서 **책임을 3층으로 나눈다.**

```
유니티 에디터  →  배치(placement): Spawner/EventArea/SpawnPoint/Waypoint/Prop 를 Stage 위에 놓고 Key+좌표 export
        │ (per-stage 레이아웃 데이터 파일)
        ▼
C++ (메커니즘): MonsterSpawner(밀도/리스폰/팩), SpawnMonster(NavMesh 스냅), 전투/이동/AOI ── 무거운 시뮬
        │ (얇은 바인딩 API)
        ▼
Lua (정책/오케스트레이션): when/which ── 타이머, 이벤트 콜백, ActivateSpawner, 웨이브, 목표 ── 가벼운 결정
```

원칙: **무거운 건 C++, 정책만 Lua.** 스크립트의 `SpawnMonster`/`ActivateSpawner` 는 C++ 메커니즘을 호출하는 얇은 바인딩이지 거기서 몬스터를 시뮬레이션하지 않는다. 좌표는 에디터가 권위, 스크립트는 Key만 안다.

---

## 2. 다른 게임 사례 (가져올 것 / 버릴 것)

| 사례 | 방식 | 우리 채택 |
|---|---|---|
| **WoW 에뮬(TrinityCore + Eluna)** | 서버사이드 **Lua** 로 이벤트/퀘스트/스폰 스크립팅, 훅 등록 방식 | **핵심 채택** — 우리 콜백/등록 모델의 직접 모델 |
| **Source 엔진 (entity I/O)** | 에디터 배치 엔티티의 output→input 연결 | 에디터 배치 + Key 참조 발상 채택 |
| **EVE Online (Stackless Python)** | 서버 전체를 스크립트로 | 언어는 안 가져옴(GIL/무거움). "스크립트로 콘텐츠" 철학만 |
| **Roblox (Luau)** | 타입 있는 Lua + 샌드박스 | 안전 가드·샌드박스 발상 채택, 언어는 v1 Lua 5.4 |
| 비주얼 스크립팅(Blueprint/Bolt) | 노드 그래프 | v1 안 함(텍스트가 버전관리·복잡도에 유리). 나중 옵션 |
| MMO 기본(에디터+DB 데이터) | 단순 스폰/순찰은 데이터만 | 채택 — 단순 스폰은 스크립트 없이 스포너 데이터로(스크립트는 복잡 로직만) |

> 성숙한 패턴 = **배치는 에디터→데이터, 오케스트레이션은 스크립트.** 단순한 상시 스폰은 스크립트조차 필요 없다(`MonsterSpawner` 의 `Always`/`PlayerProximity` 자동 모드). 스크립트는 **웨이브·이벤트·조건부·목표** 같은 "로직"에만 쓴다.

---

## 3. 언어 선택 — Lua 5.4 + sol2

| 언어 | 성격 | 평가 |
|---|---|---|
| **Lua 5.4 + sol2** | 작고 빠름, sol2는 헤더온리 C++ 바인딩 | **채택.** 검증·생태계·바인딩 편의 최고, 임베딩 표준 |
| LuaJIT | 극한 성능, 단 Lua 5.1/5.2 묶임·개발 정체 | Stage 로직은 핫패스 아님 → 불필요. 병목 시 교체(API 동일) |
| Luau | Lua+점진적 타입+샌드박스 | 타입안정·대규모 안전 원하면 매력적. C++ 바인딩 생태계는 sol2보다 얇음 → 나중 검토(문법 상위호환이라 이행 쉬움) |
| AngelScript | C++ 유사 정적타입 | 타입 안전↑·반복속도↓·진입장벽↑. 기획자 친화도 낮음 |
| C#(Roslyn) | 팀이 C# 사용(클라/툴) | C++ 서버에 .NET 임베딩 무겁고 복잡 → 비추 |
| Python | EVE 사용 | GIL·무거움, 매 tick 다수 Stage 부적합 |

**Lua 5.4 + sol2.** 가장 가볍고, sol2가 C++↔Lua 바인딩(타입/함수 등록, 호출)을 거의 공짜로 만들며, 정체된 LuaJIT보다 유지보수가 낫다. 타입 안전이 절실해지면 Luau로 이행.

> 빌드: sol2는 헤더온리(vcpkg `sol2` + `lua`). `몬스터스폰.md`/`서버구조개요.md` 의 vcpkg 관리 방식에 맞춰 추가.

---

## 4. 아키텍처 — Stage ↔ lua_State

### 4.1 Stage당 lua_State 1개 (확정 결정 ②)

Lua VM(`lua_State`)은 스레드 안전하지 않다. 그런데 **우리 Stage는 각자 한 컨텐츠 스레드에서 돈다.** 따라서 **Stage 인스턴스마다 자기 lua_State 를 소유**하면 락 없이 자연스럽게 안전하다. 우리 스레드 모델과 정확히 맞는다.

부수 이득: **인스턴스 상태 격리.** 던전 인스턴스 100개가 같은 스크립트를 써도 각자 lua_State라 변수(웨이브 카운트, 목표 진행)가 자동으로 분리된다. 별도 격리 코드 0.

```cpp
class StageScript {                  // Stage 가 소유 (1:1). 내부에 lua_State 1개 + 스크립트 여러 개(4.3).
public:
    bool Load(Stage& stage, const std::vector<std::string>& scriptNames);  // lua_State 1개 생성 + 각 스크립트를 개별 환경으로 로드 + 바인딩 등록
    // 생애주기 호출: 정의한 모든 스크립트에 멀티캐스트(4.3).
    void CallOnStart();
    void CallOnPlayerEnter(int64 userId);
    void CallOnEnterEventArea(int32 eventKey, int64 userId, const Vector3& loc);
    void CallOnMonsterDead(int64 objectId, int32 monsterKey, int64 killerId);            // death-watch 통과분만(Q4)
    void CallOnSpawnerMonsterDead(int32 spawnerKey, int64 objectId, int32 monsterKey);   // 스포너별 watch(Q4)
    void Update(int64 deltaMs);       // 타이머 만기 + 코루틴 재개 → Lua 콜백 (컨텐츠 스레드)
private:
    struct ScriptEnv { sol::environment env; /* 정의된 콜백 핸들 */ };
    Stage*      m_pStage = nullptr;
    sol::state  m_lua;                       // sol2 (RAII) — Stage당 1개(스레드 안전)
    std::vector<ScriptEnv> m_scripts;        // ScriptName1~N (4.3)
    std::unordered_set<int32> m_deathWatchKeys;          // OnMonsterDead 발동 대상 monsterKey (Q4)
    // 타이머/코루틴/이벤트영역 멤버 (8·9·시퀀스 장)
};
```

### 4.2 생애주기

```
Stage 생성 → StageScript::Load(scriptName)         // lua_State 생성, 바인딩 등록, 파일 로드(컴파일)
Stage::OnStart()        → OnStageStart()            // 스크립트 진입점(사용자 예시의 main 역할)
Stage::OnUpdate(dt)     → (시스템메시지 처리) → OnStageUpdate(dt):
                            └ MonsterSpawner.Update(dt)         // 메커니즘
                            └ StageScript.Update(dt)            // 타이머 만기 + 코루틴 재개 + secure 영역 폴링
Stage::OnUserEnter      → OnPlayerEnter(userId)
Stage::OnUserLeave      → OnPlayerLeave(userId)
Stage::OnUserPacket     → (EventAreaEnterReq/ExitReq) 위치 검증 → OnEnterEventArea/OnExitEventArea   // 클라 선판정(8장)
Monster 사망(MarkDead)  → OnMonsterDead / OnSpawnerMonsterDead   // death-watch 통과분만(Q4)
Stage 종료/닫힘         → OnStageEnd()              // 정리(타이머 자동 해제)
```

기존 가상 훅(`OnStart`/`OnStageUpdate`/`OnUserEnter`/`OnUserLeave`)에 스크립트 호출을 끼우는 것이라 **Stage 구조 변경은 최소**다.

### 4.3 1개 Stage = 여러 스크립트 (Q3, 확정 결정 ⑨)

Stage마다 기본 스크립트가 있지만, 게임 내 행사 등으로 **기존 스크립트를 수정하지 않고 특별 로직을 더해야** 할 때가 있다. 그래서 한 Stage가 여러 스크립트를 보유한다.

- **데이터:** `GameData_Stage` 에 `ScriptName1`, `ScriptName2`, `ScriptName3` 컬럼(숫자접미 순회 규칙 `GetScriptNameCount()/GetScriptName(i)` — `게임데이터.md`). 비어있으면 그 슬롯 없음. 예: `ScriptName1=town_base`, `ScriptName2=event_winter`.
- **로드:** **lua_State 는 여전히 Stage당 1개**(스레드 안전 유지). 각 스크립트는 **개별 환경(`sol::environment`, 메타테이블 `__index = _G`)** 으로 로드한다. 그러면 스크립트는 **전역 함수 스타일 그대로**(`function OnStageStart() ... end`) 작성해도 그 함수·지역상태가 자기 환경에만 들어가 다른 스크립트와 **충돌하지 않는다**(당신 최초 스케치·§14·§7.8 예시 방식 유지). 엔진은 각 환경에서 콜백 이름(`OnStageStart` 등)을 찾아 호출한다.
- **멀티캐스트:** 생애주기 콜백은 **정의한 모든 스크립트에 차례로 호출**된다. 예: `OnMonsterDead` 를 base·event 둘 다 정의했으면 둘 다 불린다. 타이머·death-watch 등록도 스크립트별로 누적된다.
- **격리:** 한 스크립트의 에러는 `pcall`(10장)로 격리되어 다른 스크립트·서버에 전파되지 않는다.
- **효율 — 무시 가능:** 추가 비용은 (i) 환경 테이블 몇 개(수 KB)와 (ii) 콜백당 N(=1~3)번 호출, (iii) 공유함수(`math` 등) 읽을 때 `__index`로 `_G` 까지 **메타 hop 1회**뿐이다. (iii)은 타이트 루프에서나 의미 있고 `local floor = math.floor` 로컬라이즈로 사라진다. 콜백은 핫패스가 아니며(타이머 성김, `OnMonsterDead` 는 watch 필터, 이벤트영역은 이벤트 구동), 메모리·스케일의 실제 드라이버는 **Stage당 lua_State**(단일/다중 스크립트와 무관)다. 유일하게 관리할 것은 *인스턴스마다 N개 스크립트 컴파일* 인데 **바이트코드 1회 컴파일 후 각 VM에 로드**(§10 최적화)로 분할상환한다. → **다중 스크립트 유지가 타당.**

> 행사 종료 시 `ScriptName2` 만 제거하고 재배포하면 기본 로직(`ScriptName1`)은 손대지 않는다. 행사 스크립트는 base 스크립트가 켜둔 스포너/이벤트영역을 **같은 Key로** 추가 조작할 수 있다.

---

## 5. 에디터 배치 데이터 (유니티 export) — 좌표의 권위 (확정 결정 ③)

기획자가 좌표를 정확히 손입력하긴 어렵다. **유니티 에디터에서 Stage 씬 위에 마커 오브젝트를 놓고 export** 한 데이터를 서버가 로드하며, 스크립트는 Key로만 참조한다.

### 5.1 배치 오브젝트 종류

| 오브젝트 | 모양 | 데이터 | 스크립트에서 |
|---|---|---|---|
| **SpawnPoint** | 점 | key, pos, yaw | `SpawnMonster(key, count)` 의 기준 / `GetSpawnPoint(key)` |
| **Spawner** | 점+반경 | key, pos, radius (→ `GameData_Spawner` 의 동작파라미터와 결합) | `ActivateSpawner(key)` / `DeactivateSpawner(key)` |
| **EventArea** | 박스/구 | key, shape, center, size | `OnEnterEventArea(key,...)` 콜백 발생 |
| **Waypoint Path** | 점 배열 | key, pos[] | 순찰/에스코트 경로 `GetWaypointPath(key)` |
| **Marker/Prop** | 점 | key, pos, type | NPC/문/레버 등 배치 기준 |

### 5.2 데이터 포맷·로딩 (NavMesh와 동형)

- NavMesh가 `OUTPUT/.../Map/NavMesh` 에 있고 시작 시 `NavMeshManager.LoadAll` 로 일괄 로드되듯(`GameServer.cpp`), **Stage 레이아웃은 `Map/StageLayout/<name>.json`** 에 두고 동형으로 로드한다. `<name>` 은 `GameData_Stage.NavMeshFileName`(예: `Town`)을 재사용하면 NavMesh와 짝이 맞는다.
- 포맷: `nlohmann/json`(이미 의존성에 있음). 좌표/Key/모양만 담은 단순 구조.
- 서버는 export 좌표를 **그대로 믿지 않고** 스폰 시 NavMesh 스냅(이미 `SpawnMonster` 가 함)으로 보정한다 — 클라(유니티) 권위 금지.

```jsonc
// Map/StageLayout/Town.json (유니티 export 예시)
{
  "spawnPoints": [ { "key": 1, "pos": [10,0,10], "yaw": 0 } ],
  "spawners":    [ { "key": 1001, "pos": [0,0,0], "radius": 20 } ],
  "eventAreas":  [ { "key": 1001, "shape": "Sphere", "center": [30,0,0], "radius": 8 } ],
  "waypoints":   [ { "key": 1, "points": [[0,0,0],[10,0,0],[10,0,10]] } ]
}
```

### 5.3 `몬스터스폰.md` 와의 정합 (좌표 일원화)

`몬스터스폰.md` 의 `GameData_Spawner` 는 `CenterX/Y/Z` 를 csv에 손입력하게 돼 있었다. 좌표 출처가 둘(csv vs 에디터)로 갈리므로 **좌표는 에디터 레이아웃으로 일원화**한다:

- `GameData_Spawner` 에서 **`CenterX/Y/Z`/`Radius` 제거** → 동작 파라미터만 보유(`SpawnGroupKey`, `MaxPacks`, `RespawnDelayMs`, `Activation`, `ActivationRange`). 위치/반경은 레이아웃의 Spawner 오브젝트(같은 Key)에서 온다.
- `ESpawnActivation` 에 **`Manual` 추가**: 스크립트가 `ActivateSpawner` 호출 전까지 dormant. (기존 `Always`/`PlayerProximity` 는 스크립트 없이 자동.)
- `StageKey` 컬럼도 불필요해짐 — 어떤 Stage의 스포너인지는 그 Stage의 레이아웃 파일에 그 Key가 있느냐로 결정.

> 이 변경들은 `몬스터스폰.md` (3·4·5.2·6.2·6.3·6.5·8·9·11·12장)에 **반영 완료**.

---

## 6. 생애주기 콜백 (엔진 → Lua)

스크립트가 **선택적으로 정의**하는 함수들. 정의 안 하면 no-op.

| 콜백 | 시점 | 인자 | 용도 |
|---|---|---|---|
| `OnStageStart()` | Stage 시작 1회 | — | 타이머 등록, 초기 스폰/스포너 활성화 (사용자 예시의 `main`) |
| `OnPlayerEnter(userId)` | 유저 입장 | userId | 입장 인원 카운트, 첫 입장 이벤트 |
| `OnPlayerLeave(userId)` | 유저 퇴장 | userId | 전원 퇴장 시 던전 정리 등 |
| `OnEnterEventArea(eventKey, userId, loc)` | 플레이어가 이벤트영역 진입 | key,userId,pos | 트랩, 트리거 스폰, 대화, 컷 |
| `OnExitEventArea(eventKey, userId)` | 이벤트영역 이탈 | key,userId | 영역 인원 추적 |
| `OnMonsterDead(objectId, monsterKey, killerId)` | **watch 등록된 monsterKey** 사망 시만 | id,key,killer | 처치 카운트, 웨이브 클리어, 목표 진행. **대량몹 대비 필터 — Q4·아래 "대량 몬스터" 절** |
| `OnSpawnerMonsterDead(spawnerKey, objectId, monsterKey)` | 그 스포너가 만든 watch 등록 몹 사망 시 | spawnerKey,id,key | 특정 스포너 무리의 클리어 판정 (Q4) |
| `OnTimer(timerId)` | `RegisterTimer` 주기 만기 | timerId | 주기 로직 (또는 등록 시 넘긴 함수 직접 호출) |
| `OnObjectInteract(markerKey, userId)` | 배치 오브젝트 상호작용 | key,userId | 문/레버/NPC. (상호작용 패킷 연동, 나중) |
| `OnStageEnd()` | Stage 닫힘 | — | 명시적 정리(타이머는 자동 해제) |

> **콜백은 정의한 모든 스크립트에 멀티캐스트**된다(4.3).
> 매 tick `OnStageUpdate(dt)` 도 노출 가능하지만 **권장하지 않는다**(확정 결정 ⑤) — 다수 Stage × 매 tick Lua 호출은 비싸다. 주기 로직은 `RegisterTimer` 로(코어 50ms tick의 배수). 정말 필요할 때만 opt-in.

### 6.1 대량 몬스터 대비 — 사망 콜백 필터 (Q4)

몬스터가 대량으로 나오면 **사망마다 무조건 Lua `OnMonsterDead` 를 부르는 건 부하**다(대부분 잡몹 죽음은 스크립트에 무의미). 그래서 **구독(watch)한 것만** Lua를 탄다.

- **전역 watch (Q4a):** `WatchMonsterDeath(monsterKey)` 로 등록한 key만 `OnMonsterDead` 발동. 사망 경로는 작은 `unordered_set` 멤버십만 확인(O(1)) — 미등록 잡몹은 **Lua 진입 0**. 기본=watch 없음=콜백 없음.
- **스포너별 watch (Q4b):** `ActivateSpawner(key, {watchKeys})` 또는 레이아웃 Spawner json의 `watchKeys` 필드로, 그 스포너가 생성한 몬스터 중 등록 key가 죽으면 `OnSpawnerMonsterDead(spawnerKey, ...)` 발동. 몬스터는 스폰 시 `spawnerKey` 태깅(`몬스터스폰.md` §6.4·9), 사망 시 `(spawnerKey, monsterKey)` 가 watch에 있으면만 호출.
- 두 경로 모두 **사망당 해시 조회 1회**, Lua는 watch된 죽음에만 진입. 수천 마리 잡몹이 죽어도 비용은 조회뿐.
- 구현: `Monster::MarkDead` → `Stage` → `StageScript` 로 (objectId, monsterKey, killerId, spawnerKey) 전달, StageScript가 watch 필터링.

---

## 7. 엔진 API (Lua → C++)

sol2로 바인딩하는 함수들. **경계를 작고 안정적으로** 유지한다(`몬스터AI.md` 의 "패킷 계약" 처럼 "스크립트 API 계약").

### 7.1 타이머
| API | 설명 |
|---|---|
| `RegisterTimer(periodMs, fn) -> timerId` | 주기 호출. `fn` 은 Lua 함수. 컨텐츠 스레드에서 호출(9장) |
| `SetTimeout(delayMs, fn) -> timerId` | 1회 호출(원샷) |
| `CancelTimer(timerId)` | 해제. Stage 종료 시 전부 자동 해제 |

### 7.2 스폰 / 스포너
| API | 설명 |
|---|---|
| `SpawnMonster(monsterKey, location, count)` | 좌표(또는 SpawnPoint 결과)에 즉시 N마리. 내부적으로 `Stage::SpawnMonster` ×N(NavMesh 스냅) |
| `SpawnMonsterAt(spawnPointKey, monsterKey, count)` | 배치된 SpawnPoint Key 기준 스폰 |
| `ActivateSpawner(spawnerKey [, {watchKeys}])` / `DeactivateSpawner(spawnerKey)` | `MonsterSpawner`(`몬스터스폰.md`) 스포너 on/off. 밀도/리스폰/팩은 C++가 처리. `watchKeys` 주면 그 몹 사망 시 `OnSpawnerMonsterDead`(Q4b) |
| `WatchMonsterDeath(monsterKey)` / `UnwatchMonsterDeath(monsterKey)` | 전역 `OnMonsterDead` 발동 대상 등록/해제(Q4a) |
| `DespawnMonster(objectId)` | 즉시 디스폰 |

### 7.3 배치 오브젝트 조회
| API | 설명 |
|---|---|
| `GetSpawnPoint(key) -> location, yaw` | 배치 좌표 |
| `GetEventArea(key) -> center, shape, size` | 영역 정보 |
| `GetWaypointPath(key) -> {points}` | 순찰/에스코트 경로 |
| `GetProp(key) -> location, type` | NPC/문/레버 등 prop 기준점 |

### 7.4 질의 / 카운트
| API | 설명 |
|---|---|
| `GetAliveMonsterCount(filter)` | (스포너/영역/몬스터키 필터) 생존 수 — 웨이브 클리어 판정 |
| `CountPlayersInArea(eventAreaKey)` | 영역 내 플레이어 수 |
| `GetPlayersInStage() -> {userId}` | 전체 |
| `GetStageElapsedMs()` | Stage 가동 경과 |
| `Random(min, max)` | 난수(서버 권위) |

### 7.5 플레이어 / 이동
| API | 설명 |
|---|---|
| `TeleportPlayer(userId, location)` | Stage 내 순간이동 |
| `MoveMonsterAlongPath(objectId, waypointKey)` | 배치 경로로 순찰/에스코트(기존 `WaypointMover` 재사용) |

### 7.6 이벤트 UI / 공지 (패킷 필요 — 11장)
| API | 설명 | v1 |
|---|---|---|
| `Notice(userId 또는 all, message)` | 화면 공지 배너 | v1(`StageNoticeNtf`) |
| `ShowObjective(scope, text, current, total)` | 목표 UI(예: "처치 7/10") | 나중 |
| `ShowCountdown(scope, seconds, label)` | 카운트다운 UI(예: "60초 방어") | 나중 |
| `StageClear(scope)` / `StageFail(scope)` | 클리어/실패 연출 | 나중 |

### 7.7 상태 / 유틸
| API | 설명 |
|---|---|
| `SetStageVar(key, value)` / `GetStageVar(key)` | Stage 블랙보드(Lua 변수로도 가능하나 디버그/공용 접근용) |
| `Log(message)` | 서버 로그(Logger 경유) |

> 보상/드랍(`Monster.ItemDropGroup`/`Exp`)은 **별도 시스템.** v1 API에 두지 않고, 도입 시 `OnMonsterDead` 에서 보상 시스템을 부르거나 `GrantReward` API를 추가한다.

### 7.8 시퀀스 스폰 — 코루틴 (Q5)

"무리 생성 → 3초 대기 → 무리 생성 → … → key 1000 몬스터가 죽으면 다음 → 무리 생성" 같은 **순차 스폰**은 매우 흔하다. 콜백·타이머 중첩(`SetTimeout` 안에 `SetTimeout`…)으로도 되지만 읽기 어렵다. **Lua 코루틴**으로 선형으로 쓰는 게 정답이다 — 엔진이 yield 지점을 제공한다:

| yield 프리미티브 | 의미 |
|---|---|
| `Wait(ms)` | ms 만큼 코루틴 일시중단 후 재개(타이머 기반) |
| `WaitForMonsterDead(monsterKey)` | 그 key 몹이 죽을 때까지 중단(death-watch 기반, Q4) |
| `WaitForSpawnerClear(spawnerKey)` | 그 스포너 무리가 전멸할 때까지 중단 |
| `WaitForCount(filter, n)` | 생존 수가 n 이하가 될 때까지 중단 |

```lua
function OnStageStart()
    StartSequence(WaveSequence)        -- 엔진이 코루틴으로 실행
end

function WaveSequence()
    SpawnMonsterAt(1, 51, 10)          -- a. 무리 생성
    Wait(3000)                         -- b. 3초 대기
    SpawnMonsterAt(2, 51, 10)          -- c. 무리 생성
    Wait(3000)                         -- d. 3초 대기
    SpawnMonsterAt(3, 50, 5)           -- e. 무리 생성
    WaitForMonsterDead(1000)           -- f. key 1000 몹 사망까지 대기
    SpawnMonsterAt(4, 9000, 1)         -- g. 무리(보스) 생성
end
```

- **사용자 질문(Q5) 그대로 가능.** 위가 a~g 시퀀스다.
- **스케줄러:** 중단된 코루틴을 `StageScript::Update(dt)`(컨텐츠 스레드)에서 조건 만족 시 `coroutine.resume` 한다 — 타이머/death-watch 인프라(9장·Q4) 재사용. **resume도 `pcall` 격리 + 인스트럭션 가드**(10장) 적용.
- 코루틴은 그 Stage의 lua_State 안에서만 돈다(스레드 안전 유지).

---

## 8. 이벤트영역 트리거 메커니즘 — 클라 선판정 + 서버 검증 (Q1·Q2)

### 8.1 왜 클라 선판정인가 (Q1 — 당신 제안 채택)

처음엔 서버가 매 tick 모든 영역×플레이어를 재계산(diff)하려 했으나, **당신 지적이 옳다**: 영역이 많아지면 비용이 계속 늘고, 영역 크기까지 고려한 sector 버킷 후보군 계산이 까다롭다. 그래서 **클라가 먼저 판정하고 서버가 검증**하는 방식으로 간다:

```
클라: 자기 캐릭터가 이벤트영역 진입 감지(유니티 씬에 영역이 이미 있음)
   → 서버에 EventAreaEnterReq{eventKey} 전송
서버: 그 플레이어의 권위 위치 vs eventKey 영역을 교차 검사(허용 오차 포함)
   → 정합하면 트리거 발동(OnEnterEventArea 콜백), 아니면 무시/로그
이탈도 동일(EventAreaExitReq).
```

**이점:** 비용이 영역수×플레이어×tick → **실제 경계 통과 이벤트당 O(1)** 로 바뀐다. 영역이 아무리 많아도 폴링 0. 클라가 영역 지오메트리를 이미 갖고 있으므로(에디터 씬에 배치됨) 추가 전송도 불필요.

**이는 우리 넷코드 철학과 일치한다** — `몬스터AI.md` 8장의 "클라가 판정에 관여하되 권위는 서버"(클라 위치를 입력으로 주고 서버가 검증)와 같은 패턴. 권위는 서버에 남는다.

### 8.2 검증과 악용 방지

- **위치 검증:** 서버는 `EventAreaEnterReq` 수신 시 그 플레이어의 **권위 위치**가 해당 영역 안(또는 허용 오차 내)인지 확인. 거짓 보고는 거부.
- **악용 한계 인지:** 클라가 진입 보고를 **누락**(트랩 회피)하거나 가짜 이탈을 보낼 수 있다. PvE 트리거 대부분은 영향이 작다. **중요/적대적 영역**(안티익스플로잇 게이트 등)만 `secure=true` 플래그로 **서버 폴링 유지**(소수라 비용 작음) → 하이브리드.
- **서버 주도 변위 보정:** 텔레포트·넉백 등 **서버가 위치를 옮긴 경우** 클라 보고가 안 올 수 있다. 이 경우 서버가 그 플레이어에 대해서만 영역 멤버십을 재평가(서버는 "안에 있던 자" 집합을 영역 객체에 들고 있으므로 이탈 감지 가능).

### 8.3 EventArea = StageObject 파생 (Q2 — 채택)

이벤트영역 하나를 **`StageObject` 파생 객체**로 만든다(당신 제안). 영역마다 자기 상태를 갖는다:

- **베이스:** `StageObject`(위치/소속). Actor 가 아니므로 `ActorObject` 가 아니라 `StageObject` 직접 파생. **클라 가시성 통보 안 함**(클라는 유니티 씬에서 이미 영역을 앎) — `SpawnMonster` 류 AOI 통보 경로를 타지 않는다.
- **보유 상태:** `eventKey`, 모양(Sphere/Box)+크기, **발동 여부(triggered)**, **현재 안에 있는 userId 집합**, `secure` 플래그, 쿨다운 등 영역별 런타임 상태.
- **로드:** `StageLayout` 의 `eventAreas`(§5.2)에서 영역마다 1개 생성, Stage가 `m_eventAreas`(map<key, EventAreaPtr>)로 보유.
- **sector 등록은 선택:** 클라 선판정이 기본이라 서버가 공간질의할 일이 적다. `secure` 영역(서버 폴링)만 sector 버킷에 올려 후보를 좁힌다.
- 모양 판정은 Sphere(거리)·Box(AABB)만 v1(스킬 `EffectShape` Circle/Obb 판정과 발상 동일).

> 영역을 객체로 두니 "한 번만 발동", "N초 쿨다운 후 재발동", "현재 인원수" 같은 상태를 영역 자신이 들고, 스크립트는 `OnEnterEventArea` 안에서 그 의미만 처리한다.

---

## 9. 타이머 메커니즘 (확정 결정 ④)

- **OS 타이머/ServerBase 타이머 스레드 금지.** 별도 스레드 타이머는 Stage 객체를 크로스스레드로 만져 위험하다(`몬스터스폰.md` 와 같은 교훈).
- `RegisterTimer(periodMs, fn)` 는 `StageScript` 의 등록 리스트에 `{periodMs, accumMs, luaFnRef}` 로 쌓는다.
- `StageScript::Update(deltaMs)`(컨텐츠 스레드) 에서 `accumMs += deltaMs`; `accumMs >= periodMs` 면 `pcall(luaFn)` 호출 후 `accumMs -= periodMs`.
- 주기는 코어 tick(50ms)의 해상도로 양자화된다(예: 1000ms는 20 tick). 충분.
- Stage 종료 시 등록 리스트와 sol2 함수 레퍼런스를 정리(자동).

---

## 10. 안전성 (확정 결정 ⑥) — 폭주 가드가 필수

기획자가 `while true do end` 를 짜면 **그 Stage 스레드가 멈추고, 같은 컨텐츠 스레드의 다른 Stage들까지 전부 멈춘다.** 1차 작성자가 사내라도 사고는 난다.

1. **인스트럭션 상한.** `lua_sethook(L, hook, LUA_MASKCOUNT, N)` 으로 한 번의 Lua 진입에서 실행 인스트럭션이 N을 넘으면 에러를 던져 끊는다. 무한루프가 스레드를 영구 점유하는 걸 방지.
2. **pcall 격리.** 모든 엔진→Lua 호출(콜백/타이머)은 `sol::protected_function`(=pcall)로 감싼다. Lua 에러가 C++로 전파돼 서버를 죽이지 않고, 에러 로그 + 해당 콜백만 스킵.
3. **샌드박스(경량).** `io`/`os`/`require`/파일시스템 등 위험 표준 라이브러리를 글로벌에서 제거. 1차 사내라 강한 샌드박싱은 불필요하지만 사고 표면은 줄인다.
4. **핫 리로드(개발 편의).** 개발 빌드에서 스크립트 파일 변경 시 해당 Stage의 lua_State 재로드(`OnStageStart` 재호출). 운영 빌드는 비활성. (미구현)
5. **바이트코드 캐시 — 구현됨(`StageAssetManager`, 18.5).** 시작 시 각 스크립트를 1회 컴파일해 바이트코드로 보관하고, 각 Stage의 lua_State는 이를 `binary` 로드만 한다(파싱 생략). 다인스턴스 던전의 매 인스턴스 재컴파일을 제거.

---

## 11. 패킷

| 사건 | 패킷 | 방향 | 신규/재사용 |
|---|---|---|---|
| 스폰/리스폰/팩/디스폰 | `ObjectVisibilityNtf`(`MonsterSpawnInfo`) | S→C | 재사용 (스폰은 서버 내부) |
| 위치/이동 | `SnapshotNtf` | S→C | 재사용 |
| **이벤트영역 진입/이탈 보고** | **`EventAreaEnterReq`/`EventAreaExitReq`(신규)** | **C→S** | 신규 — 클라 선판정(Q1·8장) |
| **이벤트 공지 배너** | **`StageNoticeNtf`(신규, 최소)** | S→C | 신규 — `Notice()` 용 |
| 목표 UI / 카운트다운 / 클리어·실패 | (나중) `StageObjectiveNtf` 등 | S→C | 나중 |
| 오브젝트 상호작용 | (나중) `ObjectInteractReq/Ntf` | C↔S | 나중 |

> 스폰/오케스트레이션은 **신규 0**(2장 `몬스터스폰.md` 원칙 유지). 와이어를 건너는 신규는 **이벤트영역 보고(C→S)** 와 **공지(S→C)** 뿐. 풍부한 목표/타이머 UI는 나중.

```proto
// 클라 -> 서버: 이벤트영역 진입/이탈 선보고. 서버가 권위 위치로 검증 후 트리거(8장).
message EventAreaEnterReq { int32 event_key = 1; }
message EventAreaExitReq  { int32 event_key = 1; }

// 서버 -> 클라: Stage 이벤트 공지 배너. 스크립트 Notice() 가 발생.
message StageNoticeNtf {
    string message     = 1;
    int32  duration_ms = 2;   // 표시 시간(0=기본)
}
```

---

## 12. 게임데이터 / 파일

- **`GameData_Stage` 컬럼 추가:** `ScriptName1`, `ScriptName2`, `ScriptName3`(string, server) — 로드할 Lua 파일들(숫자접미 순회, §4.3). 전부 비어있으면 스크립트 없는 Stage(순수 데이터/자동 스포너). 보통 `ScriptName1`=기본, `ScriptName2~`=행사 등 추가.
- **스크립트 파일 경로:** `OUTPUT/.../Map/StageScript/<ScriptName#>.lua` (NavMesh/Layout과 형제 폴더).
- **레이아웃 데이터 경로:** `OUTPUT/.../Map/StageLayout/<NavMeshFileName>.json` (5.2).
- `GameData_Spawner` 변경: 5.3 참조(좌표 제거, `Manual` 활성화 추가) — `몬스터스폰.md` 반영 완료.

| `GameData_Stage` (확장 예) | Key | StageType | NavMeshFileName | sectorSize | **ScriptName1** | **ScriptName2** |
|---|---|---|---|---|---|---|
| | 104 | Dungeon | Town | 10 | `dungeon_wave_defense` | (행사 시 추가) |

---

## 13. 서버 코드 매핑 (현재 → 목표)

| 위치 | 현재 | 바꿀 방향 |
|---|---|---|
| `Stage` | 스크립트 없음 | **`StageScript m_script`** + **`StageLayout m_layout`** + **`map<int32,EventAreaPtr> m_eventAreas`** 멤버 추가 |
| `Stage::OnStart` | 베이스 처리 | `m_layout.Load` + EventArea 객체 생성 + `m_script.Load({ScriptName1,2,3})` + `m_script.CallOnStart()` |
| `Stage::OnStageUpdate` (`Stage.h:292`) | Town/Field override | `m_spawner.Update` + `m_script.Update(dt)`(타이머·코루틴 재개·secure 영역 폴링) 호출 |
| `Stage::OnUserEnter/OnUserLeave` (`Stage.h:309-310`) | 입퇴장 처리 | 뒤에 `m_script.CallOnPlayerEnter/Leave` 추가 |
| **`Stage::OnUserPacket`**(`Stage.h:335`) | 유저 패킷 핸들 | **`EventAreaEnterReq/ExitReq` 핸들러 추가** — 위치 검증 후 `m_script.CallOnEnterEventArea`(Q1·8장) |
| `Monster` 사망 경로(`MarkDead`) | 사망/시체 | Stage 경유 `m_script.CallOnMonsterDead`/`CallOnSpawnerMonsterDead` — **death-watch 필터 통과분만**(Q4). `monsterKey`+`spawnerKey` 전달 |
| `Town::OnStart`(`Town.cpp:33-40`) | 하드코딩 스폰 | 제거 → 스크립트/스포너로 |
| `Field::OnStageUpdate`(`Field.cpp:21`) | 빈 TODO | `m_spawner.Update`(스크립트는 선택) |
| `StageNavMesh` | SamplePosition/FindPath | (`몬스터스폰.md`) `SampleRandomPoint` 추가 — 스폰 배치 공용 |
| 신규 | — | **`StageScript`**(lua_State 1개+다중 스크립트 환경+바인딩+타이머+코루틴 스케줄러+death-watch), **`StageLayout`**(json 로드+Key 조회), **`EventArea`**(StageObject 파생, Q2), **`StageScriptApi`**(sol2 바인딩 등록 한 곳) |

신규 클래스 `StageLayout`: `Load(name)`, `GetSpawnPoint(key)`, `GetEventArea(key)`, `GetWaypointPath(key)`, `ForEachSpawner(fn)`, `ForEachEventArea(fn)`. 전부 컨텐츠 스레드 전용·락 없음.
신규 클래스 `EventArea`(`StageObject` 파생): `eventKey`, 모양/크기, `triggered`, 안에 있는 `userId` 집합, `secure`. `Contains(pos)` 판정.

---

## 14. 예시 스크립트 (사용자 예시 확장 — 웨이브 방어 던전)

```lua
-- dungeon_wave_defense.lua
local wave = 0

function OnStageStart()
    Log("wave defense start")
    WatchMonsterDeath(9000)                   -- 보스 죽음만 OnMonsterDead 발동(Q4 — 잡몹은 콜백 안 함)
    RegisterTimer(1000,  TickBanner)          -- 주기 함수
    SetTimeout(3000, function() StartWave(1) end)  -- 3초 후 1웨이브
end

function StartWave(n)
    wave = n
    Notice("all", "Wave " .. n .. " 시작!", 3000)
    ActivateSpawner(1000 + n)                 -- 에디터에 놓인 웨이브별 스포너
end

function TickBanner()
    -- 남은 적이 0이면 다음 웨이브
    if GetAliveMonsterCount() == 0 and wave > 0 then
        if wave < 3 then
            StartWave(wave + 1)
        else
            Notice("all", "클리어!", 5000)
            wave = 0
        end
    end
end

-- 이벤트영역(에디터 배치) 진입 콜백 — 사용자 예시와 동일 구조
function OnEnterEventArea(eventKey, userId, loc)
    if eventKey == 1001 then
        ActivateSpawner(1004)                 -- 함정방
    elseif eventKey == 1002 then
        SpawnMonsterAt(7, 51, 5)              -- SpawnPoint 7 에 근접몹 5마리
    end
end

function OnMonsterDead(objectId, monsterKey, killerId)
    -- 보스(예: monsterKey 9000) 처치 시 출구 개방
    if monsterKey == 9000 then
        Notice("all", "보스 처치! 출구가 열렸다.", 4000)
    end
end
```

사용자가 그린 `main`/`RegisterTimer`/`ActivateSpawner`/`OnEnterEventArea` 구조를 그대로 따르되, 진입점 이름만 `OnStageStart` 로 명확화했다(원하면 `main` 별칭 가능).

---

## 15. v1 범위 / 나중에

**v1 (지금):**
- Lua 5.4 + sol2, **Stage당 lua_State 1개 + 다중 스크립트**(`ScriptName1~3`, 멀티캐스트, Q3), `StageScript`/`StageLayout`/`EventArea`/`StageScriptApi`.
- 콜백: `OnStageStart`/`OnPlayerEnter`/`OnPlayerLeave`/`OnEnterEventArea`/`OnExitEventArea`/`OnMonsterDead`(watch 필터)/`OnSpawnerMonsterDead`/타이머.
- API: 타이머, `SpawnMonster`/`SpawnMonsterAt`/`ActivateSpawner(,watch)`/`DeactivateSpawner`/`DespawnMonster`, `WatchMonsterDeath`, 배치조회, `GetAliveMonsterCount`/`CountPlayersInArea`/`GetPlayersInStage`, `Notice`, `Log`, `Random`.
- **시퀀스 스폰 코루틴**(`Wait`/`WaitForMonsterDead`/`WaitForSpawnerClear`/`WaitForCount`, Q5).
- **대량몹 사망 콜백 필터**(전역 watch + 스포너별 watch, Q4) — 미등록 죽음은 Lua 진입 0.
- 에디터 배치 export(json) 로드 + Key 참조, 서버 NavMesh 스냅 보정.
- **이벤트영역: 클라 선판정 + 서버 검증**(Q1), `EventArea`=StageObject 파생(Q2). `secure` 영역만 서버 폴링.
- 안전 가드(인스트럭션 상한 + pcall + 경량 샌드박스), 개발용 핫리로드.
- 신규 패킷 = `StageNoticeNtf`(S→C) + `EventAreaEnterReq/ExitReq`(C→S). 스폰은 0.
- `Town`/`Field` 하드코딩 제거.

**나중에:**
- 목표/카운트다운/클리어·실패 UI(`StageObjectiveNtf` 등), 오브젝트 상호작용(문/레버/NPC 대화).
- 순찰/에스코트(`MoveMonsterAlongPath` + Waypoint), 컷신/연출.
- 보상·드랍 연동(`OnMonsterDead` → 보상 시스템).
- Luau 이행(타입 안전), 비주얼 스크립팅 옵션.
- 바이트코드 캐시(인스턴스 폭증), sector 기반 이벤트영역 후보 좁히기.
- 유니티 에디터 export 툴 자체(별도 작업 — GameDataGenerator 류 파이프라인).

---

## 16. 구현 작업 분해 (v1)

> 의존 순서대로. 각 작업에 **검증(verify)** 기준 동반.

**의존 그래프**
```
P0 빌드/바인딩 ─► P1 StageScript(VM+생애주기+pcall) ─► P2 타이머 ─┐
P3 StageLayout(에디터 export 로드) ──────────────────────────────┼─► P4 엔진 API ─► P5 이벤트영역 ─► P6 통합검증
                                                                  │
P0b StageNoticeNtf(패킷) ─────────────────────────────────────────┘
```

### P0 — 빌드 / sol2 바인딩 토대
| 작업 | verify |
|---|---|
| vcpkg에 `lua`/`sol2` 추가, GameServer 링크 | 최소 `m_lua.script("return 1+1")` 동작 |
| `StageNoticeNtf`(S→C) + `EventAreaEnterReq/ExitReq`(C→S) proto 신설 + ID | C++/C# 생성, `COMPILE_CHECK` |

### P1 — StageScript (VM + 다중 스크립트 + 생애주기 + 격리)
| 작업 | verify |
|---|---|
| `StageScript::Load` (lua_State 1개, 각 `ScriptName#` 를 개별 환경으로, 샌드박스) | 스크립트 N개 로드/문법오류 로그, 전역 충돌 없음 |
| 생애주기 멀티캐스트 `OnStageStart`/`OnPlayerEnter/Leave` (전부 `protected_function`) | 모든 스크립트에 호출, 에러 시 서버 생존(pcall) |
| `lua_sethook` 인스트럭션 상한 | `while true do end` 가 스레드 안 멈추고 에러로 끊김 |

### P2 — 타이머 + 코루틴 스케줄러 (Q5)
| 작업 | verify |
|---|---|
| `RegisterTimer`/`SetTimeout`/`CancelTimer` + `Update(dt)` 누적 만기 | 1000ms 타이머가 ~20 tick마다, 원샷 1회, Stage 종료 시 해제 |
| `StartSequence`+코루틴 yield(`Wait`/`WaitForMonsterDead`/`WaitForSpawnerClear`/`WaitForCount`) 재개 | Q5 a~g 시퀀스 순서대로 진행, resume도 pcall/가드 |

### P3 — StageLayout (에디터 배치 로드) + EventArea 객체 (Q2)
| 작업 | verify |
|---|---|
| `Map/StageLayout/<name>.json` 로드(nlohmann/json) + Key 조회 | 예시 json 로드, `GetSpawnPoint/EventArea/Waypoint` 정확 반환 |
| `EventArea`(StageObject 파생) 생성 + `Contains` 판정(Sphere/Box) | 영역 객체 상태(triggered/occupants) 보유 |
| `GameData_Stage.ScriptName1~3` 컬럼 + 경로 배선 | 데이터 변경이 로드에 반영 |

### P4 — 엔진 API (Lua→C++) + 사망 watch (Q4)
| 작업 | verify |
|---|---|
| 스폰/스포너 API(`SpawnMonster`/`SpawnMonsterAt`/`ActivateSpawner(,watch)`/`DeactivateSpawner`) | 스크립트 호출 → 몬스터 스폰/스포너 on-off 관측 |
| `WatchMonsterDeath` + 사망 경로 필터 → `OnMonsterDead`/`OnSpawnerMonsterDead` | watch된 key만 콜백, 미등록 잡몹 대량사망 시 Lua 진입 0(로그/프로파일) |
| 조회/카운트(`GetSpawnPoint`/`GetAliveMonsterCount`/`CountPlayersInArea`/`GetPlayersInStage`) | 값 정확 |
| `Notice`(`StageNoticeNtf` 송신) + `Log`/`Random` | 클라 배너 표시 |

### P5 — 이벤트영역 트리거 (클라 선판정, Q1)
| 작업 | verify |
|---|---|
| `EventAreaEnterReq/ExitReq` 핸들러 + 권위 위치 검증 → `OnEnterEventArea`/`OnExitEventArea` | 진입/이탈 시 1회씩 콜백, 거짓 보고(위치 불일치) 거부 |
| `secure` 영역 서버 폴링 + 서버 변위(텔포/넉백) 시 이탈 재평가 | secure 영역은 보고 누락에도 정확, 텔포 시 이탈 발동 |

### P6 — 통합 검증
| 시나리오 | verify |
|---|---|
| 웨이브 방어 스크립트(14장): 타이머·웨이브·클리어 | 화면 + 로그 |
| 시퀀스 스폰(Q5 a~g): 대기/사망대기 순차 진행 | 화면 + 로그 |
| 이벤트영역 진입(클라 보고→서버 검증) → 연계 스폰 | 화면 + `packet` |
| 보스 처치 → watch → `OnMonsterDead`/`OnSpawnerMonsterDead` → 공지 | 화면 + `packet` |
| 대량 잡몹 사망: 미watch 죽음에 Lua 진입 0(부하) | 서버 CPU/로그 |
| 다중 스크립트(base+행사): 콜백 멀티캐스트, 환경 격리 | 로그 |
| 던전 인스턴스 2개 동시: 상태 격리(웨이브 변수 독립) | 로그 |
| 폭주 스크립트 가드 / Lua 에러 격리 | 서버 생존 확인 |
| `Town`/`Field` 하드코딩 제거 후 데이터/스크립트로 동일 결과 | 화면 |

**완료 정의(v1):** 기획자가 Lua로 Stage 로직(타이머·웨이브·이벤트영역 반응·연계 스폰·공지)을 작성하고, 에디터에 놓은 스포너/영역을 Key로 구동하며, 좌표는 손입력 없이 export에서 오고, 폭주/에러 스크립트가 서버를 멈추지 않고, 던전 인스턴스 간 상태가 격리된다.

---

## 17. 확정/제안 결정

| # | 항목 | 결정 |
|---|---|---|
| 1 | 언어 | **Lua 5.4 + sol2** (LuaJIT/Luau/AngelScript는 대안, 병목·타입안전 시 검토) |
| 2 | VM | **Stage당 lua_State 1개** — 스레드 안전(컨텐츠 스레드 1:1) + 인스턴스 상태 격리 |
| 3 | 좌표 권위 | **유니티 에디터 배치 export(json)** — 스크립트는 Key 참조, 서버 NavMesh 스냅 보정 |
| 4 | 타이머 | **컨텐츠 스레드 누적**(`OnStageUpdate` 내) — OS/ServerBase 타이머 금지 |
| 5 | 매 tick Lua | **노출 안 함**(타이머 권장) — 다수 Stage 비용. 필요 시 opt-in |
| 6 | 안전 | **`lua_sethook` 인스트럭션 상한 + 모든 진입 pcall + 경량 샌드박스** (필수) |
| 7 | 패킷 | 스폰/오케스트레이션 **0** / 이벤트 UI는 **`StageNoticeNtf` 1개**부터, 풍부한 UI는 나중 |
| 8 | 메커니즘 분리 | `MonsterSpawner`(`몬스터스폰.md`)는 C++ 메커니즘, 스크립트는 `ActivateSpawner` 로 구동 |
| 9 | 다중 스크립트 (Q3) | `GameData_Stage.ScriptName1~3`, **lua_State 1개 + 스크립트별 환경**, 콜백 멀티캐스트. 행사 로직을 기본 스크립트 수정 없이 추가 |
| 10 | 이벤트영역 (Q1·Q2) | **클라 선판정 + 서버 권위 검증**(폴링 비용 제거) + `secure` 영역만 서버 폴링. `EventArea` = **StageObject 파생**(영역별 상태 보유) |
| 11 | 대량몹 사망 콜백 (Q4) | **watch 등록분만** `OnMonsterDead`/`OnSpawnerMonsterDead` 발동(전역 watch + 스포너별 watch). 미등록 죽음은 해시 조회만, Lua 진입 0 |
| 12 | 시퀀스 스폰 (Q5) | **Lua 코루틴** + 엔진 yield(`Wait`/`WaitForMonsterDead`/`WaitForSpawnerClear`/`WaitForCount`). 컨텐츠 스레드에서 resume |
| 13 | `몬스터스폰.md` 보정 | `GameData_Spawner` 좌표 제거(레이아웃으로 이전) + `ESpawnActivation.Manual` + 몬스터 `spawnerKey` 태깅 — **반영 완료** |

---

## 18. 구현 현황 (v1 — 실제 코드 / 보정)

> 이 장은 설계(1~17장) 위에 **실제로 구현·검증된 결과 + 구현 중 확정/보정된 사항**을 기록한다.
> 상태: **서버+클라 구현 + 빌드 + 런타임/통합 검증 완료.** 생애주기 콜백·인스트럭션 가드·타이머·코루틴 시퀀스·스폰/사망 연동·`WaitForMonsterDead` 웨이브에 더해, **이벤트영역(클라 선판정+서버 검증/폴링)·공지 배너(클라)·배치 export 툴(에디터 마커→json)** 까지 통합테스트 통과(EventArea/Waypoint/Spawner/SpawnPoint 4종).

### 18.1 라이브러리 / 빌드

- **Lua 5.4.7 + sol2 3.5.0** (vcpkg, 커스텀 triplet `x64-windows-static-md-2022`). `lua.lib` 링크, sol2 헤더온리.
- ⚠️ Lua 5.5 + sol2 3.5.0 은 비호환(`unsupported Lua version`) → 5.4 로 고정.
- sol include 구간만 `min/max` 매크로를 `push/undef/pop` 으로 격리(전역 NOMINMAX 미정의 회피, 외과적).

### 18.2 구현된 것 (`StageScript`)

- **pimpl 구조** — `sol::state`/`sol::environment` 등 sol 타입을 `.cpp`(`struct Impl`)에 완전히 은닉. (`sol::environment` 가 타입 별칭이라 전방선언 불가 → 헤더에서 sol 노출 0.)
- **Stage당 lua_State 1개** + **스크립트별 `sol::environment`**(전역함수 스타일 작성 유지, 환경 격리). 다중 스크립트 머신러리(Load 가 리스트, 콜백 멀티캐스트) 존재.
- **생애주기 콜백 멀티캐스트**: `OnStageStart`/`OnPlayerEnter`/`OnPlayerLeave`/`OnMonsterDead`/`OnSpawnerMonsterDead`/`OnEnterEventArea`/`OnExitEventArea`. 전부 `protected_function`(pcall) 격리.
- **안전 가드**: `lua_sethook`(인스트럭션 5M 상한) — 호출마다 재장전. 무한루프 차단 런타임 검증됨.
- **엔진 API**:
  - `Log` / `Notice(msg[,ms])`(→ Stage 전체 유저에 `StageNoticeNtf` 송신).
  - 타이머: `RegisterTimer`/`SetTimeout`/`CancelTimer` + `Update(dt)` 누적 만기(컨텐츠 스레드).
  - 코루틴 시퀀스: `StartSequence` + `Wait(ms)` / `WaitForMonsterDead(key)` / `WaitForCount(n)` / `WaitForSpawnerClear(key)`. 시퀀스마다 `sol::thread`, `advanceSequence` 스케줄러(시간·조건 대기=Update 폴링, 사망 대기=CallOnMonsterDead 가 재개).
  - 스폰 연동: `SpawnMonster(key,x,y,z)`, `SpawnMonsterAt(spawnPointKey,key,count)`, `ActivateSpawner`/`DeactivateSpawner`, `GetAliveMonsterCount`.
  - 배치 오브젝트 조회: `GetSpawnPoint(key)→{x,y,z,yaw}`, `GetWaypointPath(key)→{{x,y,z},…}`, `GetEventArea(key)→{x,y,z,shape,radius,sizeX,sizeZ}`. `StageLayout` 가 json 의 `spawnPoints`/`waypoints`/`eventAreas` 로드(`spawners` 와 함께).
  - 사망 watch: `WatchMonsterDeath`/`UnwatchMonsterDeath` (전역 key) + `WatchSpawnerDeath`/`UnwatchSpawnerDeath` (스포너별, Q4b).
- **사망 훅**: `Stage::ApplyEffectDamage` 의 `justDied` + `EObjectType::Monster` → `StageScript::CallOnMonsterDead(objectId, monsterKey, spawnerKey, killerId)`. 한 경로에서 ① 시퀀스 `WaitForMonsterDead` 재개(watch 무관) ② `OnMonsterDead`(monsterKey watch) ③ `OnSpawnerMonsterDead`(spawnerKey watch, Q4b) 처리. **전부 watch 등록분만 Lua 진입**(대량몹 부하 방지). `spawnerKey` 는 `Monster::GetSpawnerKey()`(MonsterSpawner 가 태깅).
- **Stage 배선**: `m_pScript` 멤버, `OnStart` 로드+`OnStageStart`, `OnUserEnter/Leave`→콜백, `OnUpdate` 4.6단계 `Update(dt)`. `StageScript` 는 Stage 의 `friend`.

### 18.3 설계 대비 보정된 사항

- VM 구현을 **pimpl** 로(설계 §4.1 의 헤더 멤버 노출 대신) — sol 타입 별칭 전방선언 불가 회피.
- 다중 스크립트 = **(a) per-script environment** 채택(§4.3 결정대로), bare-global 작성 유지. (b) 모듈 반환 안 씀.
- 스크립트 로드 = **`GameData_Stage.ScriptName1~3` 데이터 구동** (빈 슬롯 제외). 다중 스크립트 멀티캐스트 런타임 검증됨(Town = `town` + `town_event` → 양쪽 `OnStageStart`/`OnPlayerEnter` 호출). (`ScriptName#` 컬럼은 string/server, `GetScriptName` 헬퍼 생성됨.) 실제 파일/바이트코드는 **`StageAssetManager` 가 시작 시 선로드·검증·공유**한다(18.5).
- `WaitForMonsterDead` 는 **monsterKey 단위**(매칭 사망 아무거나 재개). objectId 정밀 대기(`WaitForObjectDead`)는 미구현.

### 18.5 자산 선로드 + 바이트코드 공유 (`StageAssetManager`)

다인스턴스 던전이 같은 레이아웃/스크립트를 공유하는 비용을 줄이기 위해, 시작 시 1회 로드·검증·공유한다(NavMesh/GameData 와 동일 패턴).

- **레이아웃** — `GameData_Stage` 전체를 순회해 로드. 파일명은 **`GameData_Stage.StageLayoutFileName`** 기준(예: `Town.json` — NavMesh 파일명과 동일 컨벤션, 같은 이름 공유 Stage 는 같은 파일을 읽음). `m_layouts` 는 stageDataKey 키 유지라 Stage 는 `const StageLayout*` 로 **공유 불변 참조**만(인스턴스마다 json 파싱 안 함). 불변이라 멀티 컨텐츠 스레드 락 없이 안전. (← 초기 설계의 `<stageDataKey>.json` 네이밍에서 `StageLayoutFileName` 로 보정.)
- **스크립트 바이트코드** — 명시된 `ScriptName#` 을 **이름별 1회 컴파일**(raw Lua C API `luaL_loadfile`+`lua_dump`, strip=0)해 바이트코드 보관. 각 Stage 의 lua_State 는 `sol::load(..., load_mode::binary)` 로 **로드만**(파싱 생략). VM 은 인스턴스별 유지(불가피).
- **fail-fast 검증** — 명시된 스크립트 파일 누락/컴파일 실패면 `LoadAll`→false→`GameServer::Initialize` 가 서버 시작 중단. **레이아웃도 동일** — `StageLayoutFileName` 이 명시됐는데 `.json` 이 없거나 파싱 실패하면 false. (`StageLayoutFileName`/`ScriptName` 이 비어있으면 본래 비어있는 Stage·SystemStage 로 정상 허용.)
- 배선: `GameServer` 가 `StageAssetManager` 소유, NavMeshManager 직후 `LoadAll`. `Stage::OnStart` 가 `FindLayout`/`FindScriptBytecode` 로 조회.
- 메모/속도 노트: 레이아웃은 완전 공유(이득 큼). 스크립트는 바이트코드만 공유 — lua_State(VM) 셋업·바인딩은 인스턴스별이라 남는다(VM 이 인스턴스당 메모리 주범). 단일 인스턴스 Stage(Town/필드)는 공유 이득 없음.

### 18.6 이벤트영역 — 클라 선판정 + 서버 검증/폴링 (구현 완료, Q1·Q2)

- **패킷**: `EventAreaEnterReq`/`EventAreaExitReq`(4012/4013, C→S, `event_key`+`pos_x/y/z`) 신설. `StageLayout` 가 `eventAreas`(key/shape/center/radius|size/secure) 로드.
- **`EventArea` 클래스** = `StageObject` 파생(신규 enum값 `EObjectType::EventArea`). 중심=오브젝트 위치, `Contains(px,pz,tol)` 평면(X-Z) 판정, occupant(userId) set 보유. Stage 가 `m_eventAreas`(eventKey→`shared_ptr<EventArea>`)로 보유. **AOI/sector 미등록**(클라는 씬에서 이미 앎 — 가시성 통보 안 함). (설계 Q2 그대로. `EObjectType::EventArea` 는 `GameEnum.xlsx` 에 추가.)
- **검증(§8.2 보정 — 이동속도 기반 허용오차)**: 클라는 예측 이동이라 서버 **권위 위치가 latency 만큼 lag** → 권위 위치를 직접 영역과 비교하면 거의 항상 빗나간다(mismatch 폭주). 그래서 **영역 포함 판정은 클라가 보고한 위치**로 하고, 권위 위치와의 평면 괴리를 `MoveSpdTotal × kLagSeconds(0.5) + kAntiCheatMargin(1.0)` 이내로만 제한한다(거짓 보고 방지). `MoveSpdTotal` 은 `Character::GetStat().Get(EStat::MoveSpdTotal)`.
- **secure 영역**: `eventAreas` 의 `secure:true` 면 클라 보고를 무시하고, `Stage::pollSecureEventAreas`(`OnUpdate` 4.7단계)가 매 tick **권위 위치로 직접** 진입/이탈 판정(안티익스플로잇 게이트용). 일반 영역은 클라 보고 구동(폴링 0).
- **콜백/occupant**: `OnEnterEventArea`/`OnExitEventArea` 멀티캐스트. occupant set 으로 중복 진입/이탈 방지, `OnUserLeave` 시 occupant 정리(stale 방지). `GetEventArea(key)` 바인딩 제공.
- **클라(`EventAreaDetector`, Game 어셈블리)**: 플레이어에 물리 콜라이더가 없어(NavMesh 이동) `OnTriggerEnter` 대신 **매 프레임 평면 거리판정**. 영역 안에 있는 동안 **0.5초 간격 Enter 재전송**(서버 일시 거부 자가치유 — 서버 occupant 중복제거로 트리거는 1회). 지오메트리는 런타임 프리팹의 `EventAreaMarker` 에서.

### 18.7 공지 배너(클라) + 배치 export 툴 (구현 완료)

- **공지 배너(클라)**: `UI_StageNotice`(코드빌드, 프리팹 무의존 — 상단 중앙, `durationMs` 후 페이드아웃). `StageManager` 가 `StageNoticeNtf` 핸들러 등록 → `Notice()` 전 구간(스크립트→서버→클라 배너) 완성.
- **배치 export 툴(에디터)**: 메뉴 `Tools/StageLayout/Export Active Scene` + 브릿지 명령 `EXPORT_STAGELAYOUT:<scene>`. 활성 씬의 마커(`SpawnerMarker`/`SpawnPointMarker`/`EventAreaMarker`/`WaypointMarker`)를 스캔해 **`<씬이름>.json`** 을 서버 `OUTPUT/Map/StageLayout/` 에 출력(NavMesh export 와 동형). 클라는 런타임 프리팹의 `EventAreaMarker` 를 직접 쓰므로 json 은 **서버 전용**. 마커 컴포넌트는 `NavMeshSource` 와 같은 "씬 오토링 마커" 부류라 **NavMesh 어셈블리**에 둠.
- **콘솔 UTF-8**: `GameServer/main.cpp` 에 `SetConsoleOutputCP/SetConsoleCP(CP_UTF8)` — exe 를 콘솔에서 직접 실행할 때 한글 로그가 깨지지 않게(로그/Lua 가 UTF-8).

### 18.8 오브젝트 상호작용 — 문/레버/NPC (구현 완료, fire-and-forget)

- **패킷**: `ObjectInteractReq`(4014, C→S, `prop_key`+`pos_x/y/z`) 신설. 상태 동기화 없음 → **Ntf 없음**(연출은 스크립트가 `Notice` 등으로).
- **배치**: `StageLayout` 가 `props`(`Prop{key,type,x,y,z,range}`) 로드 + `GetProp(key)` 바인딩. 클라 씬 마커(`PropMarker`)를 export 로 수집.
- **검증**: EventArea 와 동일 결 — 클라 보고 위치가 마커 `range` 안인지 + 권위 위치와의 평면 괴리를 `MoveSpdTotal×lag+여유` 로 제한(거짓 보고 방지).
- **콜백**: `OnObjectInteract(markerKey, userId)` 멀티캐스트. prop 은 StageObject 안 만듦(발동만 — `m_propObjects` 미사용).
- **클라**: `PropMarker`(NavMesh 어셈블리, Key/Type/InteractRange) — export 소스 + 런타임 근접 판정 겸용. `PropInteractor`(Game, StageManager 부착) 가 Interact 키(Input System `Gameplay/Interact`=F)에 범위 내 최근접 `PropMarker` 로 `ObjectInteractReq` 송신. (`InputManager.OnInteract` 이벤트 경유.)

### 18.9 아직 안 된 것 / 다음

- `TeleportPlayer` / `MoveMonsterAlongPath`(순찰·에스코트 — Waypoint 데이터는 로드/조회되나 실제 이동은 미구현) — 미구현.
- `ShowObjective` / `ShowCountdown` / `StageClear` / `StageFail` UI(목표/카운트다운/클리어·실패) — 미구현.
- 핫리로드 — 미구현. (바이트코드 캐시는 18.5 로 구현됨.)

---

## 부록: 참고

- Eluna Lua Engine (TrinityCore/MaNGOS 서버사이드 Lua 스크립팅): https://github.com/ElunaLuaEngine/Eluna
- Eluna 소개/예시 — RochetCode: https://rochet2.github.io/Eluna.html
- Lua in C++ with sol2: https://thatonegamedev.com/cpp/introduction-to-lua-in-c-with-sol2/
- Using Lua with C++ (LuaJIT/sol2 논의): https://edw.is/using-lua-with-cpp/
- 스크립트 엔진 비교(Lua vs AngelScript 등): https://discourse-urho3d.github.io/t/comparison-of-scripting-engines-just-for-fun/1439/
