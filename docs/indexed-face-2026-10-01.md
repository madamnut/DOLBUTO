# 블록 면 정점 재사용 비교 (2026-10-01)

최종 판단은 두 후보 모두 미채택이다. 그림자까지 바꾼 초기 후보는 전체 GPU가 증가했고, 일반 지형 전용 후보도 전체 평균 감소가 작고 반복/p95가 일관되지 않아 기존 구현으로 복원했다. 이전 블룸16샘플/descriptor 재사용은 유지한다.

## 초기 후보 구현 — 그림자 경로는 미채택

일반 지형/LOD 디버그와 근거리 그림자에서 면당6개 순차 정점 대신4개의 고유 정점 번호를 공유한다.12바이트 UINT16 인덱스 버퍼 [0,1,2,3,2,1] 하나를 WorldView 수명 동안 재사용하며 vkCmdDrawIndexed의 인스턴스 수/firstInstance는 기존과 같다. 삼각형은 여전히 면당2개이며 draw 수나 메시/조명 버퍼는 줄이지 않는다. 실제 GPU 정점 셰이더 호출 수는 별도 계측하지 않았다.

world.vert의 specialization constant2로 새 경로를 선택한다. 주 화면 고체/디버그 및 world.vert 그림자에만 활성화하고, 물/얼음의 색상·SSR·깊이 패스, 플레이어와 LOD 메시 렌더링은 기존 경로를 유지한다. 용암이 고체 그룹에 포함될 수 있으므로 새 경로도 유체 높이와 삼각형별 법선을 처리한다. buffer 생성/flush는 초기화 때 한 번, 해제는 GPU idle 이후 수행한다.

기본 코너는0=(0,0),1=(1,0),2=(1,1),3=(0,1)이다. 다음 삼각형 순서는 순환 순열만 달라 형상·대각선·앞뒷면 방향이 같다.

| 대각선 / 방향 | 기존 두 삼각형 | 후보 두 삼각형 |
|---|---|---|
|기본 / +|012, 023|120, 302|
|기본 / −|021, 032|102, 320|
|반대 / +|013, 123|013, 231|
|반대 / −|031, 132|031, 213|

