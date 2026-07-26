using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading.Tasks;
using DummyClient.Config;
using DummyClient.Metrics;
using DummyClient.Nav;
using DummyClient.Net;
using GamePacket;
using PId = Common.GamePacketId;

namespace DummyClient.Sim
{
    // 봇 1개. 중앙 틱 루프(BotManager)가 Tick()을 주기 호출한다.
    // 네트워크는 비동기(BotConnection)로 돌고, 수신 패킷은 Tick 에서 드레인/처리한다.
    public sealed class Bot
    {
        private const int ResultSuccess = 1; // EResultCode.Success
        private const int TownStageKey = 100;

        private readonly BotContext m_ctx;
        public int Index { get; }
        public string LoginId { get; }
        private string Password => LoginId; // 비밀번호 = 아이디

        private BotConnection m_conn;
        private Task<bool> m_connectTask;

        public BotState State { get; private set; } = BotState.Idle;
        public int StageDataKey { get; private set; }
        public int JobId { get; private set; }

        // 인증 정보
        private string m_gatewayIp;
        private int m_gatewayPort;
        private long m_accountId;
        private ulong m_authToken;

        // 스테이지/이동
        private NavField m_nav;
        private float m_worldMinX, m_worldMinZ, m_worldMaxX, m_worldMaxZ;
        private Vector3 m_pos;
        private readonly List<Vector3> m_path = new();
        private int m_pathIdx;
        private Vector3 m_dest;
        private bool m_moving;
        private uint m_inputSeq;
        private long m_lastMoveSendMs;
        private long m_nextRepickMs;
        private float m_lastYaw;
        private long m_myObjectId;       // in-stage object id (= character_id). 자기 스냅샷 식별용
        private float m_serverMoveSpeed; // StatUpdateNtf 의 MoveSpdTotal (0 = 미수신 → config fallback)

        // 서버가 알려준 실제 이동속도. 없으면 config 값으로 폴백.
        private float MoveSpeed => m_serverMoveSpeed > 0f ? m_serverMoveSpeed : m_ctx.Config.Move.MoveSpeed;

        private const int StatMoveSpdTotal = 35;         // EStat.MoveSpdTotal
        private const float ReconcileHardSnapSq = 2.5f * 2.5f; // 이보다 크게 벌어지면 스냅+재경로
        private const float ReconcileGain = 0.35f;       // 작은 오차는 매 스냅샷 이 비율로 당김

        private long m_reconnectAtMs;
        private readonly Random m_rng;

        // 전투/몬스터 추적
        private enum Behavior { Roaming, Fighting }
        private Behavior m_behavior = Behavior.Roaming;
        private sealed class Mon { public Vector3 Pos; public bool Dead; }
        private sealed class Prop { public int PropKey; public Vector3 Pos; }
        private sealed class PendingProjectileHit
        {
            public long DueMs;
            public SkillHitItem Hit;
        }
        private readonly Dictionary<long, Mon> m_monsters = new();
        private readonly Dictionary<long, Prop> m_props = new();
        private readonly List<PendingProjectileHit> m_pendingProjectileHits = new();
        private long m_nextSkillMs;
        private int m_skillRotation;
        private readonly Dictionary<int, long> m_skillReadyAt = new(); // skillKey → 다음 시전 가능 시각(ms)
        private long m_lastRepathMs;
        private long m_nextStageMoveMs;
        private long m_nextDisconnectMs;
        private long m_targetPortalId;
        private bool m_stageChangeRequested;
        private bool m_directTownReturnPending;
        private bool m_initialStageSelected;
        private bool m_isDead;
        private bool m_stopping;
        private long m_portalDeadlineMs;
        private long m_stageMoveResponseDeadlineMs;
        private long m_stageLoadDeadlineMs;
        private long m_connectStartedMs = -1;
        private long m_loginStartedMs = -1;
        private long m_gatewayAuthStartedMs = -1;
        private long m_characterCreateStartedMs = -1;
        private long m_characterSelectStartedMs = -1;
        private long m_stageLoadStartedMs = -1;
        private long m_stageMoveStartedMs = -1;

        // 통계 접근용
        public bool IsConnected => m_conn?.IsConnected ?? false;
        public bool IsLoggedIn => m_accountId != 0;
        public int SkillCastRequests { get; private set; }
        public int SkillsCast { get; private set; }
        public int ProjectileHitsSent { get; private set; }
        public int StageMoves { get; private set; }
        public int Deaths { get; private set; }
        public int Revives { get; private set; }
        public int PortalAttempts { get; private set; }
        public int PortalFailures { get; private set; }
        public int PortalTimeouts { get; private set; }
        public int DirectReturnAttempts { get; private set; }
        public int DirectReturns { get; private set; }
        public int DirectReturnFailures { get; private set; }
        public int StageLoadTimeouts { get; private set; }
        public int Reconnects { get; private set; }
        public bool IsFighting => State == BotState.InStage && !m_isDead && m_behavior == Behavior.Fighting;
        public bool IsDead => State == BotState.InStage && m_isDead;

        public Bot(int index, string loginId, BotContext ctx)
        {
            Index = index;
            LoginId = loginId;
            m_ctx = ctx;
            m_rng = new Random(index * 7919 + 17);
        }

