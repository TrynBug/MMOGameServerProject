using System.Collections.Generic;
using System.IO;

namespace GameData
{
    // 게임데이터 기본클래스
    public abstract class GameDataBase
    {
    }

    // 게임데이터파일 기본클래스
    public abstract class GameDataTableBase
    {
        protected string m_dataFilePath = "";

        public string GetDataFilePath() => m_dataFilePath;

        public abstract string GetDataName();

        public bool LoadData(string csvDir)
        {
            m_dataFilePath = Path.Combine(csvDir, GetDataName() + ".csv");

            if (!File.Exists(m_dataFilePath))
                return false;

            string[] lines = File.ReadAllLines(m_dataFilePath);
            if (lines.Length < 2)
                return true;

            // 첫 번째 행은 헤더이므로 건너뜀
            for (int i = 1; i < lines.Length; i++)
            {
                string line = lines[i].Trim();
                if (string.IsNullOrEmpty(line))
                    continue;

                if (!MakeGameData(line))
                    return false;
            }

            return OnLoadComplete();
        }

        protected abstract bool MakeGameData(string line);
        protected virtual bool OnLoadComplete() => true;

        public static bool StringToBool(string s) => s == "true" || s == "True" || s == "TRUE";
    }
}