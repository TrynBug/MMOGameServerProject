using GameData;
using UnityEngine;

namespace Client.Game
{
    // prop GameObject 를 생성하는 정적 팩토리. (MonsterFactory 와 같은 패턴)
    //
    // 책임:
    //   - prop 게임데이터 Key 로 PrefabPath 조회
    //   - Resources 에서 prefab 로드 + Instantiate
    //   - PropObject 컴포넌트 부착 + 초기화
    //
    // prop prefab 은 임의의 아트 에셋이라 PropObject 가 미리 붙어있지 않을 수 있어 런타임에 AddComponent 한다.
    // 향후 Addressables / 풀링 도입 시 이 함수가 교체 지점이 된다.
    public static class PropFactory
    {
        public static PropObject Create(long objectId, int propKey, Vector3 pos, float dirY, int state)
        {
            // 1) prop 게임데이터 조회.
            GameData_Prop data = GameDataTable_Prop.FindData(propKey);
            if (data == null)
            {
                Debug.LogError($"[PropFactory] Prop 게임데이터를 찾을 수 없습니다. propKey={propKey}");
                return null;
            }

            // 2) PrefabPath 검증.
            if (string.IsNullOrEmpty(data.PrefabPath))
            {
                Debug.LogError($"[PropFactory] PrefabPath 가 비어있습니다. propKey={propKey}");
                return null;
            }

            // 3) prefab 로드 + Instantiate (Resources 기반 동기 로드).
            GameObject go = Managers.Managers.Resource.Instantiate(data.PrefabPath);
            if (go == null)
            {
                Debug.LogError($"[PropFactory] prop prefab 로드 실패: {data.PrefabPath} (propKey={propKey})");
                return null;
            }

            // 4) PropObject 컴포넌트 확보 (없으면 부착).
            PropObject po = go.GetComponent<PropObject>();
            if (po == null)
                po = go.AddComponent<PropObject>();

            // 5) 초기화.
            po.Initialize(objectId, propKey, pos, dirY, state);

            go.name = $"Prop_{objectId}_key{propKey}";

            Debug.Log($"[PropFactory] Created {go.name} at {pos} state={state}");
            return po;
        }
    }
}
