using Client.Managers;
using UnityEngine;

namespace Client.Game
{
    // PlayerCharacter GameObject 를 생성하는 정적 팩토리.
    //
    // 책임:
    //   - Resources 에서 캐릭터 prefab 로드 + Instantiate
    //   - PlayerCharacter 컴포넌트 초기화 (Initialize 호출)
    //   - LocalPlayer 인 경우 PlayerMoveController 부착
    //
    // 책임이 아닌 것:
    //   - 캐릭터 컬렉션 관리 (StageManager 가 함)
    //   - 캐릭터 선택 흐름 (CharacterSelector 가 함)
    //
    // 향후 직업별 prefab, 머터리얼 교체, 무기 장착 등이 들어오면 이 함수가 커질 예정.
    // 그 시점에 MonoBehaviour 매니저로 승격할 수 있음.
    //
    // prefab 경로:
    //   현재는 "Characters/PlayerCharacter" 한 종류만 사용.
    //   직업별 prefab 도입 시 enum/string 으로 분기.
    public static class CharacterFactory
    {
        // Resources/ 하위 경로 (확장자 없음).
        private const string k_playerPrefabPath = "Characters/PlayerCharacter";

        public static PlayerCharacter Create(long userId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            // prefab 로드 + Instantiate.
            // 현재는 동기 Load (Resources). 추후 Addressables 도입 시 InstantiateAsync 로 교체.
            GameObject go = Managers.Managers.Resource.Instantiate(k_playerPrefabPath);
            if (go == null)
            {
                Debug.LogError($"[CharacterFactory] PlayerCharacter prefab 로드 실패: {k_playerPrefabPath}");
                return null;
            }

            // prefab 에 부착된 PlayerCharacter 컴포넌트 획득.
            // (prefab 에 미리 붙어 있어야 함)
            PlayerCharacter pc = go.GetComponent<PlayerCharacter>();
            if (pc == null)
            {
                Debug.LogError($"[CharacterFactory] prefab 에 PlayerCharacter 컴포넌트가 없습니다: {k_playerPrefabPath}");
                GameObject.Destroy(go);
                return null;
            }

            pc.Initialize(userId, name, isLocalPlayer, pos, dirY);

            // LocalPlayer 에만 입력→이동 컨트롤러 부착.
            // 타 유저 prefab 에는 PlayerMoveController 가 없어야 함 (구조적 보장).
            if (isLocalPlayer)
            {
                go.AddComponent<PlayerMoveController>();
            }

            Debug.Log($"[CharacterFactory] Created {go.name} at {pos}");
            return pc;
        }
    }
}