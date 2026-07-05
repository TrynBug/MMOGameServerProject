# 디아블로4 일반몬스터 AI 설계 — 패킷 / 게임데이터 구조

> **상태: v1 서버↔클라 구현·런타임 검증 완료.** 실제 코드/데이터/구현 중 결정은 **14장(구현 현황)** 참조. (1~13장은 설계 근거.)

> 범위: **일반몬스터(Normal) + FSM AI** 만 다룬다. 엘리트 = Behavior Tree 는 나중.
> 전제: 위치 동기화(SnapshotNtf 스트리밍 + 보간), 20슬롯 포위 이동(`MonsterFsmAI.cpp`), 스킬 효과 파이프라인(`GameData_Skill` + `EffectShape` + `ApplyEffectDamage`)은 **유지·재사용**한다.
> 핵심 결론을 먼저: 새로 추가할 패킷은 사실상 **1개(시전 시작 통보, `AbilityCastNtf`)** 뿐이고, 몬스터 공격의 효과/대미지/사망은 전부 기존 스킬 시스템을 그대로 탄다. 새 게임데이터는 **AI 프로파일 테이블 1개 + GameData_Monster 컬럼 몇 개** 면 충분하다. 단, **AI 업데이트 주기는 think(성김) / 이동·전투타이머(매 tick)를 분리**해야 하고, **몬스터 투사체는 서버 권위로 시뮬레이션**한다. **피격감은 서버 권위를 유지하되 텔레그래프=히트박스 + 수비자 우대 lag-comp + 피드백 가독성으로 보완**한다.

> **확정된 결정 (9장 상세):** ① 이동속도 = `MoveSpdTotal` 스탯, ② AI 튜닝 = `GameData_MonsterAI` 프로파일 분리, ③ 시전 통보 = 신규 `ability_packet.proto`, ④ 근접 단타는 발동 시점 `SkillCastNtf` 생략, ⑤ 패킷명 = `AbilityCastNtf`(NPC/엘리트 공용), ⑥ 업데이트 = 적응형 주기, ⑦ 몬스터 투사체 = 서버 권위, ⑧ 피격감 = 권위 유지 + 수비자 우대.

---

## 1. 디아블로4 일반몹 AI 분석

공개 자료(공식 GDC 강연은 없음)와 인게임 동작에서 확인되는 D4 잡몹의 행동 골격은 다음과 같다. 핵심은 "똑똑해 보이지만 규칙은 단순하고, 모든 위협이 **읽을 수 있게(telegraph)** 예고된다"는 점이다.

**(1) 활성화 / 어그로.** 몬스터는 시야·근접 범위에 플레이어가 들어오면 깨어난다. D4는 한 마리가 깨면 같은 팩(pack)이 함께 활성화되는 경향이 있다(어그로 전파). — 우리 게임: 현재 `AcquireTarget`(주변 sector 최근접 유저)로 충분. 팩 전파는 *나중*.

**(2) 분산 / 포위.** D4 잡몹은 한 점에 겹치지 않고 플레이어를 둘러싸며 측면을 친다. 근접은 붙고, 원거리는 거리를 유지하며 카이팅한다. — 우리 게임: **이미 구현됨**(타겟 둘레 20슬롯 + objectId 지터, 원거리 desiredRange 밴드). 이 구조를 그대로 유지한다.

**(3) 텔레그래프 공격 (D4의 핵심).** 의미 있는 공격은 전부 **윈드업(선딜) + 바닥/모션 예고 → 타격 → 회복** 의 3박자다. 플레이어의 반격(회피)은 "예고를 읽고 피한다"로 성립한다. 잡몹은 예고가 짧고 약하며, 엘리트/보스는 크고 위험하다.

**(4) 공격 커밋(commitment).** 윈드업이 시작되면 몬스터는 그 공격에 **묶인다** — 이동/재조준 불가, 윈드업 도중 타겟 위치는 **고정**된다(그래서 옆으로 굴러 피할 수 있다). 오직 CC(스턴/넉백/빙결)나 사망만 이를 끊는다.

**(5) 회복(recovery).** 타격 후 짧은 경직이 있어, 이 틈이 플레이어의 반격 창이 된다.

**(6) 반응성.** 피격 시 플린치(움찔), CC에 의한 시전 캔슬, 넉백/이동기에 의한 변위.

**(7) 리쉬 / 리셋.** 스폰지점에서 너무 멀어지면 추격을 포기하고 복귀 + 풀피 회복. — 우리 게임: **이미 구현됨**(`Return` 상태 + leash).

**가져올 것 / 버릴 것 (우리 게임 = 가벼운 hack&slash)**

| D4 요소 | 우리 채택 | 비고 |
|---|---|---|
| 텔레그래프 + 윈드업 + 커밋 + 회복 | **채택 (핵심)** | 본 설계의 주제. 타격감·회피의 기반 |
| 분산·포위, 원거리 카이팅 | **유지** | 이미 구현(20슬롯/밴드) |
| 리쉬/리셋 | **유지** | 이미 구현 |
| 팩 어그로 전파 | 나중 | v1은 개별 어그로 |
| 난이도별 AI 스케일링(공속/타게팅 강화) | 나중 | 데이터로 확장 가능하게만 |
| 정교한 strafe/측면 기동 | 나중 | v1은 슬롯 이동으로 충분 |

---

## 2. 핵심 설계 원칙 — "AI 상태가 아니라 관측 가능한 행동을 복제한다"

가장 중요한 결정. 클라이언트는 몬스터의 **FSM 내부 상태를 절대 모른다.** 와이어를 건너는 것은 두 종류뿐이다:

1. **권위 트랜스폼** — 위치/회전/이동플래그 (`SnapshotNtf`, 매 tick 스트리밍).
2. **관측 가능한 행동 사건** — 시전 시작(예고), 효과 발생, 대미지, 사망.

이렇게 두면 얻는 이득이 로드맵과 정확히 맞는다:

- **클라는 AI 종류에 불변(agnostic).** 일반몹=FSM, 엘리트=BT 로 두뇌가 달라도 둘 다 **같은 행동 패킷**을 쏜다. 그래서 BT를 나중에 붙여도 클라 코드는 건드릴 필요가 없다. (이게 패킷명을 `MonsterCastNtf` 가 아니라 **`AbilityCastNtf`** 로 가는 이유 — NPC/엘리트도 공용.)
- **서버 AI를 자유롭게 갈아끼운다.** 지금 FSM 구조를 버리고 새로 짜도 패킷 계약만 지키면 클라 영향 0.

이 원칙의 직접적 귀결: **이동 의도/추격/포위 같은 AI 행동은 패킷이 없다.** 클라는 위치 스트림만 보고 idle/run 을 그릴 뿐이다. 새 패킷이 필요한 유일한 지점은 "위치만으로는 알 수 없는 행동" = **공격 시전(예고)** 이다.

---

## 3. FSM 상태 머신 (현재 → 확장)

현재: `Idle → Chase → Attack → Casting → Return → Dead` (`MonsterFsmAI.h`).
D4식 3박자를 명시적으로 드러내도록 `Casting` 을 **윈드업/타격/회복**으로 정리한다(상태 추가는 최소화).

```
        ┌────────────────────── 사망/CC ──────────────────────┐
        ▼                                                      │
 [Idle] ──aggro──▶ [Chase] ◀──band 이탈── [Attack(스킬선택)]   │
   ▲                 │                          │              │
   │            leash│ 타겟소실                  │ 스킬 ready    │
   │                 ▼                          ▼              │
   └──도착+풀피── [Return]              [Windup(=현 Casting)]   │
                                          · 시전시작 통보 ──────┘  ← AbilityCastNtf
                                          · 타겟/방향 고정(커밋)
                                          · CastDelayMs 동안 잠금
                                              │ 선딜 경과
                                              ▼
                                       [Strike] 효과 발동
                                          · ApplyEffectDamage(기존)
                                          · (투사체/장판이면) 서버 투사체 spawn + SkillCastNtf(기존)
                                              │
                                              ▼
                                       [Recovery] ActionLockMs 잠금
                                              │ 경과 → Attack 로
```

상태별 책임 요약:

| 상태 | 하는 일 | 와이어로 나가는 것 |
|---|---|---|
| Idle | 어그로 탐색 | (위치 스트림만) |
| Chase | 슬롯/카이팅 밴드로 이동 | 위치 스트림(isMoving=1) |
| Attack | 사거리 내 사용 가능 스킬 선택 | — |
| **Windup** | 타겟·방향 **고정**, 선딜 잠금 | **AbilityCastNtf (신규)** |
| **Strike** | 효과/대미지 발동 | SkillDamageNtf / (투사체·장판) SkillCastNtf |
| **Recovery** | 후딜 잠금(반격 창) | (위치 스트림, isMoving=0) |
| Return | 스폰 복귀 | 위치 스트림 |
| Dead | 정지 | ObjectDeathNtf |

**커밋/캔슬 규칙 (D4 동일):**
- Windup 진입 시 타겟 위치·시전 방향을 **스냅샷으로 고정.** 윈드업 도중 재조준/재회전 금지(그래서 회피가 성립).
- 캔슬은 **CC(스턴/넉백/빙결)와 사망**만. 캔슬 시 자원 환불 없음(우리 `스킬.md` 규칙과 동일). v1에서 몬스터 CC가 아직 없으면 사망만 처리하고 CC 캔슬은 훅만 남겨둔다.
- Recovery 의 길이 = `GameData_Skill.ActionLockMs`. 이 값이 플레이어의 반격 창을 만든다.

**시야(LoS) 게이트:**
- Chase→Attack 진입(inAttackBand)과 Attack 유지(needReposition) 판정에 몬스터→타겟 LoS 를 검사한다. 벽/절벽 너머면 공격하지 않고 접근/재배치로 전환한다(길찾기가 벽을 돌아감). 거리 조건을 통과했을 때만 raycast(성능).
- 원리·공용 프리미티브(`StageNavMesh::IsLineOfSight` / `Stage::HasLineOfSight`)와 폭발/범위 LoS 는 게임서버.md "시야(LoS) 검증" 참조.
- 한계: NavMesh raycast 는 지표 2D 판정(평지형 전용). 원거리 몬스터는 LoS 얻는 위치를 스마트하게 잡진 못함(v1 허용).

---

## 4. 패킷 설계

### 4.1 무엇이 와이어를 건너는가

| 사건 | 패킷 | 신규/재사용 | 방향 |
|---|---|---|---|
| 몬스터 스폰/디스폰 | `ObjectVisibilityNtf` (`MonsterSpawnInfo`) | 재사용 | S→C AOI |
| 위치/회전/이동 | `SnapshotNtf` (`ActorStateInfo.flags`) | 재사용 | S→C unicast/tick |
| **공격 시전 시작(예고)** | **`AbilityCastNtf`** | **신규** | S→C AOI |
| 효과 비주얼(투사체/장판) | `SkillCastNtf` | 재사용 | S→C AOI |
| 대미지 숫자 + HP | `SkillDamageNtf` (+공격자/방향 필드, 8장) | 재사용·확장 | S→C AOI |
| 사망 | `ObjectDeathNtf` | 재사용 | S→C AOI |

