# 클라이언트 디버그 UI 설계

> C++ IOCP 서버 + C# Unity 클라이언트 / 클라 치트 콘솔로 활성화되는 인게임 디버그 도구
> **현재 코드베이스(Development/) 기준으로 작성됨.**

---

## 1. 목표와 설계 원칙

서버 권위(server-authoritative) 구조에서, 클라가 정상 플레이 중에는 받지 않는 서버측 정보를 개발자가 실시간으로 확인하기 위한 디버그 UI를 만든다.

핵심 원칙:

- **서버가 진실의 원천.** 클라가 추정/보간한 값이 아니라 서버 원본 값을 본다.
- **이미 있는 데이터는 재사용한다.** (아래 §3 핵심) 우리 클라는 이미 AOI 내 오브젝트의 스탯/버프/서버위치를 받아 보관 중이다. 디버그 UI는 우선 이 보관소를 읽어 표시하고, **정말 없는 데이터에 대해서만 디버그 패킷을 새로 만든다.** (프로젝트 규칙: Simplicity First)
- **활성화는 기존 치트 경로를 그대로 쓴다.** 별도 인증 핸드셰이크를 만들지 않는다. `CheatConsole` → `CheatReq`(name+args) → 서버 `CheatManager` 경로가 이미 `_DEBUG` 게이팅까지 갖춰져 있다.
- **평상시 비용 0.** UI가 꺼져 있으면 디버그 패킷 구독도, 오버레이 렌더링도 없다.

---

## 2. 다른 게임/엔진은 어떻게 만드는가 (참고)

| 패턴 | 대표 사례 | 우리 적용 |
|---|---|---|
| 치트/플래그 게이트 | Source 엔진 `sv_cheats` (서버가 허용 판정) | `CheatReq` `_DEBUG` 게이팅 (이미 존재) |
| 카테고리 "구독" | Unreal Gameplay Debugger (켠 카테고리만 서버가 복제) | 치트 명령으로 구독 토글 + 디버그 NTF push |
| 타깃 액터 지정 | Unreal GDT 조준 대상만 상세 전송 | `targetObjectId` 인자 |
| 토글 단축키 | Source 콘솔 / 다수 | `CheatConsole` 백틱(이미 존재), 디버그 HUD F-key |
| 월드 오버레이 드로우 | Unreal `DrawDebug*`, GDT 뷰포트 오버레이 | `SectorGridDebug` 패턴(이미 존재) 재사용 |
| 즉시모드 디버그 패널 | Dear ImGui | Unity는 `OnGUI`(IMGUI) — `CheatConsole`이 이미 사용 |

가장 가까운 레퍼런스는 **Unreal Gameplay Debugger**다. 서버 데이터를 클라 뷰포트에 오버레이하되, 켠 카테고리만 서버가 복제해 보내는 구독 모델 — 우리가 하려는 것과 정확히 같다. 우리는 이 "구독" 토글을 **이미 있는 치트 명령**으로 처리하면 새 패킷·새 인증을 거의 추가하지 않고 구현된다.

출처는 문서 하단 참고.

---

## 3. 핵심: 이미 있는 것 vs 새로 필요한 것

요청하신 3대 기능을 현재 코드와 대조했다. 클라의 `ActorObject`는 모든 캐릭터/몬스터의 베이스이고 **`Stats`(StatHolder)와 `Buffs`(BuffHolder)를 각자 보유**한다. `StageManager`가 `StatUpdateNtf`/`HpMpNtf`/`BuffNtf`/`BuffRemoveNtf`/`SnapshotNtf`를 받아 각 오브젝트의 보관소에 라우팅한다.

