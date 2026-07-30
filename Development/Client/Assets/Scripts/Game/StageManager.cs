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
    //   - StageLoadCompleteRes, StageMoveRes, ObjectVisibilityNtf, SnapshotNtf, MovePosCorrectNtf 처리
    //   - LocalPlayer 1회 생성(EnsureLocalPlayer) 후 스테이지 이동마다 숨김/재배치
    //
    // 책임이 아닌 것:
    //   - 캐릭터 선택 흐름 (CharacterSelector 가 함)
    //   - 캐릭터 GameObject 생성 자체 (CharacterFactory 가 함)
    //   - 로그인/인증 (LoginSceneController 가 함)
    public class StageManager : MonoBehaviour
    {
        public static StageManager Instance { get; private set; }

        // account_id -> PlayerCharacter
        private readonly Dictionary<long, PlayerCharacter> m_characters = new Dictionary<long, PlayerCharacter>();

        // object_id -> MonsterObject (몬스터 전용 레지스트리. 디스폰은 despawnObject 로 공용 처리)
        private readonly Dictionary<long, MonsterObject> m_monsters = new Dictionary<long, MonsterObject>();

        // object_id -> PropObject (prop 전용 레지스트리. 디스폰은 despawnObject 로 공용 처리, 상태변경은 PropStateNtf)
        private readonly Dictionary<long, PropObject> m_props = new Dictionary<long, PropObject>();

        // object_id -> 소유자에게만 전송된 필드 드롭 아이템.
        // 서버가 다른 캐릭터의 드롭을 패킷에 넣지 않으므로 이 레지스트리에는 내 드롭만 존재한다.
        private readonly Dictionary<long, DropObject> m_drops = new Dictionary<long, DropObject>();

        // 요청을 보낸 드롭의 재전송 허용 시각(Time.unscaledTime). 응답 유실/지연 중 매 frame 같은
        // ID를 보내지 않게 하며, 실패 응답도 짧은 cooldown 뒤 서버 상태를 다시 확인하도록 한다.
        private readonly Dictionary<long, float> m_dropPickupPendingUntil = new Dictionary<long, float>();

        // 습득 응답으로 변경된 Item 행의 최신 스냅샷. 현재는 UI가 없으므로 부분 캐시만 유지하며,
        // 추후 인벤토리 UI는 캐릭터 진입 시 전체 목록과 이 updated_items를 같은 모델에 반영해야 한다.
        private readonly Dictionary<long, DataStructures.Item> m_inventoryItems = new Dictionary<long, DataStructures.Item>();
        private float m_nextDropPickupScanTime;

        // 클라이언트 요청 반경은 서버 권위 반경(2m)보다 작게 두어 위치 동기화 오차를 흡수한다.
        private const float k_dropPickupClientRange = 1.5f;
        private const float k_dropPickupScanIntervalSec = 0.1f;
        private const float k_dropPickupRetrySec = 2f;
        private const int k_dropPickupMaxBatch = 32;

        // 내 캐릭터 (편의 접근). CharacterSelectRes 데이터모델로 1회 생성 후 영속.
        // 스테이지 이동 시 파괴하지 않고 숨김(SetActive false)+재배치만 한다.
        public PlayerCharacter LocalPlayer { get; private set; }
        // LocalPlayer 의 objectId (m_characters 의 키). despawn 시 LocalPlayer 제외 판정에 사용.
        private long m_localObjectId;

        public long CurrentStageId { get; private set; }

        // 현재 스테이지 맵 비주얼 인스턴스 (프리팹에서 생성). 스테이지 이동 시 파괴 후 재생성.
        private GameObject m_currentMapInstance;

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
            PacketDispatcher.Instance.Register<SnapshotNtf>(GamePacketId.SnapshotNtf, onSnapshotNtf);
            PacketDispatcher.Instance.Register<TimeSyncNtf>(GamePacketId.TimeSyncNtf, onTimeSyncNtf);
            PacketDispatcher.Instance.Register<MovePosCorrectNtf>(GamePacketId.MovePosCorrectNtf, onMovePosCorrectNtf);
            PacketDispatcher.Instance.Register<StatUpdateNtf>(GamePacketId.StatUpdateNtf, onStatUpdateNtf);
            PacketDispatcher.Instance.Register<HpMpNtf>(GamePacketId.HpMpNtf, onHpMpNtf);
            PacketDispatcher.Instance.Register<BuffNtf>(GamePacketId.BuffNtf, onBuffNtf);
            PacketDispatcher.Instance.Register<BuffRemoveNtf>(GamePacketId.BuffRemoveNtf, onBuffRemoveNtf);
            PacketDispatcher.Instance.Register<ObjectDeathNtf>(GamePacketId.ObjectDeathNtf, onObjectDeathNtf);
            PacketDispatcher.Instance.Register<ObjectReviveNtf>(GamePacketId.ObjectReviveNtf, onObjectReviveNtf);
            PacketDispatcher.Instance.Register<ActorActionNtf>(GamePacketId.ActorActionNtf, onActorActionNtf);
            PacketDispatcher.Instance.Register<StageNoticeNtf>(GamePacketId.StageNoticeNtf, onStageNoticeNtf);
            PacketDispatcher.Instance.Register<PropStateNtf>(GamePacketId.PropStateNtf, onPropStateNtf);
            PacketDispatcher.Instance.Register<ObjectInteractRes>(GamePacketId.ObjectInteractRes, onObjectInteractRes);
            PacketDispatcher.Instance.Register<ItemPickupRes>(GamePacketId.ItemPickupRes, onItemPickupRes);

            // 이벤트영역 진입/이탈 감지기(로컬 플레이어 위치로 매 프레임 판정 → Req 송신).
            gameObject.AddComponent<EventAreaDetector>();

            // prop 상호작용 감지기(근접 + Interact 키 → ObjectInteractReq).
            gameObject.AddComponent<PropInteractor>();

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
            // 로컬 캐릭터는 데이터모델의 직업/외형프리셋으로 prefab 분기.
            PlayerCharacter pc = CharacterFactory.Create(objectId, data.Name, isLocalPlayer: true, Vector3.zero, 0f, jobId: data.JobId, presetId: data.AppearancePresetId);
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
        public void BeginStageLoad(int stageDataKey)
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

            // 스테이지가 바뀌면 보간 재생 시계를 초기화 (이전 스테이지 서버시각이 섞이지 않도록).
            NetClock.Reset();

            // NavMesh 교체 (같은 스테이지면 NavMeshService 가 no-op).
            loadNavMeshForStage(stageDataKey);

            // 스테이지 맵 비주얼(프리팹) 교체.
            loadStageMap(stageDataKey);

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
        public void RequestStageMove(int stageDataKey, EStagePositionType positionType, int targetGameServerId = 0)
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

        // 현재 스폰된 캐릭터 목록 (LocalPlayer 포함. 미니맵 등 읽기 전용 뷰).
        // LocalPlayer 를 제외하려면 순회 시 IsLocalPlayer 또는 ObjectId 로 거른다.
        public IReadOnlyDictionary<long, PlayerCharacter> Characters => m_characters;

        // 현재 스테이지의 월드 X/Z 경계 (StageLoadCompleteRes 로 수신. 미니맵 좌표변환용).
        // 수신 전에는 안전 기본값(±2048)이다.
        public float WorldMinX => m_worldMinX;
        public float WorldMaxX => m_worldMaxX;
        public float WorldMinZ => m_worldMinZ;
        public float WorldMaxZ => m_worldMaxZ;

        // 현재 스테이지 맵 이름 (NavMeshFileName, 예: "SyntyForest"). 미니맵 텍스처 경로 결정용. 미로드 시 null.
        public string CurrentMapName { get; private set; }

        // pos 의 X-Z 반경 내 가장 가까운 캐릭터(플레이어)를 찾는다. 없으면 null.
        // 몬스터 투사체가 "플레이어에 닿으면" 비주얼을 종료하는 데 쓴다(플레이어엔 콜라이더가 없어 거리로 판정).
        public PlayerCharacter FindCharacterInRadiusXZ(Vector3 pos, float radius)
        {
            float bestSq = radius * radius;
            PlayerCharacter best = null;
            foreach (PlayerCharacter c in m_characters.Values)
            {
                if (c == null)
                    continue;
                Vector3 d = c.transform.position - pos;
                float dsq = d.x * d.x + d.z * d.z;   // X-Z 평면 거리
                if (dsq <= bestSq)
                {
                    bestSq = dsq;
                    best = c;
                }
            }
            return best;
        }

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

            // SnapshotNtf 위치 양자화 해제용 월드 X/Z 경계 저장 (서버 인코드와 동일 범위 → 정확 복원).
            m_worldMinX = res.WorldMinX;
            m_worldMinZ = res.WorldMinZ;
            m_worldMaxX = res.WorldMaxX;
            m_worldMaxZ = res.WorldMaxZ;

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
                myCharacterId = LocalPlayer.ObjectId;

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
                    objectId: characterSpawnInfo.ObjectId,
                    name: characterSpawnInfo.Name,
                    pos: new Vector3(characterSpawnInfo.PosX, characterSpawnInfo.PosY, characterSpawnInfo.PosZ),
                    dirY: characterSpawnInfo.Yaw,
                    jobId: characterSpawnInfo.JobId,
                    presetId: characterSpawnInfo.AppearancePresetId);

                if (remoteCharacter != null)
                {
                    // 원격 캐릭터의 HP/MP 는 spawn 정보로 설정한다. (관전자는 그 캐릭터의 StatUpdateNtf/HpMpNtf 를 받지 못한다.)
                    // 최대치(HpTotal/MpTotal)를 먼저 넣어야 SetCurHp/SetCurMp 의 clamp 가 올바르다.
                    remoteCharacter.Stats.Set(EStat.HpTotal, characterSpawnInfo.MaxHp);
                    remoteCharacter.SetCurHp(characterSpawnInfo.Hp);
                    remoteCharacter.Stats.Set(EStat.MpTotal, characterSpawnInfo.MaxMp);
                    remoteCharacter.SetCurMp(characterSpawnInfo.Mp);

                    applyBuffSnapshot(remoteCharacter.Buffs, characterSpawnInfo.Buffs);

                    // 사망 상태로 스폰(부활 대기 중 시야 진입): 사망 끝포즈로 고정 + IsDead 세팅. (몬스터 SpawnAsCorpse 와 동일)
                    if (characterSpawnInfo.IsDead)
                        remoteCharacter.SpawnAsCorpse();
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

            // prop 스폰정보 처리 (현재 상태 포함). prop 은 정적이라 위치 스냅샷을 받지 않는다.
            foreach (PropSpawnInfo propSpawnInfo in ntf.PropSpawns)
            {
                spawnProp(
                    objectId: propSpawnInfo.ObjectId,
                    propKey: propSpawnInfo.PropKey,
                    pos: new Vector3(propSpawnInfo.PosX, propSpawnInfo.PosY, propSpawnInfo.PosZ),
                    dirY: propSpawnInfo.Yaw,
                    state: propSpawnInfo.State);

                Debug.Log($"[StageManager] ObjectVisibilityNtf: PropSpawn objectId={propSpawnInfo.ObjectId} key={propSpawnInfo.PropKey} state={propSpawnInfo.State}");
            }

            // 드롭은 이동 Snapshot 대상이 아니다. origin/landing을 한 번 받아 로컬 비산 연출을
            // 재생하고, 이후 서버 despawn ID가 올 때까지 landing에 고정한다.
            foreach (DropSpawnInfo dropSpawnInfo in ntf.DropSpawns)
            {
                spawnDrop(
                    objectId: dropSpawnInfo.ObjectId,
                    itemKey: dropSpawnInfo.ItemKey,
                    count: dropSpawnInfo.Count,
                    origin: new Vector3(dropSpawnInfo.OriginX, dropSpawnInfo.OriginY, dropSpawnInfo.OriginZ),
                    landing: new Vector3(dropSpawnInfo.PosX, dropSpawnInfo.PosY, dropSpawnInfo.PosZ),
                    createdServerTimeMs: dropSpawnInfo.CreatedServerTimeMs);
            }

            // 오브젝트 디스폰 정보 처리 (캐릭터/몬스터/prop 공용)
            foreach (long despawnObjectId in ntf.DespawnIds)
            {
                despawnObject(despawnObjectId);

                Debug.Log($"[StageManager] ObjectVisibilityNtf: Despawn objectId={despawnObjectId}");
            }
        }

        // prop 상태 변경 알림. 해당 prop 을 찾아 상태값/비주얼을 갱신한다.
        // 대상이 없으면(아직 미스폰/이미 디스폰) 조용히 무시 (idempotent).
        private void onPropStateNtf(PropStateNtf ntf)
        {
            if (m_props.TryGetValue(ntf.ObjectId, out PropObject prop) && prop != null)
            {
                prop.SetState(ntf.State);
                Debug.Log($"[StageManager] PropStateNtf: objectId={ntf.ObjectId} state={ntf.State}");
            }
        }

        // prop 상호작용 결과
        // 성공 시 상태변화는 PropStateNtf, 포탈 이동은 StageMoveRes 로 각각 따로 온다.
        private void onObjectInteractRes(ObjectInteractRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                Debug.LogWarning($"[StageManager] ObjectInteractRes 실패: {res.ErrorMsg} (objectId={res.ObjectId})");
                return;
            }

            Debug.Log($"[StageManager] ObjectInteractRes OK. objectId={res.ObjectId}");
        }

        // batch 요청은 드롭마다 결과가 다를 수 있다. 성공 ID는 곧 별도 despawn 패킷으로 제거되고,
        // 실패 ID는 cooldown을 갱신해 일시적인 PENDING/DB 오류를 과도하게 재요청하지 않는다.
        // updated_items는 DB commit까지 끝난 행의 최종값만 담는다.
        private void onItemPickupRes(ItemPickupRes res)
        {
            foreach (ItemPickupResultEntry result in res.Results)
            {
                if (result.Result == EItemPickupResult.ItemPickupResultSuccess)
                    m_dropPickupPendingUntil.Remove(result.DropObjectId);
                else
                    m_dropPickupPendingUntil[result.DropObjectId] = Time.unscaledTime + k_dropPickupRetrySec;
            }

            foreach (DataStructures.Item item in res.UpdatedItems)
                m_inventoryItems[item.ItemId] = item.Clone();
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

        // 오브젝트 부활 알림 (자동 리스폰). 사망했던 액터를 부활 상태로 전환한다.
        // 서버가 사망 N초 후 부활 위치/자원을 결정해 보낸다. 클라는 받은 대로 적용만 한다.
        //   - IsDead 해제 + HP/MP 복원 (PlayerCharacter 는 Locomotion 애니 복귀, 입력 게이트 자동 재개)
        //   - 리스폰 위치 재배치 (플레이어는 SetPosition 으로 예측/화해 히스토리까지 리셋)
        private void onObjectReviveNtf(ObjectReviveNtf ntf)
        {
            ActorObject actor = FindActor(ntf.ObjectId);
            if (actor == null)
            {
                Debug.LogWarning($"[StageManager] ObjectReviveNtf: actor not found. ObjectId={ntf.ObjectId}");
                return;
            }

            actor.Revive(ntf.Hp, ntf.Mp);

            Vector3 pos = new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ);
            if (actor is PlayerCharacter pc)
                pc.SetPosition(pos, ntf.Yaw);
            else
                actor.transform.SetPositionAndRotation(pos, Quaternion.Euler(0f, ntf.Yaw, 0f));

            Debug.Log($"[StageManager] ObjectReviveNtf: ObjectId={ntf.ObjectId} pos={pos} hp={ntf.Hp}");
        }

        // 코스메틱 액션 알림 (점프/감정표현). 원격 캐릭터에 해당 모션을 재생한다. 연출 전용.
        // 내 액션(로컬)은 이미 입력 시점에 재생했으므로 무시(중복 방지). 액터가 몬스터/미존재면 무시.
        private void onActorActionNtf(ActorActionNtf ntf)
        {
            if (LocalPlayer != null && ntf.ActorObjectId == LocalPlayer.ObjectId)
                return;
            if (!(FindActor(ntf.ActorObjectId) is PlayerCharacter player))
                return;

            switch (ntf.ActionId)
            {
                case ActorAction.Jump:  player.PlayJump();          break;
                case ActorAction.Emote: player.PlayEmote(ntf.Param); break;
            }
        }

        // 서버 Stage 스크립트의 Notice() 가 보낸 화면 공지 배너.
        // 표시는 UI_StageNotice 가 전담(지연 생성). duration_ms=0 이면 기본 표시시간.
        private void onStageNoticeNtf(StageNoticeNtf ntf)
        {
            UI_StageNotice.Notify(ntf.Message, ntf.DurationMs);
        }

        // 서버 AOI 스냅샷 수신. 원격 액터(타 캐릭터/몬스터)는 보간 버퍼에 쌓고,
        // 본인 캐릭터 항목은 무시한다(자기 위치 화해는 Phase 2).
        // spawn 전인 object_id 의 상태는 조용히 버린다(다음 ObjectVisibilityNtf 후 따라온다).
        private void onSnapshotNtf(SnapshotNtf ntf)
        {
            // 시각 앵커의 권위 소스는 TimeSyncNtf(저빈도). 여기선 스냅샷이 흐를 때 더 잦은 앵커를 공짜로 추가한다
            // (NetClock 의 단조 가드로 중복/역순 호출은 안전). SnapshotNtf 가 0건이어도 시계는 TimeSyncNtf 로 유지된다.
            NetClock.OnServerTick(ntf.ServerTickSeq);
            double serverMs = ntf.ServerTickSeq * NetClock.ServerTickIntervalMs;

            long myObjectId = (LocalPlayer != null) ? LocalPlayer.ObjectId : 0;

            foreach (ActorStateInfo s in ntf.States)
            {
                // 양자화 해제 (계약은 move_packet.proto ActorStateInfo 주석 / 서버 인코드와 일치).
                DecodeActorState(s, out Vector3 pos, out float yaw);

                if (LocalPlayer != null && s.ObjectId == myObjectId)
                {
                    LocalPlayer.ReconcileTo(pos, yaw, serverMs, ntf.AckInputSeq);   // 본인: 예측↔서버 화해 (시간정렬)
                    continue;
                }

                bool moving = (s.Flags & 0x1u) != 0;

                if (m_characters.TryGetValue(s.ObjectId, out PlayerCharacter character) && character != null)
                    character.OnSnapshot(serverMs, pos, yaw, moving);
                else if (m_monsters.TryGetValue(s.ObjectId, out MonsterObject monster) && monster != null)
                    monster.OnSnapshot(serverMs, pos, yaw, moving);
            }
        }

        // NetClock 시각 앵커(권위 소스). SnapshotNtf 와 무관하게 저빈도로 항상 도착하므로,
        // 혼자 유휴(스냅샷 0건) 상태에서도 재생 시계가 초기화/전진한다.
        private void onTimeSyncNtf(TimeSyncNtf ntf)
        {
            NetClock.OnServerTick(ntf.ServerTickSeq);
        }

        // ── ActorStateInfo 양자화 해제 (계약은 move_packet.proto / 서버 인코드와 반드시 일치) ──
        // X/Z 범위는 StageLoadCompleteRes 로 받은 이 stage 의 월드 경계(서버 인코드와 동일). Y 는 고정 범위.
        private float m_worldMinX = -2048f, m_worldMinZ = -2048f;   // StageLoadCompleteRes 수신 전 안전 기본값.
        private float m_worldMaxX =  2048f, m_worldMaxZ =  2048f;
        private const float k_quantYMin = -512f, k_quantYMax = 512f;   // Y 16bit 고정 범위 (~1.6cm)

        private static float dequantUnit(ushort q, float mn, float mx)
        {
            return mn + (q / 65535f) * (mx - mn);
        }

        // qpos_xz / qpos_y_yaw 두 fixed32 에서 위치(Vector3)와 yaw(degree)를 복원한다.
        private void DecodeActorState(ActorStateInfo s, out Vector3 pos, out float yaw)
        {
            uint xz   = s.QposXz;
            uint yyaw = s.QposYYaw;
            float x = dequantUnit((ushort)(xz   >> 16),    m_worldMinX, m_worldMaxX);
            float z = dequantUnit((ushort)(xz   & 0xFFFF), m_worldMinZ, m_worldMaxZ);
            float y = dequantUnit((ushort)(yyaw >> 16),    k_quantYMin, k_quantYMax);
            yaw = ((ushort)(yyaw & 0xFFFF) / 65535f) * 360f;
            pos = new Vector3(x, y, z);
        }

        // 원격 보간 재생 시계를 매 프레임 전진시킨다.
        private void Update()
        {
            NetClock.Tick(Time.deltaTime);
            requestNearbyDropPickup();
        }

        // 착지한 내 드롭 중 가까운 항목을 찾아 최대 32개까지 한 패킷으로 요청한다.
        // 이 검사는 네트워크 요청 최적화와 연출 보호 목적이며 보안 판정은 서버가 다시 수행한다.
        private void requestNearbyDropPickup()
        {
            // Update마다 전체 dictionary를 순회하지 않도록 10Hz로 제한한다. 게임 timeScale과
            // 무관하게 네트워크 cooldown이 흐르도록 unscaledTime을 사용한다.
            if (Time.unscaledTime < m_nextDropPickupScanTime)
                return;
            m_nextDropPickupScanTime = Time.unscaledTime + k_dropPickupScanIntervalSec;

            NetworkManager net = NetworkManager.Instance;
            if (IsStageLoading || LocalPlayer == null || !LocalPlayer.gameObject.activeInHierarchy ||
                net == null || !net.IsConnected)
                return;

            float rangeSq = k_dropPickupClientRange * k_dropPickupClientRange;
            Vector3 playerPos = LocalPlayer.transform.position;
            ItemPickupReq req = new ItemPickupReq();

            foreach (KeyValuePair<long, DropObject> kv in m_drops)
            {
                DropObject drop = kv.Value;
                // 비산 중 transform이 플레이어 근처를 지나더라도 실제 착지 전에는 요청하지 않는다.
                if (drop == null || !drop.IsLanded)
                    continue;
                if (m_dropPickupPendingUntil.TryGetValue(kv.Key, out float pendingUntil) &&
                    Time.unscaledTime < pendingUntil)
                    continue;

                // 서버와 동일하게 높이(Y)는 제외하고 XZ 평면 거리만 사용한다.
                Vector3 delta = drop.transform.position - playerPos;
                if (delta.x * delta.x + delta.z * delta.z > rangeSq)
                    continue;

                // 전송 전에 pending을 기록해야 다음 scan이 응답보다 먼저 돌아도 중복 요청하지 않는다.
                req.DropObjectIds.Add(kv.Key);
                m_dropPickupPendingUntil[kv.Key] = Time.unscaledTime + k_dropPickupRetrySec;
                if (req.DropObjectIds.Count >= k_dropPickupMaxBatch)
                    break;
            }

            if (req.DropObjectIds.Count > 0)
                net.Send(GamePacketId.ItemPickupReq, req);
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
        // 캐릭터는 objectId(=characterId)를 키로 m_characters 에 저장되므로 바로 찾는다.
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
        private static void showSectorGridDebug(int stageDataKey, float groundY)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageDataKey);
            if (stageData == null)
            {
                Debug.LogWarning($"[StageManager] sector grid: Stage 게임데이터를 찾을 수 없습니다. stageDataKey={stageDataKey}");
                return;
            }
            SectorGridDebug.ShowForStage(stageData.NavMeshFileName, stageData.sectorSize, groundY);
        }

        private PlayerCharacter spawnRemoteCharacter(long objectId, string name, Vector3 pos, float dirY, int jobId, int presetId)
        {
            if (m_characters.TryGetValue(objectId, out PlayerCharacter existing))
            {
                // 이미 존재(원격은 보간 중)하면 위치를 직접 덮어쓰지 않는다 — 스냅샷 보간과 충돌해 깜빡임/snap 유발.
                // 중복 spawn 은 위치 갱신 없이 무시한다(위치는 SnapshotNtf 가 담당).
                Debug.LogWarning($"[StageManager] character already exists. objectId={objectId}. Reusing (pos untouched).");
                return existing;
            }

            PlayerCharacter pc = CharacterFactory.Create(objectId, name, isLocalPlayer: false, pos, dirY, jobId: jobId, presetId: presetId);
            m_characters.Add(objectId, pc);
            return pc;
        }

        // 몬스터 스폰. 게임데이터 Key 로 MonsterFactory 가 prefab 을 찾아 생성한다.
        // 이미 같은 objectId 가 있으면 위치만 갱신 (idempotent).
        private MonsterObject spawnMonster(long objectId, int monsterKey, Vector3 pos, float dirY, bool isDead, double curHp, double maxHp)
        {
            if (m_monsters.TryGetValue(objectId, out MonsterObject existing) && existing != null)
            {
                // 이미 존재(보간 중)하면 위치를 직접 덮어쓰지 않는다 — 스냅샷 보간과 충돌. 중복 spawn 무시.
                return existing;
            }

            MonsterObject mo = MonsterFactory.Create(objectId, monsterKey, pos, dirY, isDead, curHp, maxHp);
            if (mo != null)
                m_monsters.Add(objectId, mo);
            return mo;
        }

        // prop 스폰. 게임데이터 Key 로 PropFactory 가 prefab 을 찾아 생성한다.
        // 이미 같은 objectId 가 있으면 중복 spawn 무시 (idempotent).
        private PropObject spawnProp(long objectId, int propKey, Vector3 pos, float dirY, int state)
        {
            if (m_props.TryGetValue(objectId, out PropObject existing) && existing != null)
                return existing;

            PropObject po = PropFactory.Create(objectId, propKey, pos, dirY, state);
            if (po != null)
                m_props.Add(objectId, po);
            return po;
        }

        // AOI 중복 spawn에도 안전하도록 objectId 기준 idempotent 생성으로 처리한다.
        // createdServerTimeMs는 늦은 AOI 진입 시 남은 비산 시간 계산에 사용한다.
        private DropObject spawnDrop(long objectId, int itemKey, int count, Vector3 origin, Vector3 landing,
            long createdServerTimeMs)
        {
            if (m_drops.TryGetValue(objectId, out DropObject existing) && existing != null)
                return existing;

            DropObject drop = DropFactory.Create(objectId, itemKey, count, origin, landing, createdServerTimeMs);
            if (drop != null)
                m_drops.Add(objectId, drop);
            return drop;
        }

        // 로컬 플레이어 위치(pos)에서 maxRange(평면 X-Z) 안의 가장 가까운 prop 을 찾는다(없으면 null).
        // PropInteractor 의 상호작용 대상 선택용. 실제 사거리 검증은 서버 권위.
        public PropObject FindNearestProp(Vector3 pos, float maxRange)
        {
            PropObject best = null;
            float bestSq = maxRange * maxRange;
            foreach (PropObject po in m_props.Values)
            {
                if (po == null)
                    continue;
                Vector3 c = po.transform.position;
                float dx = pos.x - c.x;
                float dz = pos.z - c.z;
                float dsq = dx * dx + dz * dz;
                if (dsq <= bestSq)
                {
                    bestSq = dsq;
                    best = po;
                }
            }
            return best;
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

            foreach (PropObject prop in m_props.Values)
            {
                if (prop != null)
                    Destroy(prop.gameObject);
            }
            m_props.Clear();

            foreach (DropObject drop in m_drops.Values)
            {
                if (drop != null)
                    Destroy(drop.gameObject);
            }
            m_drops.Clear();
            m_dropPickupPendingUntil.Clear();
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

            if (m_props.TryGetValue(objectId, out PropObject prop) && prop != null)
            {
                m_props.Remove(objectId);
                Destroy(prop.gameObject);
                return;
            }


            if (m_drops.TryGetValue(objectId, out DropObject drop) && drop != null)
            {
                m_drops.Remove(objectId);
                m_dropPickupPendingUntil.Remove(objectId);
                Destroy(drop.gameObject);
            }
        }

        // stageId 로 게임데이터를 조회해서 NavMesh 파일을 로드.
        // 게임데이터가 없거나 NavMeshFileName 이 비어있으면 경고만 남기고 진행
        // (PlayerCharacter 는 NavMesh 없으면 직선 이동으로 폴백함).
        // 같은 stage 가 이미 로드되어 있으면 NavMeshService.Load 가 no-op 처리.
        private static void loadNavMeshForStage(int stageDataKey)
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

        // 스테이지 맵 비주얼(프리팹)을 교체한다.
        // 프리팹 경로 규약: Resources/Stages/{NavMeshFileName}  (예: Stages/Town, Stages/Field1)
        // 프리팹이 아직 없으면 경고만 남기고 현재 씬 지오메트리를 그대로 둔다(no-op).
        //   → 스테이지별 맵 프리팹을 만들어 Resources/Stages/ 에 넣으면 자동으로 적용된다.
        //   (그 시점엔 Game 씬에 고정 배치된 정적 맵 지오메트리를 제거해야 중복되지 않는다.)
        private void loadStageMap(int stageDataKey)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageDataKey);
            if (stageData == null || string.IsNullOrEmpty(stageData.NavMeshFileName))
                return;

            // 미니맵 텍스처 경로 결정에 쓰이는 맵 이름 (프리팹 유무와 무관하게 갱신).
            CurrentMapName = stageData.NavMeshFileName;

            GameObject prefab = Resources.Load<GameObject>(stageData.StagePrefabPath);
            if (prefab == null)
            {
                Debug.LogWarning($"[StageManager] 스테이지 맵 프리팹 없음: Resources/{stageData.StagePrefabPath} (현재 씬 지오메트리 유지)");
                return;
            }

            // 이전 맵 인스턴스 파괴 후 새 맵 생성.
            if (m_currentMapInstance != null)
                Destroy(m_currentMapInstance);

            m_currentMapInstance = Instantiate(prefab);
            m_currentMapInstance.name = prefab.name;

            assignMapPhysicsLayers(m_currentMapInstance);

            Debug.Log($"[StageManager] 스테이지 맵 교체: {stageData.StagePrefabPath}");
        }

        // 인스턴스화된 맵의 물리 레이어를 규약(GameLayers)에 맞게 지정한다.
        // 맵 프리팹은 아트/절차생성 산출물이라 레이어가 Default 로 오는데, 여기서 런타임에 일괄 지정하면
        // 모든 스테이지에 유지보수 없이 자동 적용되고 맵을 재생성해도 안전하다.
        //   - Terrain(지형) → Ground : 걷는 바닥 + 둔덕(벽)이 한 콜라이더라 통째로 Ground.
        //   - 그 외 solid(비트리거) 콜라이더 → Obstacle : 바위/돌담/기둥 등 정적 차단물.
        //   - 트리거 콜라이더(수면/이벤트영역 등)는 건드리지 않는다.
        // 투사체는 Ground|Obstacle 을 레이캐스트해 벽에서 소멸한다(Projectile.cs).
        private static void assignMapPhysicsLayers(GameObject mapRoot)
        {
            Terrain[] terrains = mapRoot.GetComponentsInChildren<Terrain>(true);
            foreach (Terrain t in terrains)
                t.gameObject.layer = GameLayers.Ground;

            int obstacleCount = 0;
            Collider[] colliders = mapRoot.GetComponentsInChildren<Collider>(true);
            foreach (Collider c in colliders)
            {
                if (c.isTrigger || c is TerrainCollider)
                    continue;   // 트리거(수면 등)·지형(위에서 Ground)은 제외.
                c.gameObject.layer = GameLayers.Obstacle;
                ++obstacleCount;
            }

            Debug.Log($"[StageManager] 맵 물리 레이어 지정: Terrain {terrains.Length}개→Ground, Obstacle {obstacleCount}개");
        }
    }
}