인덱스의0과3은 각각 삼각형의 첫 정점이며 서로 공유하지 않는다. 평평하게 보간하는 유체 법선은 첫 정점0에 첫 삼각형 법선, 첫 정점3에 둘째 법선을 계산한다. 공유 정점1·2의 법선은 flat 값 선택에 사용되지 않는다. 눈/물/용암의 반대 대각선 비트 해석은 기존 예외를 유지하고 AO·조명은 실제 corner 번호로 읽는다. 근거: [Vulkan 기본 provoking vertex 규칙](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_provoking_vertex.html), [indexed VertexIndex 정의](https://docs.vulkan.org/refpages/latest/refpages/source/VertexIndex.html).

## 비교 조건과 기준

사용자가 지정한 최우선 기준은 전체 GPU 실행 시간이며 구간별 개선만으로 채택하지 않는다. 프레젠테이션/입력부터 화면까지의 지연시간은 headless GPU timestamp와 다르며 이번에는 측정하지 않는다.

- Windows/Clang Release, i7-14700F/RTX5060, headless1280×900, LOD64/FOV75/블룸12/TAA ON, 평지 seed1337. 기존 블룸16샘플/descriptor 재사용은 양쪽에 포함하고 취소된 구름 기여0 분기는 양쪽 모두 없다.
- 근거리12/24/32, height193.6/yaw−90/pitch−22/hour12/origin0/0. 준비 완료 뒤240프레임 예열, 실행당640steady. 초기 계획은 각 거리3쌍18회였으나 초기 후보는9회 완료 뒤 전체 GPU 악화로 중단했다. 아래 후속 일반 지형 전용 후보는 별도18회 비교한다. 반복1·3은 거리 정순/before→after, 반복2는 역순/after→before. 순차 실행, 완료된 불리한 결과도 제거하지 않는다.
- 별도 검증은 TAA OFF/블룸40/validation ON, 기본·순환 경계(origin8191/8191,yaw45,pitch−2)·상공(height600,origin−1/8192,yaw137,pitch−89)·근거리 LOD 디버그 전후8회/각64steady. 일반 세 장면은 CPU 세부 계측 ON, 디버그는 GPU 계측만 사용하고 원거리 LOD를 OFF로 둔다. 기본 시점 raw 그림자도 캡처한다.
- 비교 원본 소스·바이너리·셰이더·설정·로그·CSV·PNG·실행 인자/해시: build/release/indexed-face-compare. 빌드 로그 build/indexed-face-build.log.

일반 평지와 여러 시점 검증은 실제 물/용암/눈/얼음 편집 장면 전체를 검증한 것이 아니다. 삼각형별 유체 법선과 코너 대응은 소스 경로 및 위 순서로 검토했다. 게임 입력·이동·리사이즈를 자동 조작하지 않으며 자동 테스트/CTest/CU/합성 입력은 추가하지 않는다.

## 검증 결과

최종8회 모두exit0/Vulkan errors0/기존 미사용 셰이더 출력 warnings10/UI0/texture0. 네 장면 모두 전후 RGB 픽셀 동일. 일반 세 장면은 CPU/GPU64steady frame 일치, LOD pending/queued0, draw/triangles/LOD tiles/columns 일치, descriptor 갱신0 유지. 디버그는 GPU64steady와 삼각형 수 일치. 기본 시점 raw 그림자 all-depth.f32/solid-depth.f32/colour.rgba8/shaft.rgba8/extent.txt도 바이트 동일했다. 후보 기본 PNG에서 지형·선택 테두리·플레이어 그림자를 직접 확인했다.

최초 디버그 시도는 기존 CLI의 --profile-draws와 --lod-debug 동시 사용 제한으로 실행 전에 종료돼 invalid-arguments-*로 남기고 제외했다. GPU만 계측한 첫 디버그 쌍은 원거리 LOD 준비를 기다리지 않아 before201/after199 tiles로 달랐으므로 partial-lod-*로 보존하고 출력 비교에서 제외했다. 최종 디버그 쌍은 원거리 LOD를 꺼서 변경한 근거리 디버그 파이프라인만 비교했다. 이는 실패한 검증을 숨기거나 전체 LOD 디버그 검증으로 주장하지 않기 위한 구분이다.

before EXE SHA256 C8D7DD370D0610DE2D3BB857EAB1F4D24E4D526A0763E9F3522EF41A905E754F, 초기after336F2DD0094026ED077E594230EF9B0C70F39A27112A5AF4D36CBA59ACDECE91. world.vert SPIR-V before133F23B371871BD19191EDF279A5EFC78AB320E3E66DE9B4D345248AA08D267E, after7742FEA9C9174B1CAC5BD1F4766C135F50F46B17CF86000FC5D76F324356AEB7.

## 초기 성능 확인과 범위 축소

완료9회(4쌍+단독 r24-after-2)는 모두640steady/오류0이며 결과를 전부 보존했다. r24-before-2 진행 중 실행을 중단해 미완료 실행은 통계에서 제외했다. 초기 후보를18회 완료한 것으로 보고하지 않는다.

| 완성 쌍 | 전체 GPU 평균 전→후(ms) |
|---|---|
|거리12 반복1|1.636683 → 1.655714|
|거리24 반복1|1.366937 → 1.399885|
|거리32 반복1|1.479112 → 1.529028|
|거리32 반복2|1.475085 → 1.522810|

거리24 반복1에서 일반 지형 구간은0.361430→0.345350ms로 감소했지만 그림자는0.343700→0.398421ms로 증가했다. 거리32 반복1도 일반 지형0.474711→0.457094ms 감소/그림자0.350694→0.418854ms 증가였다. 같은 정점 재사용이 두 패스에서 다르게 나타났으며 원인을 셰이더 계산·캐시·인덱스 처리 중 하나로 단정하지 않는다. [초기 전체 결과](benchmarks/indexed-face-pilot-2026-10-01-summary.json).

따라서 그림자와 SceneEffects 파이프라인의 변경만 원래대로 되돌린 후, 일반 지형/근거리 디버그에만 적용하는 후보를 별도로 빌드했다. 이미 승인된 같은 최적화의 적용 범위를 줄인 것이며 다른 알고리즘을 추가하지 않았다. 후속 fixture는 build/release/near-indexed-face-compare, 빌드 로그 build/near-indexed-face-build.log. 기준은 동일한 최적화 전 EXE/셰이더다. 초기 후보 소스는 원래 fixture의 candidate-source에 보존한다.

## 일반 지형 전용 후보 검증

SceneEffects 소스는 이번 작업 시작 전 사본과 해시가 같고 그림자 vkCmdDraw 및 specialization 설정도 원래대로다. world.vert의 indexed 경로는 주 화면 일반 지형/근거리 디버그에서만 활성화한다. 그 외 경로에서는 specialization constant2의 기본false로 기존 코드가 사용된다. after EXE SHA256 D07A03E17CE113471AFCC8B5935CA99EC346BDBEF15DEDE865D00570861130BC. world.vert SPIR-V는 초기 후보와 같고 pipeline specialization 설정이 다르다.

같은 네 장면의 전후8회를 새 fixture에서 다시 실행했다. 모두exit0/Vulkan errors0/기존 warnings10/UI0/texture0, 네 장면 RGB 픽셀 동일. 일반 세 장면의64steady CPU/GPU frame·출력 카운터가 일치했고 디버그의64steady GPU/삼각형 수도 일치했다. 기본 시점 raw 그림자4종+extent.txt도 전후 바이트 동일했다. 실제 유체 편집이나 실행 중 F4/리사이즈 조작을 수행한 것은 아니다.

## 일반 지형 전용 후보 성능과 최종 판단

전18회/11,520steady 모두exit0/Vulkan errors0/warnings0/UI0/texture0. 각640steady CPU/GPU frame 일치, LOD pending/queued0, 거리별 draw/triangles/LOD tiles/columns 전후 동일. 아래는 각 변형1920steady 평균과 nearest-rank p95, 단위ms다.

| 거리 | 전체 GPU 평균 전→후 | 감소 | 전체 GPU p95 전→후 | 일반 지형·플레이어 평균 전→후 |
|---|---|---:|---|---|
|12|1.646618 → 1.640579|0.4%|1.796736 → 1.780352|0.244334 → 0.238454|
|24|1.355904 → 1.351321|0.3%|1.499616 → 1.506048|0.360151 → 0.352178|
|32|1.488540 → 1.479828|0.6%|1.640928 → 1.632640|0.477373 → 0.465125|

전체 평균 절감량은 프레임당 약0.0046~0.0087ms다. 일반 지형·플레이어 구간은2.2~2.6% 감소했지만, 전체9쌍 중3쌍은 오히려 증가했다: 거리12 반복1의1.625584→1.627010, 거리24 반복2의1.361215→1.363001, 거리32 반복2의1.487888→1.491151. 이 세 쌍의 전체p95도 증가했고 합산 거리24의p95 역시 증가했다. 유효 표본을 제거하지 않았다.

따라서 평균상 작은 감소는 관찰했으나 전체 GPU 실행 시간을 일관되게 줄였다는 근거는 부족하다. 사용자의 GPU 지연 우선 기준에 따라 최종 미채택했다. 고유 정점 번호4개라는 수치만으로 하드웨어 실행 비용 감소를 보장하지 않으며 정점 셰이더 실제 호출/캐시/메모리/명령 처리 비용은 따로 분리 계측하지 않았다. 입력부터 화면 표시까지의 지연시간 또는 FPS 향상도 주장하지 않는다.

TAA ON 성능 PNG9쌍의 평균 절대 RGB 차이 최대0.338088/255, 해당 쌍 최대32다. 서로 다른 캡처 프레임/TAA 위상이므로 픽셀 동일 판정에는 위 TAA OFF 검증을 사용한다. [최종 후보 전체 통계](benchmarks/near-indexed-face-2026-10-01-summary.json).

## 최종 복원

world_view.cpp/world_view.hpp/world.vert를 이번 작업 전 사본으로 복원하고 수정시간을 갱신했다. SceneEffects도 작업 전 사본과 해시가 같음을 확인했다. 기존 descriptor 계측/재사용이나 블룸16샘플은 제거하지 않았다. build/indexed-face-restored-build.log에서 world.vert SPIR-V 재생성과 world_view.cpp의 실제 재컴파일을 확인했고 build/indexed-face-restored-package.log로 패키징했다. 새 후보 소스는 near-indexed-face-compare/candidate-source에 남긴다.

복원 실행은 기본 시점/거리12/TAA OFF/블룸40/validation ON/세부 CPU 계측 ON으로64steady를 확인했다. exit0/errors0/기존 warnings10/UI0/texture0, CPU/GPU frame 일치, near draw209/LOD tiles402/triangles570112/descriptor 갱신0. PNG와 raw 그림자4종+extent 모두 before와 바이트/픽셀 동일하다.

build/bin·out/DOLBUTO·restored EXE SHA256은 A0F8D792B37D0E6B31A282025EDC2420DCA0F9A839C029EA4564876ABDA07569로 동일하다.24개 SPIR-V 모두 기준과 같고 사용자 settings 파일 해시도 보존했다. commit/push는 수행하지 않았다.
