using System.Collections.Generic;
using Client.Managers;
using Client.UI;
using GameData;
using UnityEngine;

namespace Client.Game
{
    // 인게임 버프 바 HUD (정식 UI 파이프라인 버전).
    //
    // UI_Scene 으로서 UIManager.ShowSceneUI<UI_BuffBar>() 로 표시된다.
    // 매 프레임 LocalPlayer.Buffs(BuffHolder)를 읽어 UI_BuffIcon SubItem 을 생성/갱신/제거한다.
    //
    // 게임 상태(StageManager/BuffHolder/GameData)를 읽으므로 Game 어셈블리에 둔다.
    // (UI 어셈블리는 Game 을 모르는 최하위라 여기엔 둘 수 없다. 표시만 하는 UI_BuffIcon 은 UI 에 있음.)
    //
    // 프리팹 경로: Resources/UI/Scene/UI_BuffBar
    // 프리팹 자식 GameObject 이름이 아래 enum 과 일치해야 Bind 된다:
    //   GameObject : BuffContainer   (HorizontalLayoutGroup 권장. 여기에 아이콘이 쌓인다)
    public class UI_BuffBar : UI_Scene
    {
        private enum Objects { BuffContainer }

        private Transform m_container;
        private readonly Dictionary<long, UI_BuffIcon> m_icons = new Dictionary<long, UI_BuffIcon>();
        private readonly List<long> m_removeScratch = new List<long>();

        public override void Init()
        {
            base.Init();

            Bind<GameObject>(typeof(Objects));

            GameObject container = Get<GameObject>((int)Objects.BuffContainer);
            m_container = (container != null) ? container.transform : transform;
        }

        private void Update()
        {
            StageManager sm = StageManager.Instance;
            BuffHolder holder = (sm != null && sm.LocalPlayer != null) ? sm.LocalPlayer.Buffs : null;

            if (holder == null || holder.Count == 0)
            {
                if (m_icons.Count > 0)
                    clearAll();
                return;
            }

            // 1) 더 이상 없는 버프의 아이콘 제거.
            m_removeScratch.Clear();
            foreach (long key in m_icons.Keys)
            {
                if (!holder.Buffs.ContainsKey(key))
                    m_removeScratch.Add(key);
            }
            foreach (long key in m_removeScratch)
                removeIcon(key);

            // 2) 현재 버프 아이콘 생성/갱신.
            foreach (KeyValuePair<long, BuffHolder.Entry> kv in holder.Buffs)
            {
                if (!m_icons.TryGetValue(kv.Key, out UI_BuffIcon icon))
                {
                    icon = Managers.Managers.UI.MakeSubItem<UI_BuffIcon>(m_container);
                    if (icon == null)
                        continue;   // 프리팹(Resources/UI/SubItem/UI_BuffIcon) 미작성 시 스킵.

                    icon.Init();    // MakeSubItem 은 Init 을 호출하지 않으므로 여기서 1회 바인딩.
                    m_icons.Add(kv.Key, icon);
                }

                applyToIcon(icon, kv.Key, kv.Value);
            }
        }

        private static void applyToIcon(UI_BuffIcon icon, long buffKey, BuffHolder.Entry entry)
        {
            GameData_Buff data = GameDataTable_Buff.FindData(buffKey);

            string name = (data != null && !string.IsNullOrEmpty(data.Name)) ? data.Name : $"#{buffKey}";

            string timeText;
            if (entry.IsPermanent)
            {
                timeText = string.Empty;   // 영구는 카운트다운 없음.
            }
            else
            {
                float sec = entry.RemainSecondsNow();
                timeText = (sec >= 10f) ? $"{Mathf.CeilToInt(sec)}s" : $"{sec:0.0}s";
            }

            icon.SetData(name, entry.StackCount, timeText, categoryColor(data));
        }

        private static Color categoryColor(GameData_Buff data)
        {
            if (data == null)
                return new Color(0.10f, 0.10f, 0.10f, 0.60f);

            switch (data.Category)
            {
                case EBuffCategory.Buff:   return new Color(0.12f, 0.35f, 0.15f, 0.70f);   // 초록
                case EBuffCategory.Debuff: return new Color(0.40f, 0.12f, 0.12f, 0.70f);   // 빨강
                default:                   return new Color(0.10f, 0.10f, 0.10f, 0.60f);
            }
        }

        private void removeIcon(long buffKey)
        {
            if (m_icons.TryGetValue(buffKey, out UI_BuffIcon icon))
            {
                if (icon != null)
                    Managers.Managers.Resource.Destroy(icon.gameObject);
                m_icons.Remove(buffKey);
            }
        }

        private void clearAll()
        {
            foreach (UI_BuffIcon icon in m_icons.Values)
            {
                if (icon != null)
                    Managers.Managers.Resource.Destroy(icon.gameObject);
            }
            m_icons.Clear();
        }
    }
}
