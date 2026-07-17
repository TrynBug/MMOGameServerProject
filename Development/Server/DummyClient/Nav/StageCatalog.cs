using System.Collections.Generic;
using System.IO;

namespace DummyClient.Nav
{
    // 서버 GameData 의 Stage.csv 를 읽어 stageDataKey → NavMeshFileName 매핑을 만든다.
    // CSV 헤더: Key,StageType,NavMeshFileName,StageLayoutFileName,sectorSize,...
    public sealed class StageCatalog
    {
        private readonly Dictionary<int, string> m_navMeshByKey = new();

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
                string navName = cols[2].Trim();
                if (!string.IsNullOrEmpty(navName))
                    cat.m_navMeshByKey[key] = navName;
            }
            return cat;
        }

        // stageDataKey 의 NavMesh 파일명(확장자 제외). 없으면 null.
        public string GetNavMeshName(int stageDataKey)
            => m_navMeshByKey.TryGetValue(stageDataKey, out string name) ? name : null;
    }
}
