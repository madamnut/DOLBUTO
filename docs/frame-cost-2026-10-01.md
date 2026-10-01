# CPU 전체 프레임·GPU 병목 계측 (2026-10-01)

사용자 요청 `측정할수있게 해서 헤드리스로 측정해봐`에 따라 선택적 `--profile-frame`을 추가했다. 렌더링 최적화는 하지 않았다. 같은 평지 시점에서 거리12는 GPU 대기가 지배적이고, 거리24는 GPU 우세와 CPU 지연이 섞이며, 거리32는 CPU 메인 스레드가 제한한다. 기존 일부 CPU 구간 수치만으로 GPU 병목을 가정했던 판단을 이 결과로 구체화한다.

## 조건

Intel Core i7-14700F(20코어/28논리 프로세서), NVIDIA GeForce RTX 5060, Release. 1280×900 헤드리스/오프스크린2슬롯, VSync·FPS 제한 없음. 시드1337, 평지, 카메라(8,193.6,8), yaw−90/pitch−22, 정오 고정. LOD 거리64, 그림자 품질2/거리192, TAA ON, 블룸12, 나머지는 fixture settings를 따른다. 근거리·조명·업로드·LOD 준비 뒤240프레임 예열하고1200steady를 수집했다. 각 거리3회, 총9회/10,800steady. 반복 순서는12/24/32 →32/24/12 →12/24/32이며 성능 실행에서 validation·캡처·청크별 draw 계측은 껐다.

## 결과

모든 값은 ms/프레임이다. 평균은 거리별3600steady를 합산하고 p95는 nearest-rank다. 로딩 프레임과 캡처 실행은 제외했다.

| 거리 | CPU 작업 평균 / p95 | GPU 평균 / p95 | GPU 완료 대기 평균 | 프레임 간격 평균 | 판단 |
|---|---|---|---|---|---|
| 12 | 0.497802 / 0.786000 | 1.642106 / 1.655616 | 1.155091 | 1.653142 | GPU 제한 |
| 24 | 1.082746 / 1.672000 | 1.355901 / 1.367552 | 0.353808 | 1.437106 | GPU 우세, CPU 지연도 있음 |
| 32 | 1.885514 / 2.139200 | 1.483415 / 1.483360 | 0.006628 | 1.892742 | CPU 메인 스레드 제한 |

거리12는 프레임 슬롯 fence가99.36%의 프레임에서 아직 완료되지 않았고 평균1.155ms를 기다렸다. 거리32는 대기 진입 전 미완료가2.53%뿐이며 기다린 시간도0.00663ms다. OS가 집계한 메인 스레드 실행 평균도 거리12/24/32에서0.521/1.059/1.853ms로, 거리32의 높은 CPU 작업 시간이 단순한 GPU 대기 때문이 아님을 뒷받침한다.

거리32의 world CPU 구간은 평균1.777ms로 CPU 작업의 약94%다. 월드 준비·가시성·정렬·모든 장면 패스의 명령 기록 등을 포함하므로, 이 숫자만으로 개별 기능이나 정렬만을 원인으로 지목할 수 없다. 거리24는 반복 CPU 작업 평균0.938~1.238ms, GPU1.355~1.357ms로 CPU 쪽 편차가 있고 간헐적인 GPU 공급 공백도 있다.

같은 GPU 시계에서 프레임 구간을 합집합으로 합친 시간 비율은 거리12/24/32에서99.32/94.40/78.42%다. TOP_OF_PIPE 시작과 ALL_COMMANDS 종료 사이 구간에는 의존성·스케줄링이 포함되고 프레임끼리 겹칠 수 있으므로 이는 GPU 하드웨어 사용률이 아니다. 대기·CPU 실행 수치와 함께 보조 근거로만 사용한다.

거리별 근거리/LOD 구성과 그리는 양이 달라 GPU 시간이 거리와 함께 단조 증가하지 않는다. 이전 --profile-draws 실행과는 계측 비용도 다르므로 이번 값을 기존 성능 개선율 계산에 쓰지 않는다.

| 실행 | CPU 작업 평균 | GPU 평균 | fence 대기 평균 | OS 메인 스레드 평균 |
|---|---|---|---|---|
| r12-1 | 0.492563 | 1.642282 | 1.158593 | 0.455729 |
| r24-1 | 1.072843 | 1.355729 | 0.399919 | 0.976562 |
| r32-1 | 1.851582 | 1.481545 | 0.006962 | 1.822917 |
| r12-2 | 0.507114 | 1.641115 | 1.150351 | 0.533854 |
| r24-2 | 0.937710 | 1.354677 | 0.453173 | 1.002604 |
| r32-2 | 1.868092 | 1.482313 | 0.005180 | 1.848958 |
| r12-3 | 0.493729 | 1.642922 | 1.156328 | 0.572917 |
| r24-3 | 1.237685 | 1.357297 | 0.208332 | 1.197917 |
| r32-3 | 1.936867 | 1.486387 | 0.007743 | 1.888021 |

## 계측 의미

