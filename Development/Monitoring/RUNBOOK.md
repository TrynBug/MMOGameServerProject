# MMO 서버 alert runbook

이 문서는 `prometheus/alerts.yml`의 초기 alert에 대한 진단 순서다. 원인 확인 전에 process를 일괄 재시작하지 않는다. 재시작이 필요하면 영향받은 서버, session 정리와 Registry 상태를 먼저 기록한다.

## 공통 확인

1. Prometheus **Status > Targets**에서 target과 마지막 scrape error를 확인한다.
2. `up`, `mmo_server_ready`, `mmo_server_uptime_seconds`를 같은 시점으로 조회한다.
3. Grafana `MMO Server Overview`에서 CPU/memory, network queue, ContentsThread, DB, user/Stage 추세를 확인한다.
4. 해당 서버의 Error/Warn 로그를 alert 발생 전후 5분 범위로 확인한다.
5. 일시적인 spike인지 지속 증가인지 구분하고, 대응 시각과 변경 사항을 기록한다.

## 가용성

### MmoServerDown

- 의미: MMO metric endpoint가 1분 동안 scrape되지 않았다.
- 확인: `up{job="mmo-servers"}`, process 생존 여부, metric port listener, Security Group/Windows Firewall, 서버 종료 로그.
- 대응: process가 죽었으면 crash dump와 마지막 로그를 보존한 뒤 해당 서버의 표준 복구 절차를 수행한다. process가 살아 있으면 게임 listener와 metric listener를 구분해 확인한다.

### MmoServerNotReady

- 의미: process는 응답하지만 필수 초기화 또는 연결이 2분 이상 완료되지 않았다.
- 확인: `mmo_server_ready`, Registry 연결, DB 연결, GameData 로드 오류.
- 대응: 의존 서비스 상태와 설정을 먼저 복구한다. 초기화 실패를 재시작만으로 반복시키지 않는다.

### RegistryServerDisconnected

- 의미: Registry에 disconnected 상태의 서버 entry가 남아 있다.
- 확인: `mmo_registry_registered_servers`, `rate(mmo_registry_heartbeat_timeouts_total[5m])`, 서버별 heartbeat와 network 오류.
- 대응: 대상 서버 process와 Registry 연결을 확인한다. 반복 disconnect면 IOCP 오류, host CPU 정체와 packet loss를 함께 조사한다.

## 처리 지연과 DB

### ContentsThreadBacklog

- 의미: 가장 오래된 task가 1초 이상 대기했다.
- 확인: `mmo_contents_task_oldest_age_seconds`, `mmo_contents_task_queue_depth`, 전체 tick p99와 Stage 수.
- 대응: 요청 시 진단으로 hot Stage와 장시간 task를 식별한다. queue가 계속 증가하면 신규 유입 또는 Stage 이동을 제한할지 판단한다.

### ContentsThreadTickOverrun

- 의미: 5분 동안 tick budget 초과가 반복됐다.
- 확인: `rate(mmo_contents_tick_overrun_total[5m])`, `histogram_quantile(0.99, sum by (le,instance) (rate(mmo_contents_tick_duration_seconds_bucket[5m])))`, CPU와 Stage object 수.
- 대응: 과밀 Stage, monster/skill/projectile 급증과 동기 실행 작업을 확인한다. 부하 분산 전 snapshot을 보존한다.

### DatabaseBacklog

- 의미: 가장 오래된 DB 요청이 1초 이상 queue에서 대기했다.
- 확인: `mmo_db_queue_oldest_age_seconds`, `sum by (instance) (mmo_db_queue_depth)`, active worker, queue wait/execution p99.
- 대응: DB 자체 지연과 worker 고갈을 구분한다. DB가 느린 상태에서 worker만 무작정 늘리지 않는다.

### DatabaseErrors

- 의미: 최근 5분에 `db_error` 결과가 발생하고 2분 이상 지속됐다.
- 확인: `sum by (instance,db_type,operation) (rate(mmo_db_results_total{result="db_error"}[5m]))`, DB 연결 로그, timeout/deadlock/connection 분류.
- 대응: 영향 operation과 shard를 식별하고 DB 상태를 복구한다. 데이터 변경 작업은 retry 안전성을 확인한 후 재시도한다.