| 디버그 기능 | 클라에 이미 있나? | 보유 위치 | 새 패킷 필요? |
|---|---|---|---|
| ① 스탯 — 합성 최종값 | **있음**(AOI 내) | `ActorObject.Stats` (`StatHolder`, EStat→값, 0 아닌 항목만) | 아니오 |
| ① 스탯 — raw/base, 0값 포함 전체, 기여원 | 없음 | 서버 `StatComponentBase`만 보유 | **예** (`DebugStatNtf`) |
| ② 버프 — key/스택/남은시간 | **있음**(AOI 내) | `ActorObject.Buffs` (`BuffHolder`) | 아니오 |
| ② 버프 — 시전자, 적용 스탯 수정치, 내부 타이머 | 없음 | 서버 `BuffComponent`/`BuffInstance`만 보유 | **예** (`DebugBuffNtf`) |
| ③ 위치 — AOI 내 오브젝트 서버 좌표 | **있음** | `SnapshotNtf`(매 tick) + `SnapshotInterpolator` | 아니오 |
| ③ 위치 — 클라 렌더 위치 vs 서버 권위 위치 오차 | 부분(보간 전 raw가 스냅샷에 있음) | 스냅샷 raw vs 렌더 transform 비교로 클라에서 계산 | 아니오 |
| ③ 위치 — **AOI 밖(스테이지 전체)** 오브젝트 좌표 | 없음 | 서버 `Stage::m_objects`만 보유 | **예** (`DebugStagePositionsNtf`) |

**결론 / 푸시백:** "모든 정보를 위해 디버그 패킷을 만들자"는 과설계다. ①의 최종 스탯, ②의 버프 뱃지, ③의 AOI 내 위치는 **새 패킷 없이 기존 보관소를 읽어 표시**하면 된다. 디버그 패킷은 아래 세 가지 "진짜 없는 데이터"에만 추가한다.

1. **`DebugStatNtf`** — 선택 오브젝트의 전체 스탯(raw + 합성 total, 0 포함) 스냅샷.
2. **`DebugBuffNtf`** — 선택 오브젝트 버프의 상세(시전자, 적용 스탯 수정치, 만료 타이머 원본).
3. **`DebugStagePositionsNtf`** — 스테이지 전체(AOI 무관) 오브젝트의 서버 좌표 목록.

(②는 1차 구현에서 제외하고 기존 `BuffHolder` 표시로 시작해도 충분하다. 필요할 때 추가한다.)

---

## 4. 활성화 & 구독 — 기존 치트 경로 재사용

별도의 `DebugAuth` 패킷을 만들지 않는다. 활성화·구독 토글을 **치트 명령**으로 처리한다. 그러면:

- 빌드 게이팅이 공짜로 해결된다 — `Stage::handleCheatReq`가 이미 `#ifdef _DEBUG`에서만 등록되므로, 라이브 빌드는 치트 자체가 안 들어온다.
- 클라 진입점도 이미 있다 — `CheatConsole`(`#if UNITY_EDITOR || DEVELOPMENT_BUILD`), 백틱 토글.

### 4.1 추가할 치트 명령 (서버: `CheatManager`)

`CheatManager.cpp`의 `m_table`에 핸들러 한 줄씩 등록(기존 `god`/`packet`과 동일 패턴). 구독 상태는 요청 유저(`User`)에 보관한다 — `User`가 이미 패킷 로그 모드(`GetCheatPacketLogMode`)를 갖고 있는 것과 같은 방식.

```
dbgstat   <objectId>          // 해당 오브젝트 전체 스탯 구독 토글 → DebugStatNtf push 시작/중단
dbgbuff   <objectId>          // 버프 상세 구독 토글 → DebugBuffNtf
dbgpos    [on|off]            // 스테이지 전체 위치 구독 토글 → DebugStagePositionsNtf
```

- 인자 없는 토글, `<objectId>` 지정은 기존 치트들과 동일한 인자 파싱 방식.
- 구독 상태는 `User`에 비트/필드로 저장하고, 서버 디버그 Tick이 이 상태를 보고 push 한다.
- `dbgpos`의 대상 스테이지는 요청 유저가 속한 `Stage`로 한정(전 서버 X).

### 4.2 클라 트리거

