#pragma once

#include "Types.h"

#include <array>
#include <cstddef>

namespace netlib
{

// NetLib 내부에서 수집하는 통계 카운터 목록
// 새 카테고리를 추가할 때는 _End 직전에 삽입하고 NetLibStats.cpp의 s_counterNames 배열에도 대응하는 이름을 추가해야 함.
enum class StatCounter : int32
{
    // Accept, Session
    AcceptCount,              // Accept 성공 횟수
    SessionCreated,           // Session 객체 생성 횟수
    SessionDestroyed,         // Session 객체 소멸 횟수

    // 연결 종료
    GracefulDisconnect,       // 상대방 정상 종료 (recv 0바이트 수신)
    AbnormalDisconnect,       // 비정상 종료 (에러 등으로 인한 강제 종료)

    // 패킷 관련
    InvalidPacketHeader,      // 패킷 헤더 검증 실패
    PacketPoolAllocFail,      // PacketPool Alloc 실패 (크기 초과 등)

    // Recv
    RecvPosted,               // WSARecv 호출 횟수
    RecvCompleted,            // Recv 완료 통지 처리 횟수
    RecvKnownFailed,          // Recv 오류가 발생했지만 정상적인 상황인 횟수
    RecvUnknownFailed,        // Recv 알수없는 오류 발생 횟수
    RecvBufferFull,           // 수신 링버퍼가 가득 차서 연결 끊은 횟수

    // Send
    SendPosted,               // WSASend 호출 횟수
    SendCompleted,            // Send 완료 통지 처리 횟수
    SendKnownFailed,          // Send 오류가 발생했지만 정상적인 상황인 횟수
    SendUnknownFailed,        // Send 알수없는 오류 발생 횟수

    // Connect (NetClient)
    ConnectPosted,            // ConnectEx 호출 횟수
    ConnectCompleted,         // Connect 완료 통지 처리 횟수 (성공/실패 포함)
    ConnectFailed,            // Connect 실패 횟수

    _End,                     // 끝 표시. 배열 크기로 사용
};


// NetLib 통계데이터 수집 클래스. TLS로 구현되어 있음
class NetLibStats
{
public:
    // 카운터를 delta만큼 증가한다.
    static void Inc(StatCounter counter, uint64 delta = 1);

    // 현재 카운터값 얻기
    static uint64 GetCount(StatCounter counter);

    // 모든 카운터값을 한번에 얻는다. 배열 크기는 StatCounter::_End.
    static void GetAllCount(std::array<uint64, static_cast<size_t>(StatCounter::_End)>& outCounts);

    // StatCounter 이름 얻기
    static const char* GetCounterName(StatCounter counter);

	// 현재 카운터값들 출력(디버깅용)
    static void LogSnapshot();

    // 모든 카운터 0 초기화 (테스트/재시작용). 매우 조심해서 써야 함.
    // 주의: 다른 스레드가 Inc 중일 때 호출하면 race가 있을 수 있음.
    static void ResetAll();
};

} // namespace netlib
