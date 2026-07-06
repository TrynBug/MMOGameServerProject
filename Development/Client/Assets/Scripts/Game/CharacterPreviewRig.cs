using UnityEngine;

namespace Client.Game
{
    // 캐릭터 생성 UI 의 3D 라이브 프리뷰용 오프스크린 렌더 리그.
    //
    // 책임:
    //   - 전용 카메라 + 라이트로 (job, preset) prefab 을 RenderTexture 에 렌더
    //   - 그 RenderTexture 를 UI 의 RawImage 가 표시 (UI_CharacterSelection.SetPreviewTexture)
    //   - 천천히 회전 + Animator 기본 상태(idle) 재생으로 살아있는 프리뷰
    //
    // 실제 게임 월드와 섞이지 않도록 아주 먼 좌표(OFF)에 배치한다.
    // 직업→prefab 매핑은 CharacterFactory.ResolvePrefabPath 를 재사용한다(단일 출처).
    public class CharacterPreviewRig : MonoBehaviour
    {
        private static readonly Vector3 OFF = new Vector3(3000f, 3000f, 3000f);

        private Camera m_cam;
        private RenderTexture m_rt;
        private Transform m_pivot;
        private GameObject m_char;

        public float rotateSpeed = 35f;
        public RenderTexture Texture => m_rt;

        public void Initialize(int width = 440, int height = 640)
        {
            m_rt = new RenderTexture(width, height, 24) { antiAliasing = 2 };
            m_rt.Create();

            var pivotGo = new GameObject("PreviewPivot");
            pivotGo.transform.SetParent(transform, false);
            pivotGo.transform.position = OFF;
            m_pivot = pivotGo.transform;

            var camGo = new GameObject("PreviewCam");
            camGo.transform.SetParent(transform, false);
            m_cam = camGo.AddComponent<Camera>();
            m_cam.clearFlags = CameraClearFlags.SolidColor;
            m_cam.backgroundColor = new Color(0.15f, 0.16f, 0.21f, 1f);
            m_cam.fieldOfView = 30f;
            m_cam.nearClipPlane = 0.05f;
            m_cam.farClipPlane = 50f;
            m_cam.targetTexture = m_rt;
            // 고정 카메라. 캐릭터가 잘리지 않도록 거리를 넉넉히(3.0→4.5) 두고,
            // 시선 높이는 캐릭터 중심(≈0.95)으로 맞춘다(낮으면 키 큰 캐릭터 머리가 잘림).
            camGo.transform.position = OFF + new Vector3(0f, 1.0f, 4.5f);
            camGo.transform.LookAt(OFF + new Vector3(0f, 0.95f, 0f));

            var lightGo = new GameObject("PreviewLight");
            lightGo.transform.SetParent(transform, false);
            var light = lightGo.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.2f;
            lightGo.transform.rotation = Quaternion.Euler(45f, -25f, 0f);
        }

        // (jobId, presetId) prefab 으로 프리뷰 캐릭터 교체.
        public void SetCharacter(int jobId, int presetId)
        {
            if (m_char != null) Destroy(m_char);

            string path = CharacterFactory.ResolvePrefabPath(jobId, presetId);
            GameObject prefab = Resources.Load<GameObject>(path);
            if (prefab == null)
            {
                Debug.LogWarning($"[CharacterPreviewRig] prefab 로드 실패: {path}");
                return;
            }

            // 캐릭터 교체 시 쇼케이스 회전을 정면(카메라를 바라보는 기본 자세)으로 재설정.
            m_pivot.localRotation = Quaternion.identity;

            m_char = Instantiate(prefab, m_pivot);
            m_char.transform.localPosition = Vector3.zero;
            m_char.transform.localRotation = Quaternion.identity;

            // 프리뷰에서는 게임 로직(PlayerCharacter) 비활성 — 표시/애니메이션만 필요.
            // Start 가 돌기 전에 꺼서 네트워크/이동 관련 로직이 실행되지 않도록 한다.
            if (m_char.GetComponent("PlayerCharacter") is MonoBehaviour pc) pc.enabled = false;
        }

        private void Update()
        {
            if (m_pivot != null) m_pivot.Rotate(0f, rotateSpeed * Time.deltaTime, 0f);
        }

        private void OnDestroy()
        {
            if (m_char != null) Destroy(m_char);
            if (m_rt != null) { m_rt.Release(); Destroy(m_rt); }
        }
    }
}
