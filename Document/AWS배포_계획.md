# AWS 라이브 배포 계획

서버를 AWS에, 클라이언트를 외부(인터넷)에 두고 실행하기 위한 설계·설정·배포 가이드.
2026-07-07 코드/빌드 구현 완료. 남은 것은 AWS 인프라 세팅과 클라 재빌드.

---

## 1. 서버 구조 요약

서버는 4개 컴포넌트로 나뉘며, 클라이언트가 직접 접속하는 것은 **Login → Gateway** 둘뿐이다.

| 컴포넌트 | 클라 접속 포트 | 내부 포트 | 역할 | DB |
|---|---|---|---|---|
| Registry | ❌ | 10001 | 서버들이 서로를 찾는 디렉토리(서비스 디스커버리) | - |
| Login | ✅ 8001 | 10010 | 로그인 처리 → 접속할 Gateway 주소를 클라에 알려줌 | AccountDB |
| Gateway | ✅ 8101 | 10100 | 클라 ↔ GameServer 중계 | - |
| Game | ❌ | 10200 | 게임 로직 | Account + GameDB |

**클라 접속 흐름**
1. 클라 → Login 접속 (주소는 클라의 `serverconfig.json`에 기록)
2. `LoginReq` → `LoginRes` (LoginRes 안에 **Gateway 접속 주소**가 담겨 옴)
3. 클라 → Gateway 접속 (LoginRes로 받은 주소)
4. 이후 게임 진행

---

## 2. 핵심 문제와 해결

### 문제
기존에는 각 서버의 `[Server] IP` **한 값**이 3가지 역할을 겸했다.
1. 리슨 소켓 **bind** 주소
2. 서버끼리 서로 접속할 때 쓰는 주소 (Registry에 등록되어 전파)
3. Gateway가 **클라에게 알려주는** 접속 주소 (`LoginRes.gateway_ip`)

AWS EC2에서는 퍼블릭 IP(Elastic IP)가 NIC에 직접 붙지 않고 NAT된다. 따라서
- 퍼블릭 IP로는 `bind()`가 실패한다 (`Cannot assign requested address`).
- 하지만 클라(③)에게는 퍼블릭 IP를 알려줘야 한다.
- 서버끼리(②)는 VPC 내부 프라이빗 IP로 통신해야 한다.

한 값으로는 이 셋을 동시에 만족할 수 없다.

### 해결: PrivateIp / PublicIp 분리
`public_ip` 필드를 새로 추가해 **클라에게 광고하는 주소만** 분리했다.

- **`privateIp`** = bind 주소 + 서버간 내부통신 주소 (VPC 프라이빗 IP)
  - 프라이빗 IP는 NIC에 실제로 존재하므로 bind 정상 동작.
  - 인터넷 클라 트래픽도 `EIP → NAT → 프라이빗 IP`로 이 소켓에 도달하므로 문제 없음.
- **`publicIp`** = 클라에게 광고하는 외부접속 주소 (Elastic IP)
  - **Gateway만 필수.** Login/Registry/Game은 사용하지 않는다.
  - (Login은 클라가 `serverconfig.json`으로 직접 접속하므로 광고 불필요)

이 방식의 장점: **bind 로직과 서버간 접속 로직은 그대로 두고**, 클라 광고 경로 한 곳만 `publicIp`를 쓰면 된다.

---

## 3. 변경된 코드

### 3.1 프로토콜
`Development/Server/PacketGenerator/Proto/ServerPacket/server_registry_packet.proto`
- `ServerInfoMsg`: `ip` → `private_ip`, `public_ip` 필드 추가
- `RegistryRegisterReq`: `ip` → `private_ip`, `public_ip` 필드 추가
- 수정 후 `PacketGenerator/generate_proto.bat` 실행하여 `.pb.h/.pb.cc` 재생성

### 3.2 서버 C++
| 파일 | 변경 |
|---|---|
| `ServerBase/ServerBase.h` | `ServerBaseConfig`: `serverIp` → `privateIp`, `publicIp` 추가 |
| `ServerBase/RegistryClient.h` | `RegistryClientConfig`: `myIp` → `myPrivateIp`, `myPublicIp` 추가 |
| `ServerBase/Types.h` | `ServerInfo`: `ip` → `privateIp`, `publicIp` 추가 |
| `RegistryServer/ServerEntry.h` | `ServerEntry`: `ip` → `privateIp`, `publicIp` 추가 (+ ToServerInfo 매핑) |
| `ServerBase/ServerBase.cpp` | regConfig에 privateIp/publicIp 전달 + **IP 설정 검증** 추가 |
| `ServerBase/RegistryClient.cpp` | 등록 송신·폴링/알림 수신에서 `private_ip`/`public_ip` 처리 |
| `RegistryServer/RegistryServer.cpp` | 등록 수신 저장 + ServerInfoMsg 빌드 3곳 `set_private_ip`/`set_public_ip` |
| `LoginServer/LoginServer.cpp` | 내부연결은 `privateIp`, **클라 광고(`set_gateway_ip`)만 `publicIp`** |
| `GameServer/GameServer.cpp` | Gateway 내부연결에 `privateIp` 사용 |
| `*/main.cpp` (4개) | ini `IP` → `PrivateIP`, `PublicIP` 읽기 추가 |

