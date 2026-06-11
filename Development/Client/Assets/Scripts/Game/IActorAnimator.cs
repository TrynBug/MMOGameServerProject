namespace Client.Game
{
    // Actor(몬스터/원격 캐릭터/내 캐릭터 등) 공통 애니메이션 재생 인터페이스.
    //
    // 게임 로직(MonsterObject 등)은 "지금 어떤 상태인가"라는 의미만 전달하고,
    // 실제 클립 선택/블렌딩/Animator 파라미터 변환은 구현체(AnimatorActorAnimator)가 가진다.
    // 이렇게 분리해두면 게임 로직이 특정 렌더링 방식(Unity Animator)에 묶이지 않으며,
    // 나중에 군중용 GPU 애니메이션 등 다른 구현으로 갈아끼울 수 있다.
    //
    // 누가 상태를 "결정"하느냐와는 무관하다. 결정은 구동(driver) 쪽 책임이다:
    //   - 몬스터/원격 캐릭터: 서버 패킷을 받아 구동 (판단 없음).
    //   - 내 캐릭터(LocalPlayer): 입력으로 예측 구동 + 서버 보정 시 정정.
    // 이 인터페이스는 오직 "재생"만 담당한다.
    public interface IActorAnimator
    {
        // 이동/정지 전환. true 면 이동(walk/run), false 면 정지(idle).
        void SetMoving(bool isMoving);

        // 사망 연출 1회 재생. 사망 시점(ObjectDeathNtf)에 호출한다 — 사망 애니메이션을 처음부터 재생.
        // 구현체가 Animator 에 사망 트리거가 없으면 조용히 무시한다.
        void PlayDead();

        // 사망 끝 포즈로 즉시 고정. corpse 상태로 늦게 spawn 될 때 호출한다 — 애니메이션 재생 없이 마지막 프레임(쓰러진 포즈)부터 표시.
        // 구현체가 사망 상태가 없으면 조용히 무시한다.
        void SetDeadPose();

        // 공격/스킬 시전(윈드업) 1회 재생. 몬스터/NPC 가 AbilityCastNtf 를 받은 시점에 호출한다.
        // 구현체가 Animator 에 시전 트리거가 없으면 조용히 무시한다.
        void PlaySkill();

        // (추후) 피격(one-shot) 은 해당 기능이 실제로 들어올 때 추가한다.
        //   void PlayHit();
    }
}
