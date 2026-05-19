using UnityEngine;
using UnityEditor;
using System.IO;

[InitializeOnLoad]
public class AIEditorBridge
{
    private static string triggerPath = "Assets/Editor/ai_command.txt";

    static AIEditorBridge()
    {
        // 유니티 에디터가 업데이트될 때마다 파일 감지
        EditorApplication.update += CheckAICommand;
    }

    private static void CheckAICommand()
    {
        if (File.Exists(triggerPath))
        {
            string command = File.ReadAllText(triggerPath).Trim();
            File.Delete(triggerPath); // 명령 확인 후 삭제

            ExecuteCommand(command);
        }
    }

    private static void ExecuteCommand(string command)
    {
        Debug.Log($"[AI MCP] 명령 수신: {command}");
        
        switch (command)
        {
            case "PLAY":
                EditorApplication.isPlaying = true;
                break;
            case "STOP":
                EditorApplication.isPlaying = false;
                break;
            case "REFRESH":
                AssetDatabase.Refresh();
                break;
            default:
                Debug.LogWarning("알 수 없는 AI 명령입니다.");
                break;
        }
    }
}