using UnityEngine;

namespace Client.Game
{
    // 1마리 몬스터를 표현하는 컴포넌트.
    // 서버의 ObjectVisibilityNtf(monster_spawns) 수신 시 StageManager 가 동적으로 생성한다.
    //
    // 현재 단계는 정적 몬스터(이동/AI 없음)이므로 식별자와 위치만 보관한다.
    // 스탯/HP, 이동/AI 가 클라에 필요해지면 ActorObject 상속 등으로 확장한다.
    //
    // prefab 은 임의의 아트 에셋이라 이 컴포넌트가 미리 붙어있지 않을 수 있다.
    // 그래서 MonsterFactory 가 Instantiate 후 런타임에 AddComponent 한다.
    public class MonsterObject : MonoBehaviour
    {
        // 서버가 발급한 오브젝트 식별자 (디스폰 시 이 값으로 찾는다).
        public long ObjectId { get; private set; }

        // 몬스터 게임데이터 Key (종류 식별).
        public long MonsterKey { get; private set; }

        // MonsterFactory 가 생성 직후 1회 호출.
        public void Initialize(long objectId, long monsterKey, Vector3 pos, float dirY)
        {
            ObjectId = objectId;
            MonsterKey = monsterKey;

            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
        }
    }
}
