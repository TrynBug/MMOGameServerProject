using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 한 액터(캐릭터/몬스터)가 현재 보유한 버프 상태를 보관/조회하는 경량 보관소.
    // StatHolder 의 버프 버전이다. 클라는 버프를 계산하지 않고 서버가 내려준 정보만 들고 있다가
    // UI(버프 바/뱃지)가 읽는 용도다.
    //
    // 서버가 채워주는 경로:
    //   - BuffNtf       : upsert (같은 buffKey 면 스택/남은시간 갱신, 없으면 추가)
    //   - BuffRemoveNtf : 제거 (만료/디스펠/정리 공통)
    //   - spawn 스냅샷  : 시야 진입(=재스폰) 시 전체 목록 → ApplySnapshot 으로 통째 교체
    //
    // 남은시간 처리:
    //   서버는 패킷 시점의 RemainTimeMs 만 보낸다(-1=영구). 클라가 매 프레임 줄어드는 카운트다운을
    //   보여주려면 "받은 시점"을 같이 저장해 두고 (RemainTimeMs/1000 - 경과초) 로 계산한다.
    //   링 게이지 비율은 GameData_Buff.DurationMs 로 나눠 UI 가 구한다.
    public class BuffHolder
    {
        // 버프 1개의 표시 상태.
        public class Entry
        {
            public long  BuffKey;
            public int   StackCount;
            public int   RemainTimeMs;       // -1 = 영구
            public float ReceivedRealtime;   // 받은 시점 (Time.realtimeSinceStartup)

            public bool IsPermanent => RemainTimeMs < 0;

            // 지금 기준 남은 시간(초). 영구면 +무한대, 음수면 0.
            public float RemainSecondsNow()
            {
                if (IsPermanent)
                    return float.PositiveInfinity;
                float elapsed = Time.realtimeSinceStartup - ReceivedRealtime;
                float remain = (RemainTimeMs / 1000f) - elapsed;
                return remain > 0f ? remain : 0f;
            }
        }

        // buffKey -> Entry.
        private readonly Dictionary<long, Entry> m_buffs = new Dictionary<long, Entry>();

        // 현재 보유 버프 (UI 순회용, 읽기 전용).
        public IReadOnlyDictionary<long, Entry> Buffs => m_buffs;
        public int Count => m_buffs.Count;

        // 추가/갱신 (BuffNtf). 같은 buffKey 면 스택/남은시간/받은시점 갱신.
        public void Upsert(long buffKey, int stackCount, int remainTimeMs)
        {
            if (!m_buffs.TryGetValue(buffKey, out Entry e))
            {
                e = new Entry { BuffKey = buffKey };
                m_buffs[buffKey] = e;
            }
            e.StackCount = stackCount;
            e.RemainTimeMs = remainTimeMs;
            e.ReceivedRealtime = Time.realtimeSinceStartup;
        }

        // 제거 (BuffRemoveNtf). 없으면 no-op.
        public void Remove(long buffKey)
        {
            m_buffs.Remove(buffKey);
        }

        public void Clear()
        {
            m_buffs.Clear();
        }
    }
}