        // ────────────────────────────────────────────────────────────────
        public void Tick(long nowMs, int dtMs)
        {
            DrainRecv();

            if (State == BotState.InStage && !m_isDead)
                UpdateProjectileHits(nowMs);

            switch (State)
            {
                case BotState.Idle:
                    StartLoginConnect();
                    break;

                case BotState.ConnectingLogin:
                    if (m_connectTask is { IsCompleted: true })
                    {
                        RecordLatency(LatencyKind.LoginConnect, ref m_connectStartedMs);
                        if (m_connectTask.Result)
                        {
                            m_loginStartedMs = m_ctx.NowMs;
                            m_conn.Send(PId.LoginReq, new LoginReq { LoginId = LoginId, Password = Password });
                            State = BotState.WaitLoginRes;
                        }
                        else Fail("login connect failed");
                    }
                    break;

                case BotState.NeedGatewayConnect:
                    StartGatewayConnect();
                    break;

                case BotState.ConnectingGateway:
                    if (m_connectTask is { IsCompleted: true })
                    {
                        RecordLatency(LatencyKind.GatewayConnect, ref m_connectStartedMs);
                        if (m_connectTask.Result)
                        {
                            m_gatewayAuthStartedMs = m_ctx.NowMs;
                            m_conn.Send(PId.GatewayAuthReq,
                                new GatewayAuthReq { AccountId = m_accountId, AuthToken = m_authToken });
                            State = BotState.WaitCharList;
                        }
                        else Fail("gateway connect failed");
                    }
                    break;

                case BotState.InStage:
                    UpdateDisconnect(nowMs);
                    if (State != BotState.InStage) break;   // 연결 끊김 처리됨
                    if (m_isDead) break;
                    UpdateStageMove(nowMs);
                    if (m_stageChangeRequested || m_targetPortalId != 0)
                    {
                        UpdatePortalMove(nowMs, dtMs);
                        break;
                    }
                    UpdateBehavior(nowMs, dtMs);            // 배회 ↔ 전투 FSM
                    break;

                case BotState.Disconnected:
                    if (m_ctx.Config.Reconnect.Enabled && nowMs >= m_reconnectAtMs)
                    {
                        ResetForReconnect();
                        Reconnects++;
                        State = BotState.Idle;
                    }
                    break;

                case BotState.WaitStageMoveRes:
                    if (m_stageMoveResponseDeadlineMs > 0 && nowMs >= m_stageMoveResponseDeadlineMs)
                    {
                        if (m_directTownReturnPending)
                            DirectReturnFailures++;
                        else
                        {
                            PortalFailures++;
                            PortalTimeouts++;
                        }
                        Fail(m_directTownReturnPending ? "direct town return timeout" : "portal StageMoveRes timeout");
                    }
                    break;

                case BotState.WaitStageLoad:
                    if (m_stageLoadDeadlineMs > 0 && nowMs >= m_stageLoadDeadlineMs)
                    {
                        StageLoadTimeouts++;
                        Fail("StageLoadCompleteRes timeout");
                    }
                    break;

                // Wait* 상태들은 수신 패킷(OnPacket)이 전이시킨다.
            }
        }

        // ── 연결 단계 ────────────────────────────────────────────────────
        private void StartLoginConnect()
        {
            m_conn = NewConnection();
            m_connectStartedMs = m_ctx.NowMs;
            m_connectTask = m_conn.ConnectAsync(m_ctx.Config.LoginIp, m_ctx.Config.LoginPort);
            State = BotState.ConnectingLogin;
        }

        private void StartGatewayConnect()
        {
            m_conn?.Close(null); // 로그인 연결 종료
            m_conn = NewConnection();
            m_connectStartedMs = m_ctx.NowMs;
            m_connectTask = m_conn.ConnectAsync(m_gatewayIp, m_gatewayPort);
            State = BotState.ConnectingGateway;
        }

        private BotConnection NewConnection()
        {
            var c = new BotConnection(m_ctx.Metrics);
            c.OnClosed += reason => OnConnClosed(c, reason);
            return c;
        }

        private void OnConnClosed(BotConnection who, string reason)
        {
            // 현재 연결이 아닌(교체된) 연결의 종료 이벤트는 무시
            if (who != m_conn) return;
            if (m_stopping) return;
            if (State == BotState.Disconnected) return;
            // 게이트웨이로 갈아타려고 로그인 연결을 의도적으로 닫는 경우는 무시
            if (State == BotState.NeedGatewayConnect) return;

            Fail(reason ?? "closed");
        }

        private void Fail(string reason)
        {
            SetError(reason);
            State = BotState.Disconnected;
            m_reconnectAtMs = m_ctx.NowMs + m_ctx.Config.Reconnect.DelayMs;
            m_conn?.Close(reason);
        }

        private void ResetForReconnect()
        {
            m_conn = null;
            m_connectTask = null;
            m_nav = null;
            m_moving = false;
            m_path.Clear();
            m_monsters.Clear();
            m_props.Clear();
            m_pendingProjectileHits.Clear();
            m_skillReadyAt.Clear();
            m_skillRotation = 0;
            m_behavior = Behavior.Roaming;
            m_inputSeq = 0;
            m_myObjectId = 0;
            JobId = 0;
            m_accountId = 0;
            m_authToken = 0;
            m_serverMoveSpeed = 0f;
            m_targetPortalId = 0;
            m_stageChangeRequested = false;
            m_directTownReturnPending = false;
            m_initialStageSelected = false;
            m_isDead = false;
            m_portalDeadlineMs = 0;
            m_stageMoveResponseDeadlineMs = 0;
            m_stageLoadDeadlineMs = 0;
            m_connectStartedMs = -1;
            m_loginStartedMs = -1;
            m_gatewayAuthStartedMs = -1;
            m_characterCreateStartedMs = -1;
            m_characterSelectStartedMs = -1;
            m_stageLoadStartedMs = -1;
            m_stageMoveStartedMs = -1;
        }

        public void Stop()
        {
            m_stopping = true;
            m_conn?.Close(null);
        }

        private void SendCharacterSelect(long characterId)
        {
            m_characterSelectStartedMs = m_ctx.NowMs;
            m_conn.Send(PId.CharacterSelectReq, new CharacterSelectReq { CharacterId = characterId });
        }

        private void SetError(string reason)
        {
            m_ctx.Metrics.RecordError(reason, m_ctx.NowMs);
        }

