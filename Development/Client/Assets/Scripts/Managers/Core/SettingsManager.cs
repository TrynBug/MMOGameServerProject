using System.Collections.Generic;
using UnityEngine;

namespace Client.Managers
{
    // 화면/렌더 옵션(해상도, 창 모드, 프레임 제한, 수직 동기화)을 관리.
    //
    // 책임:
    //   - 네 옵션 값 보관 + 실제 Unity API 에 적용 (Screen / Application / QualitySettings)
    //   - PlayerPrefs 로 저장/로드 (다음 실행 때 복원)
    //   - UI 가 쓰기 편한 옵션 목록(라벨) + 현재 선택 인덱스 제공
    //
    // 책임이 아닌 것:
    //   - 볼륨 (SoundManager 담당)
    //   - UI 표시 (UI_Settings 담당)
    //
    // 사용법:
    //   Managers.Settings.SetResolutionByIndex(i);
    //   Managers.Settings.SetVSync(true);
    public class SettingsManager
    {
        // ─── 옵션 정의 ─────────────────────────────────────────────────

        public readonly struct ResolutionOption
        {
            public readonly int Width;
            public readonly int Height;
            public readonly string Label;
            public ResolutionOption(int w, int h, string label) { Width = w; Height = h; Label = label; }
        }

        // 선택 가능한 해상도 목록. (16:9 기준)
        public static readonly ResolutionOption[] Resolutions =
        {
            new ResolutionOption(1280, 720,  "HD (1280 x 720)"),
            new ResolutionOption(1920, 1080, "FHD (1920 x 1080)"),
            new ResolutionOption(2560, 1440, "QHD (2560 x 1440)"),
            new ResolutionOption(3840, 2160, "UHD 4K (3840 x 2160)"),
        };

        public readonly struct WindowModeOption
        {
            public readonly FullScreenMode Mode;
            public readonly string Label;
            public WindowModeOption(FullScreenMode mode, string label) { Mode = mode; Label = label; }
        }

        // 창 모드 목록. FullScreenWindow = 테두리 없는 전체화면 창.
        public static readonly WindowModeOption[] WindowModes =
        {
            new WindowModeOption(FullScreenMode.ExclusiveFullScreen, "전체화면"),
            new WindowModeOption(FullScreenMode.FullScreenWindow,    "테두리 없는 창"),
            new WindowModeOption(FullScreenMode.Windowed,            "창 모드"),
        };

        // 프레임 제한 목록. -1 = 무제한.
        public static readonly int[] FrameRates = { 30, 60, 120, 144, 240, -1 };

        // ─── PlayerPrefs 키 ────────────────────────────────────────────
        private const string k_keyWidth      = "gfx.width";
        private const string k_keyHeight     = "gfx.height";
        private const string k_keyWindowMode = "gfx.windowMode";
        private const string k_keyFrameRate  = "gfx.frameRate";
        private const string k_keyVSync      = "gfx.vsync";

        // ─── 현재 값 ───────────────────────────────────────────────────
        private int m_width;
        private int m_height;
        private FullScreenMode m_windowMode;
        private int m_frameRate;
        private bool m_vSync;

        public bool VSync => m_vSync;

        // 저장된 설정을 로드해 즉시 적용. Managers 생성 시 1회 호출.
        public void Init()
        {
            // 최초 실행(저장값 없음) 기본값: 현재 모니터 해상도 / 테두리 없는 창 / 60fps / VSync off.
            Resolution cur = Screen.currentResolution;
            m_width      = PlayerPrefs.GetInt(k_keyWidth,  cur.width);
            m_height     = PlayerPrefs.GetInt(k_keyHeight, cur.height);
            m_windowMode = (FullScreenMode)PlayerPrefs.GetInt(k_keyWindowMode, (int)FullScreenMode.FullScreenWindow);
            m_frameRate  = PlayerPrefs.GetInt(k_keyFrameRate, 60);
            m_vSync      = PlayerPrefs.GetInt(k_keyVSync, 0) == 1;

            applyDisplay();
            applyFrameRate();
        }

        // ─── 해상도 ────────────────────────────────────────────────────

        public List<string> ResolutionLabels()
        {
            var list = new List<string>(Resolutions.Length);
            foreach (ResolutionOption r in Resolutions) list.Add(r.Label);
            return list;
        }

        // 현재 해상도가 목록의 몇 번째인지. 목록에 없으면 0.
        public int CurrentResolutionIndex()
        {
            for (int i = 0; i < Resolutions.Length; i++)
                if (Resolutions[i].Width == m_width && Resolutions[i].Height == m_height)
                    return i;
            return 0;
        }

        public void SetResolutionByIndex(int index)
        {
            if (index < 0 || index >= Resolutions.Length) return;
            m_width = Resolutions[index].Width;
            m_height = Resolutions[index].Height;
            applyDisplay();
            PlayerPrefs.SetInt(k_keyWidth, m_width);
            PlayerPrefs.SetInt(k_keyHeight, m_height);
            PlayerPrefs.Save();
        }

        // ─── 창 모드 ───────────────────────────────────────────────────

        public List<string> WindowModeLabels()
        {
            var list = new List<string>(WindowModes.Length);
            foreach (WindowModeOption w in WindowModes) list.Add(w.Label);
            return list;
        }

        public int CurrentWindowModeIndex()
        {
            for (int i = 0; i < WindowModes.Length; i++)
                if (WindowModes[i].Mode == m_windowMode)
                    return i;
            return 0;
        }

        public void SetWindowModeByIndex(int index)
        {
            if (index < 0 || index >= WindowModes.Length) return;
            m_windowMode = WindowModes[index].Mode;
            applyDisplay();
            PlayerPrefs.SetInt(k_keyWindowMode, (int)m_windowMode);
            PlayerPrefs.Save();
        }

        // ─── 프레임 제한 ───────────────────────────────────────────────

        public List<string> FrameRateLabels()
        {
            var list = new List<string>(FrameRates.Length);
            foreach (int f in FrameRates) list.Add(f < 0 ? "무제한" : $"{f} FPS");
            return list;
        }

        public int CurrentFrameRateIndex()
        {
            for (int i = 0; i < FrameRates.Length; i++)
                if (FrameRates[i] == m_frameRate)
                    return i;
            return 0;
        }

        public void SetFrameRateByIndex(int index)
        {
            if (index < 0 || index >= FrameRates.Length) return;
            m_frameRate = FrameRates[index];
            applyFrameRate();
            PlayerPrefs.SetInt(k_keyFrameRate, m_frameRate);
            PlayerPrefs.Save();
        }

        // ─── 수직 동기화 ───────────────────────────────────────────────

        public void SetVSync(bool on)
        {
            m_vSync = on;
            applyFrameRate();   // vsync 는 프레임 제한 의미에 영향을 주므로 함께 재적용
            PlayerPrefs.SetInt(k_keyVSync, m_vSync ? 1 : 0);
            PlayerPrefs.Save();
        }

        // ─── 적용 ──────────────────────────────────────────────────────

        private void applyDisplay()
        {
            Screen.SetResolution(m_width, m_height, m_windowMode);
        }

        private void applyFrameRate()
        {
            // VSync 를 켜면(vSyncCount>=1) targetFrameRate 는 무시되고 모니터 주사율에 동기화된다.
            // VSync 를 끄면 targetFrameRate 로 상한을 건다(-1 = 무제한).
            QualitySettings.vSyncCount = m_vSync ? 1 : 0;
            Application.targetFrameRate = m_frameRate;
        }
    }
}
