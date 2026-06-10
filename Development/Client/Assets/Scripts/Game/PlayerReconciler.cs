using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 본인(LocalPlayer) 캐릭터의 서버 화해(reconciliation).
    //
    // 서버 authPos 는 ~RTT 과거다. 그래서 authPos 를 "그동안 갈 거리(RTT)"만큼 앞으로 재시뮬한
    // 기대 위치(expected)와 현재 예측을 비교한다. 이러면 예측 선행을 desync 로 오인해 뒤로 당기지 않고,
    // 이동 중에도 정밀 보정된다. 저핑(localhost)에선 오차≈0 → 보정 거의 없음(현 동작과 동일). 고핑에서 효과.
    //
    // PlayerCharacter 가 소유하고 위임한다:
    //   - 입력 송신 직후      → NotifyInputSent
    //   - 스냅샷(본인) 수신 시 → Reconcile
    //   - 매 프레임           → ApplyBleed
    //
    // 책임 아님: 이동 시뮬레이션(LocalPlayerMover), 애니메이션/원격보간(PlayerCharacter).
    public class PlayerReconciler
    {
        private const float k_reconcileHardSnap   = 3.0f;   // 이 초과 오차는 명백한 desync(텔레포트/넉백/거부) → 즉시 스냅
        private const float k_reconcileDeadzone   = 0.3f;   // 이 이내 오차는 무시(예측 신뢰, 잔떨림 방지)
        private const float k_posErrorBleedPerSec = 6f;     // 위치 오차를 매 프레임 풀어내는 속도(1/s). 클수록 빨리 보정.

        // 서버 기준으로 당겨야 할 잔여 위치 오차. 매 프레임 조금씩 적용해 소진(스냅샷율과 무관, 부드러움).
        private Vector3 m_posError;

        // RTT 추정(ms, EMA) + 입력 송신시각 기록(seq→localMs). ack 로 RTT 측정 + replay 시간 산출.
        private float m_rttMs;
        private readonly Dictionary<uint, float> m_inputSendMs = new Dictionary<uint, float>(16);
        private readonly List<uint> m_pruneScratch = new List<uint>(16);

        // 최신 송신 이동 의도(게이팅용). 이 seq 가 ack 돼야 서버가 현재 dest 로 움직이는 중이라 본다.
        private uint    m_latestInputSeq;
        private Vector3 m_latestDest;
        private bool    m_latestIsStop = true;

        // PlayerMoveController 가 MoveIntentReq 송신 직후 호출. RTT 측정 + 게이팅용 기록.
        public void NotifyInputSent(uint seq, Vector3 dest, bool isStop)
        {
            m_inputSendMs[seq] = Time.unscaledTime * 1000f;
            m_latestInputSeq = seq;
            m_latestDest = dest;
            m_latestIsStop = isStop;
        }

        // 본인 캐릭터 화해. 서버 SnapshotNtf 의 본인 항목으로 호출된다.
        //   ackSeq  = 서버가 마지막으로 처리한 입력 seq
        //   authPos = 그 시점(≈RTT 과거)의 서버 권위 위치
        // authPos 를 RTT 만큼 앞으로 재시뮬한 기대 위치와 현재 예측의 오차를 m_posError 에 적립한다.
        // 큰 desync 면 true 를 반환 → 호출자가 authPos 로 즉시 스냅(SetPosition)해야 한다.
        public bool Reconcile(Transform tf, Vector3 authPos, uint ackSeq, bool hasMoveDest, bool skillMoving, float moveSpeed)
        {
            if (skillMoving)
                return false;   // 스킬 강제이동은 서버와 동일 공식 → 보정 생략.

            // RTT 추정: ack 된 입력의 송신시각과 현재의 차이 (EMA 로 안정화).
            if (m_inputSendMs.TryGetValue(ackSeq, out float sentMs))
            {
                float sample = Time.unscaledTime * 1000f - sentMs;
                m_rttMs = (m_rttMs <= 0f) ? sample : Mathf.Lerp(m_rttMs, sample, 0.1f);
                pruneInputRecords(ackSeq);
            }

            // 큰 desync(텔레포트/넉백/거부)는 무조건 즉시 스냅.
            if (Vector3.Distance(tf.position, authPos) > k_reconcileHardSnap)
            {
                m_posError = Vector3.zero;
                return true;
            }

            // 게이팅: 서버가 아직 우리의 최신 이동 의도를 반영 못 했으면(ack < 최신 seq) 소프트 보정 생략.
            // (서버가 옛 dest 로 움직이는 중이라, 비교하면 dest 지연을 desync 로 오인한다.)
            if (ackSeq < m_latestInputSeq)
                return false;

            // authPos 를 RTT 만큼 앞으로 재시뮬해 "지금 있어야 할" 기대 위치를 만든다.
            // 이동 중이면 서버가 향하는 dest(최신 acked dest) 방향으로 straight 재시뮬(짧은 구간이라 navmesh 근사 무시).
            Vector3 expected = authPos;
            if (hasMoveDest && !m_latestIsStop)
            {
                float replayDist = moveSpeed * (m_rttMs / 1000f);
                expected = Vector3.MoveTowards(authPos, m_latestDest, replayDist);
            }

            Vector3 err = expected - tf.position;

            // 데드존: 이동 중엔 작게(0.3) — RTT 재시뮬 보상이 선행을 이미 상쇄하므로 잔오차만 본다.
            //         정지 중엔 속도 비례로 크게 — 멈춘 객체는 앞으로 재시뮬할 수 없어 예측 선행(~1틱)이
            //         그대로 오차로 남는데, 이를 보정하면 "뒤로 당김"이 생긴다. 그래서 그만큼은 무시한다.
            float deadzone = (hasMoveDest && !m_latestIsStop)
                ? k_reconcileDeadzone
                : Mathf.Max(k_reconcileDeadzone, moveSpeed * 0.12f);

            m_posError = (err.magnitude > deadzone) ? err : Vector3.zero;
            return false;
        }

        // ackSeq 이하의 송신 기록 제거(메모리 누수 방지). 작은 dict 라 단순 순회.
        private void pruneInputRecords(uint ackSeq)
        {
            m_pruneScratch.Clear();
            foreach (var kv in m_inputSendMs)
                if (kv.Key <= ackSeq) m_pruneScratch.Add(kv.Key);
            for (int i = 0; i < m_pruneScratch.Count; i++)
                m_inputSendMs.Remove(m_pruneScratch[i]);
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
    }
}