요점: **신규는 `AbilityCastNtf` 하나뿐.** 나머지는 이미 몬스터를 시전자로 지원한다(`QueryEnemiesInShape` 가 `Monster→User` 진영을 이미 처리, `ExecuteSkill` 이 이미 `ApplyEffectDamage` 호출).

### 4.2 왜 플레이어의 SkillCastNtf 를 그대로 못 쓰나

플레이어 경로는 **발동(선딜 경과) 시점**에 `SkillCastNtf` 를 쏜다. 시전 클라가 이미 로컬 예측으로 윈드업을 재생했고, 원격 클라는 **효과만** 재현하면 되기 때문이다(`SkillSystem.cs` 주석: "서버는 entry 발동 시점에 CastNtf를 보내므로 원격은 즉시 재현").

그러나 몬스터는 **로컬 예측자가 없다(항상 원격).** 누군가는 "지금부터 N ms 동안 윈드업 + 예고를 그려라"를 클라에 알려야 한다. 그래서 **시전 시작 시점** 통보가 별도로 필요하다. 이 둘은 의미가 다른 사건이다:

- `AbilityCastNtf` = **시작**(예고/윈드업 시작). Windup 진입 시 송신.
- `SkillCastNtf` = **발동**(효과 재현). Strike 시점에, **투사체/장판 스킬일 때만** 기존 그대로 송신.

**근접 단타(ContactHit 아님·투사체 아님)는 윈드업 모션이 곧 타격 모션이므로 `AbilityCastNtf` + `SkillDamageNtf` 만으로 충분하고, 발동 시점 `SkillCastNtf` 는 생략한다.** (확정 결정 ④)

### 4.3 신규 패킷 — `AbilityCastNtf`

신규 파일 **`ability_packet.proto`** 에 둔다(몬스터/NPC/엘리트 시전 통보가 늘 것에 대비). 좌표계는 기존과 동일(Y=높이, X-Z 평면, dir 정규화 X-Z, yaw=degree).

```proto
// 능력(공격) 시전 "시작" 통보 (서버 -> 클라 AOI 브로드캐스트).
// 몬스터/NPC/엘리트 공용. Windup 진입 시점에 송신. 클라는 이걸 받아:
//   1) caster 를 origin/dir 로 회전 고정,
//   2) skill_key 로 GameData_Skill 조회 → CastAnim 윈드업 재생,
//   3) EffectShape+size 로 바닥 텔레그래프를 windup_ms 동안 채운다.
// 실제 대미지/효과는 windup 종료 후 SkillDamageNtf / SkillCastNtf 로 도착한다.
message AbilityCastNtf {
    int64  caster_object_id = 1;
    int32  skill_key        = 2;   // GameData_Skill 의 Key (능력 = 스킬 데이터)
    int64  target_object_id = 3;   // 조준 대상(없으면 0). 추적형 예고/디버그용. v1은 표시에 안 써도 됨
    float  origin_x         = 4;   // 예고 기준점(근접/투사체=시전자 위치, 타겟장판=고정된 타겟 위치)
    float  origin_y         = 5;
    float  origin_z         = 6;
    float  dir_x            = 7;   // 고정된 시전 방향(정규화 X-Z)
    float  dir_z            = 8;
    int32  windup_ms        = 9;   // 선딜(=CastDelayMs). 클라 텔레그래프 채움 시간. 0이면 즉발(예고 없음)
}
```

설계 노트:
- `windup_ms` 를 패킷에 실어 보낸다(데이터에서도 알 수 있지만, **서버 권위 타이밍**을 단일 출처로 못박기 위해). 클라 텔레그래프는 이 값으로 채우고, 실제 대미지는 서버가 Strike 시점에 적용하므로 둘의 미세 오차는 허용(예고는 "읽기", 대미지는 권위). ※ 이 타이밍 정합성은 6장(업데이트 주기 분리)·8장(피격감)과 직결된다.
- `seed` 는 v1 근접/단순 장판엔 불필요. scatter(메테오류) 예고를 몬스터가 쓰게 되면 그때 필드 추가.

### 4.4 타이밍 시퀀스

```
서버 FSM                         클라(원격 관전자)
────────                         ─────────────────
Attack: 스킬 idx 선택
Windup 진입 ──► AbilityCastNtf ──► caster 회전고정 + CastAnim 윈드업 재생
  타겟/방향 고정                    + 바닥 텔레그래프 windup_ms 동안 채움
  CastDelayMs 잠금
        │ (선딜 경과)
Strike: ExecuteSkill
  · ApplyEffectDamage ─────────► SkillDamageNtf ─► 대미지 숫자 + HP바 + 방향 피격표식
  · (투사체면) 서버투사체 spawn ─► SkillCastNtf ──► 비주얼 투사체 재현(hit 보고 안 함)
  · StartSkillCooldown
Recovery: ActionLockMs 잠금
        │ (후딜 경과)
Attack 로 복귀
```

### 4.5 중간 진입(AOI late-join) 처리

윈드업 도중인 몬스터의 AOI에 유저가 새로 들어오면, 그 유저는 `AbilityCastNtf` 시작을 놓친다. v1 처리: **무시**(다음 공격부터 정상 표시). 정밀히 하려면 `MonsterSpawnInfo` 에 "현재 시전 중 스킬키 + 남은 윈드업" 필드를 추가하지만, 잡몹 선딜이 짧아 비용 대비 효과가 낮다 → *나중*. (corpse 의 `is_dead` 처럼 필요해지면 spawn 스냅샷에 얹는다.)

---

## 5. 게임데이터 설계

### 5.1 재사용: `GameData_Skill` = 몬스터 ability

몬스터의 공격 1종 = 스킬 데이터 1행. **새 ability 테이블을 만들지 않는다.** `GameData_Skill` 이 이미 텔레그래프 공격에 필요한 컬럼을 전부 갖고 있다:

| 몬스터 공격에 필요한 것 | `GameData_Skill` 컬럼 | 쓰임 |
|---|---|---|
| 공격 애니메이션 | `CastAnim` | 클라 윈드업 모션 |
| 윈드업(선딜) | `CastDelayMs` | 텔레그래프 채움 시간 = `windup_ms` |
| 회복(후딜) | `ActionLockMs` | Recovery 잠금 = 반격 창 |
| 텔레그래프 모양 | `EffectShape`(None/Circle/Obb) | 클라 예고 + **서버 히트판정 공유** |
| 모양 크기 | `Radius` / `ObbWidth` / `ObbLength` | 예고/판정 크기 |
| 효과 종류 | `EffectDamage`(None/ContactHit/Area) | 근접직격 / 투사체 / 장판 |
| 투사체 속도 | `ProjectileSpeed` | **서버 투사체 시뮬 속도** (7장) |
| 사거리 | `MaxRange` | 사용 가능 거리 / 투사체 최대사거리 |
| 쿨다운 | `CooldownMs` | 재사용 대기 |
| 대미지 계수 | `DamageCoeff` | 서버 대미지 = f(스탯, 계수) |
| 배치 기준 | `Placement`(Caster/Target/Forward) | 예고 위치 기준점 |
| 폭발 연계 | `OnHitSkillKey` | 투사체 적중/만료 시 폭발(기존 AreaEffect 경로) |

몬스터 스킬 행은 `Job=None`, `CastClass=Stationary`(시전 중 고정)로 두면 된다. `ManaCost=0`. 즉 **데이터 입력만으로** 근접 단타·원거리 투사체·바닥 장판 몬스터 공격을 만든다.

선택적 추가 컬럼(필요할 때만):
- `TelegraphPrefabPath`(client) — 예고 데칼 아트가 효과 아트와 다를 때. v1은 `EffectShape`+크기로 클라가 기본 데칼을 그려서 **불필요**.
- `HitboxScale`(server) — 서버 히트박스를 비주얼 텔레그래프보다 살짝 작게(수비자 우대, 8장). 없으면 1.0. v1은 상수 1개로 대체 가능.
- `IsMonsterOnly`(none/server) — 플레이어 스킬 UI 목록에서 몬스터 스킬을 거르고 싶을 때. 지금은 `Job=None` 으로 충분.

### 5.2 신규: `GameData_MonsterAI` (AI 프로파일 / 아키타입)

여러 몬스터 종류가 같은 행동(근접 돌격형, 원거리 카이팅형 등)을 공유하므로, **AI 튜닝을 프로파일 테이블로 분리**하고 `GameData_Monster` 가 키로 참조한다(확정 결정 ②). 현재 `Monster.h` 에 하드코딩된 상수들(`m_aggroRange`, `m_leashRange`, `m_desiredRange`)이 여기로 이동한다. **이동속도는 여기 두지 않는다 — `MoveSpdTotal` 스탯에서 온다(확정 결정 ①, 5.4 참조).**

| 컬럼 | 타입 | 출력 | 설명 |
|---|---|---|---|
| `Key` | int32 | all | AI 프로파일 키 |
| `Name` | string | all | 아키타입 라벨(예: MeleeRusher, RangedKiter) — 참고용 |
| `AIType` | enum `EMonsterAIType` | server | Fsm / BehaviorTree. **일반몹=Fsm.** BT는 나중 |
| `AggroRange` | float | server | 어그로 감지 거리 |
| `LeashRange` | float | server | 스폰 이탈 복귀 거리 |
| `DesiredRange` | float | server | 원거리 유지 거리(카이팅 밴드). **0=근접** |
| `EngagedUpdateIntervalMs` | int32 | server | 타겟 관여 중 업데이트 주기(ms). 6장 적응형 주기용. 예: 100 |

> 20슬롯 포위 상수(`k_attackSlot*`)는 요청대로 코드에 유지. 나중에 아키타입별로 다르게 하고 싶어지면 이 테이블로 끌어올린다.
> `EngagedUpdateIntervalMs` 는 6장(업데이트 주기 분리)에서 "타겟을 잡았을 때 승격할 주기"를 데이터로 잡기 위한 것. idle 주기는 코드 상수(예: 500ms)로 두고, 관여 시 이 값으로 올린다.

신규 enum (`GameEnum.xlsx` → `GameEnum_Monster`):
```
EMonsterAIType : None=0, Fsm=1, BehaviorTree=2
```

### 5.3 확장: `GameData_Monster` 컬럼 추가

현재 컬럼: `Key, Name, PrefabPath, Grade, Stat1~8, StatValue1~8`. 여기에 추가:

