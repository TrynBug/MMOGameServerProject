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
    //   - 입장 이후의 흐름 (Game 씬의 StageManager 가 함)
    public class CharacterSelector : MonoBehaviour
    {
        // 외부 이벤트. UI 가 진행 상태를 표시할 수 있도록.
        public event System.Action<string> OnStatusChanged;

        // 캐릭터 목록이 (재)수신되어 갱신되었을 때. UI 가 슬롯을 다시 구성한다.
        public event System.Action<IReadOnlyList<Character>> OnCharacterListUpdated;

        private void Start()
        {
            // 선택/생성 흐름의 응답 패킷 핸들러 등록.
            // CharacterListNtf 는 일반적으로 Login 씬에서 받아 캐시되므로 다시 받을 필요 없음.
            // 단, 다른 시나리오 대비 fallback 으로 등록은 해둠.
            PacketDispatcher.Instance.Register<CharacterCreateRes>(GamePacketId.CharacterCreateRes, onCharacterCreateRes);
            PacketDispatcher.Instance.Register<CharacterSelectRes>(GamePacketId.CharacterSelectRes, onCharacterSelectRes);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);

            // 자동 진행하지 않는다. 목록 표시/선택/생성은 UI(CharacterSelectionScene)가 주도한다.
        }

        // ─── UI 주도 API ─────────────────────────────────────────────────

        // 현재 캐시된 캐릭터 목록. UI 가 슬롯을 구성할 때 사용.
        public IReadOnlyList<Character> Characters => CharacterDataCache.Instance.Characters;

        // UI 가 슬롯 클릭 시 호출: 해당 캐릭터 선택 요청.
        public void RequestSelect(long characterId)
        {
            selectCharacterById(characterId);
        }

        // UI 가 생성 확정 시 호출: 캐릭터 생성 요청. 생성 성공 시 자동으로 그 캐릭터를 선택한다.
        // appearancePresetId: 외형 프리셋 (0-base). UI 프리셋 선택은 Stage 4, 그 전까지 기본 0.
        public void RequestCreate(string name, int jobId, int appearancePresetId = 0)
        {
            setStatus("캐릭터 생성 중...");
            sendCharacterCreateReq(name, jobId, appearancePresetId);
        }

        // ─── 흐름 ───────────────────────────────────────────────────────

        private void selectCharacterById(long characterId)
        {
            setStatus("캐릭터 선택 중...");
            sendCharacterSelectReq(characterId);
        }

        private void selectCharacter(Character ch)
        {
            setStatus($"캐릭터 선택 중: {ch.Name}");
            sendCharacterSelectReq(ch.CharacterId);
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        private void onCharacterListNtf(CharacterListNtf ntf)
        {
            // fallback 경로. 캐시를 업데이트하고 UI 에 갱신을 알린다.
            CharacterDataCache.Instance.SetCharacters(ntf.Characters);
            OnCharacterListUpdated?.Invoke(CharacterDataCache.Instance.Characters);
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

            Character character = res.Character;
            if (character == null)
            {
                Debug.LogError("[CharacterSelector] CharacterSelectRes has null Character");
                return;
            }

            Debug.Log($"[CharacterSelector] CharacterSelectRes OK. characterId={character.CharacterId}, name={character.Name}, stageDataKey={res.StageDataKey}");

            // 내 캐릭터 전체 데이터 모델을 캐시에 보관 (영속). Game 씬에서 이 모델로 LocalPlayer 를 만든다.
            // 스폰 좌표는 없음 — 로딩 완료 보고 후 StageLoadCompleteRes 로 받는다 (2단계 입장).
            CharacterDataCache.Instance.SetLocalCharacter(character, res.StageDataKey);

            // Game 씬으로 전환
            setStatus("게임 입장 중...");
            Managers.Managers.Scene.LoadScene(SceneManagerEx.SceneNames.Game);
        }

        // ─── 송신 ───────────────────────────────────────────────────────

        private void sendCharacterCreateReq(string name, int jobId, int appearancePresetId)
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
                AppearancePresetId = appearancePresetId,
            };
            net.Send(GamePacketId.CharacterCreateReq, req);

            Debug.Log($"[CharacterSelector] Sent CharacterCreateReq: name={req.Name}, jobId={req.JobId}, preset={req.AppearancePresetId}");
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
