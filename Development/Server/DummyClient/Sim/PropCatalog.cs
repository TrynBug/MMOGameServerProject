using System.Collections.Generic;
using System.IO;

namespace DummyClient.Sim
{
    // 서버 GameData 의 Prop.csv 에서 상호작용 사거리와 Portal 여부를 읽는다.
    public sealed class PropCatalog
    {
        private sealed class PropInfo
        {
            public float InteractRange;
            public bool IsPortal;
        }

        private readonly Dictionary<int, PropInfo> m_byKey = new();

        public static PropCatalog Load(string csvPath)
        {
            var catalog = new PropCatalog();
            if (!File.Exists(csvPath))
            {
                System.Console.WriteLine($"[prop] Prop.csv 없음: {csvPath}");
                return catalog;
            }

            string[] lines = File.ReadAllLines(csvPath);
            for (int i = 1; i < lines.Length; i++)
            {
                string line = lines[i];
                if (string.IsNullOrWhiteSpace(line)) continue;
                string[] cols = line.Split(',');
                if (cols.Length < 10) continue;
                if (!int.TryParse(cols[0].Trim().TrimStart('﻿'), out int key)) continue;

                float.TryParse(cols[7].Trim(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out float interactRange);
                int.TryParse(cols[9].Trim(), out int behavior);
                catalog.m_byKey[key] = new PropInfo
                {
                    InteractRange = interactRange,
                    IsPortal = behavior == 1, // EPropBehavior.Portal
                };
            }
            return catalog;
        }

        public bool IsPortal(int propKey)
            => m_byKey.TryGetValue(propKey, out PropInfo info) && info.IsPortal;

        public float GetInteractRange(int propKey)
            => m_byKey.TryGetValue(propKey, out PropInfo info) ? info.InteractRange : 0f;

        public bool HasPortal()
        {
            foreach (PropInfo info in m_byKey.Values)
                if (info.IsPortal)
                    return true;
            return false;
        }
    }
}
