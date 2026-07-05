using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// 맵 검증용 프로브 마커. 씬에 배치해두면 에디터 검증기(Tools/MapVerify)가 수집해서
    /// NavMesh 빌드 결과를 자동 회귀검사한다. (NavMeshBlockVolume 과 같은 배치 패턴)
    /// - Walkable: 이 지점은 통행 가능해야 함 (사냥방·통로 중심 등)
    /// - Blocked: 이 지점은 폴리곤이 없어야 함 (절벽 상단·수역 등)
    /// - PathFromSpawn: 스폰 지점에서 이 지점까지 경로 탐색이 성공해야 함
    /// </summary>
    public class NavProbeMarker : MonoBehaviour
    {
        public enum ProbeType { Walkable, Blocked, PathFromSpawn }

        public ProbeType type = ProbeType.Walkable;

        private void OnDrawGizmos()
        {
            Gizmos.color = type switch
            {
                ProbeType.Blocked => new Color(1f, 0.25f, 0.25f, 0.9f),
                ProbeType.PathFromSpawn => new Color(0.3f, 0.55f, 1f, 0.9f),
                _ => new Color(0.3f, 1f, 0.4f, 0.9f),
            };
            Gizmos.DrawWireSphere(transform.position, 0.6f);
            Gizmos.DrawLine(transform.position, transform.position + Vector3.up * 2f);
        }
    }
}
