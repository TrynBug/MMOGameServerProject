using Client.Network;
using Client.Packet;
using Common;
using GameData;
using DataStructures;
using GamePacket;
using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 캐릭터 선택 흐름을 담당.
    //
    // 책임:
    //   - CharacterDataCache 의 캐릭터 목록을 받아 처리
    //   - 캐릭터가 없으면 CharacterCreateReq 송신
    //   - CharacterCreateRes 받아서 선택 흐름 진행
    //   - 캐릭터가 있으면 첫 번째 캐릭터 자동 선택 (테스트 단계)
    //   - 선택된 캐릭터의 LocalPlayer 스폰을 StageManager 에 요청
    //   - 서버에 CharacterSelectReq 송신
    //
    // 책임이 아닌 것:
    //   - 캐릭터 GameObject 생성 (CharacterFactory 가 함)
    //   - 캐릭터 컬렉션 관리 (StageManager 가 함)
    //   - StageEnterNtf 이후의 흐름 (StageManager 가 함)
    public class CharacterSelector : MonoBehaviour
    {
        // 외부 이벤트. UI 가 진행 상태를 표시할 수 있도록.
        public event System.Action<string> OnStatusChanged;

        private void Start()
        {
            // CharacterCreateRes 핸들러 등록.
            // CharacterListNtf 는 일반적으로 Login 씬에서 받아 캐시되므로 다시 받을 필요 없음.
            // 단, 다른 시나리오 대비 fallback 으로 등록은 해둠.
            PacketDispatcher.Instance.Register<CharacterCreateRes>(GamePacketId.CharacterCreateRes, onCharacterCreateRes);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);

            // 캐시의 캐릭터 목록으로 즉시 처리 시작.
            CharacterDataCache cache = CharacterDataCache.Instance;
            processCharacterList(cache.Characters);
        }

        // ─── 흐름 ───────────────────────────────────────────────────────

        private void processCharacterList(IReadOnlyList<Character> characters)
        {
            if (characters == null || characters.Count == 0)
            {
                // 캐릭터 없음 → 생성 요청
                setStatus("캐릭터 생성 중...");
                sendCharacterCreateReq("test_character", 1);
                return;
            }

            // 첫 번째 캐릭터 자동 선택 (테스트 단계)
            Character first = characters[0];
            if (first == null)
            {
                Debug.LogError("[CharacterSelector] First character is null");
                return;
            }

            Debug.Log($"[CharacterSelector] Auto-selecting first character: id={first.CharacterId}, name={first.Name}");
            selectCharacter(first);
        }

        private void selectCharacter(Character ch)
        {
            setStatus($"캐릭터 선택 중: {ch.Name}");

            // StageManager 에게 LocalPlayer 스폰을 요청
            if (StageManager.Instance != null)
            {
                StageManager.Instance.SpawnLocalPlayer(
                    userId: ch.CharacterId,
                    name: ch.Name,
                    pos: new Vector3(ch.PosX, ch.PosY, ch.PosZ),
                    dirY: ch.Yaw);
            }
            else
            {
                Debug.LogError("[CharacterSelector] StageManager.Instance is null. LocalPlayer 가 스폰되지 못합니다.");
            }

            // 서버에 선택 요청
            sendCharacterSelectReq(ch.CharacterId);
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        private void onCharacterListNtf(CharacterListNtf ntf)
        {
            // fallback 경로. 캐시를 업데이트하고 처리.
            CharacterDataCache.Instance.SetCharacters(ntf.Characters);
            processCharacterList(CharacterDataCache.Instance.Characters);
        }

        private void onCharacterCreateRes(CharacterCreateRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                string msg = $"캐릭터 생성 실패: {res.ErrorMsg} (code={res.ResultCode})";
                Debug.LogError($"[CharacterSelector] {msg}");
                setStatus(msg);
                return;
            }

            Character ch = res.NewCharacter;
            if (ch == null)
            {
                Debug.LogError("[CharacterSelector] CharacterCreateRes has null Character");
                return;
            }

            Debug.Log($"[CharacterSelector] CharacterCreateRes: id={ch.CharacterId}, name={ch.Name}");

            // 생성된 캐릭터를 곧바로 선택
            selectCharacter(ch);
        }

        // ─── 송신 ───────────────────────────────────────────────────────

        private void sendCharacterCreateReq(string name, int jobId)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
            {
                Debug.LogError("[CharacterSelector] NetworkManager 가 연결되지 않음. CharacterCreateReq 송신 실패");
                return;
            }

            CharacterCreateReq req = new CharacterCreateReq
            {
                Name = name,
                JobId = jobId,
            };
            net.Send(GamePacketId.CharacterCreateReq, req);

            Debug.Log($"[CharacterSelector] Sent CharacterCreateReq: name={req.Name}, jobId={req.JobId}");
        }

        private void sendCharacterSelectReq(long characterId)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
            {
                Debug.LogError("[CharacterSelector] NetworkManager 가 연결되지 않음. CharacterSelectReq 송신 실패");
                return;
            }

            CharacterSelectReq req = new CharacterSelectReq
            {
                CharacterId = characterId,
            };
            net.Send(GamePacketId.CharacterSelectReq, req);

            Debug.Log($"[CharacterSelector] Sent CharacterSelectReq: characterId={characterId}");
        }

        // ─── 헬퍼 ───────────────────────────────────────────────────────

        private void setStatus(string msg)
        {
            Debug.Log($"[CharacterSelector] {msg}");
            OnStatusChanged?.Invoke(msg);
        }
    }
}
