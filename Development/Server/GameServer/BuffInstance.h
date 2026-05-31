#pragma once

#include "pch.h"

// 전방선언 (포인터만 보관하므로 완전타입 불필요)
struct GameData_Buff;

// ─────────────────────────────────────────────────────────────
// BuffInstance
// ─────────────────────────────────────────────────────────────
//
// 액터(Character/Monster)에 적용된 버프 1개의 런타임 인스턴스.
//
// ── 소스 추적 안 함 ──
// 게임데이터(GameData_Buff)는 로드 후 불변이므로, 적용 시 ApplyStat 한 값을
// 해제 시 같은 데이터값으로 RemoveStat 하여 되돌린다. (StatComponentBase 의 누적 모델과 동일 철학.)
// 별도의 "이 버프가 더한 델타" 저장은 하지 않는다 — pData + stackCount 로 재계산 가능.
//
// ── 시간 모델 (절대시각 아님) ──
// remainMs / tickAccumMs 는 Update(deltaMs) 로 누적/감산된다. deltaMs 는
// "마지막 Update 이후 누적 경과시간"이라(오브젝트 주기가 길면 50ms 보다 큼) 그 사이
// 발생했어야 할 주기효과 틱도 tickAccumMs 로 보정해 발사한다. (게임서버.md '서버 tick' 참조.)
struct BuffInstance
{
    const GameData_Buff* pData = nullptr;   // 버프 종류 데이터 (불변, 소유권 없음)
    int32 stackCount     = 1;               // 현재 스택 수 (1 이상)
    int64 remainMs       = 0;               // 남은 지속시간(ms). -1 = 영구.
    int64 tickAccumMs    = 0;               // 주기효과 누적시간(ms). TickIntervalMs>0 일 때만 사용.
    int64 casterObjectId = 0;               // 시전자 objectId (kill credit 용). 0 = 없음.
};
