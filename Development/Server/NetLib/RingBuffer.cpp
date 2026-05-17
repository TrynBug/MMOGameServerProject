#include "pch.h"
#include "RingBuffer.h"

namespace netlib
{

#define min(a, b)  (((a) < (b)) ? (a) : (b)) 
#define max(a, b)  (((a) > (b)) ? (a) : (b)) 

RingBuffer::RingBuffer()
{
}

RingBuffer::~RingBuffer()
{
	delete[] m_buffer;
	m_buffer = nullptr;
}

void RingBuffer::Initialize(int32 size)
{
	m_size = size;
	m_buffer = new char[m_size + 1];
	m_front = m_buffer;
	m_rear = m_buffer;
}


// 현재 남은 byte 크기 얻기
int32 RingBuffer::GetFreeSize(void)
{
	if (m_front > m_rear)
		return (int32)(m_front - m_rear - 1);
	else
		return (int32)(m_size + m_front - m_rear);
}


// 현재 사용중인 byte 크기 얻기
int32 RingBuffer::GetUseSize(void)
{
	if (m_front > m_rear)
		return (int32)(m_size + m_rear - m_front + 1);
	else
		return (int32)(m_rear - m_front);
}

// 현재 rear와 버퍼의 끝 사이의 남은 공간 byte 크기를 얻는다.
int32 RingBuffer::GetDirectFreeSize(void)
{
	if (m_front > m_rear)
		return (int32)(m_front - m_rear - 1);
	else
	{
		if (m_front != m_buffer)
			return (int32)(m_buffer + m_size - m_rear + 1);
		else
			return (int32)(m_buffer + m_size - m_rear);
	}

}

// 현재 front와 버퍼의 끝 사이의 사용중인 공간의 byte 크기를 얻는다.
int32 RingBuffer::GetDirectUseSize(void)
{
	if (m_front > m_rear)
		return (int32)(m_buffer + m_size - m_front + 1);
	else
		return (int32)(m_rear - m_front);
}

// 링버퍼에 데이터를 쓴다.
// 현재 rear 위치부터 데이터를 쓰고, 쓰고 난 뒤 rear의 위치는 데이터의 마지막 위치 +1 이다.
// @src : write할 데이터
// @len : write할 데이터 byte 길이
// @return : 입력한 데이터 byte 크기를 리턴함. buffer에 충분한 공간이 없으면 실패하며 0을 리턴함.
int32 RingBuffer::Enqueue(const char* src, int32 len)
{
	if (GetFreeSize() < len || len < 1)
		return 0;

	int32 directFreeSize = GetDirectFreeSize();
	if (directFreeSize < len)
	{
		memcpy(m_rear, src, directFreeSize);
		memcpy(m_buffer, src + directFreeSize, len - directFreeSize);
		m_rear = m_buffer + len - directFreeSize;
	}
	else
	{
		memcpy(m_rear, src, len);
		m_rear += len;
		if (m_rear == m_buffer + m_size + 1)
			m_rear = m_buffer;
	}
	return len;
}

// 링버퍼의 데이터를 꺼내서 dest에 write한다.
// 현재 front 위치부터 데이터를 읽고, 읽은만큼 front를 전진시킨다.
// @dest : 읽기 버퍼
// @len : 읽기버퍼 byte 길이
// @return : 읽은 데이터 byte 크기. 만약 링버퍼내에 len 크기만큼 데이터가 없을 경우 남아있는 모든 데이터를 읽음.
int32 RingBuffer::Dequeue(char* dest, int32 len)
{
	if (len < 1)
		return 0;

	int32 useSize = GetUseSize();
	if (useSize == 0)
		return 0;

	int32 readBytes = min(len, useSize);
	int32 directUseSize = GetDirectUseSize();
	if (directUseSize < readBytes)
	{
		memcpy(dest, m_front, directUseSize);
		memcpy(dest + directUseSize, m_buffer, readBytes - directUseSize);
		m_front = m_buffer + readBytes - directUseSize;
	}
	else
	{
		memcpy(dest, m_front, readBytes);
		m_front += readBytes;
		if (m_front == m_buffer + m_size + 1)
			m_front = m_buffer;
	}
	return readBytes;
}




// 링버퍼의 데이터를 dest에 write한다. 이 때 링버퍼의 데이터를 제거하지 않는다.
// 현재 front 위치부터 데이터를 읽고, front는 전진시키지 않는다.
// @dest : 읽기 버퍼
// @len : 읽기버퍼 byte 길이
// @return : 읽은 데이터 byte 크기. 만약 링버퍼내에 len 크기만큼 데이터가 없을 경우 남아있는 모든 데이터를 읽음.
int32 RingBuffer::Peek(char* dest, int32 len)
{
	if (len < 1)
		return 0;

	int32 useSize = GetUseSize();
	if (useSize == 0)
		return 0;

	int32 readBytes = min(len, useSize);
	int32 directUseSize = GetDirectUseSize();
	if (directUseSize < readBytes)
	{
		memcpy(dest, m_front, directUseSize);
		memcpy(dest + directUseSize, m_buffer, readBytes - directUseSize);
	}
	else
	{
		memcpy(dest, m_front, readBytes);
	}
	return readBytes;
}

// front의 위치를 dist 만큼 뒤로 옮긴다.
void RingBuffer::MoveFront(int32 dist)
{
	m_front = m_buffer + (m_front - m_buffer + dist) % (m_size + 1);
}

// rear의 위치를 dist 만큼 뒤로 옮긴다.
void RingBuffer::MoveRear(int32 dist)
{
	m_rear = m_buffer + (m_rear - m_buffer + dist) % (m_size + 1);
}

// 버퍼를 비운다.
void RingBuffer::Clear()
{
	m_front = m_buffer;
	m_rear = m_buffer;
}

} // namespace netlib
