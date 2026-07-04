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

        // 이동속도(정규화 0~1)를 직접 지정. 0=idle, 0.5=walk, 1=run 블렌드.
        // SetMoving 보다 세밀한 제어(가감속)가 필요할 때 사용. 구현체가 Speed 파라미터가 없으면 무시한다.
        void SetSpeed(float speed01);

        // 사망 연출 1회 재생. 사망 시점(ObjectDeathNtf)에 호출한다 — 사망 애니메이션을 처음부터 재생.
        // 구현체가 Animator 에 사망 트리거/상태가 없으면 조용히 무시한다.
        void PlayDead();

        // 사망 끝 포즈로 즉시 고정. corpse 상태로 늦게 spawn 될 때 호출한다 — 애니메이션 재생 없이 마지막 프레임(쓰러진 포즈)부터 표시.
        // 구현체가 사망 상태가 없으면 조용히 무시한다.
        void SetDeadPose();

        // 공격/스킬 시전(윈드업) 1회 재생. 몬스터/NPC 가 AbilityCastNtf 를 받은 시점에 호출한다.
        // 구현체가 Animator 에 시전 트리거가 없으면 조용히 무시한다.
        void PlaySkill();

        // 임의의 원샷 상태를 이름으로 재생(Jump/공격/감정표현 등). cancelOnMove=true 면 이동 시작 시 자동으로 Locomotion 복귀.
        // 구현체가 해당 상태를 갖고 있지 않으면 무시한다.
        void PlayOneShot(string stateName, bool cancelOnMove);

        // 피격(one-shot) 재생.
        void PlayHit();

        // 스턴/속박 on/off. on 이면 Stun 루프 진입, off 면 Locomotion 복귀.
        void SetStunned(bool stunned);

        // 캐스팅(홀드) 진입. castSpeed 로 재생속도를 스킬 캐스팅시간에 맞춘 뒤 castState 로 CrossFade.
        void PlayCast(string castState, float castSpeed);

        // 발동(원샷) 재생. castSpeed 로 재생속도 스케일 후 fireState 로 CrossFade → 종료 시 Locomotion 복귀.
        void PlayFire(string fireState, float castSpeed);

        // 강제로 Locomotion(이동 블렌드)으로 복귀. 부활 등에서 Dead/특수 상태를 벗어날 때 사용.
        void ReturnToLocomotion();
    }
}
