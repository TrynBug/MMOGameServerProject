using UnityEngine;

namespace Client.Game
{
    // 이벤트영역 모양. 서버 StageLayout 의 shape("Sphere"/"Box")와 대응.
    public enum EAreaShape { Sphere, Box }

    // 스테이지 맵에 배치하는 이벤트영역 마커.
    //
    // 유니티 씬(스테이지 프리팹)에 빈 GameObject 로 놓고 이 컴포넌트를 붙인다. 영역의 중심은
    // 이 오브젝트의 transform.position 이고, 모양/크기는 아래 필드로 정한다.
    // 이 마커들은 Tools/StageLayout/Export Active Scene 으로 서버 레이아웃(Map/StageLayout/<씬이름>.json)에 export 된다.
    // 런타임엔 스테이지 프리팹에 함께 포함되어 EventAreaDetector 가 직접 읽는다(클라는 json 불필요).
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
        public bool       Secure = false;  // true = 클라 미신뢰. 서버가 권위 위치로 직접 폴링(export 시 json 의 secure 로 기록).

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
