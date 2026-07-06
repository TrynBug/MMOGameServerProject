using UnityEngine;

namespace Client.Game
{
    // LayerLabCharacter.controller 의 파라미터/상태 이름 계약. (설계: Document/클라이언트.md 의 "애니메이션" 절)
    //
    // 원샷/캐스팅/감정표현은 파라미터가 아니라 "상태 이름으로 직접 재생(by-name CrossFade)" 한다.
    // 따라서 여기의 문자열이 곧 컨트롤러의 상태명이며, 오타 방지를 위해 한 곳에서 상수로 관리한다.
    public static class AnimStates
    {
        // ── 파라미터 ──────────────────────────────────────────────
        public const string PSpeed     = "Speed";       // 이동 블렌드(0=idle,0.5=walk,1=run)
        public const string PCastSpeed = "CastSpeed";   // 공격/시전 재생속도 멀티플라이어
        public static readonly int HSpeed     = Animator.StringToHash(PSpeed);
        public static readonly int HCastSpeed = Animator.StringToHash(PCastSpeed);

        // ── 상태 (Base 레이어, by-name) ───────────────────────────
        public const string Locomotion = "Locomotion";  // 기본(이동 블렌드)
        public const string Jump       = "Jump";        // 원샷(이동해도 취소 안 함)
        public const string GetHit     = "GetHit";      // 피격 원샷
        public const string Stun       = "Stun";        // 루프(해제까지)
        public const string Dead       = "Dead";        // 종료(복귀 없음)

        // 공격/시전 (직업/스킬 데이터가 어떤 걸 재생할지 지정)
        public const string Melee1     = "Melee_1";
        public const string Melee2     = "Melee_2";
        public const string CastMelee  = "Cast_Melee";  // 홀드(마지막 프레임 정지)
        public const string FireMelee  = "Fire_Melee";
        public const string CastMagic  = "Cast_Magic";  // 홀드
        public const string FireMagic1 = "Fire_Magic_1";
        public const string FireMagic2 = "Fire_Magic_2";
        public const string FireMagic3 = "Fire_Magic_3";

        // ── 감정표현(상태명 = 클립명) ─────────────────────────────
        // 이동 시작 시 취소되는 원샷. (PlayerCharacter.PlayEmote)
        public static readonly string[] Dances =
        {
            "Dance_1", "Dance_2", "Dance_3", "Dance_4",
        };
        public static readonly string[] Emojis =
        {
            "Emoji_Aghast", "Emoji_Angry", "Emoji_Applaud", "Emoji_Be_Bashful",
            "Emoji_Cheer", "Emoji_Cry", "Emoji_Gas", "Emoji_Hi", "Emoji_Nice",
            "Emoji_Pester", "Emoji_Putter_Around", "Emoji_Showmanship",
            "Emoji_SideToSide", "Emoji_Sigh", "Emoji_Smile1", "Emoji_Smile2",
        };
    }

    // Animator 를 "상태/파라미터 존재 여부"로 안전하게 구동하는 헬퍼.
    //
    // 컨트롤러마다 구성이 다르다(몬스터 Slime, LayerLab 캐릭터, 아직 아트 없는 prefab 등).
    // 없는 상태/파라미터에 대한 호출은 조용히 무시하여, 하나의 드라이버 코드가 어떤 컨트롤러에도 안전하게 동작하게 한다.
    public static class AnimPlay
    {
        private const int k_baseLayer = 0;

        public static bool HasState(Animator a, int stateHash)
            => a != null && a.runtimeAnimatorController != null && a.HasState(k_baseLayer, stateHash);

        public static bool HasParam(Animator a, int hash)
        {
            if (a == null || a.runtimeAnimatorController == null) return false;
            foreach (AnimatorControllerParameter p in a.parameters)
                if (p.nameHash == hash) return true;
            return false;
        }

        // 상태의 클립을 "비활성 sentinel"로 매핑했을 때 그 클립의 이름.
        // 오버라이드 컨트롤러에서 어떤 상태의 클립을 빈 클립 또는 이름이 "None" 인 클립으로 매핑하면
        // 그 상태는 재생을 스킵한다(상태 진입 자체를 안 함). 몹별로 특정 원샷(예: GetHit)을 끄고 싶을 때,
        // 코드/데이터 변경 없이 그 몹의 오버라이드 컨트롤러에서 클립만 sentinel 로 바꾸면 된다.
        public const string DisabledClipName = "None";

        // 이 상태의 (오버라이드된) 클립이 비활성 sentinel 이면 true → 재생 스킵.
        // aoc[stateName] 은 "그 이름의 원본 클립에 매핑된 오버라이드 클립"을 돌려준다.
        // 상태명과 베이스 클립명이 같은 상태에서만 해소된다(예: GetHit 상태의 베이스 클립도 "GetHit").
        // 그 외 상태는 null 이 돌아와 항상 재생됨(안전 — 오작동으로 끄지 않는다).
        public static bool IsStateDisabled(Animator a, string stateName)
        {
            if (a != null && a.runtimeAnimatorController is AnimatorOverrideController aoc)
            {
                AnimationClip c = aoc[stateName];
                if (c != null && (c.empty || c.name == DisabledClipName))
                    return true;
            }
            return false;
        }

        // 상태명으로 CrossFade. 해당 상태가 없거나 sentinel(None/빈클립)로 꺼둔 상태면 no-op. 재생됐으면 true.
        public static bool CrossFade(Animator a, string stateName, float fade = 0.12f)
        {
            int h = Animator.StringToHash(stateName);
            if (!HasState(a, h)) return false;
            if (IsStateDisabled(a, stateName)) return false;
            a.CrossFadeInFixedTime(h, fade);
            return true;
        }

        // 상태의 특정 정규화 시간으로 즉시 점프(전이 없이). 사망 끝 포즈 고정 등.
        public static void PlayPose(Animator a, string stateName, float normalizedTime)
        {
            int h = Animator.StringToHash(stateName);
            if (HasState(a, h)) a.Play(h, k_baseLayer, normalizedTime);
        }

        public static void SetFloatSafe(Animator a, int paramHash, float v)
        {
            if (HasParam(a, paramHash)) a.SetFloat(paramHash, v);
        }

        // 매 프레임 호출용. dampTime 동안 목표값으로 보간(idle↔walk↔run 블렌드 부드럽게).
        public static void SetFloatDamped(Animator a, int paramHash, float value, float dampTime, float deltaTime)
        {
            if (HasParam(a, paramHash)) a.SetFloat(paramHash, value, dampTime, deltaTime);
        }

        // 현재 Base 레이어 상태가 stateName 인지.
        public static bool IsCurrent(Animator a, string stateName)
            => a != null && a.runtimeAnimatorController != null
               && a.GetCurrentAnimatorStateInfo(k_baseLayer).IsName(stateName);
    }

    // 코스메틱 액션 id (ActorActionReq/Ntf.action_id). 로컬이 하면 서버가 AOI relay → 원격 재생.
    public static class ActorAction
    {
        public const int Jump  = 1;
        public const int Emote = 2;
    }
}
