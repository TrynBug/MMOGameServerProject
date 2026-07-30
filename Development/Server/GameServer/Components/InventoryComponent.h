#pragma once

#include "pch.h"

#include <map>

struct GameData_Item;

// 인벤토리 컴포넌트
// DB의 Item 테이블 데이터를 저장함
// - Character와 같은 Stage 컨텐츠 스레드에서만 접근하므로 내부 lock은 두지 않는다.
// - 이 컴포넌트는 메모리 계산만 담당한다. DB 저장 시점과 성공/실패에 따른 반영 여부는 호출자인 Stage가 결정한다
class InventoryComponent
{
public:
    // DB에서 로드한 전체 Item 행으로 초기화한다.
    bool Initialize(const std::vector<DataStructures::Item>& items);

    // 동일 item_key의 기존 stack 빈 공간을 item_id 순서로 먼저 채운 뒤, 남은 수량을 MaxStack 이하의 새 Item 행들로 나눈다. 
    // 반환값은 이번 호출에서 생성되거나 count가 바뀐 행의 '최종값'이며, 호출자는 이 행들만 DB upsert 및 클라이언트 updated_items에 사용하면 된다.
    std::vector<DataStructures::Item> AddStackable(
        const GameData_Item& itemData, int32 count, const std::function<int64()>& generateItemId);

    const std::map<int64, DataStructures::Item>& GetItems() const { return m_items; }

private:
	std::map<int64, DataStructures::Item> m_items;  // Key: item_id, Value: Item proto
};