        private void RecordLatency(LatencyKind kind, ref long startedMs)
        {
            if (startedMs >= 0)
                m_ctx.Metrics.RecordLatency(kind, Math.Max(0, m_ctx.NowMs - startedMs));
            startedMs = -1;
        }

        // ── 수신 처리 ────────────────────────────────────────────────────
        private void DrainRecv()
        {
            var conn = m_conn;
            if (conn == null) return;
            while (conn.TryDequeue(out var raw))
            {
                try { OnPacket(raw.Type, raw.Body); }
                catch (Exception e) { SetError($"parse {raw.Type}: {e.Message}"); }
            }
        }

        private void OnPacket(ushort type, byte[] body)
        {
            switch ((PId)type)
            {
                case PId.LoginRes:
                {
                    RecordLatency(LatencyKind.Login, ref m_loginStartedMs);
                    var res = LoginRes.Parser.ParseFrom(body);
                    if (!res.Success) { Fail("login rejected"); return; }
                    m_accountId = res.AccountId;
                    m_authToken = res.AuthToken;
                    m_gatewayIp = res.GatewayIp;
                    m_gatewayPort = res.GatewayPort;
                    State = BotState.NeedGatewayConnect;
                    break;
                }

                case PId.CharacterListNtf:
                {
                    if (State != BotState.WaitCharList) break;
                    RecordLatency(LatencyKind.GatewayAuth, ref m_gatewayAuthStartedMs);
                    var ntf = CharacterListNtf.Parser.ParseFrom(body);
                    if (ntf.Characters.Count == 0)
                    {
                        var jobs = m_ctx.Config.Create.JobIds;
                        int jobId = (jobs != null && jobs.Length > 0) ? jobs[m_rng.Next(jobs.Length)] : 1;
                        var presets = m_ctx.Config.Create.AppearancePresetIds;
                        int presetId = presets[m_rng.Next(presets.Length)];
                        m_characterCreateStartedMs = m_ctx.NowMs;
                        m_conn.Send(PId.CharacterCreateReq, new CharacterCreateReq
                        {
                            Name = LoginId,
                            JobId = jobId,
                            AppearancePresetId = presetId,
                        });
                        State = BotState.WaitCreate;
                    }
                    else
                    {
                        SendCharacterSelect(ntf.Characters[0].CharacterId);
                        State = BotState.WaitSelect;
                    }
                    break;
                }

                case PId.CharacterCreateRes:
                {
                    if (State != BotState.WaitCreate) break;
                    RecordLatency(LatencyKind.CharacterCreate, ref m_characterCreateStartedMs);
                    var res = CharacterCreateRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess || res.NewCharacter == null)
                    {
                        Fail($"create failed: {res.ErrorMsg}");
                        return;
                    }
                    SendCharacterSelect(res.NewCharacter.CharacterId);
                    State = BotState.WaitSelect;
                    break;
                }

                case PId.CharacterSelectRes:
                {
                    if (State != BotState.WaitSelect) break;
                    RecordLatency(LatencyKind.CharacterSelect, ref m_characterSelectStartedMs);
                    var res = CharacterSelectRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess) { Fail($"select failed: {res.ErrorMsg}"); return; }
                    m_myObjectId = res.Character?.CharacterId ?? 0; // in-stage object id = character_id
                    JobId = res.Character?.JobId ?? 0;
                    if (m_ctx.Config.Skill.GetSkillKeys(JobId) == null)
                    {
                        Fail($"skills not configured for job: {JobId}");
                        return;
                    }
                    StageDataKey = res.StageDataKey;
                    LoadNavMesh(StageDataKey);
                    m_stageLoadStartedMs = m_ctx.NowMs;
                    m_conn.Send(PId.StageLoadCompleteReq, new StageLoadCompleteReq());
                    m_stageLoadDeadlineMs = m_ctx.NowMs + m_ctx.Config.StageMove.LoadTimeoutMs;
                    State = BotState.WaitStageLoad;
                    break;
                }

                case PId.StageLoadCompleteRes:
                {
                    if (State != BotState.WaitStageLoad) break;
                    RecordLatency(LatencyKind.StageLoad, ref m_stageLoadStartedMs);
                    var res = StageLoadCompleteRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess) { Fail($"stageload failed: {res.ErrorMsg}"); return; }
                    StageDataKey = res.StageDataKey;
                    m_worldMinX = res.WorldMinX; m_worldMinZ = res.WorldMinZ;
                    m_worldMaxX = res.WorldMaxX; m_worldMaxZ = res.WorldMaxZ;
                    m_pos = new Vector3(res.MyPosX, res.MyPosY, res.MyPosZ);
                    m_lastYaw = res.MyYaw;
                    m_moving = false;
                    m_path.Clear();
                    m_monsters.Clear();
                    m_props.Clear();
                    m_pendingProjectileHits.Clear();
                    m_skillReadyAt.Clear();
                    m_skillRotation = 0;
                    m_behavior = Behavior.Roaming;
                    m_targetPortalId = 0;
                    m_stageChangeRequested = false;
                    m_directTownReturnPending = false;
                    m_isDead = false;
                    m_stageLoadDeadlineMs = 0;
                    long t = m_ctx.NowMs;
                    m_nextRepickMs = m_ctx.Catalog.IsTown(StageDataKey)
                        ? t + m_ctx.Config.Town.RoamCheckIntervalMs
                        : t; // 필드는 즉시 목적지 선정
                    m_nextSkillMs = t + m_ctx.Config.Skill.GetUseIntervalMs(StageDataKey);
                    m_nextStageMoveMs = t + m_ctx.Config.StageMove.IntervalMs;
                    m_nextDisconnectMs = t + m_ctx.Config.Disconnect.CheckIntervalMs;

