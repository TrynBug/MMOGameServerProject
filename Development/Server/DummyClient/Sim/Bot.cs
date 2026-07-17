using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading.Tasks;
using DummyClient.Config;
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

        private readonly BotContext m_ctx;
        public int Index { get; }
        public string LoginId { get; }
        private string Password => LoginId; // 비밀번호 = 아이디

        private BotConnection m_conn;
        private Task<bool> m_connectTask;

        public BotState State { get; private set; } = BotState.Idle;
        public string LastError { get; private set; }
        public int StageDataKey { get; private set; }

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
        private readonly Dictionary<long, Mon> m_monsters = new();
        private long m_nextSkillMs;
        private int m_skillRotation;
        private readonly Dictionary<int, long> m_skillReadyAt = new(); // skillKey → 다음 시전 가능 시각(ms)
        private long m_lastRepathMs;
        private long m_nextStageMoveMs;
        private long m_nextDisconnectMs;
        private int m_pendingStageKey;

        // 통계 접근용
        public int PacketsSent => m_conn?.PacketsSent ?? 0;
        public int PacketsRecv => m_conn?.PacketsRecv ?? 0;
        public int SkillsCast { get; private set; }
        public int StageMoves { get; private set; }
        public bool IsFighting => State == BotState.InStage && m_behavior == Behavior.Fighting;

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

            switch (State)
            {
                case BotState.Idle:
                    StartLoginConnect();
                    break;

                case BotState.ConnectingLogin:
                    if (m_connectTask is { IsCompleted: true })
                    {
                        if (m_connectTask.Result)
                        {
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
                        if (m_connectTask.Result)
                        {
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
                    UpdateStageMove(nowMs);
                    if (State != BotState.InStage) break;   // 스테이지 이동 시작됨
                    UpdateBehavior(nowMs, dtMs);            // 배회 ↔ 전투 FSM
                    break;

                case BotState.Disconnected:
                    if (m_ctx.Config.Reconnect.Enabled && nowMs >= m_reconnectAtMs)
                    {
                        ResetForReconnect();
                        State = BotState.Idle;
                    }
                    break;

                // Wait* 상태들은 수신 패킷(OnPacket)이 전이시킨다.
            }
        }

        // ── 연결 단계 ────────────────────────────────────────────────────
        private void StartLoginConnect()
        {
            m_conn = NewConnection();
            m_connectTask = m_conn.ConnectAsync(m_ctx.Config.LoginIp, m_ctx.Config.LoginPort);
            State = BotState.ConnectingLogin;
        }

        private void StartGatewayConnect()
        {
            m_conn?.Close(null); // 로그인 연결 종료
            m_conn = NewConnection();
            m_connectTask = m_conn.ConnectAsync(m_gatewayIp, m_gatewayPort);
            State = BotState.ConnectingGateway;
        }

        private BotConnection NewConnection()
        {
            var c = new BotConnection();
            c.OnClosed += reason => OnConnClosed(c, reason);
            return c;
        }

        private void OnConnClosed(BotConnection who, string reason)
        {
            // 현재 연결이 아닌(교체된) 연결의 종료 이벤트는 무시
            if (who != m_conn) return;
            if (State == BotState.Disconnected) return;
            // 게이트웨이로 갈아타려고 로그인 연결을 의도적으로 닫는 경우는 무시
            if (State == BotState.NeedGatewayConnect) return;

            Fail(reason ?? "closed");
        }

        private void Fail(string reason)
        {
            LastError = reason;
            State = BotState.Disconnected;
            m_reconnectAtMs = m_ctx.NowMs + m_ctx.Config.Reconnect.DelayMs;
        }

        private void ResetForReconnect()
        {
            m_conn = null;
            m_connectTask = null;
            m_nav = null;
            m_moving = false;
            m_path.Clear();
            m_monsters.Clear();
            m_skillReadyAt.Clear();
            m_skillRotation = 0;
            m_behavior = Behavior.Roaming;
            m_inputSeq = 0;
            m_myObjectId = 0;
            m_serverMoveSpeed = 0f;
        }

        // ── 수신 처리 ────────────────────────────────────────────────────
        private void DrainRecv()
        {
            var conn = m_conn;
            if (conn == null) return;
            while (conn.RecvQueue.TryDequeue(out var raw))
            {
                try { OnPacket(raw.Type, raw.Body); }
                catch (Exception e) { LastError = $"parse {raw.Type}: {e.Message}"; }
            }
        }

        private void OnPacket(ushort type, byte[] body)
        {
            switch ((PId)type)
            {
                case PId.LoginRes:
                {
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
                    var ntf = CharacterListNtf.Parser.ParseFrom(body);
                    if (ntf.Characters.Count == 0)
                    {
                        var jobs = m_ctx.Config.Create.JobIds;
                        int jobId = (jobs != null && jobs.Length > 0) ? jobs[m_rng.Next(jobs.Length)] : 1;
                        m_conn.Send(PId.CharacterCreateReq, new CharacterCreateReq
                        {
                            Name = LoginId,
                            JobId = jobId,
                            AppearancePresetId = 0,
                        });
                        State = BotState.WaitCreate;
                    }
                    else
                    {
                        m_conn.Send(PId.CharacterSelectReq,
                            new CharacterSelectReq { CharacterId = ntf.Characters[0].CharacterId });
                        State = BotState.WaitSelect;
                    }
                    break;
                }

                case PId.CharacterCreateRes:
                {
                    if (State != BotState.WaitCreate) break;
                    var res = CharacterCreateRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess || res.NewCharacter == null)
                    {
                        Fail($"create failed: {res.ErrorMsg}");
                        return;
                    }
                    m_conn.Send(PId.CharacterSelectReq,
                        new CharacterSelectReq { CharacterId = res.NewCharacter.CharacterId });
                    State = BotState.WaitSelect;
                    break;
                }

                case PId.CharacterSelectRes:
                {
                    if (State != BotState.WaitSelect) break;
                    var res = CharacterSelectRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess) { Fail($"select failed: {res.ErrorMsg}"); return; }
                    m_myObjectId = res.Character?.CharacterId ?? 0; // in-stage object id = character_id
                    StageDataKey = res.StageDataKey;
                    LoadNavMesh(StageDataKey);
                    m_conn.Send(PId.StageLoadCompleteReq, new StageLoadCompleteReq());
                    State = BotState.WaitStageLoad;
                    break;
                }

                case PId.StageLoadCompleteRes:
                {
                    if (State != BotState.WaitStageLoad) break;
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
                    m_skillReadyAt.Clear();
                    m_skillRotation = 0;
                    m_behavior = Behavior.Roaming;
                    long t = m_ctx.NowMs;
                    m_nextRepickMs = t; // 즉시 목적지 선정
                    m_nextSkillMs = t + m_ctx.Config.Skill.UseIntervalMs;
                    m_nextStageMoveMs = t + m_ctx.Config.StageMove.IntervalMs;
                    m_nextDisconnectMs = t + m_ctx.Config.Disconnect.CheckIntervalMs;
                    State = BotState.InStage;
                    break;
                }

                case PId.ObjectVisibilityNtf:
                {
                    var ntf = ObjectVisibilityNtf.Parser.ParseFrom(body);
                    foreach (var m in ntf.MonsterSpawns)
                        m_monsters[m.ObjectId] = new Mon { Pos = new Vector3(m.PosX, m.PosY, m.PosZ), Dead = m.IsDead };
                    foreach (long id in ntf.DespawnIds)
                        m_monsters.Remove(id);
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
                    if (m_monsters.TryGetValue(ntf.ObjectId, out Mon mon))
                        mon.Dead = true; // 사망 즉시 반영 → 대상에서 제외
                    break;
                }

                case PId.StageMoveRes:
                {
                    if (State != BotState.WaitStageMoveRes) break;
                    var res = StageMoveRes.Parser.ParseFrom(body);
                    if (res.ResultCode != ResultSuccess)
                    {
                        // 이동 거부 → 현재 스테이지에서 계속 플레이
                        State = BotState.InStage;
                        break;
                    }
                    // 성공: 목적지 NavMesh 로 교체하고 2단계 입장(로딩완료 보고)
                    StageDataKey = m_pendingStageKey;
                    m_monsters.Clear();
                    m_moving = false;
                    m_path.Clear();
                    LoadNavMesh(StageDataKey);
                    m_conn.Send(PId.StageLoadCompleteReq, new StageLoadCompleteReq());
                    StageMoves++;
                    State = BotState.WaitStageLoad;
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

            bool hasEnemy = FindNearestEnemy(m_ctx.Config.Skill.DetectRange, out long enemyId, out Vector3 enemyPos);

            if (m_behavior == Behavior.Roaming)
            {
                if (hasEnemy) EnterCombat(nowMs);
                else { RoamTick(nowMs, dtMs); return; }
            }

            // ── 전투 중 ──
            if (!hasEnemy) { ExitCombat(nowMs); return; }

            float dist = MathF.Sqrt(DistSqXZ(m_pos, enemyPos));
            if (dist > m_ctx.Config.Skill.Range)
            {
                ChaseTick(nowMs, dtMs, enemyPos);        // 사거리 밖 → 접근
            }
            else
            {
                if (m_moving) StopMoving(nowMs);          // 사거리 안 → 정지 후
                CastRotation(nowMs, enemyId, enemyPos);  // 스킬 로테이션 시전
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
        private void RoamTick(long nowMs, int dtMs)
        {
            if (!m_moving)
            {
                if (nowMs >= m_nextRepickMs) PickRoamDestination(nowMs);
                return;
            }
            if (AdvancePath(nowMs, dtMs)) // 도착
            {
                StopMoving(nowMs);
                m_nextRepickMs = nowMs + m_ctx.Config.Move.RepickDelayMs;
            }
        }

        private void PickRoamDestination(long nowMs)
        {
            float x = Lerp(m_worldMinX, m_worldMaxX, (float)m_rng.NextDouble());
            float z = Lerp(m_worldMinZ, m_worldMaxZ, (float)m_rng.NextDouble());
            if (SetPathTo(new Vector3(x, m_pos.Y, z)))
                SendMoveIntent(nowMs, EMoveIntent.MoveIntentTo);
            else
                m_nextRepickMs = nowMs + m_ctx.Config.Move.RepickDelayMs;
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
        private void CastRotation(long nowMs, long targetId, Vector3 targetPos)
        {
            // 전역 시전 간격(GCD 유사): 여러 스킬을 같은 순간에 쏘지 않도록.
            if (nowMs < m_nextSkillMs) return;

            var keys = m_ctx.Config.Skill.SkillKeys;
            if (keys == null || keys.Length == 0) { m_nextSkillMs = nowMs + 1000; return; }

            // 라운드로빈: 마지막 시전 다음 인덱스부터 스캔해 "쿨타임이 돈" 첫 스킬 선택.
            // → 쿨 0인 1001 이 앞자리를 독점해 1003/1008 을 굶기는 현상 방지.
            int chosen = -1;
            for (int i = 0; i < keys.Length; i++)
            {
                int idx = (m_skillRotation + i) % keys.Length;
                if (nowMs >= SkillReadyAt(keys[idx])) { chosen = idx; break; }
            }
            if (chosen < 0) { m_nextSkillMs = nowMs + 100; return; } // 전부 쿨 → 곧 재확인
            m_skillRotation = chosen + 1;

            int skillKey = keys[chosen];
            SkillInfo info = m_ctx.Skills?.Get(skillKey);

            // 방향: 캐스터→타겟 (XZ 평면)
            float dx = targetPos.X - m_pos.X, dz = targetPos.Z - m_pos.Z;
            float len = MathF.Sqrt(dx * dx + dz * dz);
            if (len > 1e-4f) { dx /= len; dz /= len; m_lastYaw = MathF.Atan2(dx, dz) * (180f / MathF.PI); }
            else { dx = 0; dz = 1; }

            // 배치(Placement)로 효과 중심 origin 결정 (클라 SkillSystem.cs 와 동일 규칙).
            Vector3 origin;
            SkillPlacement placement = info?.Placement ?? SkillPlacement.Caster;
            switch (placement)
            {
                case SkillPlacement.Target:  // 얼음지대/불기둥 등: 몬스터 위치에 시전
                    origin = targetPos;
                    break;
                case SkillPlacement.Forward: // 빔류: 캐스터 전방 ObbLength/2
                    float fwd = (info?.ObbLength ?? 0f) * 0.5f;
                    origin = new Vector3(m_pos.X + dx * fwd, m_pos.Y, m_pos.Z + dz * fwd);
                    break;
                default:                     // Caster/None: 투사체 발사점 = 캐스터
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
            m_skillReadyAt[skillKey] = nowMs + (info?.CooldownMs ?? 0);
            SkillsCast++;
            m_nextSkillMs = nowMs + m_ctx.Config.Skill.UseIntervalMs;
        }

        private long SkillReadyAt(int skillKey)
            => m_skillReadyAt.TryGetValue(skillKey, out long t) ? t : 0;

        // ── 스테이지 이동(치트 stage 와 동일 경로) ───────────────────────
        private void UpdateStageMove(long nowMs)
        {
            // 전투 중에는 스테이지 이동을 미룬다(타이머도 진행 안 시킴 → 전투 종료 후 시도).
            if (m_behavior == Behavior.Fighting) return;
            if (nowMs < m_nextStageMoveMs) return;
            m_nextStageMoveMs = nowMs + m_ctx.Config.StageMove.IntervalMs;

            if (m_rng.NextDouble() > m_ctx.Config.StageMove.Probability) return;

            var stages = m_ctx.Config.StageKeys;
            if (stages == null || stages.Length == 0) return;

            // 현재와 다른 스테이지를 우선 선택 (같은 곳으로 이동해도 서버는 허용하지만 의미 적음)
            int target = stages[m_rng.Next(stages.Length)];
            if (stages.Length > 1 && target == StageDataKey)
                target = stages[(Array.IndexOf(stages, target) + 1) % stages.Length];

            m_pendingStageKey = target;
            m_moving = false;
            const int positionTypeDefault = 1; // EStagePositionType.Default
            m_conn.Send(PId.StageMoveReq, new StageMoveReq
            {
                TargetStageDataKey = target,
                PositionType = positionTypeDefault,
                TargetGameServerId = 0,
            });
            State = BotState.WaitStageMoveRes;
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