| 추가 컬럼 | 타입 | 출력 | 설명 |
|---|---|---|---|
| `AIKey` | int32 | server | `GameData_MonsterAI.Key` 참조 |
| `SkillKey1` | int32 | server | ability = `GameData_Skill.Key`. **순서 = 우선순위** |
| `SkillKey2` | int32 | server | (없으면 0). 숫자접미 규칙으로 `GetSkillKeyCount()/GetSkillKey(i)` 자동 생성 |
| `SkillKey3` | int32 | server | 필요 수만큼. v1 잡몹은 1~2개면 충분 |

`SkillKey#` 는 게임데이터 파이프라인의 **숫자접미 순회 규칙**(`Stat#` 과 동일)을 그대로 활용한다(`게임데이터.md`). 출력은 `server` (클라는 몬스터 스킬 목록을 알 필요 없음 — 행동은 서버가 통보).

이동속도 컬럼은 추가하지 않는다. 몬스터의 기존 `Stat#/StatValue#` 에 이동속도 계열 스탯(`MoveSpdAdd` 등)을 넣으면 `BasicStatComponent` 가 `MoveSpdTotal` 로 계산한다.

### 5.4 이동속도 = `MoveSpdTotal` 스탯 (확정 결정 ①)

`m_moveSpeed`(현재 하드코딩 4.0)를 제거하고, 이동은 `GetStatTotal(EStatGroup::MoveSpd)`(= `MoveSpdTotal`, 현재 이동속도)에서 읽는다. 이렇게 하면 둔화/가속 **버프가 이동속도에 자동으로 반영**된다(버프 → 스탯 → MoveSpdTotal 재계산, 별도 배선 불필요). 몬스터의 기본 이동속도는 `GameData_Monster` 의 Stat 컬럼으로 시드한다.
- 서버 코드 영향: `Monster::MoveTo` 가 `m_moveSpeed` 대신 `GetStatTotal(EStatGroup::MoveSpd)` 를 `m_mover.Update` 에 넘긴다.

### 5.5 데이터 예시

`GameData_MonsterAI`
| Key | Name | AIType | AggroRange | LeashRange | DesiredRange | EngagedUpdateIntervalMs |
|---|---|---|---|---|---|---|
| 1 | MeleeRusher | Fsm | 10 | 20 | 0 | 100 |
| 2 | RangedKiter | Fsm | 12 | 22 | 7 | 100 |

`GameData_Skill` (몬스터 ability)
| Key | Name | Job | CastClass | CastAnim | CastDelayMs | ActionLockMs | EffectDamage | EffectShape | Radius | ObbWidth | ObbLength | ProjectileSpeed | MaxRange | CooldownMs | DamageCoeff |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 9001 | Mob_Claw | None | Stationary | mob_claw | 350 | 250 | None(근접직격) | Obb | 0 | 2 | 2.5 | 0 | 2.0 | 1500 | 1.0 |
| 9002 | Mob_Fireball | None | Stationary | mob_cast | 600 | 300 | ContactHit | Circle | 1.5 | 0 | 0 | 12 | 12 | 4000 | 1.6 |

> 근접 단타(9001)는 `EffectShape=Obb`(전방 직사각형)로 서버가 즉시 영역판정 → `ApplyEffectDamage`. 투사체(9002)는 발동 시점에 **서버 투사체 spawn**(7장) + `SkillCastNtf` 재현.

`GameData_Monster` (이동속도는 Stat 컬럼으로)
| Key | Name | PrefabPath | Grade | AIKey | SkillKey1 | SkillKey2 | Stat1 | StatValue1 | Stat2 | StatValue2 |
|---|---|---|---|---|---|---|---|---|---|---|
| 100 | Goblin | Monsters/Goblin | Normal | 1 | 9001 | 0 | StrAdd | … | MoveSpdAdd | 4.0 |
| 200 | DarkMage | Monsters/DarkMage | Normal | 2 | 9002 | 0 | IntAdd | … | MoveSpdAdd | 3.5 |

---

## 6. 업데이트 주기 분리 — think(성김) / 이동·전투타이머(매 tick)

### 6.1 문제: 단일 주기로 성기게 가면 깨진다

서버 tick 은 50ms. 오브젝트는 `SetUpdateIntervalMs` 로 정한 주기마다 `Update` 가 호출되고, `AdvanceUpdateClock` 이 누적시간(`elapsedMs`, 주기와 같거나 큼)을 넘긴다. **현재 코드는 `k_monsterUpdateIntervalMs = 50`(매 tick)이고, 바로 옆에 `TODO: AI think(성김)/이동 integrate(매tick) 분리` 주석이 있다**(`GameServerDefine.h:59`). 즉 "성기게 만들고 싶지만 그러면 분리가 필요하다"가 이미 인지된 상태다.

몬스터 `Update` 를 그냥 500ms 주기로 올리면 네 군데가 깨진다:

1. **이동이 500ms 청크로 끊김 (가장 큼).** `Monster::Update` 가 mover 를 `elapsedMs(≈500)` 한 번에 전진시키는데, 위치 복제는 `buildAndSendSnapshots` 가 매 tick(50ms) 한다. 권위 위치가 500ms마다 점프 → 클라 보간 waypoint 간격 500ms → 둥둥 뜨고 코너링·정지가 늦음. (몬스터를 50ms로 둔 이유가 바로 "스냅샷 스트리밍 부드러움".)
2. **텔레그래프/공격 타이밍이 500ms로 양자화.** 윈드업(`CastDelayMs`)·회복(`ActionLockMs`)·쿨다운 잔여가 `deltaMs=500`씩 감소. 500ms 미만 윈드업은 한 틱에 끝(실효 500ms), strike 는 500ms 경계에서만 발생. 클라 텔레그래프(`windup_ms=350`)와 서버 타격(다음 500ms 경계)이 어긋남 → **피격감 악화의 직접 원인**(8장).
3. **반응 지연.** 어그로 감지·Chase→Attack 전환·재조준(FaceTarget)이 최대 500ms 늦음. 멈출 지점을 ~500ms 지나치고, 움직이는 타겟을 늦게 바라봄.
4. **추격 목적지 stale.** 20슬롯 dest 를 500ms마다 재계산 → 움직이는 플레이어의 과거 위치를 쫓음.

### 6.2 해결: 두 박자로 나눈다

| 박자 | 주기 | 담당 |
|---|---|---|
| **integrate (매 tick, 50ms)** | 고정 50ms | 이동 적분(`m_mover.Update`), 활성 캐스트 타이머(windup/strike/recovery 카운트다운), sector 갱신 |
| **think (성김)** | idle 500ms / 관여 시 `EngagedUpdateIntervalMs`(예 100ms) | 의사결정: 어그로 탐색, 상태 전이, 스킬 선택, 슬롯/목적지 재계산, repath 판정 |

**전투 타이밍과 이동은 절대 성긴 주기로 양자화하면 안 된다.** 의사결정만 성기게 간다.

구현은 두 갈래 — 우리 구조엔 **(A) 적응형 주기**가 더 단순하다:

- **(A) 적응형 주기 (추천).** idle/Return 몬스터는 코드 상수(예 500ms)로 싸게. 타겟을 잡으면(Chase 진입) `SetUpdateIntervalMs(EngagedUpdateIntervalMs)`(예 100ms)로 승격, 타겟을 놓고 Idle/Return 으로 돌아오면 다시 500ms로 강등. `SetUpdateIntervalMs` 가 **이미 per-object** 라 변경이 작다. 대다수 잡몹이 idle 이라 비용도 절감(거리/관여도 LOD = D4 발상). 100ms 면 윈드업 350ms = 3~4스텝, 이동도 클라 보간으로 충분히 부드럽다.
- **(B) think/integrate 완전 분리.** think 는 500ms 유지, 이동 적분 + 캐스트 타이머는 매 tick 별도 패스로. 주석이 가리키는 정공법이지만, "오브젝트당 Update 1회" 모델을 깨야 해서 구조가 더 복잡하다. 잡몹 수가 폭발해 100ms think 비용도 부담될 때 도입.

**v1 권장:** (A) 적응형. idle=500ms, engaged=`EngagedUpdateIntervalMs`(데이터, 기본 100ms). 투사체는 어차피 Stage 소유로 매 tick 돌아가므로(7장) 몬스터 think 주기와 무관하다.

---

## 7. 몬스터 투사체 — 서버 권위 시뮬레이션

### 7.1 왜 서버가 시뮬레이션해야 하나 (권위 비대칭)

| | 플레이어 투사체 | 몬스터 투사체 |
|---|---|---|
| hit 판정 권위 | **클라(시전자)** 가 판정 → `SkillProjectileHitReq` 보고 | **서버** 가 시뮬레이션 |
| 서버 역할 | 사거리 sanity 검증만(투사체 안 굴림) | 매 tick 전진 + 충돌 + 대미지 |
| 이유 | 내 조작 반응성("클라가 먼저 반응") | 피해자=플레이어. 피해자 클라에 hit 맡기면 회피·치트 익스플로잇 |

`ProjectileGroup` 주석 그대로 "서버는 투사체를 매 tick 굴리지 않는다. 발사 파라미터 + 방향들만 들고 있다가, 클라가 hit 을 보고하면 그 시점 위치를 즉석 역산하여 검증한다." 이 모델은 **시전자가 곧 판정 주체**일 때만 성립한다. 몬스터는 시전자(서버측 AI)와 피해자(플레이어)가 분리되므로 권위가 서버로 가야 공정하고 안전하다.

### 7.2 좋은 소식 — 구조가 이미 받쳐준다

투사체/장판 효과는 **Stage 소유**이고 `Stage::Update` 의 `updateSkillEffects(deltaMs)` 단계(6번)에서 **매 tick(50ms) 처리**된다. `updateMonsters`(4번)와 별도다. 그래서 **몬스터가 500ms로 think 해도, 일단 발사된 투사체는 50ms로 굴러간다.** 6장의 주기 분리와 자동으로 맞물린다.

### 7.3 처리 흐름

1. **발사 (Strike 시점, `Monster::ExecuteSkill`).** 스킬의 `EffectDamage == ContactHit` 면 서버 투사체를 spawn. 파라미터(origin/dir/`ProjectileSpeed`/`MaxRange`/히트반지름=`Radius`)는 `GameData_Skill` 에서. 동시에 `SkillCastNtf` 로 클라에 비주얼 재현 지시.
2. **매 tick (`updateSkillEffects`).** 각 몬스터 투사체: `pos += dir * speed * dt` 전진 → 현재/인접 sector 의 **플레이어**와 반지름 충돌 검사 → hit 이면 `ApplyEffectDamage`(기존) + `OnHitSkillKey` 폭발(기존 `AreaEffect` 경로) 후 소멸. `MaxRange`/지형 도달 시 폭발/소멸. `ProjectileGroup::GetProjectilePosition(index, nowMs)` 가 이미 위치를 해석적으로 계산하므로 **능동 전진+충돌만 추가**하면 된다.
3. **클라.** `SkillCastNtf`(또는 `AbilityCastNtf`)를 받아 동일 speed/dir 로 **순수 비주얼** 투사체만 생성하고 **hit 보고는 하지 않는다**(플레이어 투사체와의 차이). 진짜 타격은 서버 `SkillDamageNtf` 가 권위.

