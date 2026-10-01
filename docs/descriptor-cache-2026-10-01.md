# 후처리 이미지 descriptor 재사용 (2026-10-01)

연결 준비 CPU 시간이 전후9쌍 모두 감소해 채택했다. 절약량은 프레임당 약0.0011~0.0015ms로 작으며 전체 GPU/FPS 개선은 확인하지 못했다.

## 구현과 수명

SceneEffects의 각 Frame이 마지막으로 등록한 이미지 뷰를 보관한다. 같은 프레임 슬롯의 같은 연결을 다시 쓸 때 vkUpdateDescriptorSets를 생략한다. 이미지 내용·카메라·TAA 계산·렌더 패스·이미지 장벽은 그대로 매 프레임 처리한다. 카메라 정지 여부에 의존하지 않는다.

| 갱신 묶음 | 변경을 확인하는 값 | 갱신 호출 / descriptor 수 |
|---|---|---|
| environment의 scene factor | 이전 슬롯 scene_factor 뷰 | 1 / 1 |
| 첫 블룸 mip과 tone | surface_depth_needed가 선택한 최종 장면 뷰 | 2 / 12 |
| TAA와 scene factor | 이전 슬롯 history 뷰, 현재 깊이 뷰 | 2 / 8 |
| atmosphere와 volume composite | 현재 깊이 뷰 | 3 / 10 |

초기에는 키가 모두 null이므로 두 프레임 슬롯이 각각 최초 사용될 때8회/31개를 등록한다. 이후 연결이 같으면0회/0개다. final_view가 바뀌면 블룸/tone 묶음이 다시 등록되며, 이전 슬롯이나 깊이 뷰가 바뀌면 해당 묶음이 갱신된다.

같은 Frame의 나머지 뷰 및 sampler는 SceneEffects 이미지 수명 동안 고정이다. previous history와 previous scene_factor는 같은 이전 슬롯을 따른다. 화면 크기·그림자 크기 변경은 ensure_images → wait_idle → release_images/descriptor pool reset → 재생성으로 이어진다. release_images의 f={}가 연결 키와 기존 Frame 상태를 함께 초기화하므로 Vulkan 핸들 값이 재사용되어도 새 연결을 등록한다. WorldView의 깊이 이미지는 해상도 변경 때 재생성하고 SceneEffects도 같은 해상도 변경으로 재생성한다. 슬롯의 fence 대기 후에만 해당 슬롯 descriptor를 갱신하는 기존 순서를 유지한다.

향후 Frame의 개별 이미지나 sampler를 이 경로 밖에서 교체한다면 그에 맞춰 캐시 키 또는 무효화 경로를 확장해야 한다. atmosphere와 volume_composite는 같은 command buffer 안에서 별도로 사용되는 descriptor set을 계속 유지한다.

## 계측

--profile-draws CSV 뒤에 scene_descriptor_cpu_ms, scene_descriptor_calls, scene_descriptor_writes를 추가했다. CPU 시간은 prepare의 연결 준비 묶음, atmosphere의 TAA/factor 연결 묶음, atmosphere/composite 연결 묶음 세 구간의 합이다. 인자 준비·키 비교·갱신 API 호출을 포함하며 셰이더 GPU 실행, uniform 복사/flush, 렌더 명령 기록, 초기 이미지 생성은 제외한다. 전체 후처리 CPU 시간이나 전체 프레임 시간으로 해석하면 안 된다.

양쪽 비교 빌드에 같은 타이머·카운터를 먼저 넣었고 이후 후보에만 캐시를 추가했다. 타이머 자체 비용과 호출별 카운터 증가도 포함된다. 명시적 계측 옵션이 없으면 clock 호출과 API 카운터 증가는 실행하지 않는다. stats는 ensure_images 뒤에 초기화하므로 초기 이미지 생성의 descriptor 쓰기는 이 카운터에서 제외된다.

## 비교 조건

- 기준은 HEAD31f3af5와 앞서 채택한 미커밋 블룸16샘플. 직전 push constants 분리 후보는 미채택/복원 상태다.
- Windows/Clang Release, i7-14700F/RTX5060, headless1280×900, LOD64/FOV75/블룸12/TAA ON, 평지 seed1337. 근거리12/24/32 각각 전후3회, 실행마다 준비 완료 후240프레임 예열과640steady.
- 높이193.6/yaw−90/pitch−22/hour12/origin0/0. 반복1·3은 거리 오름차순과 before→after, 반복2는 역순과 after→before. 순차 실행, 이상치 제거 없음. 성능 실행은 validation/세부 CPU 표집 OFF.
- before/after는 같은 셰이더·설정을 사용하며 이미지 렌더링 코드와 화질은 변경하지 않았다. 모든 CSV/PNG/로그/실행별 설정과 인자·해시는 build/release/descriptor-cache-compare에 보존한다.
- before EXE SHA256: 9CC53BADB8C7054D66225043872E52A5923F3BD8D74B546FA8588F5DAE4E9060.
- after EXE SHA256: D7AA96421842643C3BFDC8AA5752C5C4304A5C17C21C73AE0DE798203B41DB95.
- 빌드 로그: build/descriptor-before-build.log, build/descriptor-after-build.log.

