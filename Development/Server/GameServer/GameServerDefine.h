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
constexpr int64 k_monsterUpdateIntervalMs   = 50;     // 몬스터: 매 tick (스냅샷 스트리밍 부드러움용). TODO: AI think(성김)/이동 integrate(매tick) 분리로 비용 최적화.


// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};
