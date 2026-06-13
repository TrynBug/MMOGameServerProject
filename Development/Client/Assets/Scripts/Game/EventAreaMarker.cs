using UnityEngine;

namespace Client.Game
{
    // 이벤트영역 모양. 서버 StageLayout 의 shape("Sphere"/"Box")와 대응.
    public enum EAreaShape { Sphere, Box }

    // 스테이지 맵에 배치하는 이벤트영역 마커.
    //
    // 유니티 씬(스테이지 프리팹)에 빈 GameObject 로 놓고 이 컴포넌트를 붙인다. 영역의 중심은
    // 이 오브젝트의 transform.position 이고, 모양/크기는 아래 필드로 정한다.
    // EventKey/중심/크기는 서버 레이아웃(Map/StageLayout/<stageDataKey>.json)의 같은 영역과 일치시켜야 한다
    // (현재는 양쪽 수작업. 추후 에디터 export 툴이 둘을 한 소스에서 생성).
    //
    // 판정은 평면(X-Z) — 쿼터뷰·NavMesh 이동이라 높이는 무시한다(서버와 동일).
    // 실제 트리거는 EventAreaDetector 가 로컬 플레이어 위치로 매 프레임 검사한다.
    public class EventAreaMarker : MonoBehaviour
    {
        public int        EventKey;
        public EAreaShape Shape  = EAreaShape.Sphere;
        public float      Radius = 5f;     // Sphere 반경
        public float      SizeX  = 10f;    // Box 전체 크기(X)
        public float      SizeZ  = 10f;    // Box 전체 크기(Z)

        // worldPos 가 영역 안인지 평면 판정. (허용오차는 서버가 검증 시 적용 — 클라는 실제 경계로 판정.)
        public bool Contains(Vector3 worldPos)
        {
            Vector3 c = transform.position;
            float dx = worldPos.x - c.x;
            float dz = worldPos.z - c.z;

            if (Shape == EAreaShape.Box)
                return Mathf.Abs(dx) <= SizeX * 0.5f && Mathf.Abs(dz) <= SizeZ * 0.5f;

            return dx * dx + dz * dz <= Radius * Radius;
        }

        // 에디터 시각화(배치 편의). 게임 플레이엔 영향 없음.
        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(0.2f, 0.8f, 1f, 0.5f);
            if (Shape == EAreaShape.Box)
                Gizmos.DrawWireCube(transform.position, new Vector3(SizeX, 0.2f, SizeZ));
            else
                Gizmos.DrawWireSphere(transform.position, Radius);
        }
    }
}