`CheatConsole`에 별도 코드가 필요 없다 — `dbgstat`/`dbgbuff`/`dbgpos`는 클라치트 테이블에 없으므로 자동으로 `CheatReq`로 서버에 전송된다(기존 라우팅). 디버그 **창**의 열고 닫기와 선택 오브젝트 지정은 별도 디버그 HUD(§6)에서 하고, 그 HUD가 위 치트를 `NetworkManager.Send(GamePacketId.CheatReq, ...)`로 보내거나 사용자가 콘솔에 직접 입력한다.

---

## 5. 디버그 패킷 정의 (proto)

### 5.1 위치 / 네이밍 / 게이팅 규약

- proto 소스: `Server/PacketGenerator/Proto/GamePacket/debug_packet.proto` 신규.
- `package GamePacket;`, 메시지 네이밍 `XxxNtf` (서버→클라). 기존 컨벤션과 동일.
- `generate_proto.bat`로 C++(.pb.h)·C#(Assets/Generated) 동시 생성.
- **패킷 ID 대역 신설:** `packet_id.proto`의 `GamePacketId`에 `// 9100~9199: 디버그(개발용)` 추가. 9000~9999가 이미 "치트/개발용"이므로 그 안에 디버그 하위대역을 둔다.

```protobuf
// packet_id.proto 의 GamePacketId 에 추가
// 9100~9199: 디버그 (개발용. 서버는 _DEBUG 빌드에서만 처리)
GAME_PACKET_ID_DEBUG_STAT_NTF            = 9101;   // 서버 -> 클라: 선택 오브젝트 전체 스탯 스냅샷 (구독 중 주기 push)
GAME_PACKET_ID_DEBUG_BUFF_NTF            = 9102;   // 서버 -> 클라: 선택 오브젝트 버프 상세 (구독 중 주기 push)
GAME_PACKET_ID_DEBUG_STAGE_POSITIONS_NTF = 9103;   // 서버 -> 클라: 스테이지 전체 오브젝트 서버 좌표 (구독 중 주기 push)
```

### 5.2 메시지

좌표계·타입은 기존과 동일(Y=높이, X-Z 평면, yaw=degree, object_id=int64, EStat=GameEnum_Stat). 기존 `StatEntry`(stat,value)를 재사용한다.

```protobuf
syntax = "proto3";
package GamePacket;

// 선택 오브젝트의 전체 스탯 스냅샷 (디버그). StatUpdateNtf 와 달리 0 값도, raw 도 포함한다.
message DebugStatEntry {
    int32  stat       = 1;   // EStat (GameEnum_Stat)
    double raw_value  = 2;   // StatComponentBase::getRaw (기본/원시값)
    double total_value= 3;   // 합성 최종값 (장비/버프 적용)
}
message DebugStatNtf {
    int64 object_id = 1;
    repeated DebugStatEntry entries = 2;   // EStat 전체(0 포함)
}

// 선택 오브젝트 버프 상세 (디버그). BuffNtf(뱃지용)보다 풍부.
message DebugBuffNtf {
    int64 object_id = 1;
    message Entry {
        int32  buff_key       = 1;
        int32  stack_count    = 2;
        int32  remain_time_ms = 3;   // -1=영구
        int64  caster_id      = 4;   // 시전자 object_id
        // 적용 스탯 수정치 (어떤 스탯을 얼마나 바꾸는 버프인지)
        repeated DebugStatEntry modifiers = 5;
    }
    repeated Entry entries = 2;
}

// 스테이지 전체 오브젝트 서버 좌표 (디버그, AOI 무관).
message DebugStagePositionsNtf {
    message Pos {
        int64 object_id   = 1;
        int32 object_type = 2;   // 0=Character 1=Monster 2=Prop ... (Stage 의 타입별 맵 기준)
        float pos_x = 3;
        float pos_y = 4;
        float pos_z = 5;
        float yaw   = 6;
        uint32 flags = 7;        // SnapshotNtf.ActorStateInfo 와 동일 의미 (bit0=isMoving, bit1=isDead)
    }
    uint32 server_tick_seq = 1;  // SnapshotNtf 와 같은 tick 기준 (오차 비교용)
    repeated Pos positions = 2;
}
```

