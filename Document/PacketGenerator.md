# PacketGenerator 프로젝트
- .proto 파일로 C++, C# message 구조체를 생성합니다. C# 구조체는 서버 프로젝트 외부의 클라이언트 프로젝트 경로에 생성됩니다.  
- 생성된 message 구조체는 게임로직에 그대로 사용됩니다. 주로 해당 구조체를 멤버로 가지는 래퍼 클래스를 만들어 사용할 예정입니다.   
- 출력은 static 라이브러리 입니다.  
- 각각의 서버에서는 자신에게 필요한 message 구조체만 include하여 사용할 예정입니다.
- PacketGenerator를 빌드하기 전, Custom Build Tool 로 protoc를 자동으로 실행하게 합니다. 작업내용: C++ message 생성, C# message 생성, .proto가 바뀌었을 때만 재실행 하도록 함

# 프로그래밍
- 프로그래밍언어 : C++ 20
- protobuf 버전 : 5.29.5

# PacketGenerator 프로젝트 구조
PacketGenerator/
    Proto/
        Common/
            packet_id.proto
                - 이 파일에는 모든 패킷의 ID가 있습니다.
        GamePacket/
            - 이 폴더에는 서버와 클라이언트가 주고받는 패킷의 .proto 파일이 있습니다.
        ServerPacket/
            - 이 폴더에는 서버끼리 주고받는 패킷의 .proto 파일이 있습니다. 서버전용이라서 클라이언트용 C# 구조체는 생성되지 않습니다.
        DataStructures/
            - 이 폴더에는 DB 데이터를 표현하는 구조체의 .proto 파일이 있습니다.

    Generated/
        Common/
            - protoc에 의해 생성된 패킷ID 파일 
        GamePacket/
            - protoc에 의해 생성된 GamePacket 구조체 h, cc 파일
        ServerPacket/
            - protoc에 의해 생성된 ServerPacket 구조체 h, cc 파일
        DataStructures/
            - protoc에 의해 생성된 DataStructures 구조체 h, cc 파일

    ProtoSerializer.h, ProtoSerializer.cpp
        - 이 파일은 protoc로 생성된 message 구조체의 serialization, deserialization 기능을 제공합니다.
    ProtoJsonSerializer.h, ProtoJsonSerializer.cpp
        - 이 파일은 protoc로 생성된 message 구조체의 json string 으로의 serialization, deserialization 기능을 제공합니다.
    PacketDispatcher.h, PacketDispatcher.cpp
        - 이 파일은 패킷ID와 핸들러함수를 등록하는 기능을 제공합니다. 

# 클라이언트용 외부 경로
../../Client/Assets/Generated/
    Common/
        - protoc에 의해 생성된 패킷ID 파일 
    GamePacket/
        - 이 폴더에는 protoc로 생성된 서버와 클라이언트가 주고받는 C# 패킷 구조체가 있습니다.
    DataStructures/
        - 이 폴더에는 protoc로 생성된 DB 데이터를 표현하는 C# 구조체가 있습니다.
    (참고: ServerPacket은 서버 내부 전용이므로 C# 생성 안 함)

# protoc
- 모든 패킷의 ID는 packet_id.proto 파일에 있습니다.
- protoc 실행 시 --proto_path 는 Proto 폴더로 합니다. (.protoc 내에서 다른 폴더의 .protoc를 참조해야 하므로)
