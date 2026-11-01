# Prop 엔티티 시스템 (상태를 갖는 상호작용 월드 오브젝트)

문/레버/스위치/포탈 같은 prop 을 **상태를 갖는 월드 엔티티**(`PropObject` : `StageObject`)로 다룬다.
이전의 fire-and-forget 트리거에서 격상한 것이다. (착수 2026-06-28, 서버 v1.)

## 1. 설계 원칙 — 상태(state)와 효과(effect)의 분리

prop 동작에는 직교하는 두 축이 섞여 있다. 이 둘을 분리하는 것이 설계의 핵심이다.

| 축 | 무엇 | 누가 결정 | 스크립트 |
|----|------|----------|---------|
| **상태(state)** | 레버 on/off, 파괴됨 등 시각/동기화 상태 | **데이터(`GameData_Prop`) — 서버 권위 자동** | 불필요 |
| **효과(effect)** | 몬스터 스폰, 텔레포트 등 게임플레이 | **스크립트 `OnObjectInteract`** (+ 선언형 `Behavior`) | 기능 있는 prop 만 |

→ 상태 전이는 항상 데이터로 서버가 자동 처리하고, 게임플레이 효과만 스크립트가 담당한다.
그래서 단순 토글 레버/장식은 **무스크립트**, 기능 prop(스위치 스폰/포탈)만 스크립트 또는 선언형 데이터를 쓴다.

## 2. 데이터 2계층 (몬스터와 동형)

몬스터가 `GameData_Monster`(종류) + 스포너 배치(인스턴스)로 나뉘듯 prop 도 같다.

### `GameData_Prop` — 종류(type) 단위, 인스턴스가 공유 (`Common/GameData/Prop.xlsx`)
| 필드 | 의미 |
|------|------|
| `Key` | prop 종류 키. 배치의 `type` 이 가리킴 |
| `Interactable` | false 면 상호작용 거부(장식/정적) |
| `StateMode` | `None`(불변) / `Toggle`(순환) / `OneShot`(0→1 1회) |
| `StateCount` | Toggle 시 상태 개수(2=on/off) |
| `InitialState` | 시작 상태 |
| `MaxInteract` | 상호작용 가능 횟수(0=무제한) |
| `CooldownMs` | 재상호작용 쿨다운 |
| `InteractRange` | 상호작용 사거리(m) |
| `DespawnDelayMs` | OneShot/파괴 후 자동 제거 지연(0=영구) |
| `Behavior` | 선언형 동작: `None` / `Portal` |
| `PortalStageKey`, `PortalPositionType` | Portal 목적지 스테이지 + 도착 위치 타입(`GameData_StageStartPosition`) |

### `StageLayout::Prop` — 배치 인스턴스 단위 (유니티 export)
`key`(배치 인스턴스 키), `type`(→`GameData_Prop`), `x/y/z/yaw`, `range`/`initialState` override, `param0`(예: 포탈 목적지 스테이지 override), `param1`(예비).

## 3. 식별 — 배치키 vs ObjectId (예시2의 핵심)

스크립트는 런타임 `objectId`(서버가 스폰 때 발급)를 작성 시점에 알 수 없다.
그래서 작성자가 유니티에서 붙인 **배치 키(placementKey = layout `key`)** 로 식별한다(EventArea 와 동일 패턴).

```lua
function OnObjectInteract(propObjectId, placementKey, actorObjectId, newState)
    if placementKey == SWITCH_A then        -- ★배치 키로 분기 (objectId 아님)
        SpawnMonster(...)                    -- 예시2: 스위치 N회 → 몬스터 스폰
    elseif placementKey == BOSS_LEVER then
        SpawnMonster(BOSS)                   -- 예시3: 레버 1회 → 보스
    end
end
```
- `placementKey` → 로직 분기 (안정 식별자)
- `propObjectId` → 타게팅 (`GetPropState`/`SetPropState`)

## 4. 7가지 prop 예시 → 데이터 매핑

| # | prop | StateMode | MaxInteract | 효과 | 스크립트 |
|---|------|-----------|-------------|------|---------|
| 1 | 토글 레버(기능없음) | `Toggle(2)` | 0 | 없음 | ✗ |
| 2 | 스위치 N회 → 몬스터 스폰(상태 불변) | `None` | N | OnObjectInteract | ✓ |
| 3 | 레버 1회 → On + 보스(상태 변함) | `OneShot` | 1 | OnObjectInteract | ✓ |
| 4 | 파괴불가 장식 | `None` + `Interactable=false` | — | 없음 | ✗ |
| 5 | 파괴가능(v1 근사) | `OneShot` + `DespawnDelayMs` | 1 | (HP 파괴는 v2) | — |
| 6 | 포탈 | `None` + `Behavior=Portal` | 0 | **선언형 데이터** | ✗ |

## 5. 동기화

- **`PropSpawnInfo`** (`ObjectVisibilityNtf.prop_spawns`): `{object_id, prop_key(type), pos, yaw, state}`. AOI 진입 시 현재 상태까지 받아 즉시 올바른 비주얼.
- **`PropStateNtf`**(4015): `{object_id, state, actor_object_id}`. 상태 변경 시 prop 주변 AOI broadcast.
- prop 은 **정적이라 위치 SnapshotNtf 스트림에서 제외**(sector `m_props` 는 스냅샷 패스가 순회 안 함). 상태 변화만 이벤트로 전송 → 대역폭 최소.

## 6. 서버 구현 지도

| 요소 | 위치 |
|------|------|
| 엔티티 | `StageObjects/PropObject.{h,cpp}` — `TryInteract`(게이팅+상태전이), `SetState`, 디스폰 타이머 |
| 종류 데이터 | `GameDataLib/Generated/GameData_Prop.*` (xlsx 생성), `EPropStateMode`/`EPropBehavior` (`GameEnum_Prop.h`) |
| 배치 데이터 | `Stages/StageLayout.{h,cpp}` `Prop` 구조체 + `GetProps()` |
| 인스턴스화 | `Stage::OnStart` (EventArea 와 동형, `registerObject`) |
| 상호작용 | `Stage::handleObjectInteractReq` |
| 스폰/디스폰/상태 | `Stage::SpawnProp`/`DespawnProp`/`SetPropState`/`FindProp`/`broadcastPropState` |
| 포탈 | `Stage::triggerPropBehavior` → `executeLocalStageMove`(handleStageMoveReq 공용) |
| 라이프사이클 | `Stage::updateProps` (DespawnDelay 타이머) |
| AOI 합류 | `Stage::EnterStage`, `updateVisibilityOnSectorChange` (GetProps 수집) |
| 패킷 송신 | `PacketSender::SendObjectVisibilityNtf`(propSpawns), `SendPropStateNtf` |
| Lua | `StageScript.cpp` — `GetPropState`/`SetPropState`/`SpawnProp`/`DespawnProp`, `OnObjectInteract` 콜백 |

## 7. 미구현 / 다음 (v2)

- **클라이언트**(별도 트랙): `PropMarker`(yaw/initialState/param export + 런타임 ObjectId 매핑), `PropInteractor`(`object_id` 송신), `PropSpawnInfo`/`PropStateNtf` 수신 핸들러(상태별 시각 전환).
- **HP 기반 파괴 prop**: `ActorObject` 파생 + 전투 결합 + `ObjectDeathNtf`. (v1 은 `OneShot`+`DespawnDelay` 근사.)
- **크로스서버 포탈**: 현재 같은 서버 이동만. `BeginCrossServerMove` 연동은 추후.
