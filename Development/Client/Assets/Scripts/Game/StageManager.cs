using System.Collections.Generic;
using Client.Network;
using Client.Packet;
using Common;
using DataStructures;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 게임 세계의 상태(내 캐릭터, 주변 캐릭터들)를 관리한다.
    // 게임 관련 패킷(GameEnterNtf, StageEnterNtf, CharacterEnter/Leave, MoveNtf 등)을 처리한다.
    //
    // A-1: GameEnterNtf로 내 캐릭터만 생성. 다른 유저 처리는 A-4에서.
    public class StageManager : MonoBehaviour
    {
        public static StageManager Instance { get; private set; }

        // user_id -> PlayerCharacter
        private readonly Dictionary<long, PlayerCharacter> m_characters = new Dictionary<long, PlayerCharacter>();

        // 내 캐릭터 (편의 접근)
        public PlayerCharacter LocalPlayer { get; private set; }
        public long CurrentStageId { get; private set; }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;

            // 패킷 핸들러는 Awake에서 등록. LoginFlow.Start() 보다 먼저 호출되도록 보장하기 위해.
            // (Awake가 모든 Start보다 먼저 실행됨)
            PacketDispatcher.Instance.Register<GameEnterNtf>(GamePacketId.GameEnterNtf, onGameEnterNtf);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);
            PacketDispatcher.Instance.Register<StageEnterNtf>(GamePacketId.StageEnterNtf, onStageEnterNtf);

            Debug.Log("[StageManager] Ready. Waiting for GameEnterNtf...");
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        private void onGameEnterNtf(GameEnterNtf ntf)
        {
            // 이 패킷은 유저가 게임서버에 입장 성공했다는 의미입니다.
        }

        private void onCharacterListNtf(CharacterListNtf ntf)
        {
            // 이 패킷은 게임서버가 캐릭터 목록을 보내준 것입니다.
            // 원래 캐릭터가 없으면 생성해야 하지만, 지금은 임시로 캐릭터가 있다고 가정합니다.

            if (ntf.Characters.Count == 0)
            {
                Debug.Log($"[StageManager] CharacterListNtf: No Character");
                return;
            }

            Character ch = ntf.Characters[0]; // 임시로 캐릭터가 있다고 가정합니다.
            if (ch == null)
            {
                Debug.LogError("[StageManager] GameEnterNtf has null Character");
                return;
            }

            Debug.Log($"[StageManager] GameEnterNtf: characterId={ch.CharacterId}, name={ch.Name}, " +
                      $"pos=({ch.PosX}, {ch.PosY}), dir_y={ch.Yaw}");

            // 내 캐릭터 생성
            LocalPlayer = createCharacter(
                userId: ch.CharacterId,
                name: ch.Name,
                isLocalPlayer: true,
                pos: new Vector3(ch.PosX, ch.PosY, 0),
                dirY: ch.Yaw);

            // 캐릭터 선택 요청 패킷 보내기
            sendCharacterSelectReq(ch.CharacterId);
        }

        private void onStageEnterNtf(StageEnterNtf ntf)
        {
            // A-1: 일단 로그만. A-4에서 nearby_characters 처리.
            Debug.Log($"[StageManager] StageEnterNtf: stage={ntf.StageId}, nearby count={ntf.NearbyCharacters.Count}");

            // 위치/스탯이 최신화됐을 수 있으니 내 캐릭터 위치 동기화 (있다면)
            if (ntf.MyCharacter != null && LocalPlayer != null && LocalPlayer.UserId == ntf.MyCharacter.CharacterId)
            {
                Character mc = ntf.MyCharacter;
                LocalPlayer.SetPosition(new Vector3(mc.PosX, mc.PosY, 0), mc.Yaw);
            }

            CurrentStageId = ntf.StageId;
        }

        // ─── 내부 유틸 ──────────────────────────────────────────────────

        private PlayerCharacter createCharacter(long userId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            if (m_characters.TryGetValue(userId, out PlayerCharacter existing))
            {
                Debug.LogWarning($"[StageManager] character already exists. userId={userId}. Reusing.");
                existing.SetPosition(pos, dirY);
                return existing;
            }

            // 캡슐 프리미티브로 생성 (Collider, MeshRenderer 자동 포함)
            GameObject go = GameObject.CreatePrimitive(PrimitiveType.Capsule);

            // 캡슐의 디폴트 콜라이더는 그대로 두되, 충돌 처리는 A-1에서 안 다룸
            PlayerCharacter pc = go.AddComponent<PlayerCharacter>();
            pc.Initialize(userId, name, isLocalPlayer, pos, dirY);

            m_characters.Add(userId, pc);
            Debug.Log($"[StageManager] Spawned character {go.name} at {pos}");
            return pc;
        }


        // 게임서버에 send
        private void sendCharacterSelectReq(long characterId)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;

            CharacterSelectReq req = new CharacterSelectReq
            {
                CharacterId = characterId
            };
            net.Send(GamePacketId.CharacterSelectReq, req);

            Debug.Log($"[StageManager] Sent CharacterSelectReq characterId={characterId}");
        }
    }
}
