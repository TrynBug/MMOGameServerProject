#pragma once

#include "Types.h"

namespace netlib
{

// 링버퍼
class RingBuffer
{
public:
	RingBuffer();
	~RingBuffer();

	RingBuffer(const RingBuffer&) = delete;
	RingBuffer& operator=(const RingBuffer&) = delete;

public:
	void Initialize(int32 size);

	int32 GetSize()  const { return m_size; }
	char* GetFront() const { return m_front; }
	char* GetRear()  const { return m_rear; }
	
	char* GetBuffer() { return m_buffer; }

	// 현재 남은 byte 크기 얻기
	int32 GetFreeSize();
	// 현재 사용중인 byte 크기 얻기
	int32 GetUseSize();
	// 현재 rear와 버퍼의 끝 사이의 남은 공간 byte 크기를 얻는다.
	int32 GetDirectFreeSize();
	// 현재 front와 버퍼의 끝 사이의 사용중인 공간의 byte 크기를 얻는다.
	int32 GetDirectUseSize();

	// 데이터 쓰기
	int32 Enqueue(const char* src, int32 len);
	
	// 데이터 읽기
	int32 Dequeue(char* dest, int32 len);
	int32 Peek(char* dest, int32 len);

	// front, rear 이동
	void MoveFront(int32 dist);
	void MoveRear(int32 dist);

	// clear
	void Clear();

private:
	char*  m_buffer = nullptr;   // 버퍼
	int32  m_size = 0;           // 버퍼 크기

	char*  m_front = nullptr;    // 데이터의 시작 위치
	char*  m_rear = nullptr;     // 데이터의 마지막 위치 + 1
};

} // namespace netlib