---

## 6. 서버측 설계

### 6.1 송신 (기존 `PacketSender` 확장)

`PacketSender`에 `_DEBUG` 한정 편의함수를 추가한다(기존 `SendStatUpdateNtf` 등과 같은 자리·패턴).

```cpp
#ifdef _DEBUG
void SendDebugStatNtf(int64 accountId, const ActorObject& target);
void SendDebugBuffNtf(int64 accountId, const ActorObject& target);
void SendDebugStagePositionsNtf(int64 accountId, const Stage& stage);
#endif
```

- `SendDebugStatNtf`: 대상의 `StatComponentBase`에서 `getRaw` + 합성 total을 EStat 전 항목 순회해 채운다. (기존 `ForEachNonZeroStat`이 아니라 0 포함 전체.)
- `SendDebugStagePositionsNtf`: `Stage::m_objects`(통합 컨테이너)를 순회해 각 오브젝트의 권위 좌표/타입을 담는다. 타입은 `m_userObjects`/`m_monsterObjects`/`m_propObjects` 구분 기준.

### 6.2 구독 상태 보관 (`User`)

구독은 요청 유저별 상태다. `User`에 `_DEBUG` 한정 필드를 둔다(패킷 로그 모드 보관과 동일한 발상).

```cpp
#ifdef _DEBUG
    // 디버그 구독 상태. 0 = 미구독.
    int64 m_debugStatTarget = 0;     // DebugStatNtf 대상 objectId
    int64 m_debugBuffTarget = 0;     // DebugBuffNtf 대상
    bool  m_debugPositions  = false; // 스테이지 전체 위치 구독
#endif
```

치트 핸들러(`CheatManager`)가 이 값을 set/toggle 한다.

### 6.3 주기 push (디버그 Tick)

게임플레이 Tick과 분리된 **느린 디버그 Tick(예 200~500ms)**에서만 구독자에게 push 한다. 디버그는 실시간성이 덜 중요하므로 매 tick 보낼 필요가 없다.

- 구현 위치: `Stage`의 컨텐츠 스레드 업데이트 루프에 `_DEBUG` 한정 분기 하나 추가 — 누적 시간이 디버그 주기를 넘으면 `m_users`를 돌며 구독 플래그가 켜진 유저에게 해당 NTF 전송.
- 1차 구현은 **스냅샷 전송**(전체 재전송)으로 단순하게. 델타·압축은 트래픽이 문제될 때만 추가.
- `DebugStagePositionsNtf`는 오브젝트 수 상한을 두고, 과도하면 로그 경고.

### 6.4 게이팅 정리

| 계층 | 게이팅 | 근거 |
|---|---|---|
| 치트 수신(`handleCheatReq`) | `#ifdef _DEBUG` (이미 적용됨) | 라이브 빌드는 구독 명령 자체가 안 들어옴 |
| 디버그 `Send*`/`User` 필드/Tick 분기 | `#ifdef _DEBUG` | 라이브 빌드에 디버그 송신 코드 부재 |
| proto/패킷 ID | 항상 컴파일 | enum/메시지는 있어도 무해. 핸들러가 없으면 동작 안 함 |

라이브에서도 GM용으로 열어야 하면, 그때 `handleCheatReq`에 계정 권한 체크를 추가한다(서버가 판정, 클라 주장 불신). **지금은 `_DEBUG` 게이팅만으로 충분** — 과하게 미리 만들지 않는다.

---

## 7. 클라이언트측 설계

### 7.1 디버그 HUD (신규, `#if UNITY_EDITOR || DEVELOPMENT_BUILD`)

`CheatConsole`과 같은 패턴으로 자기 부트스트랩하는 IMGUI HUD를 추가한다(`DebugHud.cs`). 별도 Canvas/프리팹 불필요.

