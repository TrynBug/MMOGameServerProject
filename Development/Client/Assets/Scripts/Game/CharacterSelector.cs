using Client.Managers;
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
    //   - 서버에 CharacterSelectReq 송신
    //   - CharacterSelectRes 받으면 spawn 정보를 캐시에 저장하고 Game 씬으로 전환
    //
    // 책임이 아닌 것:
    //   - 캐릭터 GameObject 생성 (Game 씬의 StageManager 가 함)
    //   - StageEnterNtf 이후의 흐름 (Game 씬의 StageManager 가 함)
    public class CharacterSelector : MonoBehaviour
    {
        // 외부 이벤트. UI 가 진행 상태를 표시할 수 있도록.
        public event System.Action<string> OnStatusChanged;

        private void Start()
        {
            // 선택/생성 흐름의 응답 패킷 핸들러 등록.
            // CharacterListNtf 는 일반적으로 Login 씬에서 받아 캐시되므로 다시 받을 필요 없음.
            // 단, 다른 시나리오 대비 fallback 으로 등록은 해둠.
            PacketDispatcher.Instance.Register<CharacterCreateRes>(GamePacketId.CharacterCreateRes, onCharacterCreateRes);
            PacketDispatcher.Instance.Register<CharacterSelectRes>(GamePacketId.CharacterSelectRes, onCharacterSelectRes);
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

        private void onCharacterSelectRes(CharacterSelectRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                string msg = $"캐릭터 선택 실패: {res.ErrorMsg} (code={res.ResultCode})";
                Debug.LogError($"[CharacterSelector] {msg}");
                setStatus(msg);
                return;
            }

            Debug.Log($"[CharacterSelector] CharacterSelectRes OK. characterId={res.CharacterId}, stageDataKey={res.StageDataKey}");

            // 선택된 캐릭터 이름은 캐시의 Characters 목록에서 찾아옴.
            string name = findCharacterName(res.CharacterId);

            // 선택 결과를 캐시에 저장 (Game 씬에서 꺼내 씀).
            // 스폰 좌표는 없음 — 로딩 완료 보고 후 StageEnterNtf 로 받는다 (2단계 입장).
            CharacterDataCache.Instance.SetSelectedSpawn(
                characterId: res.CharacterId,
                name: name,
                stageDataKey: res.StageDataKey);

            // Game 씬으로 전환
            setStatus("게임 입장 중...");
            Managers.Managers.Scene.LoadScene(SceneManagerEx.SceneNames.Game);
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

        private string findCharacterName(long characterId)
        {
            foreach (Character ch in CharacterDataCache.Instance.Characters)
            {
                if (ch != null && ch.CharacterId == characterId) return ch.Name;
            }
            return $"Character_{characterId}";
        }

        private void setStatus(string msg)
        {
            Debug.Log($"[CharacterSelector] {msg}");
            OnStatusChanged?.Invoke(msg);
        }
    }
}
