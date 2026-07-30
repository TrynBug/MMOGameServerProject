using GameData;
using UnityEngine;

namespace Client.Game
{
    // Item 게임데이터의 DropPrefabPath(Resources 상대경로)를 실제 필드 오브젝트로 변환한다.
    // 드롭 프리팹에는 게임플레이 로직을 요구하지 않으며 DropObject가 없으면 런타임에 부착한다.
    public static class DropFactory
    {
        public static DropObject Create(long objectId, int itemKey, int count, Vector3 origin, Vector3 landing,
            long createdServerTimeMs)
        {
            GameData_Item data = GameDataTable_Item.FindData(itemKey);
            if (data == null)
            {
                Debug.LogError($"[DropFactory] Item 데이터가 없습니다. itemKey={itemKey}");
                return null;
            }

            // 기획 데이터에 지정된 외형을 우선 사용한다. 경로에는 Resources/ 접두사와
            // .prefab 확장자를 넣지 않는다(예: Items/Drops/DropPotion).
            GameObject go = null;
            if (!string.IsNullOrEmpty(data.DropPrefabPath))
                go = Managers.Managers.Resource.Instantiate(data.DropPrefabPath);

            // 데이터가 비었거나 프리팹 로드가 실패해도 드롭 자체를 잃지 않도록 단순 구체로
            // 대체한다. primitive의 기본 Collider는 필드 물리/투사체 판정 간섭 방지를 위해 제거한다.
            if (go == null)
            {
                go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
                go.transform.localScale = Vector3.one * 0.35f;

                Collider collider = go.GetComponent<Collider>();
                if (collider != null)
                    Object.Destroy(collider);

                Renderer renderer = go.GetComponent<Renderer>();
                if (renderer != null)
                    renderer.material.color = colorForGrade(data.Grade);
            }

            // 전용 프리팹과 fallback 모두 같은 초기화/비산 경로를 사용한다.
            DropObject drop = go.GetComponent<DropObject>();
            if (drop == null)
                drop = go.AddComponent<DropObject>();

            go.name = $"Drop_{objectId}_item{itemKey}_x{count}";
            drop.Initialize(objectId, itemKey, count, origin, landing, createdServerTimeMs);
            return drop;
        }

        // fallback 구체에서도 최소한 아이템 등급을 식별할 수 있게 하는 임시 색상 규칙이다.
        private static Color colorForGrade(int grade)
        {
            if (grade >= 3)
                return new Color(0.75f, 0.35f, 1f);
            if (grade == 2)
                return new Color(0.25f, 0.75f, 1f);
            return new Color(1f, 0.85f, 0.2f);
        }
    }
}
