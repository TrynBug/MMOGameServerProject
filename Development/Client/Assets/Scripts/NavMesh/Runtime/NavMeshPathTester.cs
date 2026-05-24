using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// Play 모드에서 NavMesh 길찾기를 테스트하는 컴포넌트.
    ///
    /// 사용법:
    /// 1. 빈 게임오브젝트를 만들고 이 컴포넌트를 붙인다.
    /// 2. stageName 에 테스트할 스테이지 이름 입력 (예: "NetworkTestScene").
    /// 3. 씬에 Camera 가 활성화되어 있어야 마우스 클릭이 동작한다.
    /// 4. Play 진입 -> 자동으로 NavMesh 로드.
    /// 5. 마우스 좌클릭: 시작점 지정
    /// 6. 마우스 우클릭: 끝점 지정 -> 경로 자동 계산되어 노란 선으로 표시
    ///
    /// 화면에 보이는 것:
    /// - 청록색: NavMesh 폴리곤 가장자리
    /// - 녹색 구체: 시작점
    /// - 빨간 구체: 끝점
    /// - 노란색 선: 계산된 경로
    /// - 흰색 구체: 경로의 각 waypoint
    ///
    /// 주의: Debug.DrawLine 은 Game 뷰의 "Gizmos" 버튼이 ON 이어야 보인다.
    /// </summary>
    public class NavMeshPathTester : MonoBehaviour
    {
        [Header("Stage")]
        [Tooltip("로드할 스테이지 이름. StreamingAssets/NavMesh/<stage>.bin 을 찾는다.")]
        public string stageName = "NetworkTestScene";

        [Header("Visualization")]
        public Color navMeshColor = new Color(0f, 0.8f, 1f, 1f);
        public Color pathColor = Color.yellow;
        [Tooltip("시작점/끝점 마커 반지름")]
        public float markerRadius = 0.3f;

        [Header("Input")]
        [Tooltip("마우스 클릭으로 NavMesh 위 점을 찾을 때 사용하는 ground layer. -1=모든 레이어")]
        public LayerMask groundMask = -1;
        [Tooltip("SamplePosition 의 검색 반경. 클릭 위치가 NavMesh 에서 살짝 벗어나도 보정함.")]
        public float samplePositionRadius = 2.0f;

        // 시작/끝 점이 설정됐는지 여부
        private bool m_hasStart;
        private bool m_hasEnd;
        private Vector3 m_start;
        private Vector3 m_end;

        // 계산된 경로 (waypoint 리스트)
        private readonly List<Vector3> m_path = new List<Vector3>(64);

        // ---------------------------------------------------------------------
        // Unity 이벤트
        // ---------------------------------------------------------------------

        private void Start()
        {
            // 시작 시 자동 로드. 이미 로드되어 있으면 NavMeshService 가 no-op 처리.
            if (!NavMeshService.Load(stageName))
            {
                Debug.LogError($"[NavMeshPathTester] NavMesh 로드 실패: {stageName}\n" +
                               $"경로: {NavMeshService.GetBinPath(stageName)}");
            }
        }

        private void Update()
        {
            HandleMouseInput();
            DrawDebug();
        }

        // ---------------------------------------------------------------------
        // 입력 처리
        // ---------------------------------------------------------------------

        private void HandleMouseInput()
        {
            // 좌클릭 = 시작점, 우클릭 = 끝점
            // 새 Input System 의 Mouse.current 사용.
            // wasPressedThisFrame 은 클릭된 프레임에만 true.
            var mouse = Mouse.current;
            if (mouse == null)
                return;

            if (mouse.leftButton.wasPressedThisFrame)
                TrySetPoint(isStart: true);
            else if (mouse.rightButton.wasPressedThisFrame)
                TrySetPoint(isStart: false);
        }

        /// <summary>
        /// 마우스 위치에서 ray 를 쏘아 ground 와 충돌하는 점을 찾고, 그 점을 NavMesh 위로 보정.
        /// </summary>
        private void TrySetPoint(bool isStart)
        {
            if (Camera.main == null)
            {
                Debug.LogWarning("[NavMeshPathTester] Camera.main 이 없습니다. MainCamera 태그가 붙은 카메라가 필요합니다.");
                return;
            }

            // 새 Input System 의 마우스 위치 읽기.
            // Mouse.current.position.ReadValue() 가 (x, y) 스크린 좌표 반환.
            var mouse = Mouse.current;
            if (mouse == null)
                return;

            Vector2 mousePos = mouse.position.ReadValue();
            Ray ray = Camera.main.ScreenPointToRay(mousePos);
            if (!Physics.Raycast(ray, out RaycastHit hit, 1000f, groundMask))
                return;

            // 레이캐스트 한 지점을 NavMesh 위로 보정
            if (!NavMeshService.SamplePosition(hit.point, samplePositionRadius, out Vector3 onNav))
            {
                Debug.LogWarning($"[NavMeshPathTester] NavMesh 위 점을 찾지 못했습니다: {hit.point}");
                return;
            }

            if (isStart)
            {
                m_start = onNav;
                m_hasStart = true;
            }
            else
            {
                m_end = onNav;
                m_hasEnd = true;
            }

            // 양쪽이 모두 설정되어 있으면 경로 재계산
            if (m_hasStart && m_hasEnd)
            {
                if (NavMeshService.FindPath(m_start, m_end, m_path))
                {
                    Debug.Log($"[NavMeshPathTester] 경로 찾음: waypoint {m_path.Count}개");
                }
                else
                {
                    m_path.Clear();
                    Debug.LogWarning("[NavMeshPathTester] 경로를 찾을 수 없습니다.");
                }
            }
        }

        // ---------------------------------------------------------------------
        // 디버그 그리기
        // ---------------------------------------------------------------------

        private void DrawDebug()
        {
            // NavMesh 가장자리
            NavMeshRuntimeDebug.DrawNavMesh(NavMeshService.Current, navMeshColor);

            // 경로
            NavMeshRuntimeDebug.DrawPath(m_path, pathColor);
        }

        /// <summary>
        /// 시작점/끝점/waypoint 의 작은 마커를 Gizmo 로 그린다.
        /// Debug.DrawLine 으로는 점을 표시하기 어려워서 Gizmos 를 사용.
        /// (Game 뷰에서 "Gizmos" 토글이 ON 이어야 보인다.)
        /// </summary>
        private void OnDrawGizmos()
        {
            if (m_hasStart)
            {
                Gizmos.color = Color.green;
                Gizmos.DrawSphere(m_start, markerRadius);
            }

            if (m_hasEnd)
            {
                Gizmos.color = Color.red;
                Gizmos.DrawSphere(m_end, markerRadius);
            }

            // waypoint 들도 작은 구체로
            if (m_path != null && m_path.Count > 0)
            {
                Gizmos.color = Color.white;
                foreach (var p in m_path)
                    Gizmos.DrawSphere(p, markerRadius * 0.4f);
            }
        }
    }
}
