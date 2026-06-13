using UnityEngine;

namespace Client.Game
{
    // 스폰지점 배치 마커. 스테이지 씬/프리팹에 두고 export 가 읽어 서버 레이아웃 json 의 spawnPoints 로 출력한다.
    //
    // 위치 = transform.position, 방향(yaw) = transform.eulerAngles.y (오브젝트 회전 그대로).
    // 스크립트의 SpawnMonsterAt(spawnPointKey, ...) 가 이 Key 를 참조한다.
    public class SpawnPointMarker : MonoBehaviour
    {
        public int Key;

        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(0.2f, 1f, 0.4f, 0.8f);
            Gizmos.DrawWireSphere(transform.position, 0.4f);
            Gizmos.DrawLine(transform.position, transform.position + transform.forward * 1.2f);   // yaw 방향
        }
    }
}
