using Client.Network;
using Client.Packet;
using Common;
using DataStructures;
using GamePacket;
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.TextCore.Text;

namespace Client.Game
{
    // 게임 세계의 상태(내 캐릭터, 주변 캐릭터들)를 관리한다.
    // 게임 관련 패킷(GameEnterNtf, StageEnterNtf, CharacterEnter/Leave, MoveNtf 등)을 처리한다.
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
            PacketDispatcher.Instance.Register<CharacterCreateRes>(GamePacketId.CharacterCreateRes, onCharacterCreateRes);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);
            PacketDispatcher.Instance.Register<StageEnterNtf>(GamePacketId.StageEnterNtf, onStageEnterNtf);
            PacketDispatcher.Instance.Register<ObjectVisibilityNtf>(GamePacketId.ObjectVisibilityNtf, onObjectVisibilityNtf);
            PacketDispatcher.Instance.Register<MoveNtf>(GamePacketId.MoveNtf, onMoveNtf);

            Debug.Log("[StageManager] Ready. Waiting for GameEnterNtf...");
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        // 유저가 게임서버에 입장 성공함
        private void onGameEnterNtf(GameEnterNtf ntf)
        {
            
        }

        // 캐릭터 생성 응답
        private void onCharacterCreateRes(CharacterCreateRes res)
        {
            if((EResultCode)res.ResultCode != EResultCode.Success)
            {
                Debug.LogError($"[StageManager] CharacterCreateRes: 캐릭터 생성 실패. result_code={res.ResultCode}, error_msg={res.ErrorMsg}");
                return;
            }

            // 생성된 캐릭터 선택
            DataStructures.Character ch = res.NewCharacter;
            if (ch == null)
            {
                Debug.LogError("[StageManager] CharacterCreateRes has null Character");
                return;
            }

            Debug.Log($"[StageManager] CharacterCreateRes: characterId={ch.CharacterId}, name={ch.Name}, " +
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

        private void onCharacterListNtf(CharacterListNtf ntf)
        {
            // 이 패킷은 게임서버가 캐릭터 목록을 보내준 것입니다.

            if (ntf.Characters.Count == 0)
            {
                // 캐릭터가 없으면 캐릭터 생성패킷 보냄

                sendCharacterCreateReq("test_character", 1);
                return;
            }

            // 캐릭터가 있으면 테스트단계이므로 일단 첫번째 캐릭터 선택
            DataStructures.Character ch = ntf.Characters[0];
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

        // 유저가 스테이지에 입장 성공
        private void onStageEnterNtf(StageEnterNtf ntf)
        {
            Debug.Log($"[StageManager] StageEnterNtf: stage={ntf.StageId}");

            // 위치/스탯이 최신화됐을 수 있으니 내 캐릭터 위치 동기화
            if (LocalPlayer != null)
            {
                LocalPlayer.SetPosition(new Vector3(ntf.MyPosX, ntf.MyPosY, 0), ntf.MyYaw);
            }

            CurrentStageId = ntf.StageId;
        }

        // 스테이지내의 오브젝트 스폰/제거 패킷
        private void onObjectVisibilityNtf(ObjectVisibilityNtf ntf)
        {
            long myCharacterId = 0;
            if(LocalPlayer)
                myCharacterId = LocalPlayer.UserId;

            // 캐릭터 스폰정보 처리
            foreach (CharacterSpawnInfo characterSpawnInfo in ntf.CharacterSpawns)
            {
                // 내 캐릭터는 무시
                if (characterSpawnInfo.ObjectId == myCharacterId)
                {
                    continue;
                }

                createCharacter(
                    userId: characterSpawnInfo.ObjectId,
                    name: characterSpawnInfo.Name,
                    isLocalPlayer: false,
                    pos: new Vector3(characterSpawnInfo.PosX, characterSpawnInfo.PosY, 0),
                    dirY: characterSpawnInfo.Yaw);
                
                Debug.Log($"[StageManager] ObjectVisibilityNtf: CharacterSpawnInfo characterId={characterSpawnInfo.ObjectId}");
            }

            // 오브젝트 디스폰 정보 처리
            foreach (long despawnObjectId in ntf.DespawnIds)
            {
                // m_characters 에서 삭제. 나중에 공용로직으로 분리해서 바꾸기 필요
                PlayerCharacter character;
                m_characters.TryGetValue(despawnObjectId, out character);
                if (character)
                {
                    m_characters.Remove(despawnObjectId);
                    Destroy(character.gameObject);
                }

                Debug.Log($"[StageManager] ObjectVisibilityNtf: DespawnObject objectId={despawnObjectId}");
            }

        }

        // 스테이지내의 오브젝트가 이동했을때 서버가 보내는 패킷
        private void onMoveNtf(MoveNtf ntf)
        {
            if (LocalPlayer)
            {
                if(LocalPlayer.UserId == ntf.ObjectId)
                {
                    // 내 캐릭터 이동 패킷일 경우, 위치가 어긋나면 보정해야 함
                    // 지금은 그냥 넘어감

                    return;
                }
            }

            PlayerCharacter character;
            m_characters.TryGetValue(ntf.ObjectId, out character);
            if(character)
            {
                // 오브젝트 이동 패킷을 받았으면, 오브젝트를 tick마다 업데이트하며 이동시켜야 함.
                // 지금은 그런로직이 없기 때문에 위치만 바꿔줌
                character.SetPosition(new Vector3(ntf.PosX, ntf.PosY, 0), ntf.Yaw);
            }

            Debug.Log($"[StageManager] MoveNtf: ObjectId={ntf.ObjectId}, PosX={ntf.PosX}, PosY={ntf.PosY}, Yaw={ntf.Yaw}, DestX={ntf.DestX}, DestY={ntf.DestY}, IsMoving={ntf.IsMoving}");
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

            // 캡슐의 디폴트 콜라이더는 그대로 두되, 충돌 처리는 아직 안다룸
            PlayerCharacter pc = go.AddComponent<PlayerCharacter>();
            pc.Initialize(userId, name, isLocalPlayer, pos, dirY);

            m_characters.Add(userId, pc);
            Debug.Log($"[StageManager] Spawned character {go.name} at {pos}");
            return pc;
        }


        // 게임서버에 send
        private void sendCharacterCreateReq(string name, int jobId)
        {
            CharacterCreateReq req = new CharacterCreateReq();
            req.Name = name;
            req.JobId = jobId;

            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;

            net.Send(GamePacketId.CharacterCreateReq, req);

            Debug.Log($"[StageManager] CharacterListNtf: No Character. send CharacterCreateReq. name={req.Name}, job=Id{req.JobId}");

        }

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
