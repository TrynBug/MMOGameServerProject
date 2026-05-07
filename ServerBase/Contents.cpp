#include "pch.h"
#include "Contents.h"
#include "Logger.h"

namespace serverbase
{

void Contents::Update(int64 deltaMs)
{
    OnUpdate(deltaMs);
}

void Contents::Start()
{
    m_bRunning = true;
    OnStart();
}

void Contents::Stop()
{
    m_bRunning = false;
    OnStop();
}

} // namespace serverbase