```text
VARIANT.exe --headless --render-distance R --profile-gpu NAME-gpu.csv --profile-draws NAME-draw.csv --profile-samples 640 --profile-view 193.6 -90 -22 12 --profile-origin 0 0 --seconds 300 --capture NAME.png
```

## 출력 검증

별도 TAA OFF/validation ON/세부 CPU 표집 ON에서 기본 시점·순환 경계·하늘·일부 효과 OFF의 전후8회, 각64steady를 확인했다. 순환 경계는 origin8191/8191,yaw45,pitch−2, 하늘은 yaw0,pitch60이다. 효과 OFF 장면은 clouds/shafts/bloom/refraction/underwater_fog OFF 및 shadow_quality1로 final_view가 composite인 경로를 확인했다. 다른 세 장면의 shadow_quality는2다.

8회 모두exit0/Vulkan errors0/기존 미사용 셰이더 출력 warnings10/UI0/texture0. 네 장면 모두 전후 RGB 픽셀 동일, draw/triangles/LOD tiles/columns 동일, CPU/GPU frame 일치. 기본 시점의 그림자 raw4종+extent.txt도 동일하다. 기본 PNG를 직접 확인해 지형·선택 테두리·플레이어 그림자·HUD가 표시됨을 확인했다.

후보의 첫 두 프레임은 각각8회 등록했고 이후 전체 프레임의 갱신 호출은0회였다. 두 슬롯의 초기화와 반복 사용을 실제 실행으로 확인했다. 실행 중 리사이즈·옵션 전환·수중 이동·메뉴 복귀는 조작하지 않았으며 해당 수명/무효화 경로는 소스로 검토했다. 별도 실행의 서로 다른 초기 설정이 실행 중 전환 검증을 대신하지는 않는다. 자동 테스트/CTest/CU/합성 입력은 추가하지 않았다.

## 성능 결과와 판단

18회/11,520steady 모두exit0/Vulkan errors0/warnings0/UI0/texture0. 각640steady CPU/GPU frame 일치와 LOD pending/queued0을 확인했다. 각 거리의 draw/triangles/LOD tiles/columns는 전후 동일하다. 각 변형1920steady 평균과 nearest-rank p95, 단위ms다.

| 거리 | 연결 준비 CPU 평균 전→후 | 감소 | 해당 CPU p95 전→후 | 전체 GPU 평균 전→후 | 전체 GPU p95 전→후 |
|---|---|---:|---|---|---|
|12|0.001319 → 0.000158|88.0%|0.002100 → 0.000300|1.643581 → 1.652598|1.787840 → 1.882496|
|24|0.001680 → 0.000194|88.4%|0.001900 → 0.000300|1.351301 → 1.352135|1.368384 → 1.380544|
|32|0.001191 → 0.000137|88.5%|0.001800 → 0.000300|1.480266 → 1.480564|1.497536 → 1.497344|

9쌍 모두 연결 준비 CPU 평균과 p95가 감소했다. 기존8회/31개 descriptor 쓰기는 steady에서0회/0개로 감소했다. 프레임당 약1.05~1.49마이크로초의 작은 절약이다. 타이머 분해능과 세 구간의 clock 비용이 포함되므로 감소율을 전체 CPU 개선율로 해석하지 않는다.

거리24 before 첫째에는 최대1.0832ms, after 둘째에는 최대0.1341ms의 큰 단일 표본이 있었고 제거하지 않았다. 평균은 이런 변동의 영향을 받으며 해당 거리 p95도 함께 제시했다. 전체 GPU 평균은 거리12/24/32에서 각각0.55/0.06/0.02% 증가했고 거리12·24의 p95도 증가했다. 셰이더·그리기·동기화 명령은 동일하며 전체 GPU 차이의 원인은 분리 계측하지 않았다. GPU/FPS 향상이나 끊김 감소를 입증한 결과가 아니다.

TAA ON 성능 PNG9쌍의 평균 절대 RGB 차이 최대0.243049/255, 해당 쌍 최대28이다. 캡처 프레임/TAA 위상이 같지 않으므로 픽셀 동일성은 TAA OFF 검증 결과와 구분한다. 통계 사본은 [JSON](benchmarks/descriptor-cache-2026-10-01-summary.json).

## 최종 상태

구현과 CLI 계측을 소스에 반영하고 Release 빌드·패키징했다. 계측 after.exe·build/release/bin/DOLBUTO.exe·out/DOLBUTO/DOLBUTO.exe 해시는 위 after SHA256과 모두 같다.24개 SPIR-V는 비교 전/빌드/배포가 모두 동일하여 기존 블룸16샘플도 유지한다. 패키지 settings.json을 보존했고 계측용 settings SHA256도285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4로 복구했다. 패키징 로그는 build/descriptor-package.log. commit/push는 수행하지 않았다.
