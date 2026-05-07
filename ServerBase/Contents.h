#pragma once

#include "pch.h"

namespace serverbase
{

// 컨텐츠 베이스 클래스
// 각 서버는 컨텐츠(예:Stage)를 구현할 때 이 클래스를 상속받아 컨텐츠 로직을 구현한다.
// 컨텐츠 스레드가 Update()를 주기적으로 호출한다.
class Contents
{
public:
    Contents() = default;
    virtual ~Contents() = default;

    Contents(const Contents&) = delete;
    Contents& operator=(const Contents&) = delete;

public:
    // 컨텐츠 스레드에서 호출된다.
    void Update(int64 deltaMs);

    // 컨텐츠가 컨텐츠 스레드에 배정될 때 한 번 호출된다.
    void Start();

    // 컨텐츠 스레드가 종료될 때 한 번 호출된다.
    void Stop();

    bool IsRunning() const { return m_bRunning; }

protected:
    virtual void OnStart() {}
    virtual void OnUpdate(int64 deltaMs) = 0;
    virtual void OnStop() {}

private:
    bool m_bRunning = false;
};

using ContentsPtr  = std::shared_ptr<Contents>;
using ContentsWPtr = std::weak_ptr<Contents>;
using ContentsUPtr = std::unique_ptr<Contents>;

} // namespace serverbase