- 토글: F-key(예 F9). `CheatConsole`의 백틱과 충돌 없게.
- 선택 오브젝트(`targetObjectId`)는 `StageManager.Monsters`/캐릭터 딕셔너리에서 클릭/사이클로 지정.
- 패널을 열 때 해당 구독 치트를 전송, 닫을 때 해제 치트 전송 → 평상시 트래픽 0.
- 디버그 NTF 수신은 `PacketDispatcher.Instance.Register<DebugStatNtf>(GamePacketId.DebugStatNtf, ...)` 등록(기존 패턴), HUD `OnDestroy`에서 `Unregister`.

### 7.2 ① 스탯 보기

- **기본(최종값):** 선택 오브젝트의 `ActorObject.Stats`(`StatHolder`)를 EStat 순회로 표시. **새 패킷·구독 불필요.**
- **상세(raw/0/기여원):** `dbgstat <id>` 구독 → `DebugStatNtf` 수신 → `raw / total` 2열 표. total이 raw와 다르면 색 강조(버프/장비 영향).

### 7.3 ② 버프 보기

- **기본:** 선택 오브젝트의 `ActorObject.Buffs`(`BuffHolder`)를 표시. `BuffHolder.Entry.RemainSecondsNow()`가 이미 카운트다운 보간을 제공. **새 패킷 불필요.**
- **상세(시전자/수정치):** 필요 시 `DebugBuffNtf` 구독으로 확장. (1차 생략 가능.)

### 7.4 ③ 서버 위치 보기 (가장 가치 큰 시각화)

- **AOI 내 — 새 패킷 불필요.**
  - 각 오브젝트의 **서버 권위 위치**는 `SnapshotNtf`의 raw 좌표(보간 전)에 있다. `SnapshotInterpolator`가 들고 있는 최신 스냅샷 표본을 읽는다.
  - **클라 렌더 위치**는 해당 `ActorObject.transform.position`.
  - 둘을 `Camera.WorldToScreenPoint`로 투영해 마커로 그리고, **두 점을 선으로 연결**하면 데스싱크가 바로 보인다. (가장 유용한 디버깅.) 거리 수치도 병기.
  - 드로잉은 `SectorGridDebug`처럼 월드/스크린 오버레이 패턴 재사용.
- **AOI 밖(스테이지 전체):** `dbgpos on` 구독 → `DebugStagePositionsNtf` 수신 → 미니맵/월드에 타입별 색 마커. 화면 밖 대상은 가장자리 클램프 + 방향 표시(선택).

### 7.5 ④ 기타 권장 기능 (확장)

기존 자산을 활용해 비용 대비 효용이 높은 것들:

- **Sector/AOI 시각화:** `SectorGridDebug`가 이미 sector 격자를 그린다. 여기에 "서버가 내게 보내는 AOI 오브젝트 집합" 하이라이트를 얹으면 "왜 저 몬스터가 안 보이지" 추적에 유용.
- **네트워크 통계 HUD:** RTT, 패킷 in/out rate, 마지막 수신 경과. `NetClock`/`NetworkClient` 통계를 읽어 경량 오버레이로. (서버 `netdelay` 치트와 함께 쓰면 지연 환경 테스트에 유용.)
- **위치 오차 그래프:** §7.4의 데스싱크를 시간축 그래프로.
- **AI 상태 보기:** 몬스터 FSM 상태(`MonsterFsmAI`)는 클라에 없다. 필요해지면 `DebugStagePositionsNtf`에 `ai_state` 필드를 더하거나 별도 디버그 필드로 확장.

기타 기능은 가능한 한 **기존 데이터/오버레이를 먼저 활용**하고, 없을 때만 디버그 패킷 필드를 추가한다.

---

## 8. 구현 단계 (제안)

