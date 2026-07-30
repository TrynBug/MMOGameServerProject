#include "pch.h"
#include "Components/InventoryComponent.h"

#include "Generated/GameData_Item.h"

bool InventoryComponent::Initialize(const std::vector<DataStructures::Item>& items)
{
    // 재접속/캐릭터 전환 때 이전 런타임 상태가 섞이지 않도록 항상 전체 교체한다.
    m_items.clear();
    for (const DataStructures::Item& item : items)
    {
        // DB JSON도 신뢰하지 않는다. 정적 Item 데이터와 stack 상한을 함께 검증한다.
        const GameData_Item* pData = GameDataTable_Item::FindData(item.item_key());
        if (item.item_id() <= 0 || item.count() <= 0 || pData == nullptr || item.count() > pData->MaxStack)
        {
            LOG_WRITE(LogLevel::Error, std::format(
                "invalid inventory item. itemId={} itemKey={} count={}", item.item_id(), item.item_key(), item.count()));
            return false;
        }
        if (!m_items.emplace(item.item_id(), item).second)
        {
            LOG_WRITE(LogLevel::Error, std::format("duplicate inventory item id. itemId={}", item.item_id()));
            return false;
        }
    }
    return true;
}

std::vector<DataStructures::Item> InventoryComponent::AddStackable(
    const GameData_Item& itemData, int32 count, const std::function<int64()>& generateItemId)
{
    std::vector<DataStructures::Item> updated;
    if (count <= 0 || itemData.MaxStack <= 0)
        return updated;

    // 기존 stack을 우선 사용하면 불필요한 DB 행과 item_id 생성을 최소화할 수 있다.
    // m_items가 std::map이라 충전 순서는 item_id 오름차순으로 결정적이다.
    for (auto& [itemId, item] : m_items)
    {
        if (count <= 0)
            break;
        if (item.item_key() != itemData.Key || item.count() >= itemData.MaxStack)
            continue;

        const int32 addCount = std::min(count, itemData.MaxStack - item.count());
        item.set_count(item.count() + addCount);
        count -= addCount;
        updated.push_back(item);
    }

    // 기존 stack으로 모두 담지 못한 수량만 새 행으로 분할한다. 현재 정책은 슬롯 제한이
    // 없으므로 generateItemId가 유효한 ID를 준다는 전제에서 남은 수량을 전부 수용한다.
    while (count > 0)
    {
        DataStructures::Item item;
        item.set_item_id(generateItemId());
        item.set_item_key(itemData.Key);
        item.set_item_type(itemData.ItemType);
        item.set_grade(itemData.Grade);
        item.set_count(std::min(count, itemData.MaxStack));
        count -= item.count();

        m_items.emplace(item.item_id(), item);
        updated.push_back(std::move(item));
    }

    return updated;
}