### 7.4 구현 선택 & 비용

- **구현:** `ProjectileGroup` 에 "server-sim 모드"(능동 전진+충돌)를 더하거나, 가벼운 별도 `MonsterProjectile` 를 둔다. 폭발은 기존 `AreaEffect` 재사용.
- **클라 폭발 위치 정합:** 정확히 맞추려면 작은 "투사체 종료(impact 좌표)" 통보를 추가할 수 있다. v1은 클라가 자체 예측 종료하거나 `SkillDamageNtf` 수신 시 터뜨리면 충분 → 통보 생략.
- **비용:** 서버가 몬스터 투사체를 매 tick 시뮬하는 게 "수많은 투사체" 목표에서 제일 비싼 지점이다. 다만 몬스터 투사체 물량은 플레이어 탄막보다 훨씬 적고 sector 쿼리로 bound 된다. 많아지면 투사체 풀링 + 광역은 투사체 대신 `Area` 로 표현해 줄인다.

---

## 8. 피격감 / 넷코드 — 서버 권위 유지 + 수비자 우대

### 8.1 문제와 원인

몬스터 피격이 서버 권위면 플레이어 피격감이 흔히 나빠진다: ① **피했는데 맞은 것 같음**, ② **못 피했는데 안 맞은 것 같음**, ③ **몬스터가 많을 때 누구한테 맞았는지 모름**. D4 유저들도 출시부터 지금(2025)까지 **러버밴딩·desync·서버랙** 불만이 꾸준하고(포럼 참조), "텔레그래프 보고 피했는데 맞았다"는 이 desync 계열이다. **즉 권위 자체가 아니라 (a) 지연(latency)과 (b) 피드백 가독성이 원인**이다. ①②는 지연 문제, ③은 연출 문제다.

핵심 통찰: 플레이어가 가장 신경 쓰는 변수 = 자기 캐릭터 위치이고, **그건 이미 저지연**이다(클라 예측 + 서버 화해, `MoveIntentReq`/`SnapshotNtf.ack_input_seq`). 내가 내 화면에서 굴렀으면 서버도 거의 같은 위치를 갖는다. 남는 문제는 "서버가 **언제**, **누구 위치 기준**으로 판정하느냐"의 디테일이다.

### 8.2 완화책 (효과 큰 순서)

**1. 텔레그래프 = 히트박스 + 커밋 + 관대한 타이밍.** 윈드업을 충분히 길게(잡몹도 300ms+), 예고 도형을 시전 시작에 **고정**하고 그 도형이 곧 서버 판정 영역이게 한다("도형 밖 = 안 맞음"이 직관적). **서버 히트박스를 비주얼 텔레그래프보다 살짝 작게**(`HitboxScale<1`) 두면 가장자리에서 억울하게 맞는 일이 준다(수비자 우대). 6장의 타이밍 양자화 방지가 전제 — 윈드업이 500ms로 뭉개지면 이 모든 게 무너진다.

**2. Lag compensation — 수비자 우대(favor-the-defender).** 서버가 strike 판정 시 **플레이어 클라가 그 순간 봤을 위치**(= strike시각 − RTT/2 의 위치)로 되감아 판정한다. FPS의 lag comp을 수비 쪽으로 적용. "내 화면에서 피했으면 안 맞는다"를 latency 한도 안에서 보장. ②(못 피했는데 안 맞음)는 이 우대의 의도된 부작용으로 받아들인다(불멸은 아님 — 윈드업·쿨다운이 상한). 투사체(7장)도 충돌 판정에 같은 보정 적용.

**3. 피드백 가독성 — ③은 권위가 아니라 연출 문제.** `SkillDamageNtf` 에 **공격자 objectId + 방향**을 실어, 방향 피격 표식(화면 가장자리 빨간 호), 공격 종류별 구분되는 hit VFX/사운드/짧은 화면 흔들림, 피격 캐릭터 플래시를 준다. 체감 개선 폭이 의외로 가장 크다.

### 8.3 "클라가 피격 판정에 관여" — 부분적으로만, 권위는 넘기지 말 것

완전히 클라가 "나 안 맞았어"를 **결정**하게 하면 한 줄짜리 불멸 핵이 된다. PvE라도 불가. 단, 안전한 "관여" 두 가지가 있다:

- **(a) 피격 예측 (연출만 클라가 선반응).** 클라가 텔레그래프 + 자기 위치로 "맞겠다"를 예측해 **즉시 플린치/플래시**를 띄우고, 실제 HP는 서버 `SkillDamageNtf` 가 확정. 텔레그래프가 결정론적·커밋이면 예측 적중률이 높다. 위험은 "안 맞을 줄 알았는데 서버가 맞춤 → HP 갑툭"인데, 텔레그래프 결정론으로 최소화.
- **(b) 클라 위치를 판정 입력으로 제공 (= 8.2의 2번).** 클라가 판정에 '관여'하되 권위는 서버. 클라는 자기 위치(이미 보내는 이동 데이터)를 주고, 서버가 그걸로 수비자 우대 판정. **사실상 정답** — 안티치트/권위 유지 + 피격감 개선.

**결론:** 완전 클라 판정 ✗. **텔레그래프=히트박스+관대한 타이밍(1) + 피드백 가독성(3)** 을 v1 우선, **서버 lag-comp 수비자 우대(2)** 를 그다음, 피격 예측(a)은 연출 디테일로 나중. "권위는 서버, 체감은 클라"라는 ARPG 표준 타협점.

### 8.4 우리 구조에 매핑 (패킷/서버)

**`SkillDamageNtf` 확장 (피드백 가독성):**
```proto
message SkillDamageNtf {
    int64 target_object_id   = 1;
    float damage             = 2;
    bool  is_duplicate       = 3;
    float remaining_hp       = 4;
    int64 attacker_object_id = 5;   // [신규] 누가 때렸나 → 방향 피격표식/연출 선택
    int32 source_skill_key   = 6;   // [신규] 어떤 공격인가 → hit VFX/사운드 분기
}
```
`Stage::ApplyEffectDamage` 가 이미 `killerObjectId` 를 받으므로 `attacker_object_id` 는 그 값을 그대로 흘리면 된다. `source_skill_key` 만 호출부에서 추가 전달.

**Lag-comp 위치 히스토리 (수비자 우대):**
- 서버가 플레이어별 **위치 링버퍼**(예: 최근 500ms를 50ms 간격 = 10슬롯, `{serverTickSeq, pos}`)를 유지. 이동 시뮬(`updateCharacters`)에서 매 tick 갱신.
- 몬스터 strike 판정 시: 대상 플레이어의 RTT 추정(이미 `MoveIntentReq.client_time_ms`/snapshot ack 로 추정 가능)으로 `strikeTick − RTT/2` 슬롯의 위치를 꺼내 텔레그래프 도형과 교차 검사.
- 되감기 상한(예: 250ms)으로 과도한 보정 차단.
- v1은 생략하고 "현재 권위 위치 + 작은 히트박스 축소"만으로 시작 가능. 불만이 실측되면 도입.

---

## 9. 확정된 결정

| # | 항목 | 결정 |
|---|---|---|
| 1 | 이동속도 | **`MoveSpdTotal` 스탯** (버프 자동 반영). 몬스터 Stat 컬럼으로 시드, `m_moveSpeed` 제거 |
| 2 | AI 튜닝 | **`GameData_MonsterAI` 프로파일 분리** (아키타입 공유) |
| 3 | `AbilityCastNtf` 위치 | **신규 `ability_packet.proto`** |
| 4 | 근접 단타 발동 비주얼 | **`SkillCastNtf` 생략** (윈드업 모션 + `SkillDamageNtf` 로 충분) |
| 5 | 패킷 명명 | **`AbilityCastNtf`** (몬스터/NPC/엘리트 공용) |
| 6 | 업데이트 주기 | **적응형(A)** — idle 500ms / 관여 시 `EngagedUpdateIntervalMs`. 이동·전투타이머는 매 tick |
| 7 | 몬스터 투사체 | **서버 권위 시뮬레이션** (Stage 소유, 매 tick). 클라는 순수 비주얼 |
| 8 | 피격감 | **서버 권위 유지** + 텔레그래프=히트박스/관대한 타이밍 + 피드백 가독성. lag-comp 수비자 우대는 그다음 |

---

## 10. 서버/클라 코드 매핑 + v1 범위

### 10.1 서버 (현재 스텁 → 데이터·권위 구동)

| 위치 | 현재 | 바꿀 방향 |
|---|---|---|
| `Monster::Initialize` | 스킬 2종 + AI 상수 **하드코딩** | `GameData_Monster.AIKey`/`SkillKey#` 로드 → AI 프로파일·ability 채움 |
| `MonsterSkill` 구조체 | 자체 필드(range/cooldown/cast/damage) | `GameData_Skill.Key` 참조 + 런타임 `remainingCooldownMs` 만 보유 |
| `Monster::ExecuteSkill` | `ApplyEffectDamage` 직접(단일대상) | `EffectDamage` 분기: 근접=영역판정, 투사체=**서버 투사체 spawn**, 장판=`AreaEffect`(기존) |
| `Monster::MoveTo` | `m_moveSpeed`(하드코딩 4.0) | `GetStatTotal(EStatGroup::MoveSpd)` |
| `MonsterFsmAI::updateAttack` | 스킬 선택 후 Casting(통보 TODO) | Windup 진입 시 `Stage::BroadcastAbilityCastNtf` 호출(현 192행 TODO 자리) |
| `MonsterFsmAI::updateCasting` | 선딜 후 ExecuteSkill | Strike → **Recovery(ActionLockMs)** 추가 후 Attack 복귀. 캐스트 타이머는 매 tick 진행 |
| 어그로 시 주기 | `k_monsterUpdateIntervalMs=50` 고정 | **적응형**: Chase 진입 시 `SetUpdateIntervalMs(EngagedUpdateIntervalMs)`, Idle/Return 복귀 시 idle 주기로 |
| `updateSkillEffects` | AreaEffect tick + 투사체 만료 sweep | **몬스터 투사체 능동 전진+충돌** 추가 |
| `ApplyEffectDamage` → `SkillDamageNtf` | target/damage/hp | **`attacker_object_id`/`source_skill_key` 추가**(피드백 가독성) |
| 플레이어 위치 히스토리 | 없음 | (나중) lag-comp용 링버퍼 + strike 판정 되감기 |
| `m_aggroRange` 등 상수 | 하드코딩 | `GameData_MonsterAI` 에서 주입 |

