Unity 클라이언트와 C++ MMORPG 서버를 함께 개발하는 프로젝트

폴더구조:    
 - Development
   - Common : 게임데이터 등의 서버/클라 공통 asset  
   - Client : 클라이언트  
   - Server : 서버
     - Logger : 로깅
     - Recast : recast 라이브러리
     - Detour : detour 라이브러리
     - GameDataLib : 게임데이터 제공
     - PacketGenerator : 패킷 구조체 제공
     - DBConnector : DB 연결 기능 제공
     - NetLib : 네트워크 기능 제공
     - ServerBase : 모든 서버의 기반 클래스
     - RegistryServer : 레지스트리 서버
     - LoginServer : 로그인 서버
     - GameServer : 게임 서버
     - GatewayServer : 게이트웨이 서버
     - CommunicationServer : 커뮤니케이션 서버
     - DummyClient : 더미 클라이언트
     - GameDataGenerator : 게임데이터 클래스 생성 기능 제공
   - Monitoring : 서버 모니터링 툴  
 - Document : 프로젝트 설계 문서  
