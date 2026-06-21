using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 본인(LocalPlayer) 캐릭터의 서버 화해(reconciliation) — Phase 1: 시간정렬 오차 측정.
    //
    // 핵심: 서버 권위 위치 authPos 는 "서버시각 T_snap 시점의 위치"다. 이걸 클라의 *현재* 위치와 비교하면
    // 시간축이 ~RTT 어긋나 고핑에서 뒤로 당김이 생긴다(구 RTT 외삽 방식의 한계). 대신 클라는 매 프레임
    // 자기 예측 위치를 "추정 서버시각"으로 스탬프해 히스토리에 쌓아두고, 스냅샷이 오면 **그 T_snap 시점에
    // 내가 예측했던 위치(predThen)** 와 authPos 를 비교한다. 같은 시간축 비교라 오차는 순수 예측오차(≈양자화)
    // 가 되고, RTT 추정/직선 외삽/속도비례 데드존 같은 근사 장치가 전부 사라진다.
    //
    // 측정된 오차(T_snap 기준)는 그 이후 같은 경로를 따라왔으므로 현재 위치에도 거의 그대로 남아 있다 →
    // m_posError 에 적립해 ApplyBleed 가 매 프레임 부드럽게 흡수한다.
    //
    // PlayerCharacter 가 소유하고 위임한다:
    //   - 매 프레임(예측 이동 후)  → RecordPrediction(추정서버시각, 현재위치)
    //   - 스냅샷(본인) 수신 시       → Reconcile(T_snap, authPos)
    //   - 매 프레임                  → ApplyBleed
    //   - 텔포/스폰/하드스냅 후       → Clear
    //
    // Phase 2(미구현): 하드보정 시 커맨드 타임라인 리플레이로 현재 재구성(NotifyInputSent 가 그 입력 기록).
    public class PlayerReconciler
    {
        private const float  k_reconcileHardSnap   = 3.0f;    // 이 초과 오차는 명백한 desync(텔레포트/넉백/거부) → 즉시 스냅
        private const float  k_reconcileDeadzone   = 0.2f;    // 이동 중 데드존. 시간정렬이라 작아도 됨(오차≈양자화).
        private const float  k_stopDeadzoneFactor  = 0.12f;   // 정지 데드존 = max(k_reconcileDeadzone, moveSpeed*이값). 환원불가 예측선행(~1틱)+양자화 흡수.
        private const float  k_posErrorBleedPerSec = 6f;      // 위치 오차를 매 프레임 풀어내는 속도(1/s). 클수록 빨리 보정.
        private const double k_historyWindowMs     = 1000.0;  // 예측 히스토리 보관 윈도우(RTT 여유).

        // 서버 기준으로 당겨야 할 잔여 위치 오차. 매 프레임 조금씩 적용해 소진(스냅샷율과 무관, 부드러움).
        private Vector3 m_posError;

        // 마지막으로 송신한 이동 의도 seq(게이팅용). ack 가 이걸 따라잡아야(서버가 최신 의도 반영) 소프트 보정.
        private uint m_latestInputSeq;

        // 예측 위치 히스토리: 추정 서버시각으로 스탬프된 (시각, 위치). 시각 오름차순.
        private struct PredSample { public double serverMs; public Vector3 pos; }
        private readonly List<PredSample> m_history = new List<PredSample>(128);

        // 매 프레임(예측 이동 + 블리딩 적용 후) 최종 렌더 위치를 추정 서버시각으로 스탬프해 기록한다.
        // 블리딩 이후 위치를 기록하므로(이미 일부 보정된 상태), 다음 측정은 남은 잔차만 본다(이중계산 방지).
        public void RecordPrediction(double serverMs, Vector3 pos)
        {
            int n = m_history.Count;
            if (n > 0 && serverMs <= m_history[n - 1].serverMs)
            {
                // 같은/역행 시각(프레임>tick, 시계 미세 역행): 마지막 항목 위치만 갱신.
                var last = m_history[n - 1];
                last.pos = pos;
                m_history[n - 1] = last;
            }
            else
            {
                m_history.Add(new PredSample { serverMs = serverMs, pos = pos });
            }

            // 윈도우 밖 prune.
            double cutoff = serverMs - k_historyWindowMs;
            int drop = 0;
            while (drop < m_history.Count && m_history[drop].serverMs < cutoff)
                drop++;
            if (drop > 0)
                m_history.RemoveRange(0, drop);
        }

        // 본인 캐릭터 화해. 서버 SnapshotNtf 의 본인 항목으로 호출된다.
        //   snapServerMs = authPos 의 서버시각(= server_tick_seq * 50ms)
        //   authPos      = 그 시점의 서버 권위 위치
        // T_snap 시점의 예측 위치(predThen)와 비교해 오차를 m_posError 에 적립한다.
        // 큰 desync(또는 히스토리 미커버)면 true 반환 → 호출자가 authPos 로 즉시 스냅(SetPosition).
        public bool Reconcile(Transform tf, double snapServerMs, Vector3 authPos, uint ackSeq,
                              bool moving, bool skillMoving, float moveSpeed)
        {
            if (skillMoving)
                return false;   // 스킬 강제이동은 서버와 동일 공식 → 보정 생략.

            if (!trySampleAt(snapServerMs, out Vector3 predThen))
            {
                // 히스토리가 이 시각을 못 덮음(접속/스테이지 이동/텔포 직후) → 권위로 스냅.
                m_posError = Vector3.zero;
                return true;
            }

            Vector3 err = authPos - predThen;   // ★시간정렬 순수 예측오차(RTT 외삽 불필요)
            float dist = err.magnitude;

            // 큰 desync(텔레포트/넉백/거부)는 게이트와 무관하게 무조건 즉시 스냅.
            if (dist > k_reconcileHardSnap)
            {
                m_posError = Vector3.zero;
                return true;
            }

            // 게이팅: 서버가 아직 우리의 최신 의도를 반영 못 했으면(ack < 최신 seq = 입력 in-flight) 소프트 보정 생략.
            // 출발/방향전환/정지 직후의 RTT 윈도우가 여기 해당 — 이때의 예측 선행(legitimate lead)은 아직 권위에
            // 반영되지 않아 오차처럼 보이므로, 보정하면 "제자리 stall + 밀림" 트랜지언트가 생긴다. 예측을 신뢰한다.
            // 잔여 m_posError 는 ApplyBleed 가 자연 소진(여기서 건드리지 않음).
            if (ackSeq < m_latestInputSeq)
                return false;

            // 데드존: 이동 중엔 작게(시간정렬이 선행을 상쇄). 정지 중엔 속도 비례로 크게 — 멈춘 객체는
            // 예측 선행(~1틱)을 화해로 없앨 수 없고(클라 프레임정밀 정지 vs 서버 틱정밀 정지), 이를 보정하면
            // "뒤로 당김"이 된다. 그만큼은 무시한다.
            float deadzone = moving
                ? k_reconcileDeadzone
                : Mathf.Max(k_reconcileDeadzone, moveSpeed * k_stopDeadzoneFactor);

            m_posError = (dist > deadzone) ? err : Vector3.zero;
            return false;
        }

        // 히스토리에서 serverMs 시점의 예측 위치를 선형보간으로 뽑는다.
        // serverMs 가 히스토리보다 과거면 false(미커버). 최신 이후면 최신으로 클램프.
        private bool trySampleAt(double serverMs, out Vector3 pos)
        {
            pos = Vector3.zero;
            int n = m_history.Count;
            if (n == 0)
                return false;
            if (serverMs < m_history[0].serverMs)
                return false;   // 너무 과거 → 커버 불가.
            if (serverMs >= m_history[n - 1].serverMs)
            {
                pos = m_history[n - 1].pos;   // 최신 이후(드묾) → 최신으로 클램프.
                return true;
            }

            // serverMs 를 감싸는 두 샘플 [i-1, i] 를 뒤에서부터 찾아 보간(최근 구간일수록 빨리 찾음).
            for (int i = n - 1; i > 0; i--)
            {
                PredSample a = m_history[i - 1];
                if (a.serverMs <= serverMs)
                {
                    PredSample b = m_history[i];
                    double span = b.serverMs - a.serverMs;
                    float t = (span > 1e-6) ? (float)((serverMs - a.serverMs) / span) : 0f;
                    pos = Vector3.Lerp(a.pos, b.pos, t);
                    return true;
                }
            }

            pos = m_history[0].pos;
            return true;
        }

        // 위치 오차(m_posError)를 매 프레임 일정 비율로 풀어 적용한다(스냅샷율 무관, 부드러움).
        // 이동 중에도 동작 — 동기화돼 있으면 오차≈0 이라 떨림 없음.
        public void ApplyBleed(Transform tf)
        {
            if (m_posError.sqrMagnitude < 1e-8f)
                return;
            float f = 1f - Mathf.Exp(-k_posErrorBleedPerSec * Time.deltaTime);
            Vector3 step = m_posError * f;
            tf.position += step;
            m_posError -= step;
        }

        // 텔포/스폰/하드스냅 후 호출. stale 히스토리와의 비교를 막기 위해 전부 리셋.
        public void Clear()
        {
            m_history.Clear();
            m_posError = Vector3.zero;
            m_latestInputSeq = 0;
        }

        // PlayerMoveController 가 MoveIntentReq 송신 직후 호출.
        // 최신 송신 seq 를 기록해 게이팅에 쓴다(ack 가 이걸 따라잡아야 소프트 보정).
        // (Phase 2 에선 여기서 커맨드 히스토리 seq/추정서버시각/dest/isStop 도 함께 적재한다.)
        public void NotifyInputSent(uint seq, Vector3 dest, bool isStop)
        {
            m_latestInputSeq = seq;
        }
    }
}
