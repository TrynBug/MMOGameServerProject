using UnityEngine;

namespace Client.Game
{
    // 원격 액터(타 유저/몬스터)의 위치를 매 프레임 "재생(표시)"하는 드라이버의 계약.
    //
    // ★핵심 설계★: 재생 루프(MonsterObject/PlayerCharacter)를 "어떤 데이터로 어떤 타임라인에서
    // 재생하는가"와 분리한다. 구현체가 자신의 타임라인(NetClock 의 어느 시각을 읽을지)을 소유한다.
    // 호출자는 Sample() 결과만 transform 에 적용할 뿐, 클럭/보간/예측 방식을 알지 못한다.
    //
    // 입력(서버 패킷)의 형태는 구현체마다 다를 수 있으므로 이 인터페이스에 넣지 않는다.
    // 스냅샷 스트림 입력은 하위 인터페이스 ISnapshotMotionDriver 가 정의한다(현 유일 구현).
    //
    // 효과: 원격 재생 방식을 바꾸고 싶을 때 MonsterObject/PlayerCharacter 의 드라이버 필드만
    // 다른 구현체로 교체하면 재생 루프(this.Sample 호출부)는 그대로 둘 수 있다.
    public interface IRemoteMotionDriver
    {
        // 이번 프레임에 표시할 위치/회전(yaw)/이동상태를 계산한다. 자체 타임라인(NetClock)을 읽는다.
        // 표시할 게 없으면(준비 전/데이터 없음) false → 호출자는 현재 transform 을 유지한다.
        bool Sample(out Vector3 pos, out float yaw, out bool moving);

        // 텔레포트/스테이지 리셋 등으로 재생 상태를 비운다.
        void Clear();
    }

    // 서버 SnapshotNtf 스트림을 보간해 재생하는 드라이버. 입력은 시계열 스냅샷 샘플이다.
    public interface ISnapshotMotionDriver : IRemoteMotionDriver
    {
        // 서버 스냅샷 1건을 보간 버퍼에 push 한다. (serverTimeMs = server_tick_seq × 50ms)
        void OnSnapshot(double serverTimeMs, Vector3 pos, float yaw, bool moving);
    }
}
