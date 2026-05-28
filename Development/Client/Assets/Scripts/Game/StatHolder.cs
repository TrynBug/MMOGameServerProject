using System.Collections.Generic;
using GameData;

namespace Client.Game
{
    // 서버가 계산해 내려준 스탯값을 보관하고 조회하는 경량 보관소.
    //
    // 클라는 스탯을 계산하지 않는다(대미지/최대치 등 모든 계산은 서버가 권위적으로 수행).
    // 이 클래스는 StatUpdateNtf 로 받은 값을 들고 있다가 UI(정보창)/HP·MP바가 읽는 용도다.
    // 그래서 서버의 StatComponent 처럼 누적 공식/Op/조밀배열·희소맵 구분이 전혀 없다.
    //
    // "값이 0인 스탯은 서버가 보내지 않는다"는 규약과 맞물려, 보관되지 않은 스탯 조회는 0을 리턴한다.
    //
    // 보관 대상은 EStat 전체(최대HP/MP=HpTotal/MpTotal, 이동속도, 공격속도, 힘 등)이며,
    // 현재 HP/MP 는 EStat 이 아닌 런타임 값이라 여기 두지 않는다(ActorObject 가 직접 보유).
    //
    // 패킷 타입에 의존하지 않기 위해 패킷→holder 변환은 핸들러(StageManager)가 담당한다.
    // 여기서는 Set/Get/Clear 만 제공한다.
    public class StatHolder
    {
        // EStat -> 값. 0이 아닌 스탯만 들어있다.
        private readonly Dictionary<EStat, double> m_stats = new Dictionary<EStat, double>();

        // 스냅샷 갱신은 보통 "통째로 교체"다(서버가 전체 재전송).
        // 새 스냅샷을 적용하기 전에 호출한다.
        public void Clear()
        {
            m_stats.Clear();
        }

        // 스탯 1개 설정. 핸들러가 StatUpdateNtf 의 각 entry 를 이걸로 넣는다.
        public void Set(EStat stat, double value)
        {
            m_stats[stat] = value;
        }

        // 스탯값 조회. 보관되지 않은 스탯은 0.
        // (서버가 0인 스탯을 안 보내므로, 미보관 == 0 규약이 성립한다.)
        public double Get(EStat stat)
        {
            return m_stats.TryGetValue(stat, out double v) ? v : 0.0;
        }
    }
}
