# 참고: 서버 기준 입니다. 클라이언트는 최대한 기존 코드 스타일대로...


# 프로그래밍
- 프로그래밍언어
	- C++ 20
	
# 인코딩
utf-8 (서명없음)

# OS
windows 기준입니다.  
줄바꿈 문자는 CRLF(\r\n) 입니다.  
	
# 데이터타입
### 기본 데이터타입
using int8   = signed char;
using int16  = signed short;
using int32  = signed int;
using int64  = signed __int64;
using uint8  = unsigned char;
using uint16 = unsigned short;
using uint32 = unsigned int;
using uint64 = unsigned __int64;

char = char
float = float
double = double

### 스마트포인터 타입
Base 이름의 클래스 또는 구조체가 있을 때, 스마트포인터의 타입은 다음과 같습니다.

using BasePtr = std::shared_ptr<Base>
using BaseWPtr = std::weak_ptr<Base>
using BaseUPtr = std::unique_ptr<Base>


# Naming Rules
기본적으로 CamelCase를 사용합니다.

### 클래스, 구조체 이름
첫글자가 대문자입니다.
예) class MonsterAI

### 변수명
- 지역변수
	- 첫글자가 소문자입니다.
	- 예) int32 errorReason;
	
- 클래스 멤버변수
	- 가장앞에 m_을 붙입니다.
	- 예) int32 m_errorReason;

- 클래스 static 변수
	- 이름앞에 sm_ 을 붙입니다.
	- 예) int32 sm_maxCount;

- 구조체 멤버변수
	- 이름앞에 m_ 을 붙이지 않습니다.
	- 예) int32 errorReason;

- pointer 변수명
	- 가장앞에 p를 붙입니다.
	- 예) Player* pPlayer;
	
- shared_ptr 변수명
	- 가장앞에 sp 를 붙입니다.
	- 예) std::shared_ptr<Player> spPlayer;
	
- weak_ptr 변수명
	- 가장앞에 wp 를 붙입니다.
	- 예) std::weak_ptr<Player> wpPlayer;

- iterator 변수명
	- iter 입니다.
	- 예) auto iter = m_sessionToServerId.find(sessionId);
	
- thread-safe container 변수명
	- 이름앞에 safe를 붙입니다.
	- 예) concurrency::concurrent_unordered_map<int, Player*> m_safePlayers;

### 멤버함수명
- public 함수
	- 첫글자가 대문자입니다.
	- 예) int32 GetPlayerCount();
- private, protected 함수
	- 첫글자가 소문자입니다.
	- 예) int32 movePlayer();

### 함수 파라미터 
- out 파라미터
	- 이름앞에 out을 붙입니다.
	- 예) bool DisconnectAll(int32& outCount);

# 데이터타입을 제외하고 using은 사용하지 않습니다.
코드의 명확성을 위해 using 사용을 자제합니다.
사용하지말것) using ms = std::chrono::milliseconds; 

# 중괄호 규칙
BSD 규칙을 따릅니다.
예)
if (...)
{
  something...
}

# 한줄이 150줄 이하일 때는 줄바꿈을 하지 않는다.
150자 정도의 길이는 줄바꿈을 하지 않고 그냥 한줄로 적는다.
bool RegistryServer::validateRegistration(int32 serverId, ServerType type, const std::string& ip, uint16 port, std::string& outErrorMsg)

# 여러개의 변수나 함수를 선언할 때, 중간에 공백을 넣어서 줄을 맞추지 않아도 됨
- 비권장 예시: 중간에 공백을 넣어서 줄을 맞췄음
int32       serverId = req.server_id();
ServerType  type     = static_cast<ServerType>(req.server_type());
std::string ip       = req.ip();
uint16      port     = static_cast<uint16>(req.port());

- 권장 예시: 중간에 공백을 넣지 않아도 됨
int32 serverId = req.server_id();
ServerType type = static_cast<ServerType>(req.server_type());
std::string ip = req.ip();
uint16 port = static_cast<uint16>(req.port());


# 1개의 코드블록이 끝나고, 그 뒤로 코드가 계속 이어진다면 코드블록이 끝난 다음 줄바꿈을 추가한다.
예시)
std::vector<int32> toRemove;
for (const auto& [buffKey, buff] : m_buffs)
{
	if (buff.pData->RemoveOnStageChange)
		toRemove.push_back(buffKey);
}

// for문이 끝났고 아래에서 코드가 계속 이어지기 때문에 여기에 줄바꿈을 추가한다.
for (int32 key : toRemove)
	RemoveBuff(key);

# 함수 작성할때 규칙
## 함수를 작성할 때 본문이 2줄 이상이라면 반드시 cpp 파일에 본문을 작성합니다.
// 이렇게 본문이 2줄 이상 넘어가면 반드시 cpp 파일에 본문을 작성합니다.
void Session::SetSimulatedDelay(int32 recvMs, int32 sendMs)
{
	m_recvDelayMs.store(recvMs, std::memory_order_relaxed);
	m_sendDelayMs.store(sendMs, std::memory_order_relaxed);
}

## .h 파일의 함수 선언 순서와 .cpp 파일의 함수 구현 순서는 일치하게 작성합니다.