                    if (!m_initialStageSelected)
                    {
                        m_initialStageSelected = true;
                        var stages = m_ctx.Config.StageKeys;
                        if (stages != null && stages.Length > 0 && stages[m_rng.Next(stages.Length)] != StageDataKey)
                            requestStageChange(t);
                    }

                    State = BotState.InStage;
                    break;
                }

                case PId.ObjectVisibilityNtf:
                {
                    var ntf = ObjectVisibilityNtf.Parser.ParseFrom(body);
                    foreach (var m in ntf.MonsterSpawns)
                        m_monsters[m.ObjectId] = new Mon { Pos = new Vector3(m.PosX, m.PosY, m.PosZ), Dead = m.IsDead };
                    foreach (var p in ntf.PropSpawns)
                        m_props[p.ObjectId] = new Prop { PropKey = p.PropKey, Pos = new Vector3(p.PosX, p.PosY, p.PosZ) };
                    foreach (long id in ntf.DespawnIds)
                    {
                        m_monsters.Remove(id);
                        m_props.Remove(id);
                        if (m_targetPortalId == id)
                        {
                            SetError("portal disappeared from AOI");
                            PortalFailures++;
                            cancelStageMoveAttempt(m_ctx.NowMs);
                        }
                    }
                    break;
                }

                case PId.SnapshotNtf:
                {
                    var snap = SnapshotNtf.Parser.ParseFrom(body);
                    foreach (var s in snap.States)
                    {
                        // 자기 캐릭터 → 서버 권위 위치로 화해(드리프트 제거)
                        if (m_myObjectId != 0 && s.ObjectId == m_myObjectId)
                        {
                            if (State == BotState.InStage)
                            {
                                DecodeActorState(s, out Vector3 apos, out bool _);
                                ReconcileSelf(apos);
                            }
                            continue;
                        }
                        // 몬스터 → 위치/사망 갱신
                        if (m_monsters.TryGetValue(s.ObjectId, out Mon mon))
                        {
                            DecodeActorState(s, out Vector3 pos, out bool dead);
                            mon.Pos = pos;
                            mon.Dead = dead;
                        }
                    }
                    break;
                }

                case PId.StatUpdateNtf:
                {
                    // 서버가 알려주는 실제 이동속도(MoveSpdTotal)를 dead-reckon 속도로 채택.
                    var ntf = StatUpdateNtf.Parser.ParseFrom(body);
                    if (ntf.ObjectId == m_myObjectId)
                        foreach (var e in ntf.Entries)
                            if (e.Stat == StatMoveSpdTotal)
                                m_serverMoveSpeed = (float)e.Value;
                    break;
                }

