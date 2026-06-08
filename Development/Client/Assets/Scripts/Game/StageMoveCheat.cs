using GameData;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Client.Game
{
    // [치트] 키보드로 스테이지 이동 요청을 보낸다. GameScene.Init 에서 코드로 부착됨.
    //
    //   F5 = 마을(100), F6 = 필드1(101), F7 = 필드2(102), F8 = 필드3(103)
    //
    // 포탈 등 정식 이동 트리거가 생기면 제거 예정.
    public class StageMoveCheat : MonoBehaviour
    {
        private void Update()
        {
            Keyboard kb = Keyboard.current;
            if (kb == null)
                return;

            if (kb.f5Key.wasPressedThisFrame) requestMove(100);
            if (kb.f6Key.wasPressedThisFrame) requestMove(101);
            if (kb.f7Key.wasPressedThisFrame) requestMove(102);
            if (kb.f8Key.wasPressedThisFrame) requestMove(103);
        }

        private static void requestMove(int stageDataKey)
        {
            if (StageManager.Instance == null)
            {
                Debug.LogError("[StageMoveCheat] StageManager.Instance is null.");
                return;
            }

            Debug.Log($"[StageMoveCheat] 스테이지 이동 요청: stageDataKey={stageDataKey}");
            StageManager.Instance.RequestStageMove(stageDataKey, EStagePositionType.Default);
        }
    }
}
