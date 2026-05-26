using Client.Network;
using Client.Packet;
using Common;
using DataStructures;
using GamePacket;
using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 게임 세계의 상태(내 캐릭터, 주변 캐릭터들)를 관리한다.
    //
    // 책임:
    //   - LocalPlayer 와 주변 캐릭터들의 GameObject 관리 (컬렉션)
    //   - StageEnterNtf, ObjectVisibilityNtf, MoveNtf, MovePosCorrectNtf 처리
    //   - 외부(CharacterSelector)에서 LocalPlayer 스폰 요청 받기
    //
    // 책임이 아닌 것:
    //   - 캐릭터 선택 흐름 (CharacterSelector 가 함)
    //   - 캐릭터 GameObject 생성 자체 (CharacterFactory 가 함)
    //   - 로그인/인증 (LoginSceneController 가 함)
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

            // 월드 오브젝트 관리 패킷만 등록.
            // 캐릭터 선택 관련 패킷 (CharacterListNtf, CharacterCreateRes) 은 CharacterSelector 가 담당.
            PacketDispatcher.Instance.Register<StageEnterNtf>(GamePacketId.StageEnterNtf, onStageEnterNtf);
            PacketDispatcher.Instance.Register<ObjectVisibilityNtf>(GamePacketId.ObjectVisibilityNtf, onObjectVisibilityNtf);
            PacketDispatcher.Instance.Register<MoveNtf>(GamePacketId.MoveNtf, onMoveNtf);
            PacketDispatcher.Instance.Register<MovePosCorrectNtf>(GamePacketId.MovePosCorrectNtf, onMovePosCorrectNtf);

            Debug.Log("[StageManager] Ready.");
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        // ─── 외부 API ──────────────────────────────────────────────────

        // CharacterSelector 가 호출. 선택된 캐릭터를 LocalPlayer 로 스폰.
        public PlayerCharacter SpawnLocalPlayer(long userId, string name, Vector3 pos, float dirY)
        {
            if (m_characters.TryGetValue(userId, out PlayerCharacter existing))
            {
                Debug.LogWarning($"[StageManager] LocalPlayer already exists. userId={userId}. Reusing.");
                existing.SetPosition(pos, dirY);
                LocalPlayer = existing;
                return existing;
            }

            PlayerCharacter pc = CharacterFactory.Create(userId, name, isLocalPlayer: true, pos, dirY);
            m_characters.Add(userId, pc);
            LocalPlayer = pc;
            return pc;
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        // 유저가 스테이지에 입장 성공
        private void onStageEnterNtf(StageEnterNtf ntf)
        {
            Debug.Log($"[StageManager] StageEnterNtf: stage={ntf.StageId}");

            // 위치/스탯이 최신화됐을 수 있으니 내 캐릭터 위치 동기화
            if (LocalPlayer != null)
            {
                LocalPlayer.SetPosition(new Vector3(ntf.MyPosX, ntf.MyPosY, ntf.MyPosZ), ntf.MyYaw);
            }

            CurrentStageId = ntf.StageId;
        }

        // 스테이지내의 오브젝트 스폰/제거 패킷
        private void onObjectVisibilityNtf(ObjectVisibilityNtf ntf)
        {
            long myCharacterId = 0;
            if (LocalPlayer != null)
                myCharacterId = LocalPlayer.UserId;

            // 캐릭터 스폰정보 처리
            foreach (CharacterSpawnInfo characterSpawnInfo in ntf.CharacterSpawns)
            {
                // 내 캐릭터는 무시
                if (characterSpawnInfo.ObjectId == myCharacterId)
                {
                    continue;
                }

                spawnRemoteCharacter(
                    userId: characterSpawnInfo.ObjectId,
                    name: characterSpawnInfo.Name,
                    pos: new Vector3(characterSpawnInfo.PosX, characterSpawnInfo.PosY, characterSpawnInfo.PosZ),
                    dirY: characterSpawnInfo.Yaw);

                Debug.Log($"[StageManager] ObjectVisibilityNtf: CharacterSpawn characterId={characterSpawnInfo.ObjectId}");
            }

            // 오브젝트 디스폰 정보 처리
            foreach (long despawnObjectId in ntf.DespawnIds)
            {
                if (m_characters.TryGetValue(despawnObjectId, out PlayerCharacter character) && character != null)
                {
                    m_characters.Remove(despawnObjectId);
                    Destroy(character.gameObject);
                }

                Debug.Log($"[StageManager] ObjectVisibilityNtf: Despawn objectId={despawnObjectId}");
            }
        }

        // 스테이지내의 오브젝트가 이동했을때 서버가 보내는 패킷.
        //
        // 다른 캐릭터의 위치 동기화 전략:
        //   - is_moving = true  : pos 무시, dest 로 SetMoveDestination 호출.
        //                         PlayerCharacter 가 자기 NavMesh 로 경로 계산해 자연스럽게 이동.
        //   - is_moving = false : 서버 pos/yaw 로 스냅.
        //   - 자기 캐릭터의 MoveNtf 는 무시 (자기 위치 보정은 MovePosCorrectNtf 가 담당).
        private void onMoveNtf(MoveNtf ntf)
        {
            if (LocalPlayer != null && LocalPlayer.UserId == ntf.ObjectId)
            {
                // 내 캐릭터 이동 패킷은 무시.
                return;
            }

            if (!m_characters.TryGetValue(ntf.ObjectId, out PlayerCharacter character) || character == null)
            {
                Debug.LogWarning($"[StageManager] MoveNtf: character not found. ObjectId={ntf.ObjectId}");
                return;
            }

            if (ntf.IsMoving)
            {
                character.SetMoveDestination(new Vector3(ntf.DestX, ntf.DestY, ntf.DestZ));
            }
            else
            {
                character.SetPosition(new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ), ntf.Yaw);
            }

            Debug.Log($"[StageManager] MoveNtf: ObjectId={ntf.ObjectId}, IsMoving={ntf.IsMoving}");
        }

        // 서버가 내 위치와 서버 위치의 오차가 허용 이상이라고 판단했을 때 보내는 패킷.
        // 내 캐릭터를 서버 위치로 즉시 스냅.
        private void onMovePosCorrectNtf(MovePosCorrectNtf ntf)
        {
            Debug.LogWarning($"[StageManager] MovePosCorrectNtf: pos=({ntf.PosX:F2},{ntf.PosY:F2},{ntf.PosZ:F2}) yaw={ntf.Yaw:F1}");

            if (LocalPlayer != null)
            {
                LocalPlayer.SetPosition(new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ), ntf.Yaw);
            }
        }

        // ─── 내부 ──────────────────────────────────────────────────────

        private PlayerCharacter spawnRemoteCharacter(long userId, string name, Vector3 pos, float dirY)
        {
            if (m_characters.TryGetValue(userId, out PlayerCharacter existing))
            {
                Debug.LogWarning($"[StageManager] character already exists. userId={userId}. Reusing.");
                existing.SetPosition(pos, dirY);
                return existing;
            }

            PlayerCharacter pc = CharacterFactory.Create(userId, name, isLocalPlayer: false, pos, dirY);
            m_characters.Add(userId, pc);
            return pc;
        }
    }
}