                case PId.MovePosCorrectNtf:
                {
                    // 서버 위치 강제 보정(현재 서버 미발동이나 대비). 즉시 스냅.
                    var ntf = MovePosCorrectNtf.Parser.ParseFrom(body);
                    if (State == BotState.InStage)
                        HardSnapTo(new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ));
                    break;
                }

                case PId.ObjectDeathNtf:
                {
                    var ntf = ObjectDeathNtf.Parser.ParseFrom(body);
                    if (ntf.ObjectId == m_myObjectId)
                    {
                        if (!m_isDead)
                            Deaths++;
                        m_isDead = true;
                        m_behavior = Behavior.Roaming;
                        m_moving = false;
                        m_path.Clear();
                        if (State == BotState.InStage)
                        {
                            if (m_stageChangeRequested || m_targetPortalId != 0)
                                PortalFailures++;
                            m_targetPortalId = 0;
                            m_stageChangeRequested = false;
                            m_portalDeadlineMs = 0;
                            m_nextStageMoveMs = m_ctx.NowMs + m_ctx.Config.StageMove.IntervalMs;
                        }
                        m_pendingProjectileHits.Clear();
                        break;
                    }
                    if (m_monsters.TryGetValue(ntf.ObjectId, out Mon mon))
                        mon.Dead = true; // 사망 즉시 반영 → 대상에서 제외
                    break;
                }

                case PId.ObjectReviveNtf:
                {
                    var ntf = ObjectReviveNtf.Parser.ParseFrom(body);
                    if (ntf.ObjectId != m_myObjectId) break;

                    bool wasDead = m_isDead;
                    m_isDead = false;
                    m_pos = new Vector3(ntf.PosX, ntf.PosY, ntf.PosZ);
                    m_lastYaw = ntf.Yaw;
                    m_moving = false;
                    m_path.Clear();
                    m_pathIdx = 0;
                    m_behavior = Behavior.Roaming;
                    long nowMs = m_ctx.NowMs;
                    m_nextRepickMs = m_ctx.Catalog.IsTown(StageDataKey)
                        ? nowMs + m_ctx.Config.Town.RoamCheckIntervalMs
                        : nowMs;
                    m_nextSkillMs = nowMs + m_ctx.Config.Skill.GetUseIntervalMs(StageDataKey);
                    if (wasDead)
                        Revives++;
                    break;
                }

                case PId.SkillCastNtf:
                {
                    var ntf = SkillCastNtf.Parser.ParseFrom(body);
                    if (ntf.CasterObjectId != m_myObjectId) break;

                    SkillsCast++;
                    SkillInfo info = m_ctx.Skills.Get(ntf.SkillKey);
                    if (info != null && info.IsProjectile && ntf.EffectId != 0)
                        scheduleProjectileHits(ntf, info, m_ctx.NowMs);
                    break;
                }

                case PId.StageMoveRes:
                {
                    if (State != BotState.WaitStageMoveRes) break;
                    RecordLatency(LatencyKind.StageMove, ref m_stageMoveStartedMs);
                    var res = StageMoveRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess)
                    {
                        // 이동 거부 → 현재 스테이지에서 계속 플레이
                        SetError($"stage move failed: {res.ErrorMsg}");
                        if (m_directTownReturnPending)
                            DirectReturnFailures++;
                        else
                            PortalFailures++;
                        cancelStageMoveAttempt(m_ctx.NowMs);
                        State = BotState.InStage;
                        break;
                    }
                    // 성공: 목적지 NavMesh 로 교체하고 2단계 입장(로딩완료 보고)
                    if (m_directTownReturnPending)
                        DirectReturns++;
                    StageDataKey = res.TargetStageDataKey;
                    m_monsters.Clear();
                    m_props.Clear();
                    m_pendingProjectileHits.Clear();
                    m_moving = false;
                    m_path.Clear();
                    m_targetPortalId = 0;
                    m_stageChangeRequested = false;
                    m_directTownReturnPending = false;
                    m_portalDeadlineMs = 0;
                    m_stageMoveResponseDeadlineMs = 0;
                    LoadNavMesh(StageDataKey);
                    m_stageLoadStartedMs = m_ctx.NowMs;
                    m_conn.Send(PId.StageLoadCompleteReq, new StageLoadCompleteReq());
                    m_stageLoadDeadlineMs = m_ctx.NowMs + m_ctx.Config.StageMove.LoadTimeoutMs;
                    StageMoves++;
                    State = BotState.WaitStageLoad;
                    break;
                }

                case PId.ObjectInteractRes:
                {
                    var res = ObjectInteractRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess && State == BotState.WaitStageMoveRes)
                    {
                        SetError($"portal interact failed: {res.ErrorMsg}");
                        PortalFailures++;
                        cancelStageMoveAttempt(m_ctx.NowMs);
                        State = BotState.InStage;
                    }
                    break;
                }
            }
        }

        // ── NavMesh / 이동 ───────────────────────────────────────────────
        private void LoadNavMesh(int stageKey)
        {
            string name = m_ctx.Catalog.GetNavMeshName(stageKey);
            if (string.IsNullOrEmpty(name)) { m_nav = null; return; }
            var mesh = NavMeshCache.GetMesh(m_ctx.NavMeshDir, name);
            m_nav = mesh != null ? new NavField(mesh) : null;
        }

        // ── 행동 FSM: 배회 ↔ 전투 ────────────────────────────────────────
        // 배회 중 탐지범위 내 적 발견 → 전투. 전투 중 사거리 밖이면 추격, 안이면
        // 멈춰서 스킬 로테이션 난사. 탐지범위 내 살아있는 적이 없어지면 다시 배회.
        private void UpdateBehavior(long nowMs, int dtMs)
        {
            if (m_nav == null) return;

            if (m_ctx.Catalog.IsTown(StageDataKey))
            {
                m_behavior = Behavior.Roaming;
                RoamTick(nowMs, dtMs, m_ctx.Config.Town.RoamCheckIntervalMs, m_ctx.Config.Town.RoamProbability);
                return;
            }

            bool hasEnemy = FindNearestEnemy(m_ctx.Config.Skill.DetectRange, out long enemyId, out Vector3 enemyPos);

            if (m_behavior == Behavior.Roaming)
            {
                if (hasEnemy) EnterCombat(nowMs);
                else { RoamTick(nowMs, dtMs, m_ctx.Config.Move.RepickDelayMs, 1.0); return; }
            }

            // ── 전투 중 ──
            if (!hasEnemy) { ExitCombat(nowMs); return; }

            bool hasReadySkill = TrySelectReadySkill(nowMs, out int skillIndex, out int skillKey, out SkillInfo skillInfo);
            float castRange = hasReadySkill ? GetCastRange(skillInfo) : m_ctx.Config.Skill.Range;
            float dist = MathF.Sqrt(DistSqXZ(m_pos, enemyPos));
            if (dist > castRange)
            {
                ChaseTick(nowMs, dtMs, enemyPos);        // 사거리 밖 → 접근
            }
            else
            {
                if (m_moving) StopMoving(nowMs);          // 사거리 안 → 정지 후
                if (hasReadySkill)
                    CastRotation(nowMs, enemyId, enemyPos, skillIndex, skillKey, skillInfo);
            }
        }

        private void EnterCombat(long nowMs)
        {
            m_behavior = Behavior.Fighting;
            if (m_moving) StopMoving(nowMs);
            m_nextSkillMs = nowMs; // 즉시 첫 시전
        }

        private void ExitCombat(long nowMs)
        {
            m_behavior = Behavior.Roaming;
            m_nextRepickMs = nowMs; // 즉시 다음 목적지 선정
        }

        // ── 배회 ──
        private void RoamTick(long nowMs, int dtMs, int repickDelayMs, double repickProbability)
        {
            if (!m_moving)
            {
                if (nowMs < m_nextRepickMs) return;
                m_nextRepickMs = nowMs + repickDelayMs;
                if (m_rng.NextDouble() <= repickProbability)
                    PickRoamDestination(nowMs, repickDelayMs);
                return;
            }
            if (AdvancePath(nowMs, dtMs)) // 도착
            {
                StopMoving(nowMs);
                m_nextRepickMs = nowMs + repickDelayMs;
            }
        }

        private void PickRoamDestination(long nowMs, int repickDelayMs)
        {
            float x = Lerp(m_worldMinX, m_worldMaxX, (float)m_rng.NextDouble());
            float z = Lerp(m_worldMinZ, m_worldMaxZ, (float)m_rng.NextDouble());
            if (SetPathTo(new Vector3(x, m_pos.Y, z)))
                SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
            else
                m_nextRepickMs = nowMs + repickDelayMs;
        }

        // ── 추격: 움직이는 적 위치로 주기적 재경로 ──
        private void ChaseTick(long nowMs, int dtMs, Vector3 enemyPos)
        {
            if (!m_moving || nowMs - m_lastRepathMs >= 300)
            {
                if (SetPathTo(enemyPos))
                {
                    m_lastRepathMs = nowMs;
                    SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
                }
            }
            if (m_moving && AdvancePath(nowMs, dtMs))
                StopMoving(nowMs); // 경로 끝 → 다음 틱에 재평가(사거리 재확인)
        }

        // 경로를 따라 전진. 도착 시 true. 이동 중 주기적으로 MoveIntent(TO) 송신.
        private bool AdvancePath(long nowMs, int dtMs)
        {
            float step = MoveSpeed * (dtMs / 1000f);
            while (step > 0f && m_pathIdx < m_path.Count)
            {
                Vector3 wp = m_path[m_pathIdx];
                Vector3 to = wp - m_pos;
                float d = to.Length();
                if (d <= step) { m_pos = wp; m_pathIdx++; step -= d; }
                else { m_pos += (to / d) * step; step = 0f; }
            }

            bool arrived = m_pathIdx >= m_path.Count ||
                           Vector3.Distance(m_pos, m_dest) <= m_ctx.Config.Move.ArriveDistance;
            if (arrived) return true;

            if (nowMs - m_lastMoveSendMs >= m_ctx.Config.Move.SendIntervalMs)
                SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
            return false;
        }

        // goal 로 가는 NavMesh 경로 설정. 성공 시 m_moving=true.
        private bool SetPathTo(Vector3 goal)
        {
            if (m_nav.SamplePosition(goal, 8f, out Vector3 navPt) &&
                m_nav.FindPath(m_pos, navPt, m_path) && m_path.Count >= 2)
            {
                m_dest = m_path[m_path.Count - 1];
                m_pathIdx = 1;
                m_moving = true;
                return true;
            }
            m_moving = false;
            return false;
        }

        private void StopMoving(long nowMs)
        {
            if (!m_moving) return;
            m_moving = false;
            m_path.Clear();
            m_pathIdx = 0;
            SendMoveIntent(nowMs, EMoveIntent.MoveIntentStop);
        }

        private void SendMoveIntent(long nowMs, EMoveIntent intent)
        {
            // 다음 waypoint(없으면 dest) 방향으로 yaw 계산
            Vector3 aim = (m_pathIdx < m_path.Count) ? m_path[m_pathIdx] : m_dest;
            Vector3 dir = aim - m_pos;
            if (dir.LengthSquared() > 1e-6f)
                m_lastYaw = MathF.Atan2(dir.X, dir.Z) * (180f / MathF.PI);

            m_conn.Send(PId.MoveIntentReq, new MoveIntentReq
            {
                InputSeq = ++m_inputSeq,
                ClientTimeMs = (uint)nowMs,
                Intent = intent,
                DestX = m_dest.X,
                DestY = m_dest.Y,
                DestZ = m_dest.Z,
                Yaw = m_lastYaw,
            });
            m_lastMoveSendMs = nowMs;
        }

        // 탐지/사거리 판정용: range 내 가장 가까운 살아있는 적. 없으면 false.
        private bool FindNearestEnemy(float range, out long id, out Vector3 pos)
        {
            id = 0; pos = default;
            float best = range * range;
            foreach (var kv in m_monsters)
            {
                if (kv.Value.Dead) continue;
                float sqr = DistSqXZ(m_pos, kv.Value.Pos);
                if (sqr <= best) { best = sqr; id = kv.Key; pos = kv.Value.Pos; }
            }
            return id != 0;
        }

        // 스킬 시전: 쿨타임이 돈 스킬을 라운드로빈으로 골라, 배치(Placement)에 맞는 origin 으로 시전.
        private bool TrySelectReadySkill(long nowMs, out int chosen, out int skillKey, out SkillInfo info)
        {
            chosen = -1;
            skillKey = 0;
            info = null;
            // 전역 시전 간격(GCD 유사): 여러 스킬을 같은 순간에 쏘지 않도록.
            if (nowMs < m_nextSkillMs) return false;

            int[] keys = m_ctx.Config.Skill.GetSkillKeys(JobId);
            if (keys == null || keys.Length == 0) return false;

            // 라운드로빈: 마지막 시전 다음 인덱스부터 스캔해 "쿨타임이 돈" 첫 스킬 선택.
            // → 쿨 0인 1001 이 앞자리를 독점해 1003/1008 을 굶기는 현상 방지.
            for (int i = 0; i < keys.Length; i++)
            {
                int idx = (m_skillRotation + i) % keys.Length;
                if (nowMs >= SkillReadyAt(keys[idx])) { chosen = idx; break; }
            }
            if (chosen < 0) return false;

            skillKey = keys[chosen];
            info = m_ctx.Skills?.Get(skillKey);
            return info != null;
        }

        private float GetCastRange(SkillInfo info)
        {
            if (info.MaxRange > 0f) return info.MaxRange;
            if (info.Placement == SkillPlacement.Target) return m_ctx.Config.Skill.Range;
            if (info.ObbLength > 0f) return info.EffectCenterForwardOffset + info.ObbLength * 0.5f;
            if (info.Radius > 0f) return info.Radius;
            return m_ctx.Config.Skill.Range;
        }

        private void CastRotation(long nowMs, long targetId, Vector3 targetPos, int chosen, int skillKey, SkillInfo info)
        {
            m_skillRotation = chosen + 1;

            // 방향: 캐스터→타겟 (XZ 평면)
            float dx = targetPos.X - m_pos.X, dz = targetPos.Z - m_pos.Z;
            float len = MathF.Sqrt(dx * dx + dz * dz);
            if (len > 1e-4f) { dx /= len; dz /= len; m_lastYaw = MathF.Atan2(dx, dz) * (180f / MathF.PI); }
            else { dx = 0; dz = 1; }

            // 배치(Placement)로 효과 중심 origin 결정 (클라 SkillSystem.cs 와 동일 규칙).
            Vector3 origin;
            SkillPlacement placement = info.Placement;
            switch (placement)
            {
                case SkillPlacement.Target:  // 얼음지대/불기둥 등: 몬스터 위치에 시전
                    origin = targetPos;
                    break;
                default: // Caster/SkillCastOrigin/None: 서버가 권위 앵커 위치를 다시 계산한다.
                    origin = m_pos;
                    break;
            }

            m_conn.Send(PId.SkillCastReq, new SkillCastReq
            {
                SkillKey = skillKey,
                OriginX = origin.X, OriginY = origin.Y, OriginZ = origin.Z,
                DirX = dx, DirZ = dz,
                Seed = (uint)m_rng.Next(),
                TargetObjectId = targetId,
                TargetPosX = targetPos.X, TargetPosZ = targetPos.Z,
            });

            // 스킬별 쿨타임 등록 + 전역 시전 간격.
            m_skillReadyAt[skillKey] = nowMs + info.CooldownMs;
            SkillCastRequests++;
            m_nextSkillMs = nowMs + m_ctx.Config.Skill.GetUseIntervalMs(StageDataKey);
        }

        private void scheduleProjectileHits(SkillCastNtf ntf, SkillInfo info, long nowMs)
        {
            Vector3 origin = new Vector3(ntf.OriginX, ntf.OriginY, ntf.OriginZ);
            Vector3 castDir = new Vector3(ntf.DirX, 0f, ntf.DirZ);
            if (castDir.LengthSquared() <= 1e-6f)
                return;
            castDir = Vector3.Normalize(castDir);
            int count = Math.Max(1, info.ProjectileCount);
            List<Vector3> dirs = computeFanDirs(castDir, count, info.FanAngleDeg);
            const float collisionRadius = 0.75f;

            for (int index = 0; index < dirs.Count; index++)
            {
                Vector3 dir = dirs[index];
                long targetId = 0;
                float travelDistance = info.MaxRange;
                Vector3 hitPos = origin + dir * info.MaxRange;

                foreach (var (objectId, monster) in m_monsters)
                {
                    if (monster.Dead) continue;
                    Vector3 to = monster.Pos - origin;
                    float forward = to.X * dir.X + to.Z * dir.Z;
                    if (forward < 0f || forward > travelDistance) continue;
                    float perpendicularSq = to.X * to.X + to.Z * to.Z - forward * forward;
                    if (perpendicularSq > collisionRadius * collisionRadius) continue;

                    targetId = objectId;
                    travelDistance = forward;
                    hitPos = monster.Pos;
                }

                long flightMs = (long)Math.Ceiling(travelDistance / info.ProjectileSpeed * 1000f);
                var hit = new SkillHitItem
                {
                    EffectId = ntf.EffectId,
                    ProjectileIndex = index,
                    TargetObjectId = targetId,
                    ExplodedAtMaxRange = targetId == 0,
                    HitX = hitPos.X,
                    HitZ = hitPos.Z,
                };
                m_pendingProjectileHits.Add(new PendingProjectileHit { DueMs = nowMs + flightMs, Hit = hit });
            }
        }

        private void UpdateProjectileHits(long nowMs)
        {
            SkillProjectileHitReq req = null;
            for (int i = m_pendingProjectileHits.Count - 1; i >= 0; i--)
            {
                PendingProjectileHit pending = m_pendingProjectileHits[i];
                if (nowMs < pending.DueMs) continue;

                req ??= new SkillProjectileHitReq();
                req.Hits.Add(pending.Hit);
                m_pendingProjectileHits.RemoveAt(i);
            }

            if (req == null || req.Hits.Count == 0) return;
            m_conn.Send(PId.SkillProjectileHitReq, req);
            ProjectileHitsSent += req.Hits.Count;
        }

        private static List<Vector3> computeFanDirs(Vector3 dir, int count, float fanAngleDeg)
        {
            var dirs = new List<Vector3>(count);
            if (count <= 1)
            {
                dirs.Add(dir);
                return dirs;
            }

            float totalRad = fanAngleDeg * (MathF.PI / 180f);
            float step = totalRad / (count - 1);
            float startRad = -totalRad * 0.5f;
            for (int i = 0; i < count; i++)
            {
                float angle = startRad + step * i;
                float cosine = MathF.Cos(angle);
                float sine = MathF.Sin(angle);
                dirs.Add(new Vector3(dir.X * cosine - dir.Z * sine, 0f, dir.X * sine + dir.Z * cosine));
            }
            return dirs;
        }

        private long SkillReadyAt(int skillKey)
            => m_skillReadyAt.TryGetValue(skillKey, out long t) ? t : 0;

        // ── 스테이지 이동(100은 포탈, 101/107은 100으로 직접 귀환) ─────────
        private void UpdateStageMove(long nowMs)
        {
            if (m_stageChangeRequested || m_targetPortalId != 0) return;
            if (nowMs < m_nextStageMoveMs) return;
            m_nextStageMoveMs = nowMs + m_ctx.Config.StageMove.IntervalMs;

            if (m_rng.NextDouble() > m_ctx.Config.StageMove.Probability) return;

            requestStageChange(nowMs);
        }

        private void requestStageChange(long nowMs)
        {
            if (StageDataKey == 101 || StageDataKey == 107)
            {
                requestDirectTownReturn(nowMs);
                return;
            }

            m_stageChangeRequested = true;
            m_portalDeadlineMs = nowMs + m_ctx.Config.StageMove.PortalTimeoutMs;
            PortalAttempts++;
        }

        private void requestDirectTownReturn(long nowMs)
        {
            if (m_moving)
                StopMoving(nowMs);
            m_behavior = Behavior.Roaming;
            m_pendingProjectileHits.Clear();
            m_directTownReturnPending = true;
            m_stageMoveStartedMs = nowMs;
            m_conn.Send(PId.StageMoveReq, new StageMoveReq
            {
                TargetStageDataKey = TownStageKey,
                PositionType = 1, // EStagePositionType.Default
                TargetGameServerId = 0,
            });
            m_stageMoveResponseDeadlineMs = nowMs + m_ctx.Config.StageMove.ResponseTimeoutMs;
            DirectReturnAttempts++;
            State = BotState.WaitStageMoveRes;
        }

        private void UpdatePortalMove(long nowMs, int dtMs)
        {
            if (m_portalDeadlineMs > 0 && nowMs >= m_portalDeadlineMs)
            {
                SetError("portal search/move timeout");
                PortalFailures++;
                PortalTimeouts++;
                cancelStageMoveAttempt(nowMs);
                return;
            }

            if (m_nav == null)
            {
                SetError("portal move failed: NavMesh unavailable");
                PortalFailures++;
                cancelStageMoveAttempt(nowMs);
                return;
            }

            if (m_targetPortalId == 0)
            {
                var portalIds = new List<long>();
                foreach (var (objectId, prop) in m_props)
                {
                    if (m_ctx.Props.IsPortal(prop.PropKey))
                        portalIds.Add(objectId);
                }

                if (portalIds.Count == 0)
                    return; // 초기 AOI 패킷에서 포탈을 받을 때까지 대기

                m_targetPortalId = portalIds[m_rng.Next(portalIds.Count)];
                m_behavior = Behavior.Roaming;
                if (m_moving)
                    StopMoving(nowMs);

                if (!SetPathTo(m_props[m_targetPortalId].Pos))
                {
                    SetError("portal move failed: path not found");
                    PortalFailures++;
                    cancelStageMoveAttempt(nowMs);
                    return;
                }
                SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
                return;
            }

            if (!m_props.TryGetValue(m_targetPortalId, out Prop portal))
            {
                SetError("portal target unavailable");
                PortalFailures++;
                cancelStageMoveAttempt(nowMs);
                return;
            }

            float interactRange = MathF.Max(0.5f, m_ctx.Props.GetInteractRange(portal.PropKey) - 0.25f);
            if (DistSqXZ(m_pos, portal.Pos) <= interactRange * interactRange)
            {
                if (m_moving)
                    StopMoving(nowMs);

                m_conn.Send(PId.ObjectInteractReq, new ObjectInteractReq
                {
                    ObjectId = m_targetPortalId,
                    PosX = m_pos.X,
                    PosY = m_pos.Y,
                    PosZ = m_pos.Z,
                });
                m_stageChangeRequested = false;
                m_pendingProjectileHits.Clear();
                m_stageMoveStartedMs = nowMs;
                m_stageMoveResponseDeadlineMs = nowMs + m_ctx.Config.StageMove.ResponseTimeoutMs;
                State = BotState.WaitStageMoveRes;
                return;
            }

            if (!m_moving)
            {
                if (!SetPathTo(portal.Pos))
                {
                    SetError("portal move failed: repath not found");
                    PortalFailures++;
                    cancelStageMoveAttempt(nowMs);
                    return;
                }
                SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
            }

            if (m_moving && AdvancePath(nowMs, dtMs))
                StopMoving(nowMs);
        }

        private void cancelStageMoveAttempt(long nowMs)
        {
            if (m_moving)
                StopMoving(nowMs);
            m_targetPortalId = 0;
            m_stageChangeRequested = false;
            m_directTownReturnPending = false;
            m_portalDeadlineMs = 0;
            m_stageMoveResponseDeadlineMs = 0;
            m_nextStageMoveMs = nowMs + m_ctx.Config.StageMove.IntervalMs;
            m_nextRepickMs = nowMs;
        }

        // ── 확률적 연결 끊기 ─────────────────────────────────────────────
        private void UpdateDisconnect(long nowMs)
        {
            if (nowMs < m_nextDisconnectMs) return;
            m_nextDisconnectMs = nowMs + m_ctx.Config.Disconnect.CheckIntervalMs;

            if (m_rng.NextDouble() < m_ctx.Config.Disconnect.Probability)
                m_conn?.Close("random disconnect"); // OnConnClosed → Fail → (reconnect)
        }

        // 자기 위치를 서버 권위 위치로 화해. 오차가 크면 스냅+재경로, 작으면 부드럽게 당김.
        private void ReconcileSelf(Vector3 authPos)
        {
            if (DistSqXZ(m_pos, authPos) > ReconcileHardSnapSq)
                HardSnapTo(authPos);
            else
                m_pos = Vector3.Lerp(m_pos, authPos, ReconcileGain);
        }

        // 서버 위치로 즉시 스냅 + 진행 이동 취소(다음 틱에 재경로).
        private void HardSnapTo(Vector3 authPos)
        {
            m_pos = authPos;
            m_path.Clear();
            m_pathIdx = 0;
            m_moving = false;
            m_nextRepickMs = m_ctx.NowMs;
        }

        // 서버 SnapshotNtf 의 양자화 좌표 디코드 (클라 StageManager.DecodeActorState 와 동일 공식)
        private void DecodeActorState(ActorStateInfo s, out Vector3 pos, out bool dead)
        {
            uint xz = s.QposXz, yyaw = s.QposYYaw;
            float x = Dequant((ushort)(xz >> 16), m_worldMinX, m_worldMaxX);
            float z = Dequant((ushort)(xz & 0xFFFF), m_worldMinZ, m_worldMaxZ);
            float y = Dequant((ushort)(yyaw >> 16), -512f, 512f); // Y 고정범위 [-512,512]
            pos = new Vector3(x, y, z);
            dead = (s.Flags & 0x2u) != 0; // bit0=isMoving, bit1=isDead
        }

        private static float Dequant(ushort q, float mn, float mx) => mn + (q / 65535f) * (mx - mn);

        private static float DistSqXZ(Vector3 a, Vector3 b)
        {
            float dx = a.X - b.X, dz = a.Z - b.Z;
            return dx * dx + dz * dz;
        }

        private static float Lerp(float a, float b, float t) => a + (b - a) * t;
    }
}
