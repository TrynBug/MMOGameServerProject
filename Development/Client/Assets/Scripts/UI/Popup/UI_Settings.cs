using System;
using Client.Managers;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.UI
{
    // 환경설정 팝업. Esc 메뉴(UI_GameMenu)에서 "환경설정" 버튼으로 연다.
    //
    // 책임:
    //   - 해상도/창모드/프레임제한/VSync/마스터볼륨 컨트롤을 현재 값으로 채우고,
    //     변경 시 SettingsManager / SoundManager 에 즉시 반영(적용+저장은 매니저가 담당).
    //   - 닫기 버튼 클릭을 OnClose 이벤트로 발행 (실제 팝업 닫기는 GameScene 이 처리).
    //
    // 책임이 아닌 것:
    //   - 실제 화면/사운드 적용 (SettingsManager / SoundManager 가 함)
    //   - 팝업 스택 관리 (UIManager 가 함)
    //
    // UI_GameMenu 는 순수 이벤트 발행기지만, 이 팝업은 설정 매니저(전역 앤 설정)를
    // 직접 읽고 쓰는 자기완결 화면이다(게임상태=LocalPlayer 를 읽는 게 아니라 무해).
    public class UI_Settings : UI_Popup
    {
        private enum Dropdowns { ResolutionDropdown, WindowModeDropdown, FrameRateDropdown }
        private enum Sliders   { VolumeSlider }
        private enum Toggles   { VSyncToggle }
        private enum Buttons   { CloseButton }
        private enum Texts     { VolumeValueText }

        public event Action OnClose;   // 닫기 버튼 클릭

        public override void Init()
        {
            base.Init();

            Bind<TMP_Dropdown>(typeof(Dropdowns));
            Bind<Slider>(typeof(Sliders));
            Bind<Toggle>(typeof(Toggles));
            Bind<Button>(typeof(Buttons));
            Bind<TextMeshProUGUI>(typeof(Texts));

            initResolution();
            initWindowMode();
            initFrameRate();
            initVSync();
            initVolume();

            GetButton((int)Buttons.CloseButton).gameObject.BindEvent(_ => OnClose?.Invoke());
        }

        // ─── 해상도 ────────────────────────────────────────────────────
        private void initResolution()
        {
            TMP_Dropdown dd = Get<TMP_Dropdown>((int)Dropdowns.ResolutionDropdown);
            if (dd == null) return;

            dd.ClearOptions();
            dd.AddOptions(Managers.Managers.Settings.ResolutionLabels());
            dd.SetValueWithoutNotify(Managers.Managers.Settings.CurrentResolutionIndex());
            dd.onValueChanged.AddListener(i => Managers.Managers.Settings.SetResolutionByIndex(i));
        }

        // ─── 창 모드 ───────────────────────────────────────────────────
        private void initWindowMode()
        {
            TMP_Dropdown dd = Get<TMP_Dropdown>((int)Dropdowns.WindowModeDropdown);
            if (dd == null) return;

            dd.ClearOptions();
            dd.AddOptions(Managers.Managers.Settings.WindowModeLabels());
            dd.SetValueWithoutNotify(Managers.Managers.Settings.CurrentWindowModeIndex());
            dd.onValueChanged.AddListener(i => Managers.Managers.Settings.SetWindowModeByIndex(i));
        }

        // ─── 프레임 제한 ───────────────────────────────────────────────
        private void initFrameRate()
        {
            TMP_Dropdown dd = Get<TMP_Dropdown>((int)Dropdowns.FrameRateDropdown);
            if (dd == null) return;

            dd.ClearOptions();
            dd.AddOptions(Managers.Managers.Settings.FrameRateLabels());
            dd.SetValueWithoutNotify(Managers.Managers.Settings.CurrentFrameRateIndex());
            dd.onValueChanged.AddListener(i => Managers.Managers.Settings.SetFrameRateByIndex(i));

            // VSync 가 켜져 있으면 프레임 제한은 무시되므로 비활성화한다.
            dd.interactable = !Managers.Managers.Settings.VSync;
        }

        // ─── 수직 동기화 ───────────────────────────────────────────────
        private void initVSync()
        {
            Toggle t = Get<Toggle>((int)Toggles.VSyncToggle);
            if (t == null) return;

            t.SetIsOnWithoutNotify(Managers.Managers.Settings.VSync);
            t.onValueChanged.AddListener(on =>
            {
                Managers.Managers.Settings.SetVSync(on);
                // VSync ↔ 프레임제한 상호배타: 켜지면 프레임 드롭다운을 잠근다.
                TMP_Dropdown fr = Get<TMP_Dropdown>((int)Dropdowns.FrameRateDropdown);
                if (fr != null) fr.interactable = !on;
            });
        }

        // ─── 마스터 볼륨 ───────────────────────────────────────────────
        private void initVolume()
        {
            Slider s = Get<Slider>((int)Sliders.VolumeSlider);
            if (s == null) return;

            s.minValue = 0f;
            s.maxValue = 1f;
            s.SetValueWithoutNotify(Managers.Managers.Sound.MasterVolume);
            updateVolumeText(Managers.Managers.Sound.MasterVolume);

            s.onValueChanged.AddListener(v =>
            {
                Managers.Managers.Sound.SetMasterVolume(v);
                updateVolumeText(v);
            });
        }

        private void updateVolumeText(float v01)
        {
            TextMeshProUGUI txt = GetText((int)Texts.VolumeValueText);
            if (txt != null) txt.text = $"{Mathf.RoundToInt(v01 * 100f)}%";
        }
    }
}
