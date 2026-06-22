#pragma once

#include "pch.h"

#include "Enum/GameEnum_Common.h"

// 전방선언
class Stage;

// ─────────────────────────────────────────────────────────────
// StageObject 베이스 클래스
// ─────────────────────────────────────────────────────────────
//
// Stage 내에서 살아가는 모든 객체(유저, 몬스터, 프랍, 드롭아이템 등)의
// 공통 베이스 클래스이다.
//
// 공통 속성:
//   - ObjectId (int64, 서버 전체에서 유일)
//   - ObjectType (EObjectType)
//   - 위치(x, y, z), 회전(yaw)
//     좌표계: Unity 와 동일. Y가 높이, X-Z 가 평면.
//     yaw는 Y축 회전, degree 단위.
//   - 소속 Stage 포인터
//   - 소속 섹터 좌표 (캐싱용). 섹터는 X-Z 평면으로만 분할됨.
//
// 멤버 접근은 소속 Stage의 컨텐츠 스레드에서만 이루어진다.
// 그래서 별도의 락 없이 사용한다.
class StageObject
{
public:
    StageObject() = default;
    virtual ~StageObject() = default;

    StageObject(const StageObject&) = delete;
    StageObject& operator=(const StageObject&) = delete;

    // 빈 객체로 생성한 뒤 반드시 호출하여 초기화한다. 생성자는 리턴값이 없어 실패를 알릴 수 없으므로,
    // 초기화 성공 여부는 이 함수의 반환값(false=실패)으로 확인한다. 잘못된 인자(objectId==0 / objectType==None)면 false.
    [[nodiscard]] bool Initialize(int64 objectId, EObjectType objectType);

public:
    int64       GetObjectId()   const { return m_objectId; }
    EObjectType GetObjectType() const { return m_objectType; }

    // 이 오브젝트를 소유한 유저의 ID. 캐릭터 객체(Character)만 의미있는 값을 돌려주고, 그 외(Monster/Prop 등)는 0.
    // AOI 브로드캐스트(Stage::ForEachUserInAoi)에서 user 컨테이너 순회 시 사용.
    virtual int64 GetOwnerAccountId() const { return 0; }

    float       GetPosX()       const { return m_posX; }
    float       GetPosY()       const { return m_posY; }   // 높이
    float       GetPosZ()       const { return m_posZ; }   // 평면 깊이축
    float       GetYaw()        const { return m_yaw; }    // degree, Y축 회전

    Stage*      GetStage()      const { return m_pStage; }

    int32       GetCurSectorX() const { return m_curSectorX; }
    int32       GetCurSectorZ() const { return m_curSectorZ; }

    // ── 위치/회전 설정 ──
    // 단순 setter. 섹터 갱신은 Stage 쪽에서 별도로 호출한다.
    void        SetPos(float posX, float posY, float posZ) { m_posX = posX; m_posY = posY; m_posZ = posZ; }
    void        SetYaw(float yaw)                          { m_yaw = yaw; }

    // ── Stage 및 섹터 설정 ──
    // Stage 입장/퇴장 시 Stage가 호출한다.
    void        SetStage(Stage* pStage)        { m_pStage = pStage; }
    void        SetCurSector(int32 sectorX, int32 sectorZ)
    {
        m_curSectorX = sectorX;
        m_curSectorZ = sectorZ;
    }

    // ── 매 tick 업데이트 ────────────────────────────────────
    // 소속 Stage 가 이 오브젝트의 업데이트 주기에 도달했을 때 호출한다 (컨테츠 스레드 전용).
    // deltaMs 는 "마지막 Update 이후" 누적 경과시간(주기가 길면 서버 tick 보다 크다).
    // 기본 구현은 아무것도 안 함 — 매 tick 로직이 필요한 파생만 override 한다
    // (예: Character/Monster 의 이동·AI). 안 움직이는 Prop 등은 override 불필요.
    virtual void Update(int64 deltaMs) {}

    // ── 업데이트 주기 ──────────────────────────────────────────
    // Stage 는 매 서버 tick(50ms)마다 모든 StageObject 를 순회하지만, 실제 Update 는
    // 이 오브젝트의 주기(k_updateTickUnitMs 의 배수)마다 1번만 호출된다.
    // 중요 오브젝트(유저/캐릭터/보스)는 50ms, 덜 중요한 오브젝트(잡몹/프랍)는 더 긴 주기를
    // 가질 수 있다. 주기는 오브젝트 생성 직후 또는 Stage 등록 직전에 결정한다.
    // (게임서버.md '서버 tick' 참조.)
    int64 GetUpdateIntervalMs() const { return m_updateIntervalMs; }

    // 업데이트 주기 설정. k_updateTickUnitMs(50ms)의 배수로 내림 정렬하며 최소 1단위.
    void  SetUpdateIntervalMs(int64 intervalMs);

    // Stage 등록 시점에 누적시간을 0 으로 초기화한다 (업데이트 스케줄 시작점 리셋).
    void  ResetUpdateClock() { m_updateAccumMs = 0; }

    // Stage 의 매 tick 에서 호출한다. deltaMs 를 누적하고, 주기에 도달했으면 true 와 함께
    // 마지막 Update 이후의 누적 경과시간(outElapsedMs)을 돌려주고 누적을 0 으로 리셋한다.
    // 아직 주기에 도달하지 않았으면 false (이번 tick 의 Update 는 건너뛴다).
    bool  AdvanceUpdateClock(int64 deltaMs, int64& outElapsedMs);

