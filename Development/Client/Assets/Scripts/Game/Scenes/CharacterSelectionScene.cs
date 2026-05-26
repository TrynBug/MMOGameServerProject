using Client.Managers;
using Client.UI;
using UnityEngine;

namespace Client.Game
{
    // CharacterSelection 씬의 BaseScene 구현.
    //
    // 책임:
    //   - 씬 진입 시 UI_CharacterSelection 띄우기
    //   - 같은 씬의 CharacterSelector 에서 발생하는 상태 변경을 UI 에 반영
    //
    // 책임이 아닌 것:
    //   - 캐릭터 선택 로직 (CharacterSelector)
    //   - 캐릭터 생성 (CharacterFactory + StageManager)
    public class CharacterSelectionScene : BaseScene
    {
        private UI_CharacterSelection m_ui;
        private CharacterSelector m_selector;

        protected override void Init()
        {
            Debug.Log("[CharacterSelectionScene] Init");

            // UI 띄우기
            m_ui = Managers.Managers.UI.ShowSceneUI<UI_CharacterSelection>();
            if (m_ui == null)
            {
                Debug.LogError("[CharacterSelectionScene] UI_CharacterSelection 프리팹을 로드하지 못했습니다.");
                return;
            }

            m_ui.SetStatus("로딩 중...");

            // 같은 씬의 CharacterSelector 와 연결
            // (CharacterSelector 는 같은 씬의 다른 GameObject 에 부착되어 있음)
            m_selector = Object.FindAnyObjectByType<CharacterSelector>();
            if (m_selector != null)
            {
                m_selector.OnStatusChanged += onStatusChanged;
            }
            else
            {
                Debug.LogWarning("[CharacterSelectionScene] CharacterSelector 를 찾지 못했습니다.");
            }
        }

        public override void Clear()
        {
            Debug.Log("[CharacterSelectionScene] Clear");

            if (m_selector != null)
            {
                m_selector.OnStatusChanged -= onStatusChanged;
            }
        }

        private void onStatusChanged(string msg)
        {
            if (m_ui != null) m_ui.SetStatus(msg);
        }
    }
}
