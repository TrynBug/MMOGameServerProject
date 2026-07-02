using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// NavMesh 에서 "진입불가(walkable 제외) 영역"을 지정하는 볼륨 마커.
    ///
    /// 왜 콜라이더 대신 이걸 쓰나:
    /// - 콜라이더로 nav 를 막으려면 비-트리거(물리 충돌)여야 해서 투사체/물리까지 막히고,
    ///   높이도 agentHeight 보다 커야 하는 등 신경 쓸 게 많다.
    /// - 이 볼륨은 물리와 무관하게 "nav 만" 막는다. 레벨 디자이너가 박스를 크게 드래그해
    ///   못 가는 구역(맵 밖 경계, 낭떠러지, 금지 구역 등)을 손쉽게 지정하는 용도.
    ///
    /// 동작:
    /// - 빌드 시 NavMeshBuilder 가 씬의 활성 NavMeshBlockVolume 들을 모아
    ///   Recast convex volume(area = NULL)로 입력에 추가한다.
    /// - 결과적으로 이 박스 영역 안의 walkable 은 NavMesh 에서 제거된다.
    /// - 박스는 transform 의 회전/스케일을 따른다(Y축 회전 지원). center/size 로 로컬 박스를 정의.
    ///
    /// 사용법:
    /// - 빈 GameObject 에 이 컴포넌트를 붙이고 위치/회전/스케일 또는 size 로 영역을 맞춘다.
    /// - 씬 어디에 두든 활성 상태면 빌드에 반영된다(NavRoot 안/밖 무관).
    /// </summary>
    public class NavMeshBlockVolume : MonoBehaviour
    {
        [Tooltip("로컬 공간 기준 박스 중심.")]
        public Vector3 center = Vector3.zero;

        [Tooltip("로컬 공간 기준 박스 크기(transform 스케일이 추가로 곱해짐).")]
        public Vector3 size = new Vector3(10f, 4f, 10f);

        // 씬 뷰에서 빨간 박스로 no-go 영역을 표시(디자이너 편의).
        private void OnDrawGizmos()
        {
            DrawBox(new Color(1f, 0.2f, 0.2f, 0.07f), new Color(1f, 0.28f, 0.28f, 0.85f));
        }

        private void OnDrawGizmosSelected()
        {
            DrawBox(new Color(1f, 0.2f, 0.2f, 0.14f), new Color(1f, 0.4f, 0.4f, 1f));
        }

        private void DrawBox(Color fill, Color wire)
        {
            Matrix4x4 prev = Gizmos.matrix;
            // localToWorldMatrix 로 회전/스케일을 반영해 로컬 박스를 그린다.
            Gizmos.matrix = transform.localToWorldMatrix;
            Gizmos.color = fill;
            Gizmos.DrawCube(center, size);
            Gizmos.color = wire;
            Gizmos.DrawWireCube(center, size);
            Gizmos.matrix = prev;
        }
    }
}
