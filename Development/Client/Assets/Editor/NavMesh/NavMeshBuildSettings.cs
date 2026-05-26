using UnityEngine;

namespace MMO.Client.Navigation.Editor
{
    /// <summary>
    /// NavMesh 빌드 파라미터와 출력 경로를 가지는 ScriptableObject.
    ///
    /// ScriptableObject 를 쓰는 이유:
    /// - 빌드 파라미터는 게임마다, 스테이지마다 미세 조정이 필요한 값들이다.
    /// - 코드에 하드코딩하면 값을 바꿀 때마다 재컴파일이 필요하고, 에셋으로 관리할 수도 없다.
    /// - ScriptableObject 로 만들면 Project 창에서 .asset 파일로 저장되고,
    ///   인스펙터에서 편집 가능하며, 여러 설정 파일(개발용/배포용)을 만들어둘 수도 있다.
    ///
    /// 생성 방법:
    /// - Project 창 우클릭 -> Create -> MMO -> Navigation -> NavMesh Build Settings
    /// - 적당한 위치(예: Assets/Settings/) 에 저장
    /// - NavMeshBuildMenu.FindSettings() 가 프로젝트에서 첫 번째 발견된 인스턴스를 사용한다.
    ///
    /// [CreateAssetMenu] 어트리뷰트:
    /// - Unity 에디터의 Create 메뉴에 항목을 추가하는 표준 방식.
    /// - menuName 의 "/" 가 서브메뉴를 만든다.
    /// </summary>
    [CreateAssetMenu(fileName = "NavMeshBuildSettings", menuName = "MMO/Navigation/NavMesh Build Settings", order = 0)]
    public class NavMeshBuildSettings : ScriptableObject
    {
        // =====================================================================
        // Agent (캐릭터) 파라미터
        //
        // NavMesh 는 "이 크기의 캐릭터가 다닐 수 있는 영역" 을 계산하므로,
        // 캐릭터의 물리적 크기와 운동 능력을 미리 알려줘야 한다.
        // 게임에서 가장 큰 캐릭터를 기준으로 잡는 것이 안전하다.
        // (작은 캐릭터는 큰 캐릭터의 NavMesh 위에서 다닐 수 있지만, 반대는 막힐 수 있음)
        // =====================================================================

        [Header("Agent")]
        [Tooltip("캐릭터 키 (m). 이 높이가 확보되지 않는 천장 아래는 walkable 에서 제외된다.")]
        public float agentHeight = 2.0f;

        [Tooltip("캐릭터 반지름 (m). 벽에서 이 거리만큼 떨어져야 walkable. 좁은 통로 통과 가능 여부를 결정.")]
        public float agentRadius = 0.6f;

        [Tooltip("오를 수 있는 턱 높이 (m). 이보다 낮은 단차는 자연스럽게 올라간다고 간주.")]
        public float agentMaxClimb = 0.9f;

        [Tooltip("걸을 수 있는 경사 (도). 이보다 가파른 면은 walkable 에서 제외.")]
        public float agentMaxSlope = 45.0f;

        // =====================================================================
        // Voxel (복셀) 파라미터
        //
        // Recast 는 입력 메시를 일정 크기의 3D 격자(복셀)로 래스터화한 뒤 처리한다.
        // - cellSize 가 작을수록 정밀하지만 빌드 시간/메모리가 급격히 증가한다.
        // - 일반적으로 cellSize = agentRadius / 2 ~ agentRadius / 3 정도가 적정.
        // - cellHeight 는 cellSize 의 절반 정도가 표준.
        // =====================================================================

        [Header("Voxel")]
        [Tooltip("복셀 가로 크기 (m). 작을수록 정밀, 빌드 비용 증가.")]
        public float cellSize = 0.3f;

        [Tooltip("복셀 세로 크기 (m). agentMaxClimb 보다 작아야 단차 인식이 정상 동작.")]
        public float cellHeight = 0.2f;

        // =====================================================================
        // Tile (타일) 파라미터
        //
        // 타일은 NavMesh 를 격자 단위로 분할하는 단위.
        // - 큰 맵은 타일 단위로 빌드/스트리밍해야 메모리/시간이 감당 가능.
        // - tileSize 단위는 복셀 수. tileSize=32 면 32*32 복셀 = (32*cellSize) m x (32*cellSize) m.
        // - 너무 작으면 타일 경계가 너무 많아져 비효율, 너무 크면 타일 단위 갱신의 의미가 없어짐.
        // - 32 ~ 64 가 일반적인 권장값.
        // =====================================================================

        [Header("Tile")]
        [Tooltip("타일 한 변의 복셀 수. 예: 32 * cellSize(0.3) = 9.6m 한 변의 타일.")]
        public int tileSize = 32;