```
1. [클라] DebugHud 골격 + 선택 오브젝트 지정 + 기존 holder 표시 (스탯 최종값/버프)
   → verify: AOI 내 캐릭터/몬스터 선택 시 StatHolder/BuffHolder 값이 표시됨 (패킷 0개 추가)
2. [클라] 위치 오버레이: SnapshotNtf raw vs 렌더 transform 오차 시각화
   → verify: 이동 중 서버 마커가 렌더 위치를 추종, 연결선/거리 표시 확인
3. [proto] debug_packet.proto + packet_id 9100~9199 추가 → generate_proto.bat
   → verify: C++/.pb.h, C#/Assets/Generated 양쪽 생성 확인, 빌드 통과
4. [서버] CheatManager 에 dbgstat/dbgpos 등록 + User 구독 필드 + 디버그 Tick push (_DEBUG)
   → verify: 콘솔에서 dbgstat <id> 입력 → DebugStatNtf 주기 수신 로그 확인
5. [클라] DebugStatNtf / DebugStagePositionsNtf 핸들러 + HUD 표시/오버레이 확장
   → verify: raw/total 2열, 스테이지 전체 위치 마커 표시 확인
6. [검증] 라이브 설정(_DEBUG off / non-DEVELOPMENT_BUILD) 빌드에서 디버그 패킷·HUD·치트 미포함 확인
   → verify: 9100~9199 핸들러 미등록, DebugHud 컴파일 제외 확인
7. [확장] DebugBuffNtf(시전자/수정치), AI 상태, 네트 통계 등은 필요 시 점진 추가
```

---

## 9. 핵심 요약

- 요청 3대 기능 중 **스탯 최종값 / 버프 뱃지 / AOI 내 서버위치는 새 패킷 없이** 기존 `StatHolder`·`BuffHolder`·`SnapshotNtf`로 표시 가능하다 — 먼저 이걸로 만든다.
- **진짜 없는 데이터**(전체 스탯 raw·0 포함, 버프 상세, AOI 밖 위치)에만 `DebugStatNtf`/`DebugBuffNtf`/`DebugStagePositionsNtf` 3종을 추가한다.
- **활성화·구독은 기존 치트 경로 재사용** → 새 인증 패킷 불필요. 빌드 게이팅은 서버 `_DEBUG`, 클라 `UNITY_EDITOR || DEVELOPMENT_BUILD`(둘 다 이미 쓰는 규약)로 끝.
- 위치 디버깅의 핵심 가치는 **서버 권위 위치 vs 클라 렌더 위치의 오차(데스싱크) 시각화**다.

---

## 참고 자료

- [Using the Gameplay Debugger in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-gameplay-debugger-in-unreal-engine)
- [Dear ImGui](https://imgui.org/)
- [Immediate Mode GUI (ImGui) Gem — Open 3D Engine](https://docs.o3de.org/docs/user-guide/gems/reference/debug/imgui/)
- [Entity Interpolation — Gabriel Gambetta](https://www.gabrielgambetta.com/entity-interpolation.html)

---

### 부록: 참조한 기존 코드

- 패킷 ID/규약: `Server/PacketGenerator/Proto/Common/packet_id.proto`, `Proto/GamePacket/{stat,buff,move,object_visibility,cheat}_packet.proto`
- 치트: `Server/GameServer/Managers/CheatManager.{h,cpp}`, `Stages/Stage.cpp`(`handleCheatReq`, `_DEBUG` 등록), 클라 `Assets/Scripts/Game/CheatConsole.cs`
- 스탯/버프: 서버 `Components/StatComponentBase.{h,cpp}`, 클라 `Game/StatHolder.cs`, `Game/BuffHolder.cs`, `Game/ActorObject.cs`
- 위치: 서버 `Stages/Stage.h`(`m_objects` 등), 클라 `Game/SnapshotInterpolator.cs`, `Game/StageManager.cs`, 오버레이 `Game/SectorGridDebug.cs`
- 송신/수신 배관: 서버 `GameServer/PacketSender.{h,cpp}`, 클라 `Packet/PacketDispatcher.cs`, `Network/NetworkManager.cs`