신규 서버 함수: `Stage::BroadcastAbilityCastNtf(caster, skillKey, origin, dir, targetId, windupMs)` — `BroadcastSkillCastNtf` 와 같은 AOI 순회 패턴.

### 10.2 클라

| 위치 | 현재 | 추가 |
|---|---|---|
| `IActorAnimator` | `SetMoving/PlayDead/SetDeadPose` | `PlaySkill(string castAnim)` (+`PlayHit()`) — 파일에 이미 TODO로 예약됨 |
| `MonsterObject` | idle/move/death 구동 | `AbilityCastNtf` 핸들러: 회전고정 + `PlaySkill` + 텔레그래프 spawn |
| 텔레그래프 렌더러 | 없음 | 신규: `EffectShape`+크기로 바닥 데칼, `windup_ms` 동안 채움 |
| 투사체 | 플레이어용(클라 hit 보고) | **몬스터 투사체는 순수 비주얼**(hit 보고 안 함, 서버 `SkillDamageNtf` 가 권위) |
| 피격 연출 | (미흡) | `SkillDamageNtf` 의 `attacker_object_id`/`source_skill_key` 로 방향 피격표식 + 구분 VFX/사운드. (선택) 피격 예측 플린치 |
| `PacketDispatcher` | — | `AbilityCastNtf` 등록 |

효과 비주얼(투사체/장판)과 대미지 숫자는 **기존 `SkillSystem.onSkillCastNtf`/`SkillDamageNtf` 경로**를 그대로 탄다(몬스터 caster 도 처리되도록 caster 조회만 일반화).

### 10.3 v1 범위 / 나중에

**v1 (지금):**
- FSM 3박자(Windup/Strike/Recovery) + `AbilityCastNtf` 1개.
- 적응형 업데이트 주기(idle 500 / engaged 100, 이동·타이머 매 tick).
- 몬스터 투사체 서버 시뮬(매 tick, Stage 소유).
- `GameData_MonsterAI` + `GameData_Monster.AIKey/SkillKey#` + 몬스터 ability = `GameData_Skill` 행.
- 근접 단타 1종 + 원거리 투사체 1종. 클라 윈드업 모션 + 기본 바닥 텔레그래프.
- 피격감: 텔레그래프=히트박스 + 관대한 타이밍 + 히트박스 약간 축소, `SkillDamageNtf` 공격자/방향 필드로 피격 연출 가독성.

**나중에:**
- 엘리트 = Behavior Tree (같은 패킷 계약 → 클라 불변).
- 팩 어그로 전파, 난이도 스케일링, 정교한 strafe.
- 피격감: **lag-comp 수비자 우대**(플레이어 위치 히스토리 + strike 되감기), 피격 예측 플린치.
- CC에 의한 시전 캔슬(몬스터 CC 시스템 들어올 때), 피격 플린치.
- AOI 중간진입 시 진행 중 시전 동기화(`MonsterSpawnInfo` 확장).
- think/integrate 완전 분리(6장 B안) — 잡몹 수 폭증 시.
- 몬스터 투사체 풀링 + impact 통보(폭발 위치 정합).

---

## 11. 엘리트 BT — 같은 패킷 계약 공유 (스케치)

### 11.1 원칙: 두뇌는 결정만, 몸체가 행동 + 패킷

2장의 직접적 귀결이다. **패킷은 `Monster` 공유 행동 레이어(몸체)의 산물이지 두뇌(`IMonsterAI`)의 산물이 아니다.** FSM이든 BT든 `monster.~~()` 행동 API를 호출하면 동일한 `AbilityCastNtf` / `SkillCastNtf` / `SkillDamageNtf` / `ObjectDeathNtf` 가 나간다. 클라는 `caster_object_id` + `skill_key` 만 보므로 시전자가 잡몹인지 엘리트인지, 두뇌가 FSM인지 BT인지 **모른다.** → BT를 붙여도 클라 코드 0 변경.

### 11.2 전제 리팩터: 캐스트 생애주기를 두뇌 → 몸체로 내린다

지금 FSM 코드가 문제를 드러낸다. 캐스트 타이머(`m_castingSkillIndex`, `m_castRemainingMs`)와 "Windup 진입 → `AbilityCastNtf` 송신 → 선딜 → `ExecuteSkill` → Recovery" 흐름이 **`MonsterFsmAI`(두뇌) 안**에 있다(`MonsterFsmAI.cpp`). 이대로면 BT가 같은 걸 또 구현해야 하고, 그 순간 패킷 계약이 두 두뇌로 **갈라진다**(한쪽만 통보 누락 같은 버그).

해결: 캐스트 생애주기를 `Monster` 공유 레이어로 내려 **단일 지점**에서 패킷·타이밍·커밋을 보장한다.

```
// Monster 공유 API (FSM/BT 공통 호출)
bool Monster::TryBeginCast(int32 skillIndex, StageObject* target);
//   - 사거리/쿨다운 검사 → Windup 시작, 타겟·방향 고정(커밋),
//     Stage::BroadcastAbilityCastNtf 송신. 성공 시 true.
bool Monster::IsCasting()    const;   // 두뇌가 "행동 잠금 중?" 질의
bool Monster::IsInRecovery() const;

// Monster::Update (매 tick = integrate) 내부에서 캐스트 타이머 진행:
//   선딜 경과 → ExecuteSkill(효과/대미지/투사체) + StartSkillCooldown → Recovery → 완료.
```

이러면 두뇌는 **"언제 어떤 스킬을 쓸지"만** 결정하고 `TryBeginCast` 를 호출한다. 6장 integrate/think 분리와도 맞물린다 — **캐스트 타이머 = integrate(매 tick), 스킬 선택 = think(성김)**. FSM의 `updateCasting`/`m_castRemainingMs` 는 이 공유 캐스트로 흡수되어 사라진다.

### 11.2.1 헤더 스케치 (구체 시그니처)

> 코드베이스 컨벤션(`Monster.h` 스타일, `GameData_Skill` 재사용, 컨텐츠 스레드 전용·락 없음)에 맞춘 **설계 스케치**다. 실제 구현 시 출발점.

**(a) 페이즈 + ability 런타임 (구 `MonsterSkill` 리워크)**

정적 데이터(윈드업/회복/사거리/대미지/형태)는 전부 `GameData_Skill` 에서 읽고, 런타임은 쿨다운만 보유한다.

```cpp
// 캐스트 페이즈. 몸체(Monster)가 소유. 두뇌는 IsCastBusy 등으로 질의만.
enum class EMonsterCastPhase : uint8_t { None, Windup, Recovery };

// 몬스터 ability 1개의 런타임 정보. 정적 스펙은 GameData_Skill(skillKey)에서.
struct MonsterAbility
{
    int32 skillKey            = 0;   // GameData_Skill.Key
    int64 remainingCooldownMs = 0;   // 런타임 잔여 쿨다운
};
```

**(b) `Monster` 공유 캐스트 API + 상태**

```cpp
class Monster : public ActorObject
{
public:
    // ── 캐스트(공유): 두뇌(FSM/BT)의 단일 진입점 ─────────────────
    // skillIndex 의 ability 시전을 "시작"한다. 다음이면 false:
    //   · 이미 캐스트/회복 중(IsCastBusy), · 쿨다운 미회복, · 사거리 밖, · 데이터 없음.
    // 성공 시(부수효과):
    //   · 타겟/방향/origin 커밋 + 그쪽으로 1회 즉시 회전, · 이동 정지,
    //   · Windup 진입(remaining = CastDelayMs),
    //   · Stage::BroadcastAbilityCastNtf 송신(windup_ms = CastDelayMs).
    [[nodiscard]] bool TryBeginCast(int32 skillIndex, StageObject* pTarget);

    bool IsCasting()    const { return m_castPhase == EMonsterCastPhase::Windup; }
    bool IsInRecovery() const { return m_castPhase == EMonsterCastPhase::Recovery; }
    bool IsCastBusy()   const { return m_castPhase != EMonsterCastPhase::None; } // 이동/새 캐스트 잠금

    // 시전/회복 강제 취소(자원 환불 없음). CC·사망 시 호출(MarkDead 내부에서도).
    void CancelCast();

    // ability 조회 (두뇌가 사거리/쿨다운 판단에 사용)
    int32                GetAbilityCount() const { return (int32)m_abilities.size(); }
    const MonsterAbility& GetAbility(int32 i) const { return m_abilities[i]; }
    // 사용 가능한(쿨다운 끝 + 사거리 내) 우선순위 최상 ability index. 없으면 -1.
    int32 SelectReadyAbility(float distToTarget) const;

private:
    // 매 tick(integrate) Monster::Update 안에서 호출. 윈드업/회복 카운트다운.
    // 윈드업 만료 → onCastStrike() → Recovery. 회복 만료 → None.
    void advanceCast(int64 deltaMs);
    // 커밋된 (skillIndex, target, origin, dir)로 효과 발동 + 쿨다운 시작.
    void onCastStrike();
    void tickAbilityCooldowns(int64 deltaMs);   // 구 tickSkillCooldowns

    const GameData_Skill* abilitySkill(int32 index) const;   // index→ability→GameData_Skill

private:
    std::vector<MonsterAbility> m_abilities;     // 구 m_skills

    // ── 캐스트 상태(커밋 스냅샷) ──
    EMonsterCastPhase m_castPhase       = EMonsterCastPhase::None;
    int32  m_castSkillIndex             = -1;
    int64  m_castRemainingMs            = 0;      // 현재 페이즈 잔여(ms)
    int64  m_castTargetObjectId         = 0;      // 커밋된 타겟(despawn 안전 위해 id 보관)
    float  m_castOriginX = 0.f, m_castOriginY = 0.f, m_castOriginZ = 0.f;
    float  m_castDirX = 0.f, m_castDirZ = 0.f;
};
```

**(c) `Monster::Update` 통합 — integrate 먼저, 그다음 두뇌**

```cpp
void Monster::Update(int64 deltaMs)
{
    if (GetStage() == nullptr) return;
    captureSpawnPointOnce();
    tickAbilityCooldowns(deltaMs);

    advanceCast(deltaMs);          // ← integrate: 캐스트 타이머는 두뇌와 무관하게 항상 진행

    if (m_ai)                      // 두뇌는 "결정"만. 캐스트/회복 중이면 보통 no-op 을 고른다.
        m_ai->Update(*this, deltaMs);
}

void Monster::advanceCast(int64 deltaMs)
{
    if (m_castPhase == EMonsterCastPhase::None) return;
    m_castRemainingMs -= deltaMs;
    if (m_castRemainingMs > 0) return;

    if (m_castPhase == EMonsterCastPhase::Windup)
    {
        onCastStrike();                                   // 효과/대미지/투사체 발동
        const GameData_Skill* s = abilitySkill(m_castSkillIndex);
        m_castPhase       = EMonsterCastPhase::Recovery;
        m_castRemainingMs = s ? s->ActionLockMs : 0;      // 후딜(=반격 창)
    }
    else // Recovery
    {
        m_castPhase      = EMonsterCastPhase::None;
        m_castSkillIndex = -1;
    }
}
```

