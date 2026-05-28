using Client.Network;
using Client.Packet;
using Common;
using DataStructures;
using GameData;
using GamePacket;
using MMO.Client.Navigation;
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
            PacketDispatcher.Instance.Register<StatUpdateNtf>(GamePacketId.StatUpdateNtf, onStatUpdateNtf);
            PacketDispatcher.Instance.Register<HpMpNtf>(GamePacketId.HpMpNtf, onHpMpNtf);

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

            // 새 Stage 의 NavMesh 로 갈아끼움.
            // 같은 stage 면 NavMeshService 가 no-op 처리. Game 씬 진입 시에는 GameScene.Init 에서
            // 먼저 로드되고, 이후 같은 게임서버 내 Stage 이동 시에는 이 파트가 갱신을 담당한다.
            loadNavMeshForStage(ntf.StageId);

            // 서버가 알려준 spawn 위치/회전으로 내 캐릭터 동기화
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

        // 서버가 계산한 합성 스탯 스냅샷(최대HP/MP, 이동속도, 공격속도, 힘 등)을 받아
        // 대상 캐릭터의 StatHolder 를 통째로 교체한다. (서버는 0 아닌 스탯만 보낸다)
        // 클라는 계산하지 않고 받은 값을 보관만 한다(정보창/HP바가 읽음).
        private void onStatUpdateNtf(StatUpdateNtf ntf)
        {
            if (!m_characters.TryGetValue(ntf.ObjectId, out PlayerCharacter character) || character == null)
            {
                Debug.LogWarning($"[StageManager] StatUpdateNtf: character not found. ObjectId={ntf.ObjectId}");
                return;
            }

            character.Stats.Clear();
            foreach (StatEntry entry in ntf.Entries)
            {
                character.Stats.Set((EStat)entry.Stat, entry.Value);
            }

            Debug.Log($"[StageManager] StatUpdateNtf: ObjectId={ntf.ObjectId}, count={ntf.Entries.Count}");
        }

        // 현재 HP/MP 갱신. 대미지/회복으로 자주 온다.
        // SetCurHp/SetCurMp 는 최대치(StatHolder 의 HpTotal/MpTotal)로 clamp 하므로,
        // 입장 시에는 StatUpdateNtf 가 먼저 도착해 최대치가 셋팅되어 있어야 한다(서버 송신 순서 규약).
        private void onHpMpNtf(HpMpNtf ntf)
        {
            if (!m_characters.TryGetValue(ntf.ObjectId, out PlayerCharacter character) || character == null)
            {
                Debug.LogWarning($"[StageManager] HpMpNtf: character not found. ObjectId={ntf.ObjectId}");
                return;
            }

            character.SetCurHp(ntf.CurHp);
            character.SetCurMp(ntf.CurMp);

            Debug.Log($"[StageManager] HpMpNtf: ObjectId={ntf.ObjectId}, hp={ntf.CurHp:F1}, mp={ntf.CurMp:F1}");
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

        // stageId 로 게임데이터를 조회해서 NavMesh 파일을 로드.
        // 게임데이터가 없거나 NavMeshFileName 이 비어있으면 경고만 남기고 진행
        // (PlayerCharacter 는 NavMesh 없으면 직선 이동으로 폴백함).
        // 같은 stage 가 이미 로드되어 있으면 NavMeshService.Load 가 no-op 처리.
        private static void loadNavMeshForStage(long stageId)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageId);
            if (stageData == null)
            {
                Debug.LogError($"[StageManager] Stage 게임데이터를 찾을 수 없습니다. stageId={stageId}");
                return;
            }

            string navMeshName = stageData.NavMeshFileName;
            if (string.IsNullOrEmpty(navMeshName))
            {
                Debug.LogWarning($"[StageManager] Stage 의 NavMeshFileName 이 비어있습니다. stageId={stageId} name={stageData.Name}");
                return;
            }

            if (!NavMeshService.Load(navMeshName))
            {
                Debug.LogError($"[StageManager] NavMesh 로드 실패. stageId={stageId} navMeshName={navMeshName}");
            }
        }
    }
}