## 네트워크와 PacketPool

### NetworkUnknownErrors

- 의미: 분류되지 않은 send/recv 오류가 지속 발생한다.
- 확인: `rate(mmo_net_recv_errors_total[5m])`, `rate(mmo_net_send_errors_total[5m])`, disconnect rate, Windows TCP 상태.
- 대응: 오류 코드를 로그와 대조해 known reason으로 분류할 수 있는지 확인한다. 동일 host 전체에서 발생하면 NIC/OS 상태를 우선 조사한다.

### NetworkSendBacklog

- 의미: send queue가 64MiB를 1분 이상 초과했다.
- 확인: `mmo_net_send_queue_bytes`, `mmo_net_send_queue_depth`, `mmo_net_send_in_flight`, send throughput과 느린 peer.
- 대응: 증가 방향과 대상 연결을 확인하고 fan-out 또는 느린 consumer를 제한한다. queue가 계속 증가하면 메모리 고갈 전에 유입 제한을 검토한다.

### PacketPoolAllocationFailure

- 의미: 최근 5분에 PacketPool이 요청 크기를 처리하지 못했다.
- 확인: `increase(mmo_packet_pool_alloc_fail_total[5m])`, 요청 packet 크기, pool 전체 held/free/created 수.
- 대응: 비정상 packet 크기와 `MaxPacketSize` 불일치를 먼저 확인한다. 단순히 pool 크기를 늘리기 전에 실패 요청의 출처를 찾는다.

## GameServer와 Stage

### StageMonitoringSnapshotStale

- 의미: Stage snapshot counter가 2분 동안 증가하지 않았다.
- 확인: `increase(mmo_stage_metric_snapshots_total[2m])`, 전체 ContentsThread tick/backlog와 활성 Stage 수.
- 대응: 활성 Stage가 있는데 합계 counter가 멈췄다면 thread stall과 장시간 task를 조사한다. 개별 Stage는 요청 시 진단으로 식별한다.

### GameStageUserCountMismatch

- 의미: GameServer user 수와 Stage user hint 합계 차이가 5명을 넘어 2분 지속됐다.
- 확인: `mmo_game_users`, `mmo_game_stage_user_hint_total`, entering/selecting/moving 상태와 stage move 결과.
- 대응: 정상 이동 grace period를 제외하고 유실된 user lifecycle을 추적한다. container를 운영 중 수동 수정하지 않는다.

## Windows process와 host

### WindowsMetricsCollectionErrors

- 의미: 서버의 Windows API process 또는 host metric 수집이 5분 안에 실패했다.
- 확인: `increase(mmo_windows_metrics_collection_errors_total[5m])`를 `scope`와 `instance`별로 확인하고 해당 서버 로그와 권한 상태를 확인한다.
- 대응: process scope면 해당 서버를, host scope면 RegistryServer를 확인한다. 이전 metric 값은 유지되므로 timestamp 추세가 멈췄는지도 함께 본다.

### WindowsLowAvailableMemory

- 의미: host 가용 memory가 5분 동안 1GiB 미만이다.
- 확인: `mmo_host_memory_available_bytes`, `mmo_process_private_bytes`, send queue와 PacketPool held 추세.
- 대응: 증가 process와 subsystem을 식별한다. OOM 전에 신규 유입 제한, instance 교체 또는 안전한 drain을 검토한다.

## 검증 상태

- 설정 문법과 alert rule 14개는 `promtool`로 검증한다.
- `MmoServerDown`은 개발 환경에서 대상 process 정지로 전달 경로를 검증할 수 있다. Windows API 오류는 전용 test hook 없이 강제로 만들지 않는다.
- DB 지연, send backlog, PacketPool failure, tick overrun은 전용 fault injection 지점이 아직 없으므로 운영 binary에 임시 sleep이나 실패 코드를 직접 넣지 않는다. 별도 테스트 hook을 설계한 뒤 비운영 환경에서 검증한다.