> 즉발(`CastDelayMs == 0`)이면 같은 호출에서 Windup 이 바로 만료되어 strike→Recovery 로 넘어간다. 6장 B안(think/integrate 완전 분리)으로 가더라도 `advanceCast` 를 per-tick integrate 패스로 옮기기만 하면 되고, **API 시그니처는 불변**이다.

**(d) `TryBeginCast` / `onCastStrike` 흐름 (의사코드)**

```cpp
bool Monster::TryBeginCast(int32 skillIndex, StageObject* pTarget)
{
    if (IsCastBusy() || pTarget == nullptr) return false;
    if (skillIndex < 0 || skillIndex >= (int32)m_abilities.size()) return false;

    MonsterAbility& ab = m_abilities[skillIndex];
    if (ab.remainingCooldownMs > 0) return false;

    const GameData_Skill* s = GameDataTable_Skill::FindData(ab.skillKey);
    if (s == nullptr) return false;
    if (distXZ(*this, *pTarget) > s->MaxRange) return false;

    // 커밋: 회전 고정 + origin/dir 산출(Placement 기준) + 이동 정지
    FaceTarget(pTarget);
    m_castTargetObjectId = pTarget->GetObjectId();
    computeCastOriginDir(*s, *pTarget,                       // EffectParams.h 의 CalcEffectPosition 재사용
                         m_castOriginX, m_castOriginY, m_castOriginZ,
                         m_castDirX, m_castDirZ);
    StopMoving();

    m_castSkillIndex  = skillIndex;
    m_castPhase       = EMonsterCastPhase::Windup;
    m_castRemainingMs = s->CastDelayMs;

    GetStage()->BroadcastAbilityCastNtf(
        *this, ab.skillKey,
        Vector3(m_castOriginX, m_castOriginY, m_castOriginZ),
        Vector3(m_castDirX, 0.f, m_castDirZ),
        m_castTargetObjectId, /*windupMs=*/ (int32)s->CastDelayMs);
    return true;
}

void Monster::onCastStrike()
{
    // 커밋된 타겟을 지금 해소(윈드업 중 despawn 가능 → nullptr 면 효과만 빈 발동/스킵).
    StageObject* pTarget = GetStage()->FindObject(m_castTargetObjectId);
    // ExecuteSkill: EffectDamage 분기(근접 영역판정 / 투사체 spawn / AreaEffect) — 커밋된 origin/dir 사용.
    ExecuteSkill(m_castSkillIndex, pTarget, m_castOriginX, m_castOriginY, m_castOriginZ,
                 m_castDirX, m_castDirZ);
    m_abilities[m_castSkillIndex].remainingCooldownMs =
        abilitySkill(m_castSkillIndex)->CooldownMs;      // 쿨다운 시작
}
```

**(e) `Stage` 신규 송신 함수**

```cpp
// 시전 시작 통보를 시전자 주변 AOI 유저에게 broadcast. BroadcastSkillCastNtf 와 동일 패턴.
void Stage::BroadcastAbilityCastNtf(const ActorObject& caster, int32 skillKey,
                                    const Vector3& origin, const Vector3& dir,
                                    int64 targetObjectId, int32 windupMs);
```

**(f) 두뇌 측 사용 — FSM·BT 동일**

```cpp
// FSM updateAttack (리워크): m_castingSkillIndex/m_castRemainingMs/updateCasting 전부 삭제.
const int32 idx = monster.SelectReadyAbility(dist);
if (idx >= 0)
    monster.TryBeginCast(idx, pTarget);     // 통보/타이밍/커밋/효과는 전부 몸체가 보장

// BT 의 CastAbility 잎:
BtStatus CastAbilityNode::Tick(Monster& m) {
    return m.TryBeginCast(m_skillIndex, m.GetTarget()) ? BtStatus::Success : BtStatus::Failure;
}
```

> **이동 잠금:** 두뇌는 `MoveTo` 전에 `IsCastBusy()` 를 확인한다(Windup·Recovery 중 정지 = 커밋). 안전을 위해 `Monster::MoveTo` 가 `IsCastBusy()` 면 early-return 하도록 몸체에서 한 번 더 막아도 된다.
> **캔슬:** `MarkDead`/CC 적용 지점에서 `CancelCast()` 호출(환불 없음). v1 에 몬스터 CC 가 없으면 사망 경로만 연결.

### 11.3 FSM vs BT — 무엇이 같고 무엇이 다른가

| | FSM (일반몹) | BT (엘리트) |
|---|---|---|
| 행동 API(이동/타겟/캐스트/사거리) | 공유 | 공유 |
| 패킷 계약 | 공유 | 공유 |
| 게임데이터(ability = `GameData_Skill`) | 공유 | 공유 |
| 커밋/회복 규칙 | 공유 | 공유 |
| **결정 정책 표현력** | 평면 상태 + 전이 | **합성 노드 트리** |

즉 다른 건 **의사결정의 표현력뿐**이다. BT는 페이즈 전환, 쿨다운 게이트 특수기, HP 임계 enrage, 타겟 스위칭, 후퇴/소환 같은 다단계·조건부 행동을 트리로 조립한다. 하지만 모든 잎(leaf)이 결국 같은 `Monster` 행동 API로 떨어지므로 패킷은 동일하다.

### 11.4 BT 골격 (IMonsterAI 재사용)

```
class MonsterBtAI : public IMonsterAI {
    void Update(Monster& m, int64 deltaMs) override;   // FSM과 동일 인터페이스
    BtNode m_root;                                      // 트리
    Blackboard m_bb;                                    // 두뇌 내부 상태(서버 전용)
};
```

- **Composite:** `Selector`(우선순위 OR), `Sequence`(AND), (필요 시 `Parallel`)
- **Decorator:** `Cooldown`, `Condition`, `Inverter`
- **Action(Leaf) — `Monster` 공유 API 호출:** `MoveToTarget`(슬롯), `FaceTarget`, `CastAbility(idx)`(→`TryBeginCast`), `Reposition`, `Flee`, `AcquireTarget`
- **Condition(Leaf):** `HasTarget`, `InAttackRange`, `HpBelowPct`, `AbilityReady(idx)`, `DistInBand`
- **Blackboard:** 현재 페이즈, 마지막 특수기 시각 등. **와이어 안 건넘.**
- 가드: `IsCasting()` / `IsInRecovery()` 동안엔 새 `CastAbility` 를 시작하지 않게 한다(커밋 유지).

`Monster::SetAI` 는 그대로다 — `SetAI(make_unique<MonsterBtAI>(...))`. 인터페이스가 같아 주입 코드 한 줄만 분기.

### 11.5 엘리트 BT 예시 트리 (근접 엘리트)

```
Selector(root)
├─ [Condition HpBelow 30% & !enraged] ─► CastAbility(Enrage)          // 임계 enrage
├─ Sequence: [Cooldown 8s][InRange slam] ─► CastAbility(Slam: 큰 AoE) // 텔레그래프 특수기
├─ Sequence: [dist>attackRange][Cooldown 5s] ─► CastAbility(Charge)   // 갭클로즈 이동기
├─ Sequence: [InAttackRange] ─► CastAbility(BasicAttack)              // 기본 공격
└─ MoveToTarget                                                       // fallback 추격(슬롯)
```

모든 `CastAbility` 잎 → `monster.TryBeginCast` → 같은 `AbilityCastNtf`(`skill_key` = Slam/Charge/Basic/Enrage) → 클라가 `GameData_Skill` 로 텔레그래프·모션 재현. **Enrage조차 통보 1개로 처리**되고 클라 코드는 0 변경. 엘리트 차별화(더 긴 windup, 더 큰 `EffectShape`, 큰 텔레그래프)는 **전부 `GameData_Skill` 행 차이**일 뿐이다.

### 11.6 패킷 계약 (FSM = BT 완전 동일)

| 사건 | 발생시키는 `Monster` API | 패킷 |
|---|---|---|
| 시전 시작(예고) | `TryBeginCast` | `AbilityCastNtf` |
| 효과/투사체 비주얼 | (캐스트 만료) `ExecuteSkill` | `SkillCastNtf` (투사체/장판만) |
| 대미지 | `ApplyEffectDamage` | `SkillDamageNtf` |
| 이동 | `MoveTo` | `SnapshotNtf` (매 tick) |
| 사망 | `MarkDead` | `ObjectDeathNtf` |

전부 두뇌 무관. 이 표가 곧 "패킷 계약"이고, FSM·BT는 이 표를 **읽기만** 한다.

### 11.7 데이터 / 주입

- `GameData_MonsterAI.AIType = BehaviorTree`. 어떤 트리인지 지정이 필요하다:
  - **v1-later 추천:** 아키타입별 BT를 C++ 클래스로 등록(FSM이 한 클래스이듯). `GameData_MonsterAI` 에 `BtKey`(또는 `Name`)를 두고 **BT 빌더 팩토리**에 매핑. 단순하고 충분.
  - **데이터 구동 BT**(노드 자체를 데이터로): 강력하지만 별도 시스템 → 더 나중.
- 스폰 시 `Stage`: `AIType` 보고 `SetAI(make_unique<MonsterFsmAI>())` 또는 `SetAI(BtFactory::Build(btKey))`. 한 줄 분기.
- 업데이트 주기: 엘리트 think = 100ms(계획대로), integrate(이동·캐스트 타이머)는 매 tick. 적응형 메커니즘(6장) 동일 적용.

### 11.8 와이어를 건너지 않는 것 (원칙 재확인)

BT 노드 그래프, blackboard, 현재 페이즈, 쿨다운 잔여 — **전부 서버 전용**이다. 클라는 결과 행동(통보/효과/대미지/위치)만 본다. 그래서 **BT를 통째로 재설계해도 클라 영향은 0**이며, 이것이 "일반=FSM, 엘리트=BT"를 같은 클라로 굴릴 수 있는 근거다.

---

## 12. 패킷 proto 초안

실제 `PacketGenerator/Proto` 컨벤션(proto3, `package GamePacket`, ID는 `packet_id.proto`)에 맞춘 초안. 신규 1개(`AbilityCastNtf`) + 기존 1개 확장(`SkillDamageNtf`).

### 12.1 `packet_id.proto` — ID 추가

스킬 블록은 `8001~8005`. 능력(몬스터/NPC/엘리트) 시전은 스킬과 형제이므로 **8100 블록**을 새로 연다(기존 스킬 ID는 불변).

