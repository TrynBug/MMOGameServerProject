#pragma once

#include "pch.h"

#include "Enum/GameEnum_Common.h"  // EObjectType

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
    StageObject(int64 objectId, EObjectType objectType);
    virtual ~StageObject() = default;

    StageObject(const StageObject&) = delete;
    StageObject& operator=(const StageObject&) = delete;

public:
    int64       GetObjectId()   const { return m_objectId; }
    EObjectType GetObjectType() const { return m_objectType; }

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
};

using StageObjectPtr  = std::shared_ptr<StageObject>;
using StageObjectWPtr = std::weak_ptr<StageObject>;