- `frame_wall_ms`: 루프 시작부터 프레임 종료까지의 경과 시간. 이벤트/게임 로직, begin, world, UI, end, post의6구간 합과 같다. 마지막 샘플 메타데이터 저장·벡터 추가의 작은 비용은 제외하고 `frame_period_ms`가 프레임 사이 비용까지 포함한다.
- `cpu_work_ms`: wall에서 fence/acquire/present/idle/FPS limiter 시간을 뺀 값. 드라이버 제출(submit)은 작업에 포함하고 별도 열도 기록한다. 명시적 대기를 제외한 경과 시간이며 OS 스케줄 아웃·드라이버 내부 숨은 대기까지 제거한 순수 CPU 실행 시간은 아니다.
- `thread_cpu_ms`: Windows GetThreadTimes의 kernel+user 차이. 메인 스레드만 측정하며 월드 worker를 포함한 프로세스 총 CPU 사용률이 아니다. OS 집계 해상도가 거칠어 평균만 해석하고 프레임별 p95를 사용하지 않는다. 일부 반복에서 작업 경과 시간보다 크게 나오는 차이에는 집계 양자화와 대기 API 내 CPU 실행 등이 포함될 수 있다.
- `fence_wait_ms`: 프레임 슬롯 재사용을 위한 vkWaitForFences 시간.2슬롯이므로 현재 프레임 n의 기록은 n−2 제출을 기다린 시간이다. `waited_serial`과 진입 전 `fence_pending`을 함께 기록한다.
- `acquire_ms`/`present_ms`, `idle_wait_ms`, `submit_ms`, `limiter_ms`: 각각 이미지 획득/표시, Renderer의 device·upload queue idle, 일반·즉시 제출, FPS 제한 호출 경과 시간. 이번 steady 헤드리스에서는 acquire/present/idle/limiter 모두0이었다. 임의 리사이즈·다른 서브시스템의 재생성 대기를 전부 분리 계측한다는 뜻은 아니다.
- GPU CSV에는 frame 계측을 켰을 때만 기존 쿼리의 `start_tick,end_tick,timestamp_period_ns,timestamp_bits`를 추가한다. 추가 GPU 쿼리·강제 동기화 없이 얻는다. CPU/GPU 시계는 보정하지 않았으므로 서로 직접 빼지 않는다.
- CPU/GPU frame은 제출 serial로 연결한다. 서로 병렬로 일하므로 CPU 작업과 GPU 실행 시간을 더해 프레임 시간을 계산하면 안 된다. `capture=1` 프레임은 저장/동기화가 개입하므로 성능 해석에서 제외한다.
- 계측은 타이머·GetThreadTimes·fence 상태 조회 비용을 포함한다. --profile-frame을 생략하면 이 추가 타이머/OS 조회를 실행하지 않는다. 측정 결과는 완전히 로딩된 정지 장면이며 실제 창의 표시·입력 지연, 이동 중 스트리밍/블록 편집 병목은 판정하지 않았다.

## 재측정

일반 빌드·패키징에는 계측이 자동 실행되지 않는다. 실행 파일 옆 설정을 읽으므로 비교할 때 fixture와 동일한 설정을 사용해야 한다. 아래는 한 거리의 실행 예다. 두 CSV는 서로 다른 새 경로를 지정한다.

```powershell
& ./out/DOLBUTO/DOLBUTO.exe --headless --render-distance 32 --profile-gpu ./build/frame32-gpu.csv --profile-frame ./build/frame32-cpu.csv --profile-samples 1200 --profile-view 193.6 -90 -22 12 --profile-origin 0 0 --seconds 300
```

--profile-frame은 --profile-gpu가 필수이며 --profile-draws는 필요 없다. CSV에는 로딩·예열도 있으므로 steady=1/capture=0만 집계하고 목표 표본 수와 LOD 완료를 확인한다. 이때 GPU·CPU frame ID를 연결하고 wait는 waited_serial로 해석한다.

## 검증·패키징

Release 빌드와 패키징 완료. TAA OFF/블룸40/validation ON의 전후64steady 실행 각각 종료0, Vulkan 오류0, 기존 shader interface 경고10. 전후 PNG 픽셀 동일. 성능9회 종료0, 오류0, CPU/GPU 모든 steady frame 일치,6구간 합=wall 및 작업+명시 대기=wall, LOD pending/queued0 확인. 성능 실행의 경고0은 validation을 껐기 때문이며 별도 검증 실행과 구분한다. 자동 테스트 프레임워크/CTest/CU/합성 입력은 사용하지 않았다. 창 표시/리사이즈 경로는 실행 검증하지 않았다.

- 계측 전 EXE SHA-256: `BFA5A72B5745D7CD86D4B520BE7AF0F9B02716BC15EB9312B69AC4D88C757706`
- 계측 후 build/bin·fixture·out EXE SHA-256 모두 일치: `D7F799EFF29E149FC274CFF0D31BB94EC76BF6B3081B7A565D9196D6B4723C54`
- 패키지 사용자 settings SHA-256 보존: `67E209936EE549CD91945CF19FF059751F71C5DE5BFD3FBB188A75BB7168419B`
- 24개 SPIR-V 모두 작업 전/후/패키지 동일. 기존 XZ 거리순 렌더링·블룸·descriptor 변경을 보존했다. commit/push 없음.

[집계 JSON](benchmarks/frame-cost-2026-10-01-summary.json)에 각 반복·구간 평균/p95, 실행 인자와 설정/EXE 해시를 보관했다. 원시 CSV·로그·PNG·전후 실행 폴더·measure.ps1·analyze.py는 Git 제외 경로 build/release/frame-cost-profile에 있다. 빌드/패키지 로그는 build/frame-cost-build.log 및 build/frame-cost-package.log다.
