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
    //   - LocalPlayer(영속) 와 주변 캐릭터들의 GameObject 관리 (컬렉션)
    //   - StageLoadCompleteRes, StageMoveRes, ObjectVisibilityNtf, MoveNtf, MovePosCorrectNtf 처리
    //   - LocalPlayer 1회 생성(EnsureLocalPlayer) 후 스테이지 이동마다 숨김/재배치
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

        // object_id -> MonsterObject (몬스터 전용 레지스트리. 디스폰은 despawnObject 로 공용 처리)
        private readonly Dictionary<long, MonsterObject> m_monsters = new Dictionary<long, MonsterObject>();

        // 내 캐릭터 (편의 접근). CharacterSelectRes 데이터모델로 1회 생성 후 영속.
        // 스테이지 이동 시 파괴하지 않고 숨김(SetActive false)+재배치만 한다.
        public PlayerCharacter LocalPlayer { get; private set; }
        // LocalPlayer 의 objectId (m_characters 의 키). despawn 시 LocalPlayer 제외 판정에 사용.
        private long m_localObjectId;

        public long CurrentStageId { get; private set; }

        // 스테이지 전환 중 여부. BeginStageLoad ~ StageLoadCompleteRes 사이 true.
        // true 동안 LocalPlayer 는 비활성(숨김+조작불가) 상태이고, 클라는 스테이지를 진행하지 않는다.
        public bool IsStageLoading { get; private set; }

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
            PacketDispatcher.Instance.Register<StageLoadCompleteRes>(GamePacketId.StageLoadCompleteRes, onStageLoadCompleteRes);
            PacketDispatcher.Instance.Register<StageMoveRes>(GamePacketId.StageMoveRes, onStageMoveRes);
            PacketDispatcher.Instance.Register<ObjectVisibilityNtf>(GamePacketId.ObjectVisibilityNtf, onObjectVisibilityNtf);
            PacketDispatcher.Instance.Register<MoveNtf>(GamePacketId.MoveNtf, onMoveNtf);
            PacketDispatcher.Instance.Register<MovePosCorrectNtf>(GamePacketId.MovePosCorrectNtf, onMovePosCorrectNtf);
            PacketDispatcher.Instance.Register<StatUpdateNtf>(GamePacketId.StatUpdateNtf, onStatUpdateNtf);
            PacketDispatcher.Instance.Register<HpMpNtf>(GamePacketId.HpMpNtf, onHpMpNtf);
            PacketDispatcher.Instance.Register<BuffNtf>(GamePacketId.BuffNtf, onBuffNtf);
            PacketDispatcher.Instance.Register<BuffRemoveNtf>(GamePacketId.BuffRemoveNtf, onBuffRemoveNtf);
            PacketDispatcher.Instance.Register<ObjectDeathNtf>(GamePacketId.ObjectDeathNtf, onObjectDeathNtf);

            Debug.Log("[StageManager] Ready.");
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        // ─── 외부 API ──────────────────────────────────────────────────

        // 데이터모델로 LocalPlayer GameObject 를 1회 생성한다 (비활성/조작불가 상태).
        // GameScene.Init 에서 입장 시 1회 호출. 이미 있으면 no-op.
        // 위치는 비워둔다 (StageLoadCompleteRes 수신 시 서버가 준 좌표로 배치).
        public void EnsureLocalPlayer(DataStructures.Character data)
        {
            if (LocalPlayer != null)
                return;

            long objectId = data.CharacterId;
            PlayerCharacter pc = CharacterFactory.Create(objectId, data.Name, isLocalPlayer: true, Vector3.zero, 0f);
            if (pc == null)
            {
                Debug.LogError("[StageManager] EnsureLocalPlayer: LocalPlayer 생성 실패.");
                return;
            }

            // 스폰 전까지 비활성 (씬에 보이지 않고 조작/이동도 동작하지 않음).
            pc.gameObject.SetActive(false);

            m_characters[objectId] = pc;
            m_localObjectId = objectId;
            LocalPlayer = pc;

            Debug.Log($"[StageManager] EnsureLocalPlayer: created (inactive). objectId={objectId} name={data.Name}");
        }

        // 스테이지 로딩 시작 (2단계 입장의 클라 측 절차).
        // LocalPlayer 숨김 → 원격 오브젝트 제거 → NavMesh 교체 → 서버에 로딩 완료 보고.
        // 호출 경로: 최초 입장(GameScene.Init) / 스테이지 이동(onStageMoveRes 성공).
        // 이후 서버가 캐릭터를 스폰하고 StageLoadCompleteRes 를 보내면 LocalPlayer 를 활성화/배치한다.
        public void BeginStageLoad(long stageDataKey)
        {
            Debug.Log($"[StageManager] BeginStageLoad: stageDataKey={stageDataKey}");

            IsStageLoading = true;

            // LocalPlayer 는 파괴하지 않고 숨김 (조작/이동 정지). 데이터모델은 그대로 유지된다.
            if (LocalPlayer != null)
            {
                LocalPlayer.StopMove();
                LocalPlayer.gameObject.SetActive(false);
            }

            // 원격 오브젝트(타 캐릭터/몬스터)만 제거. LocalPlayer 는 위에서 숨김 처리.
            despawnRemoteObjects();

            // NavMesh 교체 (같은 스테이지면 NavMeshService 가 no-op).
            // 맵 프리팹 교체는 스테이지별 맵 리소스가 생기면 여기에 추가된다.
            loadNavMeshForStage(stageDataKey);

            // 로딩 완료 보고 → 서버가 입장 처리 후 캐릭터 스폰 + StageLoadCompleteRes.
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
            {
                Debug.LogError("[StageManager] NetworkManager 가 연결되지 않음. StageLoadCompleteReq 송신 실패");
                return;
            }
            net.Send(GamePacketId.StageLoadCompleteReq, new StageLoadCompleteReq());
        }

        // 스테이지 이동 요청 (치트/UI 가 호출). 서버가 StageMoveRes 로 응답한다.
        public void RequestStageMove(long stageDataKey, EStagePositionType positionType, int targetGameServerId = 0)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
            {
                Debug.LogError("[StageManager] NetworkManager 가 연결되지 않음. StageMoveReq 송신 실패");
                return;
            }

            StageMoveReq req = new StageMoveReq
            {
                TargetStageDataKey = stageDataKey,
                PositionType = (int)positionType,
                TargetGameServerId = targetGameServerId,
            };
            net.Send(GamePacketId.StageMoveReq, req);

            Debug.Log($"[StageManager] Sent StageMoveReq: stageDataKey={stageDataKey} positionType={positionType}");
        }

        // object_id 로 액터(캐릭터/몬스터)를 찾는다. 없으면 null. (SkillSystem 의 데미지 라우팅용)
        public ActorObject FindActor(long objectId)
        {
            if (m_characters.TryGetValue(objectId, out PlayerCharacter c) && c != null)
                return c;
            if (m_monsters.TryGetValue(objectId, out MonsterObject m) && m != null)
                return m;
            return null;
        }

        // 현재 스폰된 몬스터 목록 (오토타게팅용 읽기 전용 뷰).
        public IReadOnlyDictionary<long, MonsterObject> Monsters => m_monsters;

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        // 스테이지 이동 요청 결과. 성공이면 로딩 시작 (2단계 입장).
        private void onStageMoveRes(StageMoveRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                Debug.LogError($"[StageManager] StageMoveRes 실패: {res.ErrorMsg} (code={res.ResultCode})");
                return;
            }

            Debug.Log($"[StageManager] StageMoveRes OK. targetStageDataKey={res.TargetStageDataKey}");
            BeginStageLoad(res.TargetStageDataKey);
        }

        // 입장 처리 완료 + 캐릭터 스폰 확정 (서버가 StageLoadCompleteReq 처리 후 송신).
        // 보관 중인 LocalPlayer 를 서버가 준 좌표에 배치하고 활성화/조작가능 상태로 전환한다.
        private void onStageLoadCompleteRes(StageLoadCompleteRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                Debug.LogError($"[StageManager] StageLoadCompleteRes 실패: {res.ErrorMsg} (code={res.ResultCode})");
                return;
            }

            Debug.Log($"[StageManager] StageLoadCompleteRes OK. stage={res.StageId}");

            if (LocalPlayer == null)
            {
                Debug.LogError("[StageManager] StageLoadCompleteRes: LocalPlayer 가 없습니다. EnsureLocalPlayer 가 호출되지 않았습니까?");
                return;
            }

            // NavMesh 안전망. 정상 흐름이면 BeginStageLoad 에서 이미 로드됨 (no-op).
            loadNavMeshForStage(res.StageDataKey);

            // 보관 중인 LocalPlayer 를 활성화 + 서버가 준 좌표로 배치.
            Vector3 spawnPos = new Vector3(res.MyPosX, res.MyPosY, res.MyPosZ);
            LocalPlayer.gameObject.SetActive(true);
            LocalPlayer.SetPosition(spawnPos, res.MyYaw);

            CurrentStageId = res.StageId;
            IsStageLoading = false;

            // 디버그용 sector 격자 표시. sectorSize 는 stage_data_key 로 GameData_Stage 를
            // 조회해 얻고, 격자 원점/범위는 NavMesh 메타 bounds(서버 worldMin/Max 와 동일 소스)를 쓴다.
            showSectorGridDebug(res.StageDataKey, res.MyPosY);
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
                // 내 캐릭터는 다시 스폰하지 않지만, 보유 버프 스냅샷은 반영한다.
                if (characterSpawnInfo.ObjectId == myCharacterId)
                {
                    if (LocalPlayer != null)
                        applyBuffSnapshot(LocalPlayer.Buffs, characterSpawnInfo.Buffs);
                    continue;
                }

                PlayerCharacter remoteCharacter = spawnRemoteCharacter(
                    userId: characterSpawnInfo.ObjectId,
                    name: characterSpawnInfo.Name,
                    pos: new Vector3(characterSpawnInfo.PosX, characterSpawnInfo.PosY, characterSpawnInfo.PosZ),
                    dirY: characterSpawnInfo.Yaw);

                if (remoteCharacter != null)
                {
                    // 원격 캐릭터의 HP/MP 는 spawn 정보로 설정한다. (관전자는 그 캐릭터의 StatUpdateNtf/HpMpNtf 를 받지 못한다.)
                    // 최대치(HpTotal/MpTotal)를 먼저 넣어야 SetCurHp/SetCurMp 의 clamp 가 올바르다.
                    remoteCharacter.Stats.Set(EStat.HpTotal, characterSpawnInfo.MaxHp);
                    remoteCharacter.SetCurHp(characterSpawnInfo.Hp);
                    remoteCharacter.Stats.Set(EStat.MpTotal, characterSpawnInfo.MaxMp);
                    remoteCharacter.SetCurMp(characterSpawnInfo.Mp);

                    applyBuffSnapshot(remoteCharacter.Buffs, characterSpawnInfo.Buffs);
                }

                Debug.Log($"[StageManager] ObjectVisibilityNtf: CharacterSpawn characterId={characterSpawnInfo.ObjectId}");
            }

            // 몬스터 스폰정보 처리
            foreach (MonsterSpawnInfo monsterSpawnInfo in ntf.MonsterSpawns)
            {
                MonsterObject monster = spawnMonster(
                    objectId: monsterSpawnInfo.ObjectId,
                    monsterKey: monsterSpawnInfo.MonsterKey,
                    pos: new Vector3(monsterSpawnInfo.PosX, monsterSpawnInfo.PosY, monsterSpawnInfo.PosZ),
                    dirY: monsterSpawnInfo.Yaw,
                    isDead: monsterSpawnInfo.IsDead,
                    curHp: monsterSpawnInfo.CurHp,
                    maxHp: monsterSpawnInfo.MaxHp);

                if (monster != null)
                    applyBuffSnapshot(monster.Buffs, monsterSpawnInfo.Buffs);

                Debug.Log($"[StageManager] ObjectVisibilityNtf: MonsterSpawn objectId={monsterSpawnInfo.ObjectId} key={monsterSpawnInfo.MonsterKey}");
            }

            // 오브젝트 디스폰 정보 처리 (캐릭터/몬스터 공용)
            foreach (long despawnObjectId in ntf.DespawnIds)
            {
                despawnObject(despawnObjectId);

                Debug.Log($"[StageManager] ObjectVisibilityNtf: Despawn objectId={despawnObjectId}");
            }
        }

        // 오브젝트 사망 알림. 해당 액터를 사망 상태로 전환한다(이동 정지 + 사망 애니메이션).
        // 디스폰(시체 제거)은 corpse 시간 후 ObjectVisibilityNtf 의 Despawn 이 별도로 처리한다.
        private void onObjectDeathNtf(ObjectDeathNtf ntf)
        {
            ActorObject actor = FindActor(ntf.ObjectId);
            if (actor == null)
            {
                Debug.LogWarning($"[StageManager] ObjectDeathNtf: actor not found. ObjectId={ntf.ObjectId}");
                return;
            }

            actor.OnDeath();
            Debug.Log($"[StageManager] ObjectDeathNtf: ObjectId={ntf.ObjectId} killer={ntf.KillerObjectId}");
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

            // 캐릭터 우선 조회. 없으면 몬스터로 라우팅. (둘 다 object_id 기반, 동일 처리)
            if (m_characters.TryGetValue(ntf.ObjectId, out PlayerCharacter character) && character != null)
            {
                if (ntf.IsMoving)
                    character.SetMoveDestination(new Vector3(ntf.DestX, ntf.DestY, ntf.DestZ));
                else
                    character.SetPosition(new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ), ntf.Yaw);
                return;
            }

            if (m_monsters.TryGetValue(ntf.ObjectId, out MonsterObject monster) && monster != null)
            {
                if (ntf.IsMoving)
                    monster.SetMoveDestination(new Vector3(ntf.DestX, ntf.DestY, ntf.DestZ));
                else
                    monster.SetPosition(new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ), ntf.Yaw);
                return;
            }

            Debug.LogWarning($"[StageManager] MoveNtf: object not found. ObjectId={ntf.ObjectId}");
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

        // 버프 추가/갱신 (upsert). object_id 로 캐릭터/몬스터를 찾아 BuffHolder 를 갱신.
        // 스탯/HP 효과는 StatUpdateNtf/HpMpNtf 로 따로 오므로 여기서는 뱃지 정보만 보관한다.
        private void onBuffNtf(BuffNtf ntf)
        {
            BuffHolder holder = findBuffHolder(ntf.ObjectId);
            if (holder == null)
            {
                Debug.LogWarning($"[StageManager] BuffNtf: object not found. ObjectId={ntf.ObjectId} buffKey={ntf.BuffKey}");
                return;
            }

            holder.Upsert(ntf.BuffKey, ntf.StackCount, ntf.RemainTimeMs);
        }

        // 버프 제거 (만료/디스펠/정리). 이미 디스폰된 객체면 조용히 무시.
        private void onBuffRemoveNtf(BuffRemoveNtf ntf)
        {
            BuffHolder holder = findBuffHolder(ntf.ObjectId);
            if (holder == null)
                return;

            holder.Remove(ntf.BuffKey);
        }

        // ─── 내부 ──────────────────────────────────────────────────────

        // object_id 로 캐릭터/몬스터의 BuffHolder 를 찾는다. 없으면 null.
        // (현재 character_id == object_id == userId 체계라 캐릭터는 m_characters 에서 바로 찾힌다.)
        private BuffHolder findBuffHolder(long objectId)
        {
            if (m_characters.TryGetValue(objectId, out PlayerCharacter character) && character != null)
                return character.Buffs;
            if (m_monsters.TryGetValue(objectId, out MonsterObject monster) && monster != null)
                return monster.Buffs;
            return null;
        }

        // spawn 스냅샷의 버프 목록을 holder 에 통째로 반영(전체 교체).
        private static void applyBuffSnapshot(BuffHolder holder, IEnumerable<BuffSnapshotInfo> buffs)
        {
            holder.Clear();
            foreach (BuffSnapshotInfo b in buffs)
                holder.Upsert(b.BuffKey, b.StackCount, b.RemainTimeMs);
        }

        // 디버그: 서버 Stage 의 sector 격자를 화면에 그린다.
        // sectorSize 는 stage_data_key 로 GameData_Stage 를 조회해서 얻는다.
        private static void showSectorGridDebug(long stageDataKey, float groundY)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageDataKey);
            if (stageData == null)
            {
                Debug.LogWarning($"[StageManager] sector grid: Stage 게임데이터를 찾을 수 없습니다. stageDataKey={stageDataKey}");
                return;
            }
            SectorGridDebug.ShowForStage(stageData.NavMeshFileName, stageData.sectorSize, groundY);
        }

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

        // 몬스터 스폰. 게임데이터 Key 로 MonsterFactory 가 prefab 을 찾아 생성한다.
        // 이미 같은 objectId 가 있으면 위치만 갱신 (idempotent).
        private MonsterObject spawnMonster(long objectId, long monsterKey, Vector3 pos, float dirY, bool isDead, double curHp, double maxHp)
        {
            if (m_monsters.TryGetValue(objectId, out MonsterObject existing) && existing != null)
            {
                existing.transform.position = pos;
                existing.transform.rotation = Quaternion.Euler(0f, dirY, 0f);
                return existing;
            }

            MonsterObject mo = MonsterFactory.Create(objectId, monsterKey, pos, dirY, isDead, curHp, maxHp);
            if (mo != null)
                m_monsters.Add(objectId, mo);
            return mo;
        }

        // 원격 오브젝트(타 캐릭터/몬스터)만 제거. LocalPlayer 는 보존(영속). 스테이지 로딩 시작 시 호출.
        private void despawnRemoteObjects()
        {
            // m_characters 에서 LocalPlayer(m_localObjectId)만 남기고 나머지 파괴.
            List<long> removeIds = new List<long>();
            foreach (KeyValuePair<long, PlayerCharacter> kv in m_characters)
            {
                if (kv.Key == m_localObjectId)
                    continue;
                if (kv.Value != null)
                    Destroy(kv.Value.gameObject);
                removeIds.Add(kv.Key);
            }
            foreach (long id in removeIds)
                m_characters.Remove(id);

            foreach (MonsterObject monster in m_monsters.Values)
            {
                if (monster != null)
                    Destroy(monster.gameObject);
            }
            m_monsters.Clear();
        }

        // 오브젝트 공용 디스폰. objectId 로 캐릭터/몬스터 어느 쪽이든 찾아 제거한다.
        // 양쪽 어디에도 없으면 (이미 제거됨/모르는 객체) 조용히 무시 (idempotent).
        private void despawnObject(long objectId)
        {
            if (m_characters.TryGetValue(objectId, out PlayerCharacter character) && character != null)
            {
                m_characters.Remove(objectId);
                Destroy(character.gameObject);
                return;
            }

            if (m_monsters.TryGetValue(objectId, out MonsterObject monster) && monster != null)
            {
                m_monsters.Remove(objectId);
                Destroy(monster.gameObject);
                return;
            }
        }

        // stageId 로 게임데이터를 조회해서 NavMesh 파일을 로드.
        // 게임데이터가 없거나 NavMeshFileName 이 비어있으면 경고만 남기고 진행
        // (PlayerCharacter 는 NavMesh 없으면 직선 이동으로 폴백함).
        // 같은 stage 가 이미 로드되어 있으면 NavMeshService.Load 가 no-op 처리.
        private static void loadNavMeshForStage(long stageDataKey)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageDataKey);
            if (stageData == null)
            {
                Debug.LogError($"[StageManager] Stage 게임데이터를 찾을 수 없습니다. stageDataKey={stageDataKey}");
                return;
            }

            string navMeshName = stageData.NavMeshFileName;
            if (string.IsNullOrEmpty(navMeshName))
            {
                Debug.LogWarning($"[StageManager] Stage 의 NavMeshFileName 이 비어있습니다. stageDataKey={stageDataKey} name={stageData.Name}");
                return;
            }

            if (!NavMeshService.Load(navMeshName))
            {
                Debug.LogError($"[StageManager] NavMesh 로드 실패. stageDataKey={stageDataKey} navMeshName={navMeshName}");
            }
        }
    }
}
