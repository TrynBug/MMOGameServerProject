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

        // (추후) 스킬 시전/피격/사망 등 one-shot 은 해당 기능이 실제로 들어올 때 추가한다.
        //   void PlaySkill(int skillId);
        //   void PlayHit();
        //   void PlayDead();
    }
}
