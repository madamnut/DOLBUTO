# 일반 지형 CPU 세부 조사 · 2026-10-01

AI 작업 컨텍스트. 다음 후보인 빈 청크 순회 제거와 컬럼 단위 시야 제외 중 우선순위를 정하기 위해 사용자 `ㄱㄱ`로 세부 계측·빌드·헤드리스 조사를 승인했다. 이번 변경은 진단 기능이며 실제 그릴 목록·메시 수명·컬링 결과를 바꾸는 최적화는 아직 적용하지 않았다.

## 측정 방법

`--profile-near-detail`을 기존 `--profile-gpu` 및 `--profile-draws`와 함께 사용한다. 주 화면의 일반 지형 패스에서 다음을 기록한다.

- 공개된 컬럼, 확인한 세로 청크 슬롯, 비어 있지 않은 메시, 기존 시야 검사에서 제외/통과한 청크 수.
- 컬럼 전체 AABB(16×512×16)를 가상으로 검사했을 때 제외할 수 있는 컬럼 수와 그 안의 nonempty 청크 수. 실제로 건너뛰지 않는다. 기존 청크 시야 판정을 통과하는 청크가 가상 제외 컬럼 안에 있으면 rejection_conflicts로 기록한다.
- published 컬럼 인덱스32개 중1개만 세부 시간 측정. phase를 매 프레임0..31로 회전시킨다. 고정 장면640steady에서 각 컬럼은20번 표집된다.

표집된 컬럼 내부에서 scan은32슬롯 순회/빈 메시 검사, cull은 nonempty 청크 상대 좌표/시야 판정, record는 통과 청크 선택 표시/바인딩/push/draw/물·얼음 목록/통계 갱신이다. 외부 map 순회·컬럼 base 계산·가상 컬럼 판정·최초 파이프라인 바인딩은 이 세부 시간에 포함하지 않는다. 기존 near_cpu_ms는 전체 구간을 유지하여 상세 모드의 추가 비용도 잡는다.

sample 시간은 표집 원시합이다. 평균×32는 장면 전체에 대한 근사치지만 타이머·카운터 비용이 포함된다. 특히 매우 짧은 cull 구간은 시계 읽기 비용의 비중이 높다. 따라서 확장 추정 합이 실제 기본 실행 총시간을 초과할 수 있으며, 비율을 정상 실행 CPU 점유율이나 절감 가능량으로 해석하지 않는다. 진단 OFF에서는 constexpr로 세부 타이머/카운터를 제외한 기존 순회와 명령을 사용한다.

## 조건

- Windows x64/i7-14700F/RTX5060/Release, headless1280×900. 성능 실행 validation OFF.
- 이전 LOD 비교와 같은 settings.json: SHA256 `285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4`. 평지/seed1337/자연 물 없음/원점0,0/높이193.6/yaw−90/pitch−22/정오12/FOV75.
- 근거리12/24/32, LOD64 ON, 그림자192/품질2, 기타 효과 ON, VSync/FPS 제한 OFF.
- control은 기존 전체 CPU 계측만, detail은 세부 옵션 추가. 두 모드 모두 같은 실행 파일이다. near/LOD 준비 완료 뒤240프레임 예열,640steady. 처음·셋째 반복은12→24→32/control→detail, 둘째는32→24→12/detail→control. 순차 실행, 이상치 제거 없음.
- 원본 CSV/PNG/로그/설정/실행 파일/분석: `build/release/near-detail-inspection`. 성능 데이터에 validation 실행을 섞지 않는다.
- 최종 바이너리 SHA256: `08A07F3FB7B57D7F036509184E071A318CFE246D2994691E87C118F1D5284919`. 빌드 로그 `build/near-detail-build.log`.
- 성능18회 모두640steady로 총11,520프레임, 각 거리/모드1920표본. 통계 사본 `docs/benchmarks/near-draw-detail-2026-10-01-summary.json`에 실행별 값도 보관한다.

```text
current.exe --headless --render-distance R --profile-gpu rR-M-N-gpu.csv --profile-draws rR-M-N-draw.csv --profile-samples 640 --profile-view 193.6 -90 -22 12 --seconds 300 --capture rR-M-N.png
```

M=detail일 때 `--profile-near-detail`을 추가한다.

## CPU 관측값과 계측 영향

단위 ms. control/detail은 최적화 전후가 아니라 세부 진단 OFF/ON이다.

| 근거리 | control 평균 | detail 평균 | control p95 | detail p95 | 평균 차이 |
|---|---:|---:|---:|---:|---:|
|12|0.042464|0.053283|0.066800|0.081300|+25.5%|
|24|0.204369|0.257712|0.304700|0.408700|+26.1%|
|32|0.510251|0.613478|0.644600|0.751800|+20.2%|

