using UnityEngine;

namespace Client.Game
{
    // 원격 액터(타 유저/몬스터)의 위치를 매 프레임 "재생(표시)"하는 드라이버의 계약.
    //
    // ★핵심 설계★: 재생 루프(MonsterObject/PlayerCharacter)를 "어떤 데이터로 어떤 타임라인에서
    // 재생하는가"와 분리한다. 구현체가 자신의 타임라인을 소유한다 — 즉 NetClock 의 어느 시각을
    // 읽을지(과거 RenderTimeMs 보간 vs 현재 ServerNowMs 경로예측)를 드라이버가 결정한다.
    // 호출자는 Sample() 결과만 transform 에 적용할 뿐, 클럭/보간/예측을 알지 못한다.
    //
    // 입력(서버 패킷)의 형태는 구현체마다 다르므로(스냅샷 스트림 vs 경로 이벤트) 이 인터페이스에
    // 넣지 않는다. 입력은 구현체별 하위 인터페이스(ISnapshotMotionDriver 등)가 정의한다.
    //
    // Phase 2(경로 복제)에서 MonsterObject 의 드라이버 필드만 경로추종 구현체로 교체하면,
    // 재생 루프(this.Sample 호출부)는 한 줄도 바뀌지 않는다.
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
