using GameData;
using UnityEngine;

namespace Client.Game
{
    // 상호작용 가능한 액터(캐릭터, 향후 몬스터/펫/NPC)의 공통 베이스.
    //
    // 서버의 ActorObject 와 같은 역할이되, 클라는 Unity 컴포넌트 계층이라
    // MonoBehaviour 를 상속한다. PlayerCharacter 가 이 클래스를 상속한다.
    //
    // ── 무엇을 들고 있나 ──
    //   - 현재 HP/MP (CurHp/CurMp) : 대미지/회복으로 자주 바뀌는 런타임 값.
    //                                EStat 이 아니라 별도 멤버. HpMpNtf 로 갱신.
    //   - StatHolder               : 그 외 모든 합성 스탯(최대HP/MP, 이동속도, 공격속도, 힘 등).
    //                                StatUpdateNtf 로 갱신. 최대치는 여기서 Get 으로 읽는다.
    //   - BuffHolder               : 현재 보유 버프(키/스택/남은시간). BuffNtf/BuffRemoveNtf,
    //                                spawn 스냅샷으로 갱신. 버프 UI(바/뱃지)가 읽는다.
    //
    // 클라는 스탯을 계산하지 않으므로 서버의 StatComponent 누적 공식은 갖지 않는다.
    // 받은 값을 보관/조회할 뿐이다.
    //
    // 이동/애니메이션 등 캐릭터 전용 로직은 베이스에 두지 않는다(PlayerCharacter 가 가짐).
    // 몬스터가 클라에 구현될 때, 캐릭터와의 실제 공통점을 보고 이 베이스로 끌어올린다.
    //
    // ── HP/MP 적용 순서 주의 ──
    // SetCurHp/SetCurMp 는 MaxHp/MaxMp(=StatHolder 의 HpTotal/MpTotal)로 clamp 한다.
    // 그래서 서버는 입장 시 StatUpdateNtf(최대치 포함)를 HpMpNtf 보다 먼저 보내야 한다.
    // (최대치가 0인 상태에서 현재HP 를 받으면 0으로 clamp 되어버린다.)
    public abstract class ActorObject : MonoBehaviour
    {
        // 서버가 내려준 합성 스탯 보관소. 최대HP/MP, 이동속도, 공격속도 등을 여기서 읽는다.
        public StatHolder Stats { get; } = new StatHolder();

        // 서버가 내려준 현재 보유 버프 보관소. 버프 UI 가 읽는다.
        public BuffHolder Buffs { get; } = new BuffHolder();

        // ── 현재 HP / MP ───────────────────────────────────────
        public double CurHp { get; private set; }
        public double CurMp { get; private set; }

        // ── 최대 HP / MP ───────────────────────────────────────
        // 최대치는 합성 스탯의 Total 값이다. StatHolder 에서 읽는다.
        // (보관되지 않았으면 0 — 서버가 아직 StatUpdateNtf 를 안 보낸 상태 등.)
        public double MaxHp => Stats.Get(EStat.HpTotal);
        public double MaxMp => Stats.Get(EStat.MpTotal);

        // [0, 최대치] 로 clamp 하여 설정. HpMpNtf 핸들러가 호출한다.
        public void SetCurHp(double hp) { CurHp = clampToRange(hp, MaxHp); }
        public void SetCurMp(double mp) { CurMp = clampToRange(mp, MaxMp); }

        // 현재 HP/MP 를 각각의 최대치로 채운다.
        public void FillHp() { CurHp = MaxHp; }
        public void FillMp() { CurMp = MaxMp; }

        // 사망 여부. 서버와 동일하게 명시적 상태이다(HP 0 파생 아님). OnDeath() 로 1회 전환된다.
        // ObjectDeathNtf 수신 시, 또는 corpse 상태로 spawn 될 때 설정된다.
        public bool IsDead { get; private set; }

        // 사망으로 전환. 이미 사망 상태면 no-op (멱등). 파생(MonsterObject)은 이동 정지/사망 애니메이션을 덧붙인다.
        public virtual void OnDeath()
        {
            IsDead = true;
        }

        // 부활. 사망 상태를 해제하고 자원(HP/MP)을 복원한다. ObjectReviveNtf 수신 시 호출된다.
        // 위치는 호출측(부활 핸들러)이 별도로 세팅한다(스폰과 동일 규약).
        // 파생(PlayerCharacter)은 애니 복귀를 덧붙인다. 입력 차단은 IsDead 게이트라 여기서 false 가 되면 자동 재개된다.
        public virtual void Revive(double hp, double mp)
        {
            IsDead = false;
            SetCurHp(hp);
            SetCurMp(mp);
        }

        // value 를 [0, maxValue] 로 clamp. maxValue 가 음수면 0 으로 본다.
        private static double clampToRange(double value, double maxValue)
        {
            double hi = (maxValue > 0.0) ? maxValue : 0.0;
            if (value < 0.0) return 0.0;
            if (value > hi) return hi;
            return value;
        }
    }
}
