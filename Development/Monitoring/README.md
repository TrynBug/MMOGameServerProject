# MMO 서버 모니터링 실행 가이드

이 디렉터리는 게임 서버가 이미 노출하는 Prometheus text endpoint를 수집하고 시각화하기 위한 운영 도구 설정이다. 서버 실행 파일에 별도의 Prometheus C++ 라이브러리를 설치하거나 링크하지 않는다.

구성 요소는 다음과 같다.

- Prometheus: 서버가 노출하는 application/process/host metric 수집, alert rule 평가
- Grafana: `MMO Server Overview` dashboard 제공

로컬 Grafana의 bundled Zipkin datasource는 사용하지 않으며 시작 스크립트에서 비활성화한다. Zipkin backend가 RegistryServer의 내부 통신 port `10001`을 선점하는 충돌을 방지하기 위한 설정이다.

## 로컬 개발 환경

모니터링 endpoint는 기본적으로 `127.0.0.1`에만 bind된다. 따라서 로컬 환경에서는 Docker 컨테이너보다 Windows native binary를 사용한다. 다운로드된 실행 파일과 runtime data는 `.gitignore` 대상이다.

### 1. 도구 다운로드

PowerShell에서 다음을 실행한다.

```powershell
cd C:\project\MMOGameServerProject\Development\Monitoring
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-LocalMonitoring.ps1
```

스크립트가 다운로드하는 고정 버전은 Prometheus 3.12.0과 Grafana OSS 13.1.0이다.

게임 서버가 Windows API로 process CPU·memory·handle·I/O를 직접 수집한다. RegistryServer는 중복을 피하기 위해 host CPU·memory도 함께 노출하므로 별도의 windows_exporter 설치가 필요하지 않다.

### 2. 모니터링 시작

Grafana 초기 admin password를 현재 PowerShell process에만 설정한 뒤 시작한다. password는 저장소 파일에 기록하지 않는다.

```powershell
$env:MMO_GRAFANA_ADMIN_PASSWORD = '<local-password>'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-LocalMonitoring.ps1
```

접속 주소는 다음과 같다.

- Prometheus: <http://127.0.0.1:9090>
- Grafana: <http://127.0.0.1:3000> (`admin` / 위에서 설정한 password)

그 다음 Registry, Login, Communication, Gateway, GameServer를 실행한다. Prometheus의 **Status > Targets**에서 `mmo-servers` target 5개가 `UP`인지 확인하고, Grafana의 `MMO Server Overview` dashboard를 연다.

### 3. 종료

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Stop-LocalMonitoring.ps1
```

스크립트는 `.tools` 아래에서 시작한 것으로 확인된 process만 종료한다.

## 포함된 alert

초기 alert rule은 server down/not-ready, Registry disconnect, ContentsThread backlog/tick overrun, DB backlog/error, network unknown error/send backlog, PacketPool allocation failure, Stage snapshot 정지, Game/Stage user count 불일치, Windows API 수집 오류, 가용 memory 1 GiB 미만을 포함한다.

Prometheus는 alert rule을 계속 평가하므로 Prometheus의 **Alerts** 화면과 Grafana의 `ALERTS` query에서 firing 상태를 확인할 수 있다. 현재 외부 알림 전달 도구는 구성하지 않는다.

Alert별 영향, 확인 query와 초기 대응은 [RUNBOOK.md](RUNBOOK.md)를 따른다.

## 성능과 cardinality 측정

서버를 기동한 상태에서 다음 명령을 실행하면 각 `/metrics` endpoint의 외부 관측 응답 시간, 응답 byte, series/family 수와 서버 process의 평균 CPU 및 peak memory를 JSON으로 기록한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Measure-Monitoring.ps1 `
    -DurationSeconds 60 `
    -ScrapeIntervalSeconds 15
```

1초 과도 scrape를 확인하려면 interval만 변경한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Measure-Monitoring.ps1 `
    -DurationSeconds 60 `
    -ScrapeIntervalSeconds 1
```

특정 서버만 측정할 수도 있다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Measure-Monitoring.ps1 `
    -Targets 'game=http://127.0.0.1:19201/metrics' `
    -DurationSeconds 300 `
    -ScrapeIntervalSeconds 15
```

기본 초기 한도는 target별 scrape p95 100ms, series 10,000개다. 결과는 무시된 `results/monitoring-*.json`에 저장된다. CPU는 전체 logical processor 용량을 100%로 정규화하며, 이 값은 반드시 같은 서버 binary, bot 수, 시나리오, 측정 시간으로 수집한 baseline과 비교해야 한다.

Debug 단독 기동 결과는 도구 smoke test일 뿐 Release 성능 판정 자료로 사용하지 않는다. Release A/B에서는 먼저 모니터링 비활성 baseline을 동일 DummyClient 부하로 측정하고, 이후 모니터링 활성 상태의 15초 및 1초 scrape 결과를 비교한다.

## 운영/AWS 배치

운영에서는 Prometheus와 Grafana를 게임 프로세스와 분리된 monitoring host 또는 관리형 환경에 둔다.

1. 각 서버의 `[Monitoring] IP`를 해당 EC2 instance의 구체적인 VPC private IP로 설정한다.
2. Security Group inbound는 monitoring collector의 Security Group 또는 private IP에서 metric port로 들어오는 연결만 허용한다.
3. `prometheus/prometheus.yml`의 target을 각 서버의 private IP와 port로 변경한다.
4. Prometheus와 Grafana UI도 VPN, bastion, private load balancer 등 관리망에서만 접근하게 한다.
5. 운영 보존 기간, disk 용량, scrape interval, alert threshold는 단계 9의 Release A/B 부하 측정 결과로 확정한다.

metric endpoint를 인터넷에 직접 공개하지 않는다. 여러 GameServer instance를 배포할 때는 고정 target을 계속 복제하기보다 EC2 service discovery 또는 배포 시스템이 생성하는 file-based service discovery로 전환한다.

## 설정 검증

도구 설치 후 다음 명령으로 Prometheus 설정과 alert rule을 검증할 수 있다.

```powershell
$tools = 'C:\project\MMOGameServerProject\Development\Monitoring\.tools'
& "$tools\prometheus-3.12.0.windows-amd64\promtool.exe" check config .\prometheus\prometheus.yml
& "$tools\prometheus-3.12.0.windows-amd64\promtool.exe" check rules .\prometheus\alerts.yml
```

## 아직 남은 범위

- 중앙 로그 agent 및 metric alert에서 관련 로그로 이동하는 링크
- host 종료, DB 지연, send backlog, packet allocation failure fault injection
- Release 동일 부하 A/B 성능 비용 측정과 threshold 확정