### 3.3 IP 설정 검증 (폴백 없음)
`ServerBase::Initialize`에서 시작 시 검증한다. **잘못되면 서버를 띄우지 않는다(로컬도 동일).**
- `privateIp` : 구체적인 IPv4여야 함. 빈값 / `0.0.0.0` / 도메인명 / 형식오류 → 기동 거부
- `publicIp` : **Gateway는 필수**(구체 IPv4). 그 외 서버는 미지정 허용, 지정 시 형식만 검증

> 설계 원칙: IP는 `PrivateIp`/`PublicIp`로 명확히 구분한다. 삼항연산자·폴백을 쓰지 않는다.

### 3.4 클라이언트 (Unity)
로그인서버 주소를 씬에 하드코딩하던 것을 런타임 설정 파일로 분리했다.
- `Assets/Scripts/Network/ServerConfig.cs` : `StreamingAssets/serverconfig.json` 로드.
  파일이 없거나 값이 잘못되면 **로그인 차단**(폴백 없음).
- `Assets/StreamingAssets/serverconfig.json` : `loginHost` / `loginPort`
- `Assets/Scripts/Game/LoginSceneController.cs` : 설정 로드 후 접속에 사용
- `TcpClient.Connect`가 도메인명을 수용하므로 `loginHost`에 **IP 또는 도메인명** 모두 가능
- Gateway 주소는 `LoginRes`로 런타임에 받으므로 클라에서 따로 설정할 필요 없음

> 데스크톱(Windows) 빌드의 `StreamingAssets`는 **빌드 후에도 직접 편집 가능**하므로,
> 배포 후 `serverconfig.json`만 바꿔 접속 대상을 변경할 수 있다.

---

## 4. 설정값 레퍼런스

### 4.1 로컬 실행 (전부 한 PC, 기본값)
현재 `OUTPUT/Debug`의 ini와 `serverconfig.json`은 전부 `127.0.0.1`로 되어 있어 **그대로 실행된다.**

### 4.2 AWS 배포값
아래 예시는 서버 4개 + MySQL을 **한 인스턴스**에 올리는 가장 단순한 구성.
- 인스턴스 프라이빗 IP: `172.31.x.x` (예시)
- Gateway용 Elastic IP: `<GATEWAY_EIP>`
- Login용 Elastic IP(또는 도메인): `<LOGIN_EIP>`

**RegistryServer.ini**
```ini
[Server]
PrivateIP    = 172.31.x.x     # 인스턴스 프라이빗 IP (한 머신이면 127.0.0.1도 가능)
InternalPort = 10001
```

**LoginServer.ini**
```ini
[Server]
PrivateIP    = 172.31.x.x     # bind. 인터넷 클라는 EIP→NAT로 도달
ClientPort   = 8001
InternalPort = 10010
[Registry]
IP = 127.0.0.1                # 같은 머신이면 유지 / 다른 머신이면 Registry 프라이빗 IP
```

**GatewayServer.ini** ← 핵심
```ini
[Server]
PrivateIP    = 172.31.x.x     # bind + 서버간 통신
PublicIP     = <GATEWAY_EIP>  # ★ 클라에게 광고하는 주소. 반드시 설정(없으면 미기동)
ClientPort   = 8101
InternalPort = 10100
[Registry]
IP = 127.0.0.1
```

**GameServer.ini**
```ini
[Server]
PrivateIP    = 172.31.x.x     # 내부 전용 (클라 직접접속 없음)
InternalPort = 10200
[Registry]
IP = 127.0.0.1
```

**클라 serverconfig.json**
```json
{
    "loginHost": "<LOGIN_EIP>",   // 또는 login.mygame.com (Route53 도메인)
    "loginPort": 8001
}
```

> 여러 인스턴스로 분리 배포할 경우: 각 서버 `PrivateIP` = 자기 인스턴스의 프라이빗 IP,
> 각 서버 `[Registry] IP` = Registry 인스턴스의 프라이빗 IP, DB `Host` = DB/RDS 주소.
> 코드는 동일하며 ini 값만 바꾸면 된다.

### 4.3 라이브 운영 위생
- `[Log] Level` : `Debug` → `Info`
- DB 비밀번호(`admin1234`) 교체, ini 평문 크리덴셜 관리
- 서버는 서비스/프로세스 매니저로 등록해 자동 재시작 권장

### 4.4 빌드 구성과 런타임 의존성 (중요 함정)
**반드시 Release 빌드를 배포한다.** Debug 빌드는 디버그 CRT(`MSVCP140D.dll`, `VCRUNTIME140D.dll`, `ucrtbased.dll`)에 링크되는데, 이 디버그 DLL은 **재배포가 금지**되어 있고 Visual Studio 설치 PC에만 존재한다. Debug 빌드를 올리면 AWS에서 `...140D.dll not found` 오류가 난다.

