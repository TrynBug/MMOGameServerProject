using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// NavMesh 빌드의 "입력 지오메트리"를 표시하는 마커 컴포넌트.
    ///
    /// 왜 필요한가?
    /// - Unity 씬에는 시각용 메시(나무, 풀, 장식)와 충돌/내비용 메시(지형, 벽)가 섞여있다.
    /// - Recast/Detour는 입력 메시를 받아 walkable 영역을 계산하는데,
    ///   시각용 메시까지 입력에 넣으면 (1) 빌드가 느려지고 (2) 결과가 지저분해진다.
    /// - 그래서 "이 게임오브젝트는 NavMesh 빌드에 포함시켜라"는 명시적 표시가 필요하다.
    /// - 이 컴포넌트가 그 마커 역할을 한다.
    ///
    /// 사용법:
    /// - 충돌/내비 지오메트리의 루트 오브젝트에 이 컴포넌트를 붙인다.
    /// - includeChildren=true (기본) 면 자식 트리의 모든 MeshFilter 가 자동 수집된다.
    /// - 시각용 메시는 별도의 부모 아래 두고 이 컴포넌트를 붙이지 않으면 빌드에서 제외된다.
    ///
    /// 주의: 이 클래스는 MonoBehaviour 이므로 런타임 컴포넌트다.
    /// (Editor 폴더가 아닌 Scripts 폴더에 위치하는 이유)
    /// </summary>
    public class NavMeshSource : MonoBehaviour
    {
        /// <summary>
        /// 이 영역에 부여할 Recast area id (1~63 범위).
        ///
        /// Recast는 각 폴리곤마다 area id를 부여할 수 있고, 길찾기 시 area별로 cost를 다르게 줄 수 있다.
        /// 예: 1=Ground(일반 지면), 2=Water(물, 천천히), 3=Road(길, 빠르게) 같은 식.
        /// 0은 "통행 불가"를 의미하므로 walkable 영역에는 사용하지 않는다.
        ///
        /// 지금은 빌더 코드에서 단일 area id(1)로 일괄 처리하고 있어 이 필드는 미사용 상태.
        /// 영역별 cost 가 필요해지면 빌더 코드에서 이 값을 활용하면 된다.
        /// </summary>
        [Tooltip("이 영역의 area id (1=Ground 기본값). Recast의 area cost 와 매칭됩니다.")]
        public int areaId = 1;

        /// <summary>
        /// true 면 이 게임오브젝트의 자식 트리 전체에서 MeshFilter 를 수집한다.
        /// false 면 자기 자신의 MeshFilter 만 수집한다.
        ///
        /// 일반적으로 true 가 편리하다. 한 부모(예: "NavRoot") 아래에 walkable 메시들을 묶어두면
        /// 한 컴포넌트로 전체를 표시할 수 있다.
        /// </summary>
        [Tooltip("자식 오브젝트의 메시도 함께 수집합니다.")]
        public bool includeChildren = true;
    }
}
