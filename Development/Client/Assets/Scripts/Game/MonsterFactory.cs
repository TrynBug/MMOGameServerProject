using Client.Managers;
using GameData;
using UnityEngine;

namespace Client.Game
{
    // 몬스터 GameObject 를 생성하는 정적 팩토리. (CharacterFactory 와 같은 패턴)
    //
    // 책임:
    //   - 몬스터 게임데이터 Key 로 PrefabPath 조회
    //   - Resources 에서 prefab 로드 + Instantiate
    //   - MonsterObject 컴포넌트 부착 + 초기화
    //
    // 캐릭터 prefab 과 달리 몬스터 prefab 은 임의의 아트 에셋이라 MonsterObject 가
    // 미리 붙어있지 않을 수 있다. 그래서 없으면 런타임에 AddComponent 한다.
    //
    // 향후 Addressables / 풀링 도입 시 이 함수가 교체 지점이 된다.
    public static class MonsterFactory
    {
        public static MonsterObject Create(long objectId, long monsterKey, Vector3 pos, float dirY)
        {
            // 1) 몬스터 게임데이터 조회.
            GameData_Monster data = GameDataTable_Monster.FindData(monsterKey);
            if (data == null)
            {
                Debug.LogError($"[MonsterFactory] Monster 게임데이터를 찾을 수 없습니다. monsterKey={monsterKey}");
                return null;
            }

            // 2) PrefabPath 검증.
            if (string.IsNullOrEmpty(data.PrefabPath))
            {
                Debug.LogError($"[MonsterFactory] PrefabPath 가 비어있습니다. monsterKey={monsterKey} name={data.Name}");
                return null;
            }

            // 3) prefab 로드 + Instantiate (Resources 기반 동기 로드).
            GameObject go = Managers.Managers.Resource.Instantiate(data.PrefabPath);
            if (go == null)
            {
                Debug.LogError($"[MonsterFactory] 몬스터 prefab 로드 실패: {data.PrefabPath} (monsterKey={monsterKey})");
                return null;
            }

            // 4) MonsterObject 컴포넌트 확보 (없으면 부착).
            MonsterObject mo = go.GetComponent<MonsterObject>();
            if (mo == null)
                mo = go.AddComponent<MonsterObject>();

            // 5) 공통 애니메이터 확보 (없으면 부착).
            //    Animator 자체는 아트 prefab 의 자식에 있고, AnimatorActorAnimator 가 그걸 찾아 쓴다.
            //    MonsterObject.Initialize 가 이 컴포넌트를 IActorAnimator 로 해석하므로 Initialize 앞에 붙인다.
            if (go.GetComponent<IActorAnimator>() == null)
                go.AddComponent<AnimatorActorAnimator>();

            // 6) 초기화.
            mo.Initialize(objectId, monsterKey, pos, dirY);

            go.name = $"Monster_{objectId}_key{monsterKey}";

            Debug.Log($"[MonsterFactory] Created {go.name} at {pos}");
            return mo;
        }
    }
}
