using UnityEngine;

namespace Client.Game
{
    // PlayerCharacter GameObject 를 생성하는 정적 팩토리.
    //
    // 책임:
    //   - 캡슐 프리미티브 생성
    //   - PlayerCharacter 컴포넌트 부착 + Initialize
    //   - LocalPlayer 인 경우 PlayerMoveController 도 부착
    //
    // 책임이 아닌 것:
    //   - 캐릭터 컬렉션 관리 (StageManager 가 함)
    //   - 캐릭터 선택 흐름 (CharacterSelector 가 함)
    //
    // 향후 직업별 프리팹, 머터리얼 교체, 무기 장착 등이 들어오면 이 함수가 커질 예정.
    // 그 시점에 MonoBehaviour 매니저로 승격할 수 있음.
    public static class CharacterFactory
    {
        public static PlayerCharacter Create(long userId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            // 캡슐 프리미티브로 생성 (Collider, MeshRenderer 자동 포함)
            GameObject go = GameObject.CreatePrimitive(PrimitiveType.Capsule);
            go.name = $"Character_{userId}_{name}";

            PlayerCharacter pc = go.AddComponent<PlayerCharacter>();
            pc.Initialize(userId, name, isLocalPlayer, pos, dirY);

            // LocalPlayer 에만 입력→이동 컨트롤러 부착.
            // RequireComponent(typeof(PlayerCharacter)) 보장이 있으므로 PlayerCharacter 부착 이후에 호출.
            if (isLocalPlayer)
            {
                go.AddComponent<PlayerMoveController>();
            }

            Debug.Log($"[CharacterFactory] Created {go.name} at {pos}");
            return pc;
        }
    }
}