        // =====================================================================
        // Region (영역) 파라미터
        //
        // Recast 는 walkable 복셀들을 연결성 기반으로 region 으로 묶고,
        // 각 region 을 폴리곤 영역으로 변환한다.
        // - regionMinSize: 이보다 작은 외딴 region 은 버려진다 (작은 섬 같은 것들 제거).
        // - regionMergeSize: 작은 region 은 인접한 큰 region 과 병합 시도.
        // - 단위는 복셀 수의 한 변 길이. 예: 8 = 8x8 = 64 복셀 미만 region 은 제거.
        // =====================================================================

        [Header("Region")]
        [Tooltip("최소 영역 크기 (복셀). 이보다 작은 외딴 영역은 NavMesh 에서 제외.")]
        public int regionMinSize = 8;

        [Tooltip("영역 병합 임계값 (복셀). 이 크기 이하 영역은 큰 영역과 병합 시도.")]
        public int regionMergeSize = 20;

        // =====================================================================
        // Polygonization (폴리곤화) 파라미터
        //
        // region 들을 실제 다각형 네비메시로 변환하는 단계의 파라미터.
        // - edgeMaxLen: 폴리곤 모서리가 너무 길어지지 않게 분할하는 기준.
        // - edgeMaxError: 윤곽선을 단순화할 때 허용 오차. 클수록 폴리곤 수 감소(성능↑) 정확도↓.
        // - vertsPerPoly: 폴리곤 하나의 최대 정점 수. C++ Recast 가 6 으로 하드코딩되어 있어
        //                 호환성을 위해 반드시 6 으로 유지해야 한다.
        // =====================================================================

        [Header("Polygonization")]
        [Tooltip("폴리곤 최대 모서리 길이 (m). 너무 긴 모서리는 자동 분할.")]
        public float edgeMaxLen = 12.0f;

        [Tooltip("모서리 단순화 오차. 클수록 폴리곤 수 감소, 정확도 감소.")]
        public float edgeMaxError = 1.3f;

        [Tooltip("폴리곤당 정점 수. C++ Recast/Detour 호환을 위해 반드시 6.")]
        public int vertsPerPoly = 6;

        // =====================================================================
        // Detail Mesh (디테일 메시) 파라미터
        //
        // 메인 네비메시는 평면 다각형이지만, 실제 지형은 울퉁불퉁할 수 있다.
        // detail mesh 는 각 폴리곤 내부의 높이 정보를 보존하는 보조 메시.
        // - 작은 값일수록 정밀, 데이터 크기 증가.
        // - 평탄한 지형이면 영향이 거의 없어 기본값 그대로도 충분.
        // =====================================================================

        [Header("Detail Mesh")]
        [Tooltip("디테일 메시 샘플 거리 (cellSize 의 배수).")]
        public float detailSampleDist = 6.0f;

        [Tooltip("디테일 메시 최대 오차 (cellHeight 의 배수).")]
        public float detailSampleMaxError = 1.0f;

        // =====================================================================
        // 출력 경로
        //
        // 빌드된 .bin 파일을 어디에 저장할지.
        // - Application.dataPath 는 Unity 의 "Assets" 폴더 절대 경로.
        // - 상대경로를 기준으로 클라/서버 양쪽에 저장한다.
        // - StreamingAssets 는 빌드 시에도 변환 없이 그대로 포함되는 Unity 표준 폴더.
        //   (즉 런타임에 .bin 파일을 그대로 읽을 수 있다)
        // =====================================================================

        [Header("Output Paths")]
        [Tooltip("클라이언트 출력 경로. Application.dataPath 기준 상대경로. (StreamingAssets 권장)")]
        public string clientOutputDir = "StreamingAssets/NavMesh";

        [Tooltip("서버 출력 경로. Application.dataPath 기준 상대경로. (예: ../../Server/OUTPUT/Map/NavMesh)")]
        public string serverOutputDir = "../../Server/OUTPUT/Map/NavMesh";

        [Tooltip("true 일 때 서버 경로에도 자동으로 .bin 을 저장합니다.")]
        public bool writeToServerOutput = true;

        // =====================================================================
        // 일괄 빌드 설정
        //
        // Tools > NavMesh > Build All Scenes 메뉴가 이 폴더 안의 모든 .unity 파일을
        // 순서대로 열어 NavMesh 를 빌드한다.
        //
        // 권장: 게임플레이 씬과 분리된 "빌드 전용 씬" 폴더를 쓴다.
        //   - 이 씬들은 NavMesh 빌드 목적이므로 Build Settings 에 등록하지 않는다.
        //   - 메시/프랍은 게임플레이 씬과 공유해도 좋고, 따로 두어도 좋다.
        // =====================================================================

        [Header("Batch Build")]
        [Tooltip("일괄 빌드 시 대상 씬이 들어있는 폴더. Assets 기준 상대경로. 이 폴더 안의 모든 .unity 파일이 대상.")]
        public string batchBuildScenesDir = "Scenes/NavMeshBuild";
    }
}
