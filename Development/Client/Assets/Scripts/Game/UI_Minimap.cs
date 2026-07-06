using System.Collections.Generic;
using Client.UI;
using GameData;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 인게임 미니맵 (화면 우상단 고정). 원형 표시 + 플레이어 중심 스크롤 + 줌.
    //
    // UI_Scene 으로서 UIManager.ShowSceneUI<UI_Minimap>() 로 표시된다. (UI_PlayerHud 와 동일 사상)
    // 프리팹의 자식 GameObject 를 enum 이름으로 Bind 해서 참조한다(UI_PlayerHud 방식).
    //
    // 표시 방식(플레이어 중심):
    //   - 내 캐릭터는 항상 원 중앙에 고정되고, 배경 맵 이미지가 스크롤된다.
    //   - 줌(m_zoom)은 "맵 전체가 원에 들어오는 배율(=1)" 기준의 배수. 클수록 확대(좁게 봄).
    //   - 월드 X→오른쪽, 월드 Z→위. 북(+Z)이 항상 위(맵 회전 없음). 내 캐릭터 화살표만 방향 회전.
    //
    // 배경 맵 이미지: Resources/UI/Minimap/{맵이름}_map (예: SyntyForest_map). 없으면 원형 배경만.
    // 원형 마스크/테두리/줌버튼 스프라이트는 Layer Lab GUI Pro-FantasyRPG 에셋 사용(프리팹에 구움).
    //
    // 게임 상태(StageManager/GameData)를 읽으므로 Game 어셈블리에 둔다. (UI_PlayerHud 와 동일)
    // 프리팹 경로: Resources/UI/Scene/UI_Minimap
    //   자식 이름 규약(Bind):  Image: MapImage / GameObject: IconLayer / Button: ZoomInButton, ZoomOutButton
    public class UI_Minimap : UI_Scene
    {
        private enum Images { MapImage }
        private enum Objects { IconLayer }
        private enum Buttons { ZoomInButton, ZoomOutButton }

        // ─── 뷰포트/줌 파라미터 ───────────────────────────────────────
        // 원형 뷰포트 지름(px, 1920x1080 기준). 프리팹의 Viewport 크기와 맞춘다.
        private const float k_viewportSize = 180f;
        private const float k_zoomMin = 1.0f;    // 맵 전체가 원에 들어옴
        private const float k_zoomMax = 5.0f;    // 최대 확대
        private const float k_zoomStep = 1.3f;   // 버튼 1회 클릭 배율
        private const float k_zoomStart = 1.8f;  // 시작 시 약간 확대

        // ─── 아이콘 스타일 ────────────────────────────────────────────
        private const float k_playerIconSize = 16f;
        private const float k_monsterIconSize = 9f;
        private const float k_otherPlayerIconSize = 11f;
        private static readonly Color k_playerColor      = new Color(1f, 1f, 1f, 1f);
        private static readonly Color k_monsterColor     = new Color(0.9f, 0.25f, 0.2f, 1f);
        private static readonly Color k_eliteColor       = new Color(1f, 0.6f, 0.1f, 1f);
        private static readonly Color k_otherPlayerColor = new Color(0.3f, 0.7f, 1f, 1f);

        // ─── 런타임 상태 ──────────────────────────────────────────────
        private Image m_mapImage;
        private RectTransform m_iconLayer;
        private Image m_playerIcon;
        private readonly List<Image> m_monsterIcons = new List<Image>();
        private readonly List<Image> m_otherPlayerIcons = new List<Image>();

        private Sprite m_circleSprite;
        private Sprite m_arrowSprite;
        private string m_loadedMapName;
        private float m_zoom = k_zoomStart;

        public override void Init()
        {
            base.Init();

            Bind<Image>(typeof(Images));
            Bind<GameObject>(typeof(Objects));
            Bind<Button>(typeof(Buttons));

            m_mapImage = Get<Image>((int)Images.MapImage);
            GameObject iconLayerGo = Get<GameObject>((int)Objects.IconLayer);
            if (iconLayerGo != null)
                m_iconLayer = iconLayerGo.GetComponent<RectTransform>();

            // 배경 맵 이미지는 중앙 앵커 고정(Update 에서 크기/위치를 매 프레임 갱신).
            if (m_mapImage != null)
            {
                RectTransform mrt = m_mapImage.rectTransform;
                mrt.anchorMin = mrt.anchorMax = mrt.pivot = new Vector2(0.5f, 0.5f);
                m_mapImage.raycastTarget = false;
            }

            // 줌 버튼 연결(+/-).
            Button zoomIn = Get<Button>((int)Buttons.ZoomInButton);
            if (zoomIn != null) zoomIn.gameObject.BindEvent(_ => onZoom(true));
            Button zoomOut = Get<Button>((int)Buttons.ZoomOutButton);
            if (zoomOut != null) zoomOut.gameObject.BindEvent(_ => onZoom(false));

            m_circleSprite = makeCircleSprite();
            m_arrowSprite = makeArrowSprite();

            // 내 캐릭터 화살표(항상 존재. 다른 아이콘 위에 그려지도록 마지막 생성).
            if (m_iconLayer != null)
                m_playerIcon = makeIcon(m_arrowSprite, k_playerColor, k_playerIconSize);
        }

        private void onZoom(bool zoomIn)
        {
            m_zoom = zoomIn
                ? Mathf.Min(m_zoom * k_zoomStep, k_zoomMax)
                : Mathf.Max(m_zoom / k_zoomStep, k_zoomMin);
        }

        private void Update()
        {
            StageManager sm = StageManager.Instance;
            if (sm == null || sm.LocalPlayer == null || sm.IsStageLoading || m_iconLayer == null)
            {
                hideAll();
                return;
            }

            float worldW = sm.WorldMaxX - sm.WorldMinX;
            float worldH = sm.WorldMaxZ - sm.WorldMinZ;
            if (worldW <= 0f || worldH <= 0f)
            {
                hideAll();
                return;
            }

            ensureMapSprite(sm);

            // 픽셀/월드 비율: 줌=1 이면 맵 전체가 원 지름(뷰포트)에 들어온다.
            float minPpw = k_viewportSize / Mathf.Max(worldW, worldH);
            float ppw = minPpw * m_zoom;

            float wcx = (sm.WorldMinX + sm.WorldMaxX) * 0.5f;   // 맵 중심(월드)
            float wcz = (sm.WorldMinZ + sm.WorldMaxZ) * 0.5f;
            Vector3 pp = sm.LocalPlayer.transform.position;

            // 맵을 플레이어 중심으로 두되, 맵 가장자리에서는 뷰포트 밖으로 빈 공간이 생기지 않도록
            // 스크롤을 clamp 한다(맵 끝이 뷰포트 끝에 맞춰짐). 맵이 뷰포트보다 작으면 lim=0 → 중앙 고정.
            float mapW = worldW * ppw;
            float mapH = worldH * ppw;
            float limX = Mathf.Max(0f, (mapW - k_viewportSize) * 0.5f);
            float limZ = Mathf.Max(0f, (mapH - k_viewportSize) * 0.5f);
            Vector2 mapOffset = new Vector2(
                Mathf.Clamp((wcx - pp.x) * ppw, -limX, limX),
                Mathf.Clamp((wcz - pp.z) * ppw, -limZ, limZ));

            // 월드좌표 → 아이콘 레이어 로컬. 맵과 동일 변환이라 아이콘이 항상 맵에 정렬된다.
            // 가장자리 clamp 때문에 내 캐릭터도 중앙에서 벗어날 수 있다(맵 끝에선 뷰포트 끝 쪽).
            System.Func<Vector3, Vector2> localOf = w =>
                mapOffset + new Vector2((w.x - wcx) * ppw, (w.z - wcz) * ppw);

            // 배경 맵: 전체 맵을 ppw 로 스케일 후 clamp 된 위치로 이동.
            if (m_mapImage != null && m_mapImage.sprite != null)
            {
                m_mapImage.rectTransform.sizeDelta = new Vector2(mapW, mapH);
                m_mapImage.rectTransform.anchoredPosition = mapOffset;
            }

            // 내 캐릭터: 맵상의 실제 위치 + 방향(월드 yaw → UI 회전은 반시계라 -yaw).
            if (m_playerIcon != null)
            {
                m_playerIcon.gameObject.SetActive(true);
                m_playerIcon.rectTransform.anchoredPosition = localOf(pp);
                m_playerIcon.rectTransform.localEulerAngles = new Vector3(0f, 0f, -sm.LocalPlayer.transform.eulerAngles.y);
            }

            // 몬스터(시체 제외). 등급에 따라 색/크기 차등.
            int mi = 0;
            foreach (KeyValuePair<long, MonsterObject> kv in sm.Monsters)
            {
                MonsterObject mo = kv.Value;
                if (mo == null || mo.IsDead)
                    continue;

                bool special = mo.Grade != EMonsterGrade.Normal;
                Image icon = getPooledIcon(m_monsterIcons, mi++, k_monsterIconSize);
                icon.color = special ? k_eliteColor : k_monsterColor;
                icon.rectTransform.sizeDelta = Vector2.one * (special ? k_monsterIconSize * 1.6f : k_monsterIconSize);
                icon.rectTransform.anchoredPosition = localOf(mo.transform.position);
            }
            hideFrom(m_monsterIcons, mi);

            // 타 플레이어(내 캐릭터 제외).
            int pi = 0;
            foreach (KeyValuePair<long, PlayerCharacter> kv in sm.Characters)
            {
                PlayerCharacter pc = kv.Value;
                if (pc == null || pc.IsLocalPlayer)
                    continue;

                Image icon = getPooledIcon(m_otherPlayerIcons, pi++, k_otherPlayerIconSize);
                icon.color = k_otherPlayerColor;
                icon.rectTransform.anchoredPosition = localOf(pc.transform.position);
            }
            hideFrom(m_otherPlayerIcons, pi);
        }

        // 맵이 바뀌면 배경 스프라이트 지연 로드. 없으면 투명(원형 배경만).
        private void ensureMapSprite(StageManager sm)
        {
            if (m_mapImage == null)
                return;
            string name = sm.CurrentMapName;
            if (string.IsNullOrEmpty(name) || name == m_loadedMapName)
                return;
            m_loadedMapName = name;

            Sprite sp = Managers.Managers.Resource.Load<Sprite>($"UI/Minimap/{name}_map");
            if (sp != null)
            {
                m_mapImage.sprite = sp;
                m_mapImage.color = Color.white;
                m_mapImage.enabled = true;
            }
            else
            {
                m_mapImage.sprite = null;
                m_mapImage.enabled = false;
                Debug.LogWarning($"[UI_Minimap] 맵 텍스처 없음: Resources/UI/Minimap/{name}_map (원형 배경만 표시)");
            }
        }

        // ─── 아이콘 풀 ────────────────────────────────────────────────
        private Image getPooledIcon(List<Image> pool, int idx, float size)
        {
            while (pool.Count <= idx)
                pool.Add(makeIcon(m_circleSprite, Color.white, size));
            Image icon = pool[idx];
            icon.gameObject.SetActive(true);
            return icon;
        }

        private static void hideFrom(List<Image> pool, int from)
        {
            for (int i = from; i < pool.Count; i++)
                if (pool[i] != null)
                    pool[i].gameObject.SetActive(false);
        }

        private void hideAll()
        {
            if (m_playerIcon != null) m_playerIcon.gameObject.SetActive(false);
            hideFrom(m_monsterIcons, 0);
            hideFrom(m_otherPlayerIcons, 0);
        }

        private Image makeIcon(Sprite sprite, Color color, float size)
        {
            GameObject go = new GameObject("Icon", typeof(RectTransform));
            RectTransform rt = go.GetComponent<RectTransform>();
            rt.SetParent(m_iconLayer, worldPositionStays: false);
            rt.anchorMin = rt.anchorMax = rt.pivot = new Vector2(0.5f, 0.5f);
            rt.sizeDelta = new Vector2(size, size);

            Image img = go.AddComponent<Image>();
            img.sprite = sprite;
            img.color = color;
            img.raycastTarget = false;
            return img;
        }

        // ─── 절차 생성 스프라이트(아이콘) ──────────────────────────────
        private static Sprite makeCircleSprite()
        {
            const int d = 32;
            Texture2D t = new Texture2D(d, d, TextureFormat.RGBA32, false);
            Vector2 c = new Vector2(d / 2f, d / 2f);
            float r = d / 2f - 1f;
            for (int y = 0; y < d; y++)
                for (int x = 0; x < d; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x + 0.5f, y + 0.5f), c);
                    float a = dist <= r ? 1f : Mathf.Clamp01(r + 1f - dist);
                    t.SetPixel(x, y, new Color(1f, 1f, 1f, a));
                }
            t.wrapMode = TextureWrapMode.Clamp;
            t.filterMode = FilterMode.Bilinear;
            t.Apply();
            return Sprite.Create(t, new Rect(0, 0, d, d), new Vector2(0.5f, 0.5f));
        }

        private static Sprite makeArrowSprite()
        {
            const int d = 48;
            Texture2D t = new Texture2D(d, d, TextureFormat.RGBA32, false);
            for (int y = 0; y < d; y++)
            {
                float ty = y / (d - 1f);                       // 0(아래)~1(위)
                float halfW = (1f - ty) * (d / 2f - 1f);       // 위로 갈수록 좁아짐 → 위를 가리킴
                for (int x = 0; x < d; x++)
                {
                    float dx = Mathf.Abs((x + 0.5f) - d / 2f);
                    float a = dx <= halfW ? 1f : Mathf.Clamp01(halfW + 1f - dx);
                    t.SetPixel(x, y, new Color(1f, 1f, 1f, a));
                }
            }
            t.wrapMode = TextureWrapMode.Clamp;
            t.filterMode = FilterMode.Bilinear;
            t.Apply();
            return Sprite.Create(t, new Rect(0, 0, d, d), new Vector2(0.5f, 0.5f));
        }
    }
}