| 근거리 | 표집 scan 평균×32 | 표집 cull 평균×32 | 표집 record 평균×32 |
|---|---:|---:|---:|
|12|0.047168|0.031033|0.020623|
|24|0.198303|0.107472|0.095108|
|32|0.418017|0.226733|0.224648|

표집 내부에서는 scan이 가장 크지만 시계 읽기/카운터 비용이 크게 섞였다. 거리32의 표집 확장 합0.869398ms가 실제 control 총0.510251ms보다 크므로 이 값들을 정밀 비용 분해나 절감 예측으로 사용하면 안 된다. 삼각형을 그리는 Vulkan 명령만의 시간도 아니다. 거리24 control 실행별 평균은0.259510/0.171758/0.181840ms로 변동했으며, 이를 제외하거나 가장 유리한 실행만 고르지 않았다. OFF/ON 평균 차이에는 계측뿐 아니라 실행 간 변동도 포함된다. 정상 경로의 성능 개선을 주장하지 않는다.

이번 결과에서 더 확실한 근거는 아래 전체 카운터다. 다음 실제 최적화에서는 세부 진단을 끄고 before/after 전체 CPU 시간을 비교해야 한다.

## 정확한 개수와 후보

| 근거리 | 컬럼 | 확인 슬롯 | nonempty | 기존 시야 제외 | 화면 통과 | 가상 컬럼 제외 | 그 안의 nonempty |
|---|---:|---:|---:|---:|---:|---:|---:|
|12|441|14112|882|673|209|224|448|
|24|1793|57376|3586|2569|1017|1126|2252|
|32|3209|102688|6418|4599|1819|2112|4224|

현재 평지는 각 컬럼32슬롯 중2슬롯에만 표시 메시가 있어 빈 슬롯 비율이93.75%다. 이는 공기 블록 비율이 아니라 CPU가 순회하는 GpuChunk 중 count0인 비율이다. 컬럼 전체 검사로는50.8/62.8/65.8%의 컬럼과 그 안의 슬롯 순회를 제외할 가능성이 있다. 측정한 고정 시점에서 가상 컬럼 제외와 기존 청크 시야 판정 충돌은0이었다. 향후 실제 도입 시 좌표 순환·이동·시야/높이 변화 등을 별도로 확인해야 한다.

우선 제안은 컬럼마다 nonempty 청크 인덱스/비트마스크를 유지하여32슬롯을 반복 확인하지 않는 방법이다. 공개/재메시/블록·유체 편집/컬럼 퇴거 때 갱신해야 하며, 실행 중 메시 교체와 자료 수명 관리가 필요하다. 컬럼 AABB 선별은 별도 단계로 비교하면 효과를 구분할 수 있다. 위 비율만으로 같은 비율의 CPU/FPS 향상을 약속하지 않는다.

## 검증과 한계

- Release 빌드와 clang-format/git diff --check 성공. `--profile-near-detail` 단독은 초기화 전 명확한 의존 옵션 오류 및 exit1.
- 성능18회 모두 exit0/Vulkan errors0/UI issues0/texture failures0. 각640steady의 CPU/GPU frame 일치, LOD pending/queued0, 카운터 산술 관계 확인. detail의32 phase는 각20회이고 sampled_columns 합은 전체 컬럼 합/32다. 표의 전체 카운터는 모든 해당 steady 프레임에서 동일하며 rejection_conflicts는 모두0이다.
- control/detail 모두 거리12/24/32 triangles는 각각570112/1035648/1494656으로 일치했다. 추가 계측이 실제 그릴 양을 바꾸지 않았다.
- 별도 거리12/64steady validation ON control/detail은 모두 exit0, Vulkan errors0, 기존 미사용 셰이더 출력 warnings10, UI issues0, texture failures0.
- validation control/detail의 그림자 raw4종 및 extent.txt SHA256은 동일하며 이전 LOD 최적화 빌드의 동일 fixture 결과와도 일치했다.
- 거리32 control/detail PNG를 직접 읽어 지형·하늘·그림자·선택 표시·HUD를 확인했다. 서로 다른 종료 프레임이므로 PNG 픽셀 동일을 주장하지 않는다.
- 고정 평지 조사다. 이동 입력·물/얼음 장면·산악 지형·블록 편집·재생성은 조사하지 않았다. 자동 테스트 프레임워크/CTest/CU/합성 입력을 추가하거나 사용하지 않았다.
- `out/DOLBUTO`에 최종 빌드를 패키징했다. 로그 `build/near-detail-package.log`. 설정 파일의 위 SHA256 보존, build/runtime/out 실행 파일 해시 일치. 이번 조사 변경은 commit/push하지 않았다.