- vcpkg 트리플릿이 `x64-windows-static-md`(동적 CRT `/MD`)이므로 정적 CRT(`/MT`) 전환은 불가. **Release(`/MD`) + 재배포 패키지**가 정석.
- **AWS 인스턴스에 VC++ 재배포 패키지 설치**(1회): `https://aka.ms/vs/17/release/vc_redist.x64.exe`
  → `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` 제공. (UCRT/`ucrtbase.dll`은 Windows Server 2016+ 기본 내장)

**배포 폴더(`OUTPUT/Release`)에 반드시 함께 있어야 하는 것** — 빌드는 exe/lua.dll만 생성하므로 나머지는 수동 배치 필요:
- exe 4개 + `lua.dll`(GameServer) : 빌드 산출물
- `libmysql.dll`, `libssl-3-x64.dll`, `libcrypto-3-x64.dll` : 모든 서버 exe가 `libmysql.dll`에 의존(libmysql은 다시 OpenSSL 두 DLL에 의존) → 없으면 미기동
- ini 4개(`PrivateIP`/`PublicIP` 반영본)

**⚠️ OpenSSL DLL은 반드시 Release 버전을 써라.** `OUTPUT/Debug` 폴더의 `libssl-3-x64.dll`/`libcrypto-3-x64.dll`은 **디버그 빌드**라 `ucrtbased.dll`/`VCRUNTIME140D.dll`을 끌어온다. 이걸 배포하면 exe는 Release여도 AWS에서 디버그 DLL 오류가 난다(개발 PC에선 VS가 있어 디버그 DLL이 있으니 문제 안 보임). **Release 버전 출처: `C:\Program Files\MySQL\MySQL Server 8.4\bin\`** (release libmysql과 짝).

**검증(필수)** — 배포 폴더의 exe·dll **전부**에 대해 디버그 CRT 참조가 없어야 정상:
```
for %f in (*.exe *.dll) do dumpbin /dependents "%f" | findstr /i "140D ucrtbased"
```
한 줄도 출력되면 그 파일이 디버그 빌드다. (`dumpbin`은 VS Developer 명령 프롬프트에서 사용)

**배포 폴더 구조** — 서버는 exe의 **상위 폴더**에서 `GameData`/`Map`을 읽으므로 구조 유지 필수:
```
OUTPUT/
├─ Release/     ← exe + dll + ini (여기서 실행, 작업디렉토리=이 폴더)
├─ GameData/    ← csv 게임데이터
└─ Map/         ← 내비메시 등
```
(`Debug/`, `Tools/`, `.pdb/.lib/.idb`는 배포 불필요)

---

## 5. AWS 인프라 세팅

### 5.1 Elastic IP
- Login / Gateway 각각에 EIP 할당 (재부팅해도 IP 고정)
- Gateway의 EIP를 `GatewayServer.ini`의 `PublicIP`에 기입

### 5.2 (선택) Route53 도메인
- `login.mygame.com → Login EIP` 매핑
- `serverconfig.json`의 `loginHost`에 도메인 사용 → 서버 IP가 바뀌어도 클라 수정 불필요

### 5.3 보안그룹 (인바운드)
| 포트 | 소스 | 용도 |
|---|---|---|
| TCP 8001 | `0.0.0.0/0` | Login (인터넷 클라) |
| TCP 8101 | `0.0.0.0/0` | Gateway (인터넷 클라) |
| TCP 10001 / 10010 / 10100 / 10200 | **같은 SG만** | 서버간 내부통신 |
| TCP 3306 | **같은 SG만** (또는 localhost) | MySQL |
| TCP 22 | 관리자 IP만 | SSH |

> 서버 4개를 한 인스턴스에 올리면 내부포트·MySQL은 localhost 통신이라 SG 규칙 불필요.
> `8001`·`8101`만 인터넷에 열면 된다.

---

## 6. 배포 절차

1. **서버 빌드** — 이미 완료(`OUTPUT/Debug`). 코드 변경 시 재빌드.
2. **AWS 인프라** — EIP 할당, 보안그룹 설정, (선택)Route53.
3. **서버 ini 수정** — 위 4.2 값으로 `OUTPUT/Debug/*.ini` 편집.
4. **서버 파일 업로드 및 실행** — Registry → Login/Gateway/Game 순으로 기동.
5. **클라 설정** — `serverconfig.json`의 `loginHost`를 Login EIP/도메인으로.
6. **클라 재빌드** — Unity에서 재빌드(신규 C# 스크립트 반영). 배포 후엔 `serverconfig.json`만 바꿔도 접속 대상 변경 가능.

---

## 7. 남은 이슈 / 참고

- **DummyClient**(부하테스트 봇, `Development/Server/DummyClient`)도 Login → Gateway로 접속하므로 AWS에선 동일한 접속 설정이 필요하다. 로컬 `127.0.0.1`은 그대로 동작한다. (이번 변경에서 미반영)
- 검증이 엄격하므로(폴백 없음), ini의 `PrivateIP`/(Gateway)`PublicIP`가 비었거나 `0.0.0.0`이면 해당 서버는 기동되지 않는다. 로그의 `invalid PrivateIP`/`invalid PublicIP` 메시지로 원인 확인.
