using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace DummyClient.Sim
{
    // 서버 GameData 의 Prop.csv 에서 상호작용 사거리와 Portal 여부를 읽는다.
    public sealed class PropCatalog
    {
        private sealed class PropInfo
        {
            public float InteractRange;
            public bool IsPortal;
            public int PortalStageKey;
        }

        private sealed class PortalPlacement
        {
            public int PropKey;
            public float X;
            public float Z;
            public int TargetStageKey;
        }

        private readonly Dictionary<int, PropInfo> m_byKey = new();
        private readonly Dictionary<int, List<PortalPlacement>> m_portalsByStage = new();

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
                if (cols.Length < 12) continue;
                if (!int.TryParse(cols[0].Trim().TrimStart('﻿'), out int key)) continue;

                float.TryParse(cols[7].Trim(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out float interactRange);
                int.TryParse(cols[9].Trim(), out int behavior);
                int.TryParse(cols[10].Trim(), out int portalStageKey);
                catalog.m_byKey[key] = new PropInfo
                {
                    InteractRange = interactRange,
                    IsPortal = behavior == 1, // EPropBehavior.Portal
                    PortalStageKey = portalStageKey,
                };
            }
            return catalog;
        }

        public bool LoadStageLayout(int stageKey, string jsonPath)
        {
            if (!File.Exists(jsonPath)) return false;

            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(jsonPath));
            if (!document.RootElement.TryGetProperty("props", out JsonElement props) || props.ValueKind != JsonValueKind.Array)
                return false;

            var placements = new List<PortalPlacement>();
            foreach (JsonElement prop in props.EnumerateArray())
            {
                int propKey = prop.GetProperty("type").GetInt32();
                if (!IsPortal(propKey)) continue;

                JsonElement pos = prop.GetProperty("pos");
                int targetStageKey = prop.TryGetProperty("param0", out JsonElement param0) && param0.GetInt32() > 0
                    ? param0.GetInt32()
                    : GetDefaultPortalStageKey(propKey);
                placements.Add(new PortalPlacement
                {
                    PropKey = propKey,
                    X = pos[0].GetSingle(),
                    Z = pos[2].GetSingle(),
                    TargetStageKey = targetStageKey,
                });
            }
            m_portalsByStage[stageKey] = placements;
            return true;
        }

        public bool IsPortal(int propKey)
            => m_byKey.TryGetValue(propKey, out PropInfo info) && info.IsPortal;

        public float GetInteractRange(int propKey)
            => m_byKey.TryGetValue(propKey, out PropInfo info) ? info.InteractRange : 0f;

        public int GetPortalTargetStageKey(int stageKey, int propKey, float x, float z)
        {
            if (m_portalsByStage.TryGetValue(stageKey, out List<PortalPlacement> placements))
            {
                PortalPlacement nearest = null;
                float nearestDistanceSq = float.MaxValue;
                foreach (PortalPlacement placement in placements)
                {
                    if (placement.PropKey != propKey) continue;
                    float dx = placement.X - x;
                    float dz = placement.Z - z;
                    float distanceSq = dx * dx + dz * dz;
                    if (distanceSq < nearestDistanceSq)
                    {
                        nearest = placement;
                        nearestDistanceSq = distanceSq;
                    }
                }
                if (nearest != null)
                    return nearest.TargetStageKey;
            }
            return GetDefaultPortalStageKey(propKey);
        }

        public bool HasPortalToStage(int stageKey, int targetStageKey)
        {
            if (!m_portalsByStage.TryGetValue(stageKey, out List<PortalPlacement> placements)) return false;
            foreach (PortalPlacement placement in placements)
                if (placement.TargetStageKey == targetStageKey)
                    return true;
            return false;
        }

        private int GetDefaultPortalStageKey(int propKey)
            => m_byKey.TryGetValue(propKey, out PropInfo info) ? info.PortalStageKey : 0;

        public bool HasPortal()
        {
            foreach (PropInfo info in m_byKey.Values)
                if (info.IsPortal)
                    return true;
            return false;
        }
    }
}
