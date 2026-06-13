using UnityEngine;

namespace Client.Game
{
    // 스포너 배치 마커. 스테이지 씬/프리팹에 두고 export 가 읽어 서버 레이아웃 json 의 spawners 로 출력한다.
    //
    // 위치/반경만 여기서 정한다. 동작 파라미터(SpawnGroup/MaxPacks/RespawnDelay/Activation 등)는
    // 서버 GameData_Spawner(같은 Key)에 있다(몬스터스폰.md). 중심 = transform.position.
    //   Radius == 0 : 고정앵커(정확 좌표)
    //   Radius >  0 : 밀도존(반경 내 NavMesh 랜덤 배치)
    public class SpawnerMarker : MonoBehaviour
    {
        public int   Key;
        public float Radius = 0f;

        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(1f, 0.5f, 0.1f, 0.6f);
            if (Radius > 0f)
                Gizmos.DrawWireSphere(transform.position, Radius);
            else
                Gizmos.DrawWireCube(transform.position, Vector3.one * 0.5f);
        }
    }
}
