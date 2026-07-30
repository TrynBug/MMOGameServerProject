#pragma once

// GameServer 전용 상수, enum, 세션 메타정보 구조체 등 정의

// ── 수학 타입 ────────────────────────────────────────────────
// 3D 벡터. 좌표계는 Unity 와 동일: Y 가 높이, X-Z 가 평면.
// 현재는 스킬/효과 시스템에서만 사용한다. 향후 좌표 표현을 점진적으로 이 타입으로 통일할 예정.
// (struct 이므로 멤버에 m_ 접두를 붙이지 않는다. CodeConvention 참조.)
struct Vector3
{
    float x = 0.0f;
    float y = 0.0f;   // 높이
    float z = 0.0f;   // 평면 깊이축

    Vector3() = default;
    Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

    Vector3 operator+(const Vector3& rhs) const { return Vector3(x + rhs.x, y + rhs.y, z + rhs.z); }
    Vector3 operator-(const Vector3& rhs) const { return Vector3(x - rhs.x, y - rhs.y, z - rhs.z); }
    Vector3 operator*(float scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }

    Vector3& operator+=(const Vector3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    Vector3& operator-=(const Vector3& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }

    // 3D 내적 / 길이
    float Dot(const Vector3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    // X-Z 평면 길이 (높이 무시). 게임 로직 대부분이 평면 거리로 판정하므로 자주 쓰인다.
    float LengthSqXZ() const { return x * x + z * z; }
    float LengthXZ() const { return std::sqrt(LengthSqXZ()); }

    // 정규화된 벡터. 길이가 0 에 가까우면 영벡터를 리턴한다.
    Vector3 Normalized() const
    {
        const float len = Length();
        if (len < 1e-6f)
            return Vector3();
        const float inv = 1.0f / len;
        return Vector3(x * inv, y * inv, z * inv);
    }
};

// ── Stage 데이터 Key (GameData_Stage의 Key와 일치) ───────────────────────
// 게임서버 시작 시 항상 생성되는 고정 Stage들의 Key
constexpr int32 k_systemStageDataKey = 1;     // 캐릭터 선택창
constexpr int32 k_townStageDataKey   = 100;   // 마을

// ── AOI (Area of Interest) ───────────────────────────────────────
// 캐릭터의 시야 범위. range=1이면 자기 sector 포함 3x3, range=2면 5x5.
// 현재는 모든 캐릭터가 동일 값을 사용한다.
constexpr int32 k_aoiRange = 1;

// ── 오브젝트 업데이트 주기 (ms) ────────────────────────────────
// Stage 등록 시 각 오브젝트에 명시적으로 전달하는 업데이트 주기. 서버 tick(50ms)의 배수.
// 중요 오브젝트(캐릭터)는 매 tick, 그 외는 종류에 맞게 조정한다.
constexpr int64 k_characterUpdateIntervalMs = 50;    // 캐릭터: 매 tick (중요)
constexpr int64 k_monsterUpdateIntervalMs   = 500;   // 몬스터 idle 기본 주기(비관여, 비용 절감). 타겟 관여 시 Monster::SetEngagedTick 으로 engaged(데이터, 예 100ms)로 승격. 적응형 LOD.
constexpr int64 k_propUpdateIntervalMs      = 500;   // prop: 정적이라 거의 Update 불필요. DespawnDelay 예약된 prop 의 제거타이머만 이 주기로 진행.
constexpr int64 k_dropUpdateIntervalMs      = 500;   // 드롭: 정적이며 만료 sweep만 수행.

constexpr int64 k_dropLifetimeMs = 180000;            // 개인 드롭 유지시간 3분.
constexpr float k_dropScatterRadius = 1.0f;            // 몬스터 사망 위치 주변 착지 반경.
constexpr float k_itemPickupServerRange = 2.0f;        // 서버 권위 습득 허용 반경(클라 1.5m보다 여유).
constexpr int32 k_itemPickupMaxBatch = 32;

// ── 스냅샷 가변 송신율 ────────────────────────────────────────────
// 위치/회전이 바뀐 오브젝트는 매 tick(20Hz), 유휴 오브젝트는 이 주기로만 스냅샷에 포함한다.
// (헤더는 매 tick 항상 전송하여 클라 보간 시계를 굶기지 않는다 — 목록만 가변.)
constexpr uint32 k_snapshotIdleHeartbeatTicks = 20;   // 유휴 오브젝트 갱신 주기(tick). 20 = 1초.

// 몬스터 위치 스냅샷 cadence(tick). 캐릭터는 매 tick(1=20Hz), 몬스터는 LOD 로 이 주기(2=100ms,10Hz).
// 이동↔정지 전환과 heartbeat 은 이 throttle 을 우회해 즉시/주기대로 송신된다(정지 누락 방지).
// 클라 보간이 10Hz 에서도 매끄러우려면 NetClock.InterpDelayMs 가 충분해야 한다(현재 150ms).
constexpr uint32 k_monsterSnapshotIntervalTicks = 2;

// ── NetClock 시각 동기 ────────────────────────────────────────────
// 서버가 클라 NetClock 앵커(TimeSyncNtf)를 보내는 주기(tick). SnapshotNtf 와 독립.
// NetClock 은 로컬시간으로 자체 전진하므로 저빈도로 충분하다(2Hz = 10 tick).
constexpr uint32 k_timeSyncPeriodTicks = 10;   // 시각 동기 송신 주기(tick). 10 = 0.5초(2Hz).


// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};

// ── 패킷 로깅 치트 (개발용) ───────────────────────────────────────
// packet / packet all / packetdetail / packetdetail all 치트의 상태/헬퍼.
// 모드 단계: None < Name < Detail. 유효 모드 = max(유저별, 글로벌).
enum class EPacketLogMode : uint8 { None = 0, Name = 1, Detail = 2 };

class User;
namespace google { namespace protobuf { class Message; } }

namespace packetlog
{
    // 유효 로그 모드 = max(해당 유저의 per-client 모드, CheatManager 의 글로벌 모드).
    EPacketLogMode EffectiveMode(const User& user);

    // 패킷 1건을 서버 로그에 1줄로 출력한다.
    //   pMsg != nullptr : 이름 + JSON(내용),  nullptr : 이름만.
    //   dir : "S->C" 또는 "C->S".
    void LogPacket(const char* dir, int64 accountId, uint16 packetType, const google::protobuf::Message* pMsg);
}
