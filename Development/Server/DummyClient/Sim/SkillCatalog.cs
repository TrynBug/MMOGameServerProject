using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace DummyClient.Sim
{
    // 스킬 배치 방식 (클라 ESkillPlacement 와 동일).
    public enum SkillPlacement { None = 0, Caster = 1, Target = 2, Forward = 3 }

    public sealed class SkillInfo
    {
        public int Key;
        public int CooldownMs;
        public SkillPlacement Placement;
        public float ObbLength;   // Forward 배치에서 origin 전진거리(=ObbLength/2)에 사용
    }

    // 서버 GameData 의 Skill.csv 에서 스킬별 쿨타임/배치 정보를 읽는다.
    // 컬럼(1-based): 1 Key, 12 CooldownMs, 18 Placement, 25 ObbLength.
    public sealed class SkillCatalog
    {
        private readonly Dictionary<int, SkillInfo> m_byKey = new();

        public static SkillCatalog Load(string csvPath)
        {
            var cat = new SkillCatalog();
            if (!File.Exists(csvPath))
            {
                System.Console.WriteLine($"[skill] Skill.csv 없음: {csvPath}");
                return cat;
            }

            string[] lines = File.ReadAllLines(csvPath);
            for (int i = 1; i < lines.Length; i++) // 0행=헤더
            {
                string line = lines[i];
                if (string.IsNullOrWhiteSpace(line)) continue;
                if (i == 1) line = line.TrimStart('﻿');
                string[] c = line.Split(',');
                if (c.Length < 25) continue;
                if (!int.TryParse(c[0].Trim().TrimStart('﻿'), out int key)) continue;

                var info = new SkillInfo
                {
                    Key = key,
                    CooldownMs = ParseInt(c[11]),
                    Placement = (SkillPlacement)ParseInt(c[17]),
                    ObbLength = ParseFloat(c[24]),
                };
                cat.m_byKey[key] = info;
            }
            return cat;
        }

        public SkillInfo Get(int key) => m_byKey.TryGetValue(key, out SkillInfo info) ? info : null;

        private static int ParseInt(string s)
            => int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : 0;
        private static float ParseFloat(string s)
            => float.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float v) ? v : 0f;
    }
}
