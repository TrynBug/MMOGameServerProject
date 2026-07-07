using System;
using System.IO;
using UnityEngine;

namespace Client.Network
{
    // serverconfig.json 매핑용 DTO. (StreamingAssets/serverconfig.json)
    [Serializable]
    public class ServerConfig
    {
        public string loginHost;   // 로그인서버 접속 주소. IP 또는 도메인명(예: login.mygame.com)
        public int loginPort;      // 로그인서버 접속 포트
    }

    // 서버 접속설정 로더.
    //   - StreamingAssets/serverconfig.json 을 런타임에 읽는다(데스크톱 빌드에서는 빌드 후에도 편집 가능).
    //   - 파일이 없거나 값이 잘못되면 로드 실패로 처리한다(폴백 없음). 잘못된 설정으로 임의 접속하지 않는다.
    public static class ServerConfigLoader
    {
        public const string FileName = "serverconfig.json";

        // 성공 시 검증된 ServerConfig 반환. 실패 시 null(에러 로그 출력).
        public static ServerConfig Load()
        {
            string path = Path.Combine(Application.streamingAssetsPath, FileName);

            if (!File.Exists(path))
            {
                Debug.LogError($"[ServerConfig] 설정 파일이 없습니다: {path}");
                return null;
            }

            ServerConfig config;
            try
            {
                string json = File.ReadAllText(path);
                config = JsonUtility.FromJson<ServerConfig>(json);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[ServerConfig] 파싱 실패: {ex.Message} (path={path})");
                return null;
            }

            if (config == null)
            {
                Debug.LogError($"[ServerConfig] 파싱 결과가 null 입니다. path={path}");
                return null;
            }

            if (string.IsNullOrWhiteSpace(config.loginHost))
            {
                Debug.LogError("[ServerConfig] loginHost 가 비어있습니다.");
                return null;
            }

            if (config.loginPort <= 0 || config.loginPort > 65535)
            {
                Debug.LogError($"[ServerConfig] loginPort 가 유효하지 않습니다: {config.loginPort}");
                return null;
            }

            return config;
        }
    }
}
