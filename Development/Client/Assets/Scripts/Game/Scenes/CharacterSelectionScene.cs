using System.Collections.Generic;
using Client.Managers;
using Client.UI;
using DataStructures;
using UnityEngine;

namespace Client.Game
{
    // CharacterSelection 씬의 BaseScene 구현.
    //
    // 책임 (오케스트레이션):
    //   - UI_CharacterSelection 표시 + 3D 프리뷰 리그(CharacterPreviewRig) 생성/연결
    //   - 캐시의 캐릭터 목록으로 슬롯 구성
    //   - UI 이벤트 ↔ CharacterSelector(네트워크) 연결
    //
    // 책임이 아닌 것:
    //   - 패킷 송수신 (CharacterSelector)
    //   - 직업→prefab / 렌더 (CharacterFactory / CharacterPreviewRig)
    public class CharacterSelectionScene : BaseScene
    {
        private UI_CharacterSelection m_ui;
        private CharacterSelector m_selector;
        private CharacterPreviewRig m_preview;
        private long m_selectedCharacterId = 0;   // 현재 프리뷰/선택 중인 캐릭터 (0 = 없음)

        protected override void Init()
        {
            Debug.Log("[CharacterSelectionScene] Init");

            m_ui = Managers.Managers.UI.ShowSceneUI<UI_CharacterSelection>();
            if (m_ui == null)
            {
                Debug.LogError("[CharacterSelectionScene] UI_CharacterSelection 프리팹 로드 실패.");
                return;
            }

            // 3D 프리뷰 리그 생성 후 RenderTexture 를 UI 에 연결.
            var rigGo = new GameObject("@CharacterPreviewRig");
            m_preview = rigGo.AddComponent<CharacterPreviewRig>();
            m_preview.Initialize();
            m_ui.SetPreviewTexture(m_preview.Texture);

            // UI 이벤트 구독.
            m_ui.OnSlotClicked += onSlotClicked;
            m_ui.OnPlayClicked += onPlayClicked;
            m_ui.OnCreateOpenClicked += onCreateOpen;
            m_ui.OnCreateCancelClicked += onCreateCancel;
            m_ui.OnAppearanceChanged += onAppearanceChanged;
            m_ui.OnCreateConfirmed += onCreateConfirmed;

            // CharacterSelector(같은 씬) 연결.
            m_selector = Object.FindAnyObjectByType<CharacterSelector>();
            if (m_selector != null)
            {
                m_selector.OnStatusChanged += onStatusChanged;
                m_selector.OnCharacterListUpdated += onCharacterListUpdated;
            }
            else
            {
                Debug.LogWarning("[CharacterSelectionScene] CharacterSelector 를 찾지 못했습니다.");
            }

            // 캐시 목록으로 슬롯 구성.
            IReadOnlyList<Character> list = CharacterDataCache.Instance.Characters;
            rebuildSlots(list);
            if (list.Count > 0)
            {
                previewCharacter(list[0]);   // 첫 캐릭터를 선택 상태로 (3D 프리뷰 포함)
                m_ui.SetStatus("캐릭터를 선택하고 Play 를 누르세요.");
            }
            else
            {
                m_selectedCharacterId = 0;
                m_ui.SetPlayInteractable(false);
                m_ui.SetStatus("캐릭터가 없습니다. 새로 만드세요.");
            }
        }

        public override void Clear()
        {
            Debug.Log("[CharacterSelectionScene] Clear");

            if (m_ui != null)
            {
                m_ui.OnSlotClicked -= onSlotClicked;
                m_ui.OnPlayClicked -= onPlayClicked;
                m_ui.OnCreateOpenClicked -= onCreateOpen;
                m_ui.OnCreateCancelClicked -= onCreateCancel;
                m_ui.OnAppearanceChanged -= onAppearanceChanged;
                m_ui.OnCreateConfirmed -= onCreateConfirmed;
            }
            if (m_selector != null)
            {
                m_selector.OnStatusChanged -= onStatusChanged;
                m_selector.OnCharacterListUpdated -= onCharacterListUpdated;
            }
            if (m_preview != null) Object.Destroy(m_preview.gameObject);
        }

        // ─── 슬롯 ─────────────────────────────────────────────────────────

        private void rebuildSlots(IReadOnlyList<Character> list)
        {
            m_ui.ClearSlots();
            foreach (Character c in list)
            {
                if (c == null) continue;
                m_ui.AddSlot($"{c.Name}   Lv.{c.Level}   ({jobName(c.JobId)})", c.CharacterId);
            }
        }

        private static string jobName(int jobId)
        {
            if (jobId == (int)GameData.EJob.Mage) return "마법사";
            if (jobId == (int)GameData.EJob.Warrior) return "전사";
            return "?";
        }

        // ─── 이벤트 핸들러 ────────────────────────────────────────────────

        // 슬롯 클릭 = 게임 입장이 아니라 해당 캐릭터를 프리뷰. 입장은 Play 버튼으로.
        private void onSlotClicked(long characterId)
        {
            Character c = findCharacter(characterId);
            if (c != null) previewCharacter(c);
        }

        // Play = 선택된 캐릭터로 게임 입장 (CharacterSelectReq 송신 → Game 씬).
        private void onPlayClicked()
        {
            if (m_selectedCharacterId == 0)
            {
                m_ui.SetStatus("캐릭터를 선택하세요.");
                return;
            }
            m_selector?.RequestSelect(m_selectedCharacterId);
        }

        // 캐릭터를 선택 상태로 만들고 우측 3D 프리뷰 + 슬롯 강조를 갱신한다.
        private void previewCharacter(Character c)
        {
            m_selectedCharacterId = c.CharacterId;
            m_preview.SetCharacter(c.JobId, c.AppearancePresetId);
            m_ui.HighlightSlot(c.CharacterId);
            m_ui.SetPlayInteractable(true);
        }

        private static Character findCharacter(long characterId)
        {
            foreach (Character c in CharacterDataCache.Instance.Characters)
                if (c != null && c.CharacterId == characterId) return c;
            return null;
        }

        private void onCreateOpen()
        {
            // ShowCreatePanel 이 기본 직업/프리셋을 선택하며 OnAppearanceChanged 를 발행 → 프리뷰 갱신됨.
            m_ui.ShowCreatePanel();
        }

        private void onCreateCancel()
        {
            m_ui.HideCreatePanel();
            // 생성 취소 시 메인 프리뷰를 선택돼 있던 캐릭터로 복원.
            Character c = findCharacter(m_selectedCharacterId);
            if (c != null) m_preview.SetCharacter(c.JobId, c.AppearancePresetId);
        }

        private void onAppearanceChanged(int jobId, int presetId) => m_preview.SetCharacter(jobId, presetId);

        private void onCreateConfirmed(string name, int jobId, int presetId) => m_selector?.RequestCreate(name, jobId, presetId);

        private void onStatusChanged(string msg) { if (m_ui != null) m_ui.SetStatus(msg); }

        private void onCharacterListUpdated(IReadOnlyList<Character> list) => rebuildSlots(list);
    }
}