    // 이동 상태 노출(파생 Character/Monster 가 override). 스냅샷의 isMoving 플래그 산출에 쓰인다.
    // 안 움직이는 오브젝트(프랍 등)는 기본 false.
    virtual bool IsMoving() const { return false; }

    // ── 스냅샷 가변 송신율 ────────────────────────────────────────
    // 이번 tick 에 이 오브젝트를 스냅샷에 포함할지(due) 평가하고, due 면 마지막 송신 상태를 갱신한다.
    // due = 위치/회전이 의미있게 바뀜(이동·대시·넉백·정지 최종위치 모두 포함) OR heartbeat 주기 도달(유휴 갱신).
    // Stage::buildAndSendSnapshots 가 pass1 에서 매 tick 호출하고, pass2 에서 IsSnapshotDue() 로 읽는다.
    // minMoveIntervalTicks: 위치/회전 변화에 의한 송신을 이 주기로 throttle(LOD). 1=매 tick.
    //   몬스터는 k_monsterSnapshotIntervalTicks(2,10Hz)로 대역폭 절감. 캐릭터는 1(20Hz).
    //   ★이동↔정지 전환과 heartbeat 은 throttle 을 우회해 즉시/주기대로 보낸다(정지 누락 → "제자리 달리기" 방지).
    bool EvaluateSnapshotDue(uint32 currentTickSeq, uint32 heartbeatTicks, uint32 minMoveIntervalTicks = 1)
    {
        constexpr float kPosEpsSq  = 0.0001f;   // (0.01u)^2
        constexpr float kYawEpsDeg = 1.0f;

        const float dx = m_posX - m_lastSentPosX;
        const float dz = m_posZ - m_lastSentPosZ;
        float yawDiff = m_yaw - m_lastSentYaw;
        if (yawDiff < 0.0f) yawDiff = -yawDiff;

        const bool moving = IsMoving();

        const uint32 ticksSinceSent = currentTickSeq - m_lastSentTickSeq;

        // 이동 플래그 전환(이동↔정지)은 즉시 due — throttle 우회. 같은 자리에서 멈추면(StopAt(현재위치))
        // 위치 변화가 없어 누락되던 정지 스냅샷을 즉시 보내 클라가 heartbeat 동안 제자리 달리기 후 튕기는 현상 방지.
        const bool flagTransition = (moving != m_lastSentMoving);
        // 위치/회전 변화는 minMoveIntervalTicks 주기로만 보낸다(몬스터 10Hz LOD).
        const bool movedOrTurned  = (dx * dx + dz * dz) > kPosEpsSq || yawDiff > kYawEpsDeg;

        const bool changed   = flagTransition || (movedOrTurned && ticksSinceSent >= minMoveIntervalTicks);
        const bool heartbeat = ticksSinceSent >= heartbeatTicks;
        m_snapshotDue = changed || heartbeat;

        if (m_snapshotDue)
        {
            m_lastSentPosX    = m_posX;
            m_lastSentPosZ    = m_posZ;
            m_lastSentYaw     = m_yaw;
            m_lastSentMoving  = moving;
            m_lastSentTickSeq = currentTickSeq;
        }
        return m_snapshotDue;
    }
    bool IsSnapshotDue() const { return m_snapshotDue; }

private:
    int64       m_objectId   = 0;
    EObjectType m_objectType = EObjectType::None;

    float       m_posX = 0.0f;
    float       m_posY = 0.0f;   // 높이
    float       m_posZ = 0.0f;   // 평면 깊이축
    float       m_yaw  = 0.0f;   // degree, Y축 회전

    // 현재 소속 Stage. Stage가 소유하기 때문에 raw pointer로 들고 있어도 안전.
    // (Stage가 자신보다 먼저 destruct되지는 않음)
    Stage*      m_pStage = nullptr;

    // 현재 속한 섹터 좌표(캐싱). 섹터 이동 판정에 사용. 섹터는 X-Z 평면 분할.
    // 아직 어떤 섹터에도 속하지 않은 상태를 표현하기 위해 -1 로 초기화.
    int32       m_curSectorX = -1;
    int32       m_curSectorZ = -1;

    // ── 업데이트 주기 ──
    // 서버 tick 단위(ms). 업데이트 주기는 이 값의 배수여야 한다.
    // (ContentsThread 의 기본 update 주기와 일치. 게임서버.md '서버 tick' 참조.)
    static constexpr int64 k_updateTickUnitMs = 50;

    // m_updateIntervalMs : 이 오브젝트의 Update 호출 주기(ms). 기본값 없음 — Stage 등록 시
    //                      반드시 SetUpdateIntervalMs 로 설정된다. 0 은 "미설정" 을 뜻한다.
    // m_updateAccumMs    : 마지막 Update 이후 누적된 경과시간(ms). 주기 도달 시 0 으로 리셋.
    int64       m_updateIntervalMs = 0;
    int64       m_updateAccumMs    = 0;

    // 스냅샷 가변 송신율 상태. EvaluateSnapshotDue 가 갱신.
    float       m_lastSentPosX    = 0.0f;
    float       m_lastSentPosZ    = 0.0f;
    float       m_lastSentYaw     = 0.0f;
    bool        m_lastSentMoving  = false;
    uint32      m_lastSentTickSeq = 0;
    bool        m_snapshotDue     = false;
};

using StageObjectPtr  = std::shared_ptr<StageObject>;
using StageObjectWPtr = std::weak_ptr<StageObject>;
