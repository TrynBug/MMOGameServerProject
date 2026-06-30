using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.UI
{
    // 캐릭터 선택/생성 화면 UI.
    //
    // 책임:
    //   - 캐릭터 슬롯 목록 표시 (씬이 AddSlot 으로 채움)
    //   - "새 캐릭터" 생성 패널 (이름 입력 + 직업 선택 + 3D 프리뷰)
    //   - 사용자 입력을 원시 타입 이벤트로 외부(씬)에 발행
    //
    // 책임이 아닌 것:
    //   - 네트워크 / 패킷 (CharacterSelector)
    //   - 직업→prefab, 3D 프리뷰 렌더 (Game 어셈블리: CharacterPreviewRig)
    //
    // 어셈블리 경계: UI 는 Game/Generated 를 모른다. 그래서 Character 타입 대신
    // (label, characterId) 원시값으로만 주고받는다.
    public class UI_CharacterSelection : UI_Scene
    {
        private enum Texts { TitleText, StatusText }
        private enum Buttons { CreateButton, ConfirmButton, CancelButton, JobMageButton, JobWarriorButton, Preset0Button, Preset1Button, PlayButton }
        private enum Objects { SlotContainer, SlotTemplate, CreatePanel }

        // ─── 외부 이벤트 (씬이 구독) ──────────────────────────────────────
        public event Action<long> OnSlotClicked;        // 슬롯 클릭 → 해당 캐릭터 프리뷰
        public event Action OnPlayClicked;                // Play 클릭 → 선택 캐릭터로 게임 입장
        public event Action OnCreateOpenClicked;          // "새 캐릭터" 클릭 → 생성 패널 열기
        public event Action OnCreateCancelClicked;        // 생성 취소
        public event Action<int, int> OnAppearanceChanged;     // 외형 변경 (jobId, presetId) → 프리뷰 갱신
        public event Action<string, int, int> OnCreateConfirmed; // 생성 확정 (name, jobId, presetId)

        // 현재 생성 패널에서 선택된 직업(EJob: Mage=1, Warrior=2)과 외형 프리셋(0-base). 기본 Mage/0.
        private int m_selectedJob = 1;
        private int m_selectedPreset = 0;
        private readonly List<GameObject> m_slots = new List<GameObject>();
        private readonly Dictionary<long, GameObject> m_slotById = new Dictionary<long, GameObject>();
        private static readonly Color k_slotNormal = Color.white;
        private static readonly Color k_slotSelected = new Color(1f, 0.86f, 0.5f, 1f);

        public override void Init()
        {
            base.Init();

            Bind<TextMeshProUGUI>(typeof(Texts));
            Bind<Button>(typeof(Buttons));
            Bind<GameObject>(typeof(Objects));
            Bind<RawImage>(typeof(RawImagesEnum));
            Bind<TMP_InputField>(typeof(InputsEnum));

            GetButton((int)Buttons.CreateButton).gameObject.BindEvent(_ => OnCreateOpenClicked?.Invoke());
            GetButton((int)Buttons.CancelButton).gameObject.BindEvent(_ => OnCreateCancelClicked?.Invoke());
            GetButton((int)Buttons.ConfirmButton).gameObject.BindEvent(_ => confirmCreate());
            GetButton((int)Buttons.PlayButton).gameObject.BindEvent(_ => OnPlayClicked?.Invoke());
            GetButton((int)Buttons.JobMageButton).gameObject.BindEvent(_ => selectJob(1));
            GetButton((int)Buttons.JobWarriorButton).gameObject.BindEvent(_ => selectJob(2));
            GetButton((int)Buttons.Preset0Button).gameObject.BindEvent(_ => selectPreset(0));
            GetButton((int)Buttons.Preset1Button).gameObject.BindEvent(_ => selectPreset(1));

            // 슬롯 템플릿은 숨겨둔다 (복제용).
            GameObject tmpl = GetObject((int)Objects.SlotTemplate);
            if (tmpl != null) tmpl.SetActive(false);

            HideCreatePanel();
        }

        private enum RawImagesEnum { PreviewImage, PreviewMain }
        private enum InputsEnum { NameInput }

        // ─── 슬롯 ─────────────────────────────────────────────────────────

        public void ClearSlots()
        {
            foreach (GameObject go in m_slots)
                if (go != null) UnityEngine.Object.Destroy(go);
            m_slots.Clear();
            m_slotById.Clear();
        }

        // 캐릭터 슬롯 하나 추가. label 예) "용사  Lv.3  (전사)".
        public void AddSlot(string label, long characterId)
        {
            GameObject tmpl = GetObject((int)Objects.SlotTemplate);
            GameObject container = GetObject((int)Objects.SlotContainer);
            if (tmpl == null || container == null) return;

            GameObject slot = UnityEngine.Object.Instantiate(tmpl, container.transform);
            slot.name = $"Slot_{characterId}";
            slot.SetActive(true);

            TextMeshProUGUI text = slot.GetComponentInChildren<TextMeshProUGUI>(true);
            if (text != null) text.text = label;

            long id = characterId;
            slot.BindEvent(_ => OnSlotClicked?.Invoke(id));

            m_slots.Add(slot);
            m_slotById[id] = slot;
        }

        // 선택된 슬롯을 강조 표시 (나머지는 기본색으로 되돌림).
        public void HighlightSlot(long characterId)
        {
            foreach (KeyValuePair<long, GameObject> kv in m_slotById)
            {
                Image img = kv.Value != null ? kv.Value.GetComponent<Image>() : null;
                if (img != null) img.color = (kv.Key == characterId) ? k_slotSelected : k_slotNormal;
            }
        }

        // Play 버튼 활성/비활성 (선택된 캐릭터가 있을 때만 활성).
        public void SetPlayInteractable(bool interactable)
        {
            Button play = GetButton((int)Buttons.PlayButton);
            if (play != null) play.interactable = interactable;
        }

        // ─── 생성 패널 ────────────────────────────────────────────────────

        public void ShowCreatePanel()
        {
            GameObject panel = GetObject((int)Objects.CreatePanel);
            if (panel != null) panel.SetActive(true);

            TMP_InputField input = Get<TMP_InputField>((int)InputsEnum.NameInput);
            if (input != null) input.text = "";

            selectJob(1);    // 기본 마법사
            selectPreset(0); // 기본 프리셋
        }

        public void HideCreatePanel()
        {
            GameObject panel = GetObject((int)Objects.CreatePanel);
            if (panel != null) panel.SetActive(false);
        }

        // 3D 프리뷰 RenderTexture 를 RawImage 에 연결 (씬의 CharacterPreviewRig 가 호출).
        // 3D 프리뷰 RenderTexture 를 양쪽 RawImage(생성 패널 + 메인)에 연결.
        public void SetPreviewTexture(Texture tex)
        {
            RawImage create = Get<RawImage>((int)RawImagesEnum.PreviewImage);
            if (create != null) create.texture = tex;
            RawImage main = Get<RawImage>((int)RawImagesEnum.PreviewMain);
            if (main != null) main.texture = tex;
        }

        public void SetStatus(string message)
        {
            TextMeshProUGUI text = GetText((int)Texts.StatusText);
            if (text != null) text.text = message;
        }

        // ─── 내부 ─────────────────────────────────────────────────────────

        private void selectJob(int jobId)
        {
            m_selectedJob = jobId;
            // 토글 시각효과: 선택된 버튼을 비활성처럼 보이게(눌린 표현).
            Button mage = GetButton((int)Buttons.JobMageButton);
            Button warrior = GetButton((int)Buttons.JobWarriorButton);
            if (mage != null) mage.interactable = (jobId != 1);
            if (warrior != null) warrior.interactable = (jobId != 2);

            raiseAppearance();
        }

        private void selectPreset(int presetId)
        {
            m_selectedPreset = presetId;
            Button p0 = GetButton((int)Buttons.Preset0Button);
            Button p1 = GetButton((int)Buttons.Preset1Button);
            if (p0 != null) p0.interactable = (presetId != 0);
            if (p1 != null) p1.interactable = (presetId != 1);

            raiseAppearance();
        }

        private void raiseAppearance()
        {
            OnAppearanceChanged?.Invoke(m_selectedJob, m_selectedPreset);
        }

        private void confirmCreate()
        {
            TMP_InputField input = Get<TMP_InputField>((int)InputsEnum.NameInput);
            string name = input != null ? input.text.Trim() : "";
            if (string.IsNullOrEmpty(name))
            {
                SetStatus("이름을 입력하세요.");
                return;
            }
            OnCreateConfirmed?.Invoke(name, m_selectedJob, m_selectedPreset);
        }
    }
}
