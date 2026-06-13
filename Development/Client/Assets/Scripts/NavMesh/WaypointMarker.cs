using UnityEngine;

namespace Client.Game
{
    // 경로(순찰/에스코트) 배치 마커. 스테이지 씬/프리팹에 두고 export 가 읽어 서버 레이아웃 json 의 waypoints 로 출력한다.
    //
    // 경로 점 = 이 오브젝트의 "자식 Transform 들"(자식 순서 = 경로 순서). 빈 자식 GameObject 를 순서대로 배치.
    // 스크립트의 GetWaypointPath(key) / MoveMonsterAlongPath 가 이 Key 를 참조한다.
    public class WaypointMarker : MonoBehaviour
    {
        public int Key;

        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(0.8f, 0.4f, 1f, 0.8f);
            Transform prev = null;
            foreach (Transform child in transform)
            {
                Gizmos.DrawWireSphere(child.position, 0.3f);
                if (prev != null)
                    Gizmos.DrawLine(prev.position, child.position);
                prev = child;
            }
        }
    }
}
