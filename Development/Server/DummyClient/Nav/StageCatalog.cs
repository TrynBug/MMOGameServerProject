using System.Collections.Generic;
using System.IO;

namespace DummyClient.Nav
{
    // 서버 GameData 의 Stage.csv 를 읽어 stageDataKey → NavMeshFileName 매핑을 만든다.
    // CSV 헤더: Key,StageType,NavMeshFileName,StageLayoutFileName,sectorSize,...
    public sealed class StageCatalog
    {
        private readonly Dictionary<int, string> m_navMeshByKey = new();
        private readonly Dictionary<int, string> m_stageLayoutByKey = new();
        private readonly Dictionary<int, int> m_stageTypeByKey = new();

        public static StageCatalog Load(string csvPath)
        {
            var cat = new StageCatalog();
            if (!File.Exists(csvPath))
            {
                System.Console.WriteLine($"[stage] Stage.csv 없음: {csvPath}");
                return cat;
            }

            string[] lines = File.ReadAllLines(csvPath);
            for (int i = 1; i < lines.Length; i++) // 0행=헤더
            {
                string line = lines[i];
                if (string.IsNullOrWhiteSpace(line)) continue;
                // BOM 제거
                if (i == 1) line = line.TrimStart('﻿');
                string[] cols = line.Split(',');
                if (cols.Length < 3) continue;
                if (!int.TryParse(cols[0].Trim().TrimStart('﻿'), out int key)) continue;
                if (int.TryParse(cols[1].Trim(), out int stageType))
                    cat.m_stageTypeByKey[key] = stageType;
                string navName = cols[2].Trim();
                if (!string.IsNullOrEmpty(navName))
                    cat.m_navMeshByKey[key] = navName;
                if (cols.Length > 3)
                {
                    string stageLayoutName = cols[3].Trim();
                    if (!string.IsNullOrEmpty(stageLayoutName))
                        cat.m_stageLayoutByKey[key] = stageLayoutName;
                }
            }
            return cat;
        }

        // stageDataKey 의 NavMesh 파일명(확장자 제외). 없으면 null.
        public string GetNavMeshName(int stageDataKey)
            => m_navMeshByKey.TryGetValue(stageDataKey, out string name) ? name : null;

        public string GetStageLayoutName(int stageDataKey)
            => m_stageLayoutByKey.TryGetValue(stageDataKey, out string name) ? name : null;

        // EStageType.Town = 2. Town 은 전투 없이 저빈도 배회만 한다.
        public bool IsTown(int stageDataKey)
            => m_stageTypeByKey.TryGetValue(stageDataKey, out int stageType) && stageType == 2;

        public bool Contains(int stageDataKey) => m_stageTypeByKey.ContainsKey(stageDataKey);
    }
}