```proto
    // ── 능력(몬스터/NPC/엘리트 공용) 시전 ──
    GAME_PACKET_ID_ABILITY_CAST_NTF = 8101;   // 서버 -> 클라: 능력 시전 "시작" 통보(윈드업/텔레그래프). AOI 브로드캐스트.
```

`SkillDamageNtf`(=`8004`)는 **메시지 필드만 확장**하고 ID는 그대로 둔다.

### 12.2 `ability_packet.proto` (신규 파일)

경로: `PacketGenerator/Proto/GamePacket/ability_packet.proto` (서버 C++ + 클라 C# 둘 다 생성).

```proto
syntax = "proto3";

package GamePacket;

// ──────────────────────────────────────────────
// 능력(공격) 시전 패킷 — 몬스터/NPC/엘리트 공용
// ──────────────────────────────────────────────
//
// 흐름 (디아블로4 일반몬스터 AI 설계 문서 참조):
// - 서버 AI(FSM/BT)가 Monster::TryBeginCast → Windup 진입 시점에 AOI 브로드캐스트.
// - 클라(원격): caster 를 origin/dir 로 회전 고정 → skill_key 로 GameData_Skill 조회 →
//   CastAnim 윈드업 재생 + EffectShape/size 로 바닥 텔레그래프를 windup_ms 동안 채움.
// - 실제 대미지/효과는 windup 종료 후 SkillDamageNtf / SkillCastNtf 로 도착(이 패킷엔 효과 없음).
//
// 좌표/방향: Unity 동일. Y=높이, X-Z 평면. dir 은 정규화 X-Z (dir_y 는 0 이라 생략).
// 플레이어 SkillCastNtf 가 "발동(효과 재현)" 통보인 것과 달리, 이 패킷은 "시작(예고)" 통보다.

message AbilityCastNtf {
    int64  caster_object_id = 1;   // 시전자(몬스터/NPC) object_id
    int32  skill_key        = 2;   // GameData_Skill.Key (능력 = 스킬 데이터)
    int64  target_object_id = 3;   // 조준 대상(없으면 0). 추적형 예고/디버그용
    float  origin_x         = 4;   // 예고 기준점(근접/투사체=시전자, 타겟장판=고정된 타겟 위치)
    float  origin_y         = 5;   // 높이
    float  origin_z         = 6;   // 평면 깊이축
    float  dir_x            = 7;   // 고정된 시전 방향(정규화 X-Z)
    float  dir_z            = 8;
    int32  windup_ms        = 9;   // 선딜(=CastDelayMs). 클라 텔레그래프 채움 시간. 0=즉발(예고 없음)
}
```

### 12.3 `skill_packet.proto` — `SkillDamageNtf` 확장 (기존 파일 수정)

피드백 가독성(8장)을 위해 **공격자 + 공격 종류** 필드 추가. proto3 라 새 태그(5, 6) 추가는 **wire 호환**(구버전 클라는 unknown field 로 무시).

```proto
// 대미지 통보 (서버 -> 클라 브로드캐스트)
// 클라는 대미지 숫자 표시 + 대상 HP 갱신. (확장) 공격자/공격종류로 방향 피격표식·연출 분기.
message SkillDamageNtf {
    int64 target_object_id   = 1;
    float damage             = 2;   // 적용된 대미지 (배율 적용 후 최종값)
    bool  is_duplicate       = 3;   // 중복타격 감소 대미지인지 (표시용 flag)
    float remaining_hp       = 4;   // 대미지 적용 후 남은 HP
    int64 attacker_object_id = 5;   // [신규] 누가 때렸나 → 방향 피격표식/카메라 연출
    int32 source_skill_key   = 6;   // [신규] 어떤 공격인가 → hit VFX/사운드 분기 (GameData_Skill.Key)
}
```

### 12.4 송수신 배선 (참고)

서버:
- `PacketSender::SendAbilityCastNtf(...)` 추가 — `SendSkillCastNtf` 와 동형(SendToUser 템플릿 경유).
- `Stage::BroadcastAbilityCastNtf(...)` 가 AOI 순회(`ForEachUserInAoi`)로 위를 호출. `BroadcastSkillCastNtf` 와 동일 패턴.
- `PacketSender::SendSkillDamageNtf(...)` 시그니처에 `attackerObjectId`, `sourceSkillKey` 인자 추가. `Stage::ApplyEffectDamage` 가 이미 `killerObjectId` 를 갖고 있어 그대로 흘리고, `sourceSkillKey` 만 호출부에서 전달.

클라:
- `PacketDispatcher` 에 `AbilityCastNtf` 핸들러 등록 — caster 가 몬스터면 `MonsterObject` 로 라우팅(회전 고정 + `PlaySkill(CastAnim)` + 텔레그래프 spawn).
- `SkillDamageNtf` 핸들러는 새 필드로 방향 피격표식 + 공격종류별 연출.

생성 절차(`PacketGenerator.md`):
1. `PacketGenerator/generate_proto.bat` 실행 → C++(`.pb.h/.cc`) + 클라 C#(`.cs`) 생성.
2. 새 `ability_*.pb.*` 를 `PacketGenerator` 프로젝트에, C# 는 클라 `Assets/Generated/GamePacket/` 에 추가.
3. `PacketGenerator` 빌드 → 각 서버는 필요한 message 만 include.

### 12.5 proto3 / ID 호환성 노트

- 메시지에 **새 필드 추가**(태그 5, 6)는 wire-compatible. 서버·클라 중 한쪽만 먼저 갱신돼도 깨지지 않는다(미지원 측은 보존/무시).
- 패킷 ID enum 값은 **한 번 부여하면 변경 금지**. 삭제 시 번호를 `reserved` 로 막아 재사용 사고를 방지.
- `AbilityCastNtf` 는 효과/대미지를 절대 싣지 않는다(관측 가능 행동의 "시작"만). 효과·대미지는 기존 `SkillCastNtf`/`SkillDamageNtf` 가 권위. → 2장 원칙 유지.

---

## 13. 구현 작업 분해 (v1)

> 의존 순서대로. 각 작업은 **검증(verify) 기준**을 동반한다 — 통과 못 하면 다음으로 안 넘어간다.
> 범위 = v1(근접 1종 + 원거리 투사체 1종, **FSM only**). BT·lag-comp·팩 어그로 등은 제외(나중).

**의존 그래프**
```
P0 데이터/enum ─┐
P1 패킷 proto ──┼─► P2 서버 공유캐스트 ─► P3 서버 FSM 리워크 ─► P4 서버 투사체 시뮬
                │                                                       │
                └──────────────────────────► P5 클라(anim/handler/telegraph) ─► P6 통합검증
```
P0·P1 은 서로 독립 → 병렬 가능. P5 는 P1 만 끝나면 P2~P4 와 병렬 가능.

**검증 도구:** 서버 = VS 빌드/디버거 MCP + `packet`/`packetdetail` 치트(송신 패킷 관측). 클라 = `AIEditorBridge`(`COMPILE_CHECK`/`PLAY`/`RUN_EDIT_MODE_TESTS`/`GET_CONSOLE_LOGS`).

### P0 — 데이터 / enum (기반)

| 작업 | 파일 | verify |
|---|---|---|
| `EMonsterAIType`(None/Fsm/BehaviorTree) 추가 | `GameEnum.xlsx` | `GameEnum_Monster` 생성, None=0 규칙 |
| `GameData_MonsterAI` 신설 + 행 2개(MeleeRusher/RangedKiter) | 신규 `MonsterAI.xlsx` | 코드+csv 생성, 서버/클라 로드 무에러 |
| `GameData_Monster` 컬럼 추가(`AIKey`,`SkillKey1~3`) + `MoveSpd*` Stat 사용 | `Monster.xlsx` | `GetSkillKeyCount/GetSkillKey` 생성, 기존 행 로드 OK |
| 몬스터 ability 행 2개(9001 Mob_Claw, 9002 Mob_Fireball) | `Skill.xlsx` | `GameDataTable_Skill::FindData(9001/9002)` 성공 |

공통 verify: `RunGameDataGenerator.bat` → 서버 `GameDataLib` 빌드 + 클라 `COMPILE_CHECK` 통과.

### P1 — 패킷 proto (계약)

| 작업 | 파일 | verify |
|---|---|---|
| `ABILITY_CAST_NTF=8101` | `packet_id.proto` | 생성 |
| `AbilityCastNtf` 신설 | 신규 `ability_packet.proto` | C++/C# 생성 |
| `SkillDamageNtf` +`attacker_object_id`+`source_skill_key` | `skill_packet.proto` | 생성, 기존 송신부 컴파일 |

공통 verify: `generate_proto.bat` → `PacketGenerator` 빌드 + 클라 `COMPILE_CHECK`.

### P2 — 서버: 공유 캐스트 레이어 (핵심, 11.2.1)

| 작업 | verify |
|---|---|
| `MonsterSkill`→`MonsterAbility`(skillKey+쿨다운) 리워크 + `Monster::Initialize` 가 `SkillKey#` 로 채움 | 스폰 시 ability 수/skillKey 디버그 일치 |
| `Monster::MoveTo` 가 `GetStatTotal(MoveSpd)` 사용, `m_moveSpeed` 제거 | MoveSpd 스탯 변경 시 속도 반영 |
| `TryBeginCast`/`advanceCast`/`onCastStrike`/`CancelCast` + `IsCasting/IsInRecovery/IsCastBusy` + `SelectReadyAbility` | 디버거/로그로 Windup(CastDelayMs)→Strike→Recovery(ActionLockMs)→None 전이 **시간 측정**; 즉발(0)은 동일 tick |
| `Stage::BroadcastAbilityCastNtf` + `PacketSender::SendAbilityCastNtf` | `packetdetail` 로 `AbilityCastNtf` 1건 관측(필드값 확인) |

### P3 — 서버: FSM 리워크 + 적응형 주기

| 작업 | verify |
|---|---|
| `MonsterFsmAI::updateAttack` 가 `SelectReadyAbility`+`TryBeginCast` 호출, `updateCasting`/`m_cast*` 삭제, Recovery 대기 | 잡몹이 사거리 진입→윈드업→타격→후딜→반복 |
| 적응형 주기: Chase 진입 시 `SetUpdateIntervalMs(EngagedUpdateIntervalMs)`, Idle/Return 복귀 시 idle(500) | 디버그로 관여 100ms / 대기 500ms 확인; 이동·캐스트 타이밍 부드러움 |
| aggro/leash/desired 를 `GameData_MonsterAI` 에서 주입(하드코딩 제거) | 데이터 변경이 행동에 반영 |

### P4 — 서버: 몬스터 투사체 시뮬 (7장)

| 작업 | verify |
|---|---|
| `ExecuteSkill` 가 `EffectDamage` 분기(근접=영역판정 / ContactHit=서버 투사체 spawn / Area=AreaEffect) | 9001 근접 즉시판정, 9002 투사체 생성 |
| `updateSkillEffects` 에서 몬스터 투사체 매 tick 전진+sector 충돌 → `ApplyEffectDamage`+`OnHitSkillKey` 폭발, MaxRange/지형 소멸 | 투사체가 이동·명중·**회피 가능**, 서버 권위 |
| `SendSkillDamageNtf` 에 `attacker`/`sourceSkillKey` 채움 | `packetdetail` 필드 확인 |

### P5 — 클라 (P1 후 병렬 가능)

| 작업 | verify |
|---|---|
| `IActorAnimator.PlaySkill(castAnim)`/`PlayHit` + `AnimatorActorAnimator` 구현 | `COMPILE_CHECK` + 트리거 시 클립 재생 |
| `MonsterObject` 의 `AbilityCastNtf` 핸들러(회전고정+`PlaySkill`+텔레그래프 spawn) + `PacketDispatcher` 등록 | 몬스터가 윈드업 모션 재생 |
| 텔레그래프 렌더러: `EffectShape`(Circle/Obb)+크기 바닥 데칼, `windup_ms` 동안 채움 | 두 모양 렌더, 채움 종료 ≈ 서버 타격 |
| 몬스터 투사체 **비주얼**(hit 보고 X) + `SkillDamageNtf` 새 필드로 방향 피격표식/연출 | 비주얼 일치, "누가 때렸나" 가독 |

### P6 — 통합 검증

| 시나리오 | verify |
|---|---|
| 근접 잡몹(9001): 접근→윈드업→텔레그래프→회피 시 무피해 / 맞으면 피해 | 화면 + `packetdetail` |
| 원거리 잡몹(9002): 카이팅 + 투사체 발사·회피 가능 | 화면 + 서버 로그 |
| 사망: HP0 → `ObjectDeathNtf` 연출 → corpse 후 디스폰 | 화면 |
| 다수 몬스터 동시 교전: 적응형 주기 부하·프레임, 피격 가독성 | 스크린샷 + 서버 CPU |
| 패킷 계약(2장/11.6) 대비 실제 송신 일치 | (서브에이전트 점검 권장) `packet` 치트 로그 |

**완료 정의(v1):** 잡몹이 데이터만으로 근접/원거리 공격을 텔레그래프와 함께 수행하고, 클라가 윈드업 모션+예고를 재생하며, 회피가 성립하고, 피격 시 누가 때렸는지 읽히고, 사망까지 흐른다. 코드에 하드코딩된 스킬/AI 상수가 남아있지 않다.

---

## 14. 구현 현황 (v1 — 실제 코드 / 데이터 / 결정)

> 이 장은 설계(1~13장) 위에 **실제로 구현·검증된 결과 + 구현 중 확정/보정된 사항**을 기록한다.
> 상태: 서버↔클라 **end-to-end 구현 + 빌드 + 런타임 검증 완료**. 근접(9001 OBB)·원거리(9002 투사체) 모두 동작, 회피 성립.

### 14.1 코드 구조 (실제) — 컴포넌트로 정착

설계 11장의 "캐스트 레이어를 두뇌가 아니라 몸체로"가 **`MonsterCombatComponent`** 로 실현됐다.

- **`Monster`** (얇은 액터): 정체성(스탯/두뇌홀더/시체/종류데이터) + AI 설정값(멤버) + 이동(`WaypointMover` 래퍼 + 스폰앵커 + `FaceTarget`) + `GetCombat()`.
- **`MonsterCombatComponent`** (owner=`Monster*` 백포인터, `BuffComponent` 패턴): **타겟팅(누구와) + 스킬 보유 + 캐스트 생애주기(어떻게)** 를 한 곳에. `AcquireTarget`/`GetTarget`, `SelectReadySkill`/`GetMaxAttackRange`, `TryBeginCast`/`advanceCast`/`onCastStrike`/`CancelCast`, `executeSkill`(Area/투사체/직격 dispatch). 매 tick `Update`(쿨다운+캐스트 진행)를 `Monster::Update` 가 두뇌보다 먼저 호출.
- **`MonsterFsmAI`** (두뇌): 결정만. `monster.GetCombat().X` 로 호출. (BT는 같은 컴포넌트+패킷 재사용 → 나중, 클라 불변.)
- (리팩터 중 타겟팅을 잠깐 별도 컴포넌트로 뺐다가 **과분해**라 Combat에 통합. Monster에 추가된 컴포넌트는 Combat 하나뿐.)

### 14.2 패킷 (실제)

- **신규 `ability_packet.proto` → `AbilityCastNtf`** (ID `8101`). 시전 "시작(예고)" 통보. `caster_object_id`/`skill_key`/`target_object_id`/`origin`/`dir`/`windup_ms`.
- **`SkillDamageNtf` 확장**: `attacker_object_id`(5), `source_skill_key`(6) 추가(피격 방향표식/연출 분기용). 서버 송신 구현 완료.
- 재사용: `SnapshotNtf`(위치), `SkillCastNtf`(투사체 비주얼), `ObjectDeathNtf`(사망), `ObjectVisibilityNtf`/`MonsterSpawnInfo`(스폰).

### 14.3 게임데이터 (실제 형태)

- **`EMonsterAIType`** enum (None/Fsm/BehaviorTree).
- **`GameData_MonsterAI`** 신규 테이블 — `1001 MeleeRusher`(DesiredRange 0), `1002 RangedKiter`(DesiredRange 7). 컬럼: AIType/AggroRange/LeashRange/DesiredRange/EngagedUpdateIntervalMs.
- **`GameData_Monster`**: `AIKey` + `SkillKey1~2` 컬럼. 테스트 몹 — Key50=원거리(AIKey 1002, SkillKey 9002), Key51=근접(AIKey 1001, SkillKey 9001).
- **`GameData_Skill`** (몬스터 ability = 스킬 행) — `9001 Mob_Claw`(EffectShape=Obb / EffectDamage=Area / Placement=Forward / CastDelay 350·ActionLock 250), `9002 Mob_Fireball`(Circle / ContactHit / ProjectileSpeed 12·MaxRange 12 / CastDelay 600).
- **역할별 스킬 배정 규칙**: 근접몹엔 근접스킬만, 원거리몹엔 원거리스킬만. 공격 사거리 = `GetCombat().GetMaxAttackRange()`(보유 스킬 최대 MaxRange) — `m_attackRange` 하드코딩 제거.

### 14.4 구현 중 확정/보정된 사항 (설계 대비)

- **AI 설정값은 `Monster` 멤버로 보관**(`GameData_MonsterAI*` 직접 참조 아님). 캐시/속도 차이는 무시 가능(공유 데이터는 hot, 접근 빈도 낮음). 멤버로 둔 진짜 이유는 **런타임 per-monster 오버라이드 여지**(엘리트 affix/난이도 스케일/도발 디버프). `MoveSpd`를 스탯으로 뺀 것과 같은 결.
- **적응형 업데이트 주기**: `k_monsterUpdateIntervalMs = 500`(idle, 비용 절감). 타겟 관여(Chase 진입) 시 `EngagedUpdateIntervalMs`(데이터, 100ms)로 승격, Idle 복귀 시 강등. 캐스트 타이머·이동은 `Monster::Update`(=관여 주기) 안에서 integrate.
- **투사체 motion**: 서버가 ContactHit 발동 시 `p.motion = Linear` 를 **코드로 강제**(데이터 `EffectMotion` 무관 — 투사체는 본질적으로 직선). 데이터의 EffectMotion이 None이어도 정상.
- **클라 투사체 `ignoreMonsters`**: 몬스터 시전 투사체는 몬스터 충돌을 무시(시전자 위에서 생성되자마자 자가충돌로 즉시 소멸하던 버그 수정). 서버 권위 판정, 클라는 비주얼 전용.
- **placement-aware origin**: `TryBeginCast` 가 Placement(Caster/Forward/Target)로 origin 산출 → 텔레그래프(`AbilityCastNtf`)와 실제 타격(AreaEffect/투사체)이 **동일 기준**이라 예고-타격 일치.
- **근접 단타도 윈드업/회복 보유**(9001: 350/250) — D4식 예고(설계 3장 의도).

### 14.5 클라 비주얼 (실제)

- `IActorAnimator.PlaySkill()` 추가(+`AnimatorActorAnimator`: "Skill" 트리거, 없으면 조용히 무시).
- `SkillSystem.onAbilityCastNtf`: 시전자 회전 + `MonsterObject.PlayAbilityCast`(윈드업 모션) + **`MonsterTelegraph`**(절차적 바닥 데칼 — `EffectShape`/크기로 `windup_ms` 동안 채움, 아트 없이 동작).
- **몬스터 투사체 비주얼**: 기존 `SkillCastNtf` 경로 재사용(+`ignoreMonsters`). **플레이어 근접 시 정지** — 플레이어엔 콜라이더가 없어 `StageManager.FindCharacterInRadiusXZ`(거리 판정)으로, 닿으면 그 자리에서 비주얼 종료. 회피하면 maxRange까지 비행 후 소멸.
- **피격 피드백(본인 피격)**: `SkillDamageNtf` 의 `attacker_object_id` 활용 — **`HitDirectionIndicator`**(발밑에 공격자 방향 붉은 띠, 절차적) + **`CameraFollow.AddShake`**(화면 셰이크, 시간 감쇠). "어디서 맞았는지" 가독성 + 타격감. 본인 피격에만 발동(원격 피격은 데미지 숫자만).

### 14.6 아직 안 된 것 / 다음

- **클라 아트 의존**: 몬스터 Animator의 "Skill" 트리거(윈드업 모션 클립), 텔레그래프·투사체·피격 표식 아트 prefab — 현재 전부 절차적/placeholder.
- 피격 표식을 **화면 가장자리 방향 UI** 로 업그레이드 + `source_skill_key` 별 hit VFX/사운드 분기.
- 피격감: **lag-comp 수비자 우대**(8장).
- **엘리트 BT**(11장), 팩 어그로 전파, 난이도 스케일링.
- think/integrate 완전 분리(6장 B안) — 잡몹 폭증 시.

---

## 부록: 참고 (D4 desync/피격감 커뮤니티)

- Rubberbanding and Server Lag in 2025 — Diablo IV Forums: https://us.forums.blizzard.com/en/d4/t/rubberbanding-and-server-lag-in-2025/231401
- Rubberbanding server lag — Diablo IV Forums: https://us.forums.blizzard.com/en/d4/t/rubberbanding-server-lag/213950
- Diablo 4 lag, rubber banding, and stutter explained — PCGamesN: https://www.pcgamesn.com/diablo-4/lag
