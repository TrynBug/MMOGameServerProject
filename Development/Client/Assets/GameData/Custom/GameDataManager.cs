using UnityEngine;
using System.IO;

namespace GameData
{
    /// <summary>
    /// 게임데이터 로드 진입점.
    /// 실제 로드 로직은 GameDataGenerator가 생성하는 GameDataManagerBase.LoadAllGameData(csvDir)에서 처리.
    /// </summary>
    public static class GameDataManager
    {
        private static bool s_loaded = false;

        public static bool IsLoaded => s_loaded;

        /// <summary>
        /// 첫 씬 로드 직전에 Unity가 자동 호출. 어느 씬에서 시작해도 보장됨.
        /// 게임데이터 로드 실패는 치명적 오류이므로 즉시 종료한다.
        /// </summary>
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        public static void LoadAllGameData()
        {
            if (s_loaded)
                return;

            string csvDir = Path.Combine(Application.streamingAssetsPath, "GameData");

            if (!GameDataManagerBase.LoadAllGameData(csvDir))
            {
                Debug.LogError($"[GameDataManager] LoadAllGameData failed. csvDir={csvDir}");

#if UNITY_EDITOR
                UnityEditor.EditorApplication.isPlaying = false;
#else
                Application.Quit(1);
#endif
                return;
            }

            s_loaded = true;
            Debug.Log($"[GameDataManager] LoadAllGameData done. csvDir={csvDir}");
        }
    }
}
