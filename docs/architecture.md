# 구현 컨텍스트

## 2026-09-26 세션 LOD

현재 원거리 표현은 [세션 LOD](lod-2026-09-26.md)를 따른다. 이전 LOD 제외 기록은 과거 이력이다. 실제 블록 생성과 원형 근거리 컬럼 공개 규칙은 유지한다.

2026-09-26 최신 생성기: [주기적 Double Perlin 전환](periodic-double-perlin-2026-09-26.md). 아래 Simplex/격자/단순3D 설명은 이전 이력이다. 현재 schema10, 공유 Shift, 원본 중첩 Hermite, Blended3D를 사용한다.


## 유체 계층과 고정 틱 (2026-09-25)

world/fluid.*의 활성셀 예약/동시반영 솔버,Chunk 독립 유체snapshot,WorldEdits 유체override,WorldView의 입력 독립20TPS갱신을 추가했다. 물 수면은 양에 따라 메시를 갱신하며 기존 light/mesh workers를 공유한다. [fluids-2026-09-25.md](fluids-2026-09-25.md) 참조.

## 독립 월드 생성 편집기 (2026-09-24)

`src/editor/main.cpp`의 worldgen_editor는 world_core를 직접 링크하여 기존 설정 검증·노이즈·지형 지도·곡선 계산을 공유한다. assets/editor의 HTML/CSS/JS는 편집과 표시만 담당한다. 별도 게임 루프나 Vulkan 인스턴스를 만들지 않는다. 초안/확정 저장과 F8 수동 불러오기를 연결하며 저장 schema6은 유지한다. 자세한 API·파일 흐름·제약은 [editor.md](editor.md)를 따른다.

## 10칸 핫바 (2026-09-23)

terrain.hpp의 hotbar_blocks를10칸으로 확장하고 빈칸은Block::air placeholder로 둔다. WorldView는 블록값 대신 선택 슬롯 인덱스를 보관하므로 여러 빈칸도 각각 선택된다. select_slot의 범위검사/휠잔여초기화,10칸 modulo 휠순환, 빈칸 설치 조기반환을 사용한다. 키1~9는인덱스0~8,0은9이며 기존 UI/일시정지 입력차단 안에서 처리한다. HUD 선택 갱신 범위는 배열크기를 사용한다. world.rml의10칸과 world.rcss의 슬롯/아이콘/선택이미지를 연결했다. 추가 블록 등록/메시 ABI/저장schema 변경은 없다. 검증은 verification.md 최신 절을 따른다.

## 절차적 천체 (2026-09-23)

shaders/celestial.glsl에서 태양·보름달·별을 이미지 없이 합성한다. environment 공통 include를 통해 atmosphere의 배경 하늘과 물 반사가 공유하며 기존 구름 합성/깊이 가림을 유지한다. SceneEffects UBO는464bytes로 유지하고 water_origin.z/w를 두 표시 플래그로 사용한다. GraphicsSettings 공통 toggle 목록에 sun_moon/stars를 등록하여 두 옵션 UI와 schema8 저장을 연결했다. v1..7 설정은 새 표시만 기본ON으로 읽는다. 상세는 [graphics.md](graphics.md), 실제 확인 범위는 [verification.md](verification.md)를 따른다.

## Complementary 구름 형상 (2026-09-23)

사용자 `구름 실시`로 shaders/clouds.glsl에 원본 Unbound 밀도·두께·적분·내부 음영을 이식했다. 환경 공통 include를 통해 반해상도 구름, 깊이 경계 보완, 물 반사가 같은 경로를 쓴다. SceneEffects의 기존 cloud.z/w를 품질/정규화 바람 위상으로 사용하며 UBO464bytes와 GPU 리소스 수는 유지한다. 옵션/순환 월드/사용자 저장 파일을 보존한다. 아래 기존 자체 구름 형상 설명은 이전 이력이다. 현재 수식과 원본 대비 차이는 [graphics.md](graphics.md), 빌드와 실행 확인은 [verification.md](verification.md)를 따른다.

## 물 재질/SSR/굴절 이식 (2026-09-23, 현재)

사용자 `싹 맞춰봐 실시`로 Complementary WATER_STYLE3 물결/시차/수심색·alpha/반사/GGX/거품/수중반사를 이식했다. WaterEffects의 Hi-Z 리소스와 mip 패스를 제거하고 원본30step/6refine SSR을75% 해상도에 계산한다. 새 water_highlight.glsl은원본GGX이며 공용 full-screen vertex는fullscreen.vert다. 수면+반사 합성후굴절은SceneEffects atmosphere phase1/3에 옮겼다. env.view.w를사용하고refraction만ON이어도물depth+최종합성을실행한다. 원본 gamma-like 혼합 후 HDR로 돌리므로 물 hardware blending은OFF다. UBO크기·저장schema·월드규칙은유지한다. 상세/한계는 graphics.md최신물절.

## Complementary 조명 이식 (2026-09-23, 현재)

사용자 `실시`로 원본 기본 조명/그림자/갓레이/하늘·안개·구름 조명/톤·블룸/TAA를 이식했다. 기존20Hz/cascade는 매 프레임 동일 투영의 고체+물/고체 전용 depth 쌍과 왜곡 그림자로 대체한다. 원본형 전체 화면 TAA는 ON이고 옵션에 토글이 있다. 원본 Scene Aware 값은1×1 GPU 이력, 수중 빛줄기는 물/고체의 깊이와 원본 노이즈를 사용한다. 월드 생성/조명 전파/게임틱은 변경하지 않았다. 상세와 원본 입력 대응 한계는 [graphics.md](graphics.md)의 최신 절, 실제 빌드/검증은 [verification.md](verification.md)를 따른다. 아래 날짜별 내용은 당시 구현 이력이다.

## 갓레이 보강 롤백 (2026-09-22)

사용자 `방금 한 위 작업 롤백실시`로 직전 갓레이 보강4종을 모두 취소했다. 균일8/12/20표본, 카메라 하늘빛에 의한 감쇠, 윤곽 경계의 구름 전용 재계산,0.00065 선형 산란으로 복구한다. 새 공기 그림자/공통 볼륨 함수는 제거하며 옵션 글자 수정과 그림자20Hz 갱신은 유지한다. 새 규칙에 따라 `실시/ㄱㄱ/ㅅㅅ`는 빌드까지 포함하고 명시적인 빌드 생략 지시만 예외다. 아래 갓레이 보강 절은 취소된 구현 이력이다. 현재 상세는 [graphics.md](graphics.md), 빌드 결과는 [verification.md](verification.md).

## 근거리 갓레이 가림 보강 (2026-09-22, 이후 롤백된 이력)

후속 `실시` 승인으로 빛줄기를24/40/64표본으로 바꾸고 첫24블록에 계산을 집중했다. 공기 전용2×2 그림자 비교로 조회 비용을 제한하고 카메라 하늘빛에 의한 전체 감쇠를 제거했다. 반해상도와 윤곽 경계의 재계산이 같은 scene_volume 함수를 사용하며 구간 길이/거리 감쇠를 반영한 산란 강도를 적용한다. 그림자20Hz/지형 표면PCF/구름·물 경로/설정·UBO 구조는 유지한다. 이번에는 빌드·실행·패키징·자동테스트·컴퓨터 유즈를 하지 않는다. 상세 수치/한계는 [graphics.md](graphics.md), 검토는 [verification.md](verification.md). 아래는 당시 이력이다.

## 옵션 글자 수정·그림자20Hz 갱신 (2026-09-22)

사용자 `실시하고 빌드까지 해` 승인으로 양쪽 옵션의 체크박스 텍스트를 명시적인 flex 자식으로 감싸 표시를 복구하고, 그림자맵을50ms 주기의 공유 캐시로 바꾼다. 근거리64×64/4096²·원거리512×512/2048² 및 좌표안정화·PCF는 유지하며 이전 시간축 누적은 제거한다. 갱신 사이에는 생성 당시 그림자행렬과 깊이를 함께 재사용하고 카메라 상대좌표만 보정한다. 편집·재생성·큰 위치이동·그림자 설정변경은 즉시 갱신한다. 환경UBO352bytes/공유D32기본80MiB, 프레임당9sets/34samplers이며 RG32F history/MRT와 별도 기본물 shader 변형은 제거했다. Release 빌드·패키징을 포함하고 자동테스트는 복구하지 않는다. 사용자 settings/worldgen은 보존한다. 최신 상세는 [graphics.md](graphics.md), 수행한 검증은 [verification.md](verification.md). 아래 절들은 당시 이력이다.

## 그림자 구간 분리·시간축 안정화 (2026-09-21, 소스 반영·미빌드)

태양 고정 시 떨림이 사라진다는 사용자 확인과 후속 `실시`로 근거리64×64/4096², 원거리512×512/2048² 기본값과 구간별 픽셀 정렬·PCF 경계 혼합·시간축 그림자 필터를 적용했다. 기존 불투명 패스에서 RG32F 이력을 함께 기록하고, 법선/깊이/가림 변화 및 편집·스트리밍·빠른 시각 조절을 검사한다. 환경 UBO512bytes와독립2D 그림자맵으로 변경했으며 물 UBO/청크/생성기는 유지한다. settings schema6은 구형 파일의 그림자 프리셋만 새 기본값으로 메모리 이행한다. 파일 저장·빌드·실행·패키징은 하지 않았다. 현재 상세는 [graphics.md](graphics.md), 검토 범위는 [verification.md](verification.md). 아래 항목은 당시 이력이다.

## 물가·수중 효과·지형 그림자 보정 (2026-09-21)

사용자 `싹 실시하고 빌드까지 실시`로 물가의 짙은 물색, bit28 물 접촉 면의 공통 빛무늬, 실제 출구/지형 거리 기반 수중 안개 및 개별 옵션(schema5), 지형 그림자4×4 tent/깊이 보정을 적용했다. 이전 수중 굴절 전용 범위는 이 승인으로 확장한다. 환경 UBO는352bytes로 늘고 물 UBO224bytes/면4bytes는 유지한다. GPU 검증 중 확인된 discard 기능 활성화와swapchain acquire 동기화도 수정했다. 아래 기록은 당시 이력이며 현재 상세는 [graphics.md](graphics.md), 빌드/검증 범위는 [verification.md](verification.md)를 따른다.

## 반사 교차·구름 부피·수중 굴절 (2026-09-21)

사용자 `실시하고 빌드까지 실시`로 SSR 면 교차/둘러싸인 누락 복원, 주기적 셀 덩어리+워핑 구름, 실제 카메라 물 판정과 수중 굴절 전용 경로를 적용했다. 물 밖 구름도 굴절되도록 수중에서는 sky/fog/volume을 물 snapshot 전에 합성하고 최종색B를 사용한다. 수면 위는 기존 전경 구름 순서와75% SSR을 유지한다. 아래 과거 미빌드 기록과 구름의 얇은 value-noise 층 설명은 현재 상태가 아니다. 현재 구조/한계는 [graphics.md](graphics.md), 이번 빌드와 기본 실행 범위는 [verification.md](verification.md)를 따른다.

## 그래픽 후속 보정 (2026-09-21, 소스 반영·미빌드)

사용자 `ㅇㅋ 싹 실시`로 그림자 정밀도/필터·근거리 기준점, 구름 형상/양 조절 키, 넓은 분류별 옵션, 물 이후 전경 구름 합성, SSR 성공도·거리 분리/이웃 보충을 적용했다. 렌더 순서는 고체→하늘/안개→물 색→수면 깊이→구름/빛줄기→블룸/톤/UI다. Esc 일시정지와 전체 옵션 상태를 나누며 `-`/`=`는 구름 양0..100%를 설정과 공유한다. 이번 소스와 아래 마지막 빌드 상태를 구분한다. 세부 리소스·경계·한계는 [graphics.md](graphics.md)를 우선한다. 지형 생성/블록/조명전파/컬럼 공개는 그대로다.

## HDR 그래픽 통합 (2026-09-21, Release 빌드 완료·실행 미검증)

`SceneEffects`와 공유 `environment.glsl`로 HDR 월드→그림자/구름/대기→물→블룸/톤 매핑→UI를 연결했다. 월드/물의 색 attachment 및 snapshot은RGBA16F이며 물UBO는224bytes다. UI와 화면 캡처는 기존 swapchain을 사용한다. settings schema4와 그래픽 옵션, 품질/의존 항목, 리소스/레이아웃/한계는 [graphics.md](graphics.md)를 우선한다. 아래 물 전용 절의 UNORM snapshot/208bytes/schema3는 이전 구현 이력이다.

## 물 전용 화면 공간 효과 (2026-09-21, 미빌드)

`render/water_effects.*`가 물 전용 파이프라인, scene descriptor set2, 진행 중2프레임의 이미지/208byte UBO를 소유한다. set0 atlas/set1 압축 면·밝기와96byte vertex push는 기존 월드와 호환한다. GLSL은 world.vert, water.frag/water_ssr.frag와 공유 water_common.glsl/water_trace.glsl, Hi-Z 생성용 water_hiz.vert/frag다. CMake에 모든 셰이더와 include 의존성을 등록했다. Renderer는 불투명 pass의 depth STORE, snapshot 복사, depth/colour LOAD 재개를 지원한다. snapshot과 레이아웃 전환은 dynamic rendering 밖에서만 수행하며 UI는 최종 합성 뒤 그린다.

색 snapshot은 swapchain과 같은 UNORM 형식, 깊이는 D32, 절반 해상도 반사 결과는 RGBA16F(알파에 수면 방향을 구분하는 부호 있는 거리), 반사 가림용 깊이는 D32다. 추가 RG32F Hi-Z 이미지는 전체 mip view와 mip별 단일 view/descriptor를 가진다. scene set2 binding4가 전체 피라미드이며 binding3의 UBO 마지막 trace vec4는 거리/레벨 수/반사 노멀 강도(.30)를 전달한다. CPU offset192/총208bytes의 static_assert와 GLSL std140 구성을 맞췄다.

Hi-Z는 별도 graphics layout(set0 sampler, fragment push12bytes)으로 각 mip에 전체 화면 삼각형을 그린다. mip0은 scene depth, 다음 단계는 이전 mip의 단일 view를 읽는다. 출력 mip만 color attachment로 전환하고 끝나면 sampled read로 되돌리며 다른 mip과 읽기/쓰기가 겹치지 않는다. 홀수 크기의 보수적 min/max 축소와 bounded trace는 자체 GLSL 구현이며 새 라이브러리/compute queue/storage-image 요구는 없다. RG32F sampled/color-attachment 지원은 생성 시 확인하고 선형 필터 기능은 요구하지 않는다. SSR OFF는 생성 패스를 생략한다.

각 frame slot의 fence 대기 후 UBO를 갱신/flush하고 해당 slot의 이미지만 사용한다. 크기 변경 시 idle 후 descriptor pool을 리셋하고 mip view를 포함한 리소스를 재생성한다. 일반 프레임에는 GPU 대기를 추가하지 않는다. Hi-Z가 바꾼 descriptor/push/viewport는 후속 물 패스에서 다시 바인딩한다. WorldView 종료 시 효과 객체를 face layout보다 먼저 해제한다. 기본 물은 기존 alpha 파이프라인을 유지하고 옵션은 GameSettings schema3 그대로다. 형상 bit27/탐색/보간/메모리와 한계는 docs/world.md 물 표현 절을 따른다.

## 컬럼 내부 해변 판정 (2026-09-21, 미빌드)

`generate_tile`은 최고 고체를 찾은 뒤 `assign_surface_rules`로 각 `TerrainSite::sandy_surface`를 결정한다. 수중은 기존 규칙, 해수면 근처의 육지는 해당 X/Z 블록 중심의 Groundness/Smoothness 조건을 사용한다. 후보 좌표를 한 배치로 계산하고32청크가 결과를 재사용하며 수평 이웃 데이터는 요구하지 않는다. `ground_material`과 균일 돌 빠른 경로가 같은 표면 상태/깊이를 사용한다. 3D ON/OFF와 단독 청크 생성도 같은 규칙이며 밀도/물/메시/조명 경로는 그대로다. 조건과 한계는 docs/world.md의 해변 표면 규칙 절을 따른다.

## 지역별 PV 독립 표 (2026-09-21, 미빌드)

능선프리셋최종작성에서regional_pv_influence가일부내륙고원/평야칸을W/PV독립상수로만든다. offset/factor/jaggedness모두같은C/E조건을사용하고factor의짧은C축은먼저상수연장한다. 빈칸도채워PV종속이웃의보간침투를막는다. 정상런타임/JSONschema/F3/지도는그대로이며C/E보간만으로영향이복귀한다. 현재저장도동일표수정후백업했다. 상세는docs/world.md첫절이다.


## 압축 완화 후 능선 프리셋 (2026-09-21, 미빌드)

능선 프리셋은 offset만 sharpen하고 factor는 diverse의 표를 그대로 사용한다. Jaggedness의 Smoothness 감쇠 끝을-.1에서+.4로 넓혔다. 신규 기본 squash와 능선 버튼은0.2다. 이번에는 사용자 저장 파일도 명시적 승인 범위로 factor wrapper제거/Jaggedness범위확대/squash=.2를 적용하고 백업했다. 아래 이전 factor증폭·사용자저장불변 설명보다 docs/world.md 첫 절을 우선한다.


## 공유 워핑과 진단·시각 (2026-09-21, 미빌드)

GenerationConfig schema6의 DomainWarpSettings는 g/w 공용 2D 좌표 변형을 제어한다. TerrainGenerator::terrain_inputs가 변위2개를 한 번 배치 계산하여 Groundness와 Weirdness에 공유하고 Smoothness는 원래 좌표에서 계산한다. 개별 지도도 같은 helper로 변형한다. SIMD 노드/작업별 scratch를 재사용하며 기존 스플라인·4블록 보간에 연결한다. F8와 독립 미리보기는 warp_controls를 공유하고 before_warp는 이미지 요청 전용이다. 구형 저장 파일은 OFF다.

F3는 현재 WorldView의 const 생성기를 읽어 표시할 때만100ms마다 절차 값을 계산한다. 좌상단 FPS/위치/지형값, 우상단 성능 통계로 분리하고 기존 외곽선 글꼴을 유지한다. world.rml의 좌하단 설명은 제거했다. WorldView는 소수 tick 시각을 저장하여 정상20tick/s 또는 bracket 수동±1200tick/s로 진행한다. 기존 게임 입력 허용 조건을 공유하고 release latch로 UI 복귀 후 입력을 억제한다. 물리/조명 재계산과 시각 변화를 분리하며 상세 경계·한계는 docs/world.md 첫 절을 따른다.

## 산악 능선 프리셋 (2026-09-21, 미빌드)

`ridged_terrain_splines()`는 기존 내륙 프리셋에서 산악 offset/factor에 PV wrapper를 추가한다. 기존 함수를 자식에 유지한 채 어깨 높이를 낮추고 PV 정상 접선을 세우며 압축을 증가시킨다. Jaggedness는 강 중심·해안·평야0인 산악 전용 표로 교체한다. F8 별도 버튼은 세 표와 기존 jagged 노이즈의64/32/16블록 간격·가중치를 적용한다. 시드와 나머지 노이즈·배율·스위치를 보존한다. schema/worker 경로는 그대로이며 정적 생성 결과는 캐시한다. 기존 두 프리셋이나 사용자 저장 파일은 변경하지 않는다. 상세는 docs/world.md 첫 절.

## 내륙 다양화 프리셋 (2026-09-21, 미빌드)

`world/terrain_presets.cpp`는 원본 C/E/W/PV 표를 기존 Hermite 자료구조 안에서 변형하는 프리셋 작성 코드다. 원본 재귀 함수의 affine 변형을 자식으로 가진 W 곡선으로 중심 계곡을 유지하면서 내륙 높이·압축·잔굴곡의 대비를 만든다. F8의 별도 버튼은 세 표만 복사하고 사용자의 시드/노이즈/높이 배율/스위치를 유지한다. 반복 적용은 누적되지 않고 원본 기본값은 그대로다. 새로운 노이즈·schema·worker 생성 경로는 추가하지 않는다. 설계값과 한계는 docs/world.md 첫 절.

## 근거리 스트리밍 순서 (2026-09-21, 미빌드)

`WorldStream`은 주기적 거리 기반 task heap, 미전달 `delivery_`, 미공개 `publication_`을 분리한다. 근거리8컬럼 작업 범위가 조명/메시 예약을 제한하고, halo 데이터는 가장 가까운 소비자의 우선순위를 상속한다. 이동 시 pending 순서/heap을 갱신하고 범위 밖 active 체인은 revision으로 취소한다. CPU 슬롯은 완료 시 반환한다.

`WorldView::Resident`는 GPU 전송 완료와 공개를 구분한다. 이동 중 전송을 끝낸 먼 컬럼은 숨긴 상태로 보관하고 새 선두 컬럼의 전송을 계속한다. 최종 공개는 stream의 현재 순서와 해당 컬럼의 편집/조명 준비를 모두 확인한다. 기존 visible 컬럼은 계속 표시하며 컬럼 단위 동시 공개와 청크 단위 편집을 유지한다. 계측의 pending은 실제 공개 대기, visible은 공개 완료다. 상세 상태 전이는 docs/world.md 첫 절을 참조한다.

## 다변수 지형 스플라인 확장 (2026-09-20, 미빌드)

- `world/terrain_spline.*`: C/E 희소표, 중첩 W/PV Hermite, 원본 접선, 구조 검증. JSON schema5와 내장 프리셋은 generation_config에 연결한다. `terrain_preset.inc`는 고정된 Snowcapped 자료에서 가져온 제어점이다.
- `generator.*`: 새 모드의 offset/factor/jaggedness 및 별도 periodic jagged 노이즈, half-negative/quarter-negative 밀도 조합. 구형 v1..4 수식은 별도 경로로 보존한다. terrain.cpp의 지표/블록 생성은 동일 density 함수와 위치별 보수적 범위를 사용한다.
- `ui/terrain_spline_editor.*`: 이동 가능한 표 편집창과 재귀 곡선/기울기/단면 편집. generation_editor와 비동기지도12종이 같은 초안과 평가기를 사용한다. 모든 노이즈는 개별 가중치를 선택적으로 저장할 수 있다.
- 에셋/코드 기본 프리셋 변환은 `tools/import-terrain-preset.py`, provenance는 `assets/worldgen/NOTICE.md`. 원본 곡선의 수치는 보존하지만 노이즈와 절대 높이 대응은 프로젝트 제약에 맞춘다. 자세한 수식/이전 모드와 차이는 docs/world.md 첫 절.

## 시야각·FPS·VSync 옵션 (2026-09-20, 소스 반영·미빌드)

- core/settings.*의 GameSettings는 render_distance1..64, field_of_view 수직 정수30..110(기본75), fps_limit0 또는30..500(기본0=무제한), vsync bool(기본false)을 포함한다. settings.json schema2로 저장하며 v1은 거리만 복원하고 나머지는 기본값이다. 파일 크기/정수·bool 타입/범위 검증, 임시 파일 원자 교체를 유지한다. 읽기 실패 시 로그/옵션 안내만 하고 자동 덮어쓰지 않는다.
- main의 WorldSettings가 두 RmlUi 문서의 값/활성 상태를 동기화한다. options.rcss는 공통 스타일이며 패널에 높이 제한과 세로 스크롤이 있다. 시야각/FPS 슬라이더는 즉시 적용한다. 숫자는 text 입력으로 Enter(linebreak change)/blur에서 정수 전체를 파싱하며 중간 입력은 적용/저장하지 않는다. 잘못된 값은 안내와 마지막 유효값으로 복원한다. 체크박스/슬라이더 속성 변경에 의한 재진입은 refreshing 가드로 막는다.
- VSync를 켜면 FPS 슬라이더·숫자·무제한 체크 모두 disabled이고 fps_limit 자체는 유지한다. 끄면 해당 값 또는 무제한을 복원한다. 무제한을 해제할 때 세션의 마지막 유한 제한값(최초120)을 사용한다. 유한 값은 fps_limit로 저장하지만 무제한 상태의 보조 유한 값은 별도로 저장하지 않는다.
- runtime values와 persisted를 분리하여 --render-distance로 임시 지정한 값을 다른 옵션 변경만으로 저장하지 않는다. 실제 거리 변경 때만 persisted.render_distance를 바꾼다. 시작/메뉴 왕복에 자동 저장하지 않는다. 저장 실패 시 실행값은 유지하고 다음 변경 때 재시도한다.
- FlyCamera.field_of_view는 인스턴스 값이다. 월드 진입과 옵션 변경 때 갱신하고 월드 재생성/시점 전환 때 유지한다. 렌더용 투영 행렬에서 frustum을 도출하며 3인칭 camera_distance의 near-plane 충돌 여유도 같은 값을 사용한다.
- Renderer::set_vsync는 요청만 기록하고 begin_frame에서 기존 resize 경로(wait_idle→swapchain 교체)를 사용한다. 초기 생성 전 설정하면 첫 swapchain부터 반영하고 중복 resize를 해제한다. ON은 FIFO, OFF는 IMMEDIATE→MAILBOX→FIFO 호환 대체 순서다. 로그/F3에 실제 출력 모드를 표시한다. 이미지 수 변경 시 기존 ImGui 재초기화 경로를 사용한다.
- 수동 FPS 제한은 VSync OFF이며 fps_limit>0일 때만 적용한다. 프레임 시작+1/FPS에서 이미 소비한 시간을 빼고 프레임 제출 후 SDL_DelayPrecise로 남은 시간을 기다린다. 느린 프레임은 추가 대기/밀린 프레임 따라잡기 없이 진행한다. 측정 FPS/프레임 시간에 이 대기가 포함된다. 메뉴/월드에 공통 적용하며 기존20TPS 누적 물리 코드는 변경하지 않는다. CLI frame_limit는 총 실행 프레임 수 제한으로, FPS 제한과 별개다.
- 현재 변경은 미빌드이며 실행 파일과 사용자 저장 설정을 직접 수정하지 않았다. 스왑체인 전환/타이밍/실제 옵션 배치는 실행 검증 전이다. 아래 거리 전용 schema1 설명은 이전 이력이다.

## 최신 지형/기후 확장 (2026-09-20, 미빌드)

`GenerationConfig` schema4와 `TerrainGenerator::profile/map`이 지형 조합 및 기후 미리보기를 제공한다. groundness/smoothness/weirdness→PV로 높이·압축을 만들고 3D shape를 결합한다. `TerrainSite`는 높이/local squash/최고 고체를 공유한다. temperature/precipitation은 바이옴 준비용 독립 조회이며 블록 생성에 관여하지 않는다. 기존 FastNoise2 SIMD 확장과 청크 독립/4블록 보간/컬럼 공개 규칙은 유지한다. F8 편집·비동기 지도 UI는 기존 파일을 확장했으며 새 렌더 파이프라인은 없다. 상세 수식·호환성·초기값·제한은 world.md의 최신 절을 우선한다. 빌드와 실행은 하지 않았다.

## 현재 범위

2026-09-13 후속 승인으로 프로젝트 자동 테스트와 CTest/빌드 연동, main.cpp의 smoke 전용 입력/진행 상태를 모두 제거했다. 아래 빌드 통과 이력은 당시 기록이며 현재 테스트가 있다는 뜻이 아니다. 제거 변경은 아직 빌드하지 않았다.

사용자가 승인한 C++/Vulkan 개발 도구와 RmlUi/ImGui 조합의 실행 기반이다. 첫 언덕 월드, 비동기 컬럼 공개, 노출 면별 AO/버텍스 풀링, 자유 비행, 인게임 렌더 거리 설정을 포함한다. 그리디 메싱은 후속 합의에 따라 제거했다. 여기까지 dev/release 빌드와 제한 실행을 검증했다. 이후 F11·수평 WASD와 블록 파괴·놓기도 dev/release에 빌드하고 각 CTest 5개를 통과했다. 이후 플레이어 비행/충돌의 20 TPS 처리를 소스에 추가했으며 미빌드다. 후속 승인으로 걷기 중력/점프와 Space 두 번 모드 전환도 추가했다(미빌드). LOD와 수영은 미구현이다. 세부 구현과 남은 검증 항목은 `world.md`를 참조한다.

현재 소스는 주기적 Perlin/FastNoise2, 131072² 단일 순환 월드, groundness 높이 곡선과 F8 생성 편집/재생성/JSON 기본값 저장으로 확장했다. 자연 고체는 최고 표면 잔디(수중 흙)·아래3칸 흙·나머지 돌로 생성하고 Y192의 물과 4블록 보간을 유지한다. 독립 청크와 실제 이웃 데이터 후 메싱 규칙도 유지한다. 새 변경은 빌드·실행 대기이며 위 통과 기록과 구분한다.

## 코드 위치

Esc 옵션 오버레이도 후속 승인으로 소스에 반영했다(미빌드). MenuActions.paused가 월드 옵션의 상태이며 mouse_captured와 분리한다. body.paused는 전체 화면 반투명 검은 배경과 중앙 옵션 패널을 켠다. 폴링 후 UI class/마우스 캡처를 동기화하며 resume 버튼을 누른 클릭이 블록 편집으로 전달되지 않게 한다. 카메라/선택 슬롯/편집은 !paused로 제한한다. 포커스 상실은 옵션을 열며 자동 복귀하지 않는다. 옵션이 열린 동안 ImGui는 RmlUi 뒤에 그리고 마우스/키보드를 넘기지 않아 개발 패널도 모달 UI를 가로막지 않는다. WorldView 준비/렌더는 계속한다. Tab 설정 토글은 제거했고 Esc는 종료하지 않는다. 현재 종료 입력은 quit 버튼 또는 SDL_EVENT_QUIT다.

시작 메뉴 개편도 소스 반영/미빌드다. `main.rml/main.rcss`는 중앙의 싱글플레이/옵션/게임 종료와 옵션 패널만 가진다. MenuActions의 options 상태를 body class로 반영하며 텍스처 갤러리/상태 버튼 listener를 제거했다. WorldSettings가 메뉴/HUD 양쪽의 거리 label/slider를 동기화한다. 첫 WorldView 생성에는 현재 settings.values.render_distance를 사용하고 재입장에는 기존 WorldView를 재사용한다. 옵션 변경 시 월드가 이미 있으면 radius만 갱신하고 다음 월드 prepare에서 스트리밍한다. 메뉴 진입만으로 새 월드를 생성하지 않는다.

후속 F2/F4 입력도 소스에 반영했으며 미빌드다. F2는 기존 프레임 readback 경로를 재사용하고 `assets/ui/notifications.*`의 입력을 받지 않는 알림에 저장 결과를 표시한다. F4는 후속2026-09-26승인으로 선화를 제거하고 단계별 단색 LOD 보기로 교체했다. 실제청크회색/LOD단계색 및 타일외곽선은 기존면을 triangle list로 그린다. 상세 동작은 `world.md`, 검증 대기는 `verification.md`를 따른다.

| 위치 | 책임 |
|---|---|
| `src/main.cpp` | SDL/RmlUi/ImGui 수명, 입력 배분, 메뉴 동작, 진단용 제한 실행과 캡처 |
| `src/render/renderer.*` | Vulkan 장치 선택, 스왑체인, 프레임 동기화, VMA 자원, GPU 시간/캡처 |
| `src/ui/rml_renderer.*` | RmlUi 지오메트리 및 텍스처를 Vulkan 명령으로 변환 |
| `src/third_party.cpp` | VMA, stb_image, stb_image_write의 단일 구현 단위 |
| `src/core/world_rules.hpp` | 청크 크기, 높이, 물리 틱 상수, 음수 좌표를 포함한 청크 좌표 분해 |
| `shaders/ui.*` | UI용 정점/픽셀 셰이더. glslc로 빌드 시 SPIR-V 생성 |
| `assets/ui` | 플레이어용 RML/RCSS. 내부 진단은 ImGui 코드에만 둔다 |
| `src/world/terrain.*` | 시드 지형, 16³ 청크, 경계 halo, 노출 면별 AO와 32비트 패킹 |
| `src/world/generation_config.*` | 검증된 설정, PCHIP 높이 곡선, JSON 저장/로드 |
| `src/world/generator.*`, `periodic_perlin.*` | 불변 FastNoise2 노드, X/Z periodic SIMD Perlin fBm와 배치 계산 |
| `src/ui/groundness_preview.*` | F8 범위/랜덤 요청, 비동기 원신호 이미지 계산 및 GPU 이미지 수명 |
| `src/ui/generation_editor.*` | F8 초안/그래프, 재생성·저장 요청과 JSON 표시 |
| `cmake/fastnoise` | 고정 로컬 FN/FastSIMD 소스와 프로젝트 SIMD 확장 빌드 연결 |
| `src/world/stream.*` | 제한된 작업 스레드/완료 큐, 거리 우선 스케줄링과 취소 |
| `src/world/edit.*` | 시선 선택, 세션 편집 저장, 이웃 halo와 청크별 갱신 범위 |
| `src/world/world_view.*` | 컬럼 공개, GPU 메시 수명, 업로드 예산, 깊이·컬링·그리기 |
| `src/world/camera.hpp` | 플레이어 방향과 F5 렌더 시선 분리, 비행 입력 속도 산출과 카메라 기준 좌표 변환 |
| `src/world/player.hpp` | 이동 모드/수직 속도, 발 위치, 공통 몸체 치수, AABB sweep·접지와 블록 겹침 |
| `shaders/player.vert/frag` | 흰색 직육면체의 버퍼 없는6면 렌더; F4에서도 채운 흰색 몸체 |
| `shaders/world.*` | 월드 정점 풀링과 재질 셰이더 |

## Vulkan 규칙

- Vulkan 1.4 장치에서 dynamic rendering과 synchronization2를 확인하고 명시적으로 활성화한다. 그래픽과 화면 표시를 지원하는 큐를 사용하며 discrete GPU를 우선 선택한다.
- 현재 프레임은 2개 in flight. acquire semaphore와 command pool/fence는 프레임별로, present semaphore는 스왑체인 이미지별로 보관한다. present 완료를 단순한 submit fence 완료와 혼동하지 않는다.
- VSync 기본 OFF: ON은 FIFO이며, OFF는 스왑체인을 만들 때 지원 모드를 조회하여 IMMEDIATE를 우선한다. 미지원이면 MAILBOX, 그다음 FIFO로 대체하고 동기화된 대체 모드임을 로그/개발 패널에 명시한다. 재생성 시에도 동일하게 선택한다.
- UI 지오메트리와 텍스처의 폐기는 제출 serial과 fence 완료에 따라 지연한다. 화면에 사용 중인 GPU 자원을 즉시 파괴하지 않는다.
- 창 최소화 시 그리기를 건너뛰고, 크기 변경/OUT_OF_DATE 시 idle 이후 스왑체인을 재생성한다. 이미지 개수가 바뀌면 ImGui GPU 백엔드도 재설정한다.
- 현재 스왑체인은 UI 색상을 위해 BGRA8/RGBA8 UNORM을 사용한다. HDR/선형 월드 렌더링과 최종 톤매핑은 추후 설계다.
- `Renderer::immediate`의 텍스처 업로드는 동기식이며 UI/글꼴/월드 아틀라스 초기화에 사용한다. 청크 메시 스트리밍은 `upload_buffer`로 현재 프레임에 복사를 기록하고 스테이징을 fence 완료 후 해제한다. 스트리밍 중 queue/device idle은 사용하지 않는다.
- `begin_frame`은 명령 기록만 시작한다. 월드 업로드 후 `begin_rendering`으로 깊이를 포함한 월드를 그리고 `begin_ui`로 깊이 없는 UI 패스로 전환한다. 메뉴는 `begin_rendering()`만 호출한다. 월드와 UI 파이프라인의 depth attachment 호환성을 섞지 않는다.
- 현재 UI 지오메트리 버퍼는 CPU-visible 메모리다. 월드 메시의 메모리 배치 결정으로 해석하지 않는다.
- 새 물 패스는 고체 뒤에 alpha blend로 그린다. 동일 청크 SSBO의 고체 면 뒤에 물 면을 붙이고 firstInstance로 구분한다. 물은 depth write OFF/양면이며 거리순 청크 정렬이다. 월드 아틀라스는 물을 포함한 192×32(6타일), 원본 알파를 사용하며 UI의 premultiplied 규칙과 혼동하지 않는다.

## 생성·편집의 데이터 소유권

- 청크 생성은 시드/좌표/불변 설정만으로 독립적이며 같은 컬럼의 TerrainTile은 base_height와 최고 고체 surface_y의 선택적 계산 캐시다. WorldStream은 컬럼 데이터(타일+32청크)/컬럼 내부 조명/이웃 경계 연결/청크 메시 작업을 나눈다. 경계 연결은 내부 조명이 끝난3×3 snapshot에서 필요한 경계만 플러드 필하고, 그 후 실제27이웃을 읽는32개 메시 작업을 예약한다.
- immutable shared 블록 배열을 데이터 캐시/공개 컬럼/메싱 snapshot이 공유한다. 데이터 전용 외곽 링을 유지하고, 메싱/AO에서 절차 함수로 이웃을 다시 생성하지 않는다. 취소 토큰은 data/render 상태별로 소유한다.
- WorldEdits는 메인 스레드 전용 변경 맵이다. 원래 값은 생성된 데이터에서 가져오고 선택/편집 메시에서만 변경을 합성한다. 메싱 worker는 기본 snapshot만 처리하고 공개 직전에 영향받는 청크의 변경을 적용한다. 업로드 중 이웃 편집은 해당 청크만 재메싱하도록 무효화한다.
- 준비된 메시 후보 컬럼을 별도 큐에 보관하여, 완료 큐 공간이 생길 때 전체 렌더 반경을 매번 스캔하지 않는다. 데이터 작업과 메시 작업은 이웃 대기로 worker를 점유하지 않는다.

## UI 연결

- 게임 UI는 RmlUi의 C++ API를 직접 구현한 Vulkan 렌더러를 사용한다. 기본 지오메트리, 텍스처, scissor, transform을 지원한다. 현재 메뉴에 쓰지 않는 clip mask, 레이어 합성, blur/filter/특수 decorator 셰이더 등 고급 기능은 아직 구현하지 않았다.
- RmlUi 정점 및 텍스처는 premultiplied alpha다. PNG를 읽을 때 알파를 곱하고 blending은 ONE / ONE_MINUS_SRC_ALPHA로 한다.
- RmlUi는 기본 브라우저 스타일시트가 없으므로 div/p/h1 등의 display와 body 크기를 RCSS에 명시한다.
- SDL3 공식 RmlUi 플랫폼 구현을 연결한다. 픽셀 밀도, 입력, 클립보드와 키보드 인터페이스를 재사용한다. 별도 채팅/텍스트 입력 UI는 아직 없다.
- ImGui가 마우스/키보드 입력을 소비할 때 RmlUi에 중복 전달하지 않는다. F3 진단 표시와 F8 편집 토글, Esc 옵션은 앱 단축키로 처리한다.
- `stb_image`로 PNG를 읽고, `stb_image_write`로 자체 렌더링 결과를 저장한다. 사용자 텍스처 원본은 변환하지 않는다.

## 후속 작업 시 주의

- UI만 그리는 현재 화면의 GPU 시간을 장거리 월드 성능의 근거로 제시하지 않는다.
- 컬럼 로딩/공개는 GPU 스모크에서 실행했으며 지속 이동 중 시각적 안정성 검증은 남아 있다. 청크별 편집 갱신은 빌드와 CPU 검사를 완료했다. 실제 편집 입력·테두리·스트리밍 도중 편집의 GPU 검증은 추가로 필요하다. 작업 스레드는 세션 편집 맵에 접근하지 않는다.
- 새 게임 구현은 사용자와 범위를 합의한 뒤 진행한다.

- F8 생성 편집은 런타임 graph를 변경하지 않고 초안을 보관한다. 요청은 다음 begin_frame 전에 WorldView::regenerate 또는 JSON 저장으로 처리한다. WorldView가 shared immutable generator와 unique stream을 소유한다. 재생성/순환 키/저장 우선순위의 상세 계약은 `world.md`를 따른다.

- F8 생성 편집창은 한글 이름과 parameter_help hover 툴팁으로 값의 의미/증감 효과/적용 시점을 안내한다. main.cpp의 ImGui 초기화에서 기존 assets/fonts/NotoSansKR.ttf를 17px 기본 폰트로 로드한다. ImGui 1.92 Vulkan 백엔드의 동적 glyph 업로드를 사용하며 전체 한글 글리프를 미리 굽지 않는다. JSON 키와 생성 설정 값은 번역하지 않는다.

- WorldView는 Player 실제/이전/표시 위치를 소유하고20 TPS 이동을 처리한다. 카메라는 표시 발 위치의 눈높이를 기준으로1인칭/F5 뒤·앞3인칭을 계산한다. 3인칭은6블록 기본 거리와 확장 복셀 AABB 선분 충돌을 사용한다. 로딩된 컬럼/편집만 읽어 충돌을 처리하며 player pipeline은 물 전에 렌더한다. 재생성 위치 보존·미로딩/기존 겹침·블록 놓기 계약은 world.md를 따른다.

- WorldView가 Space 비반복 입력의0.3초 두 번 판정과 짧은 탭 점프 요청을 소유하며, 키를 누르는 동안 지면에서 반복 점프한다. Player의 fly/walk 모드에 따라20 TPS 중력/지면 점프 또는 기존 비행을 선택한다. main의 기존 게임 입력 차단은 탭 기록/미소비 점프를 지우며 카메라/F5는 이동 모드와 독립이다. 상세 수치/적분/미로딩 접지 계약은 world.md를 따른다.

- F3는 main.cpp의 debug_overlay에서 창 없이 ImGui background draw list에 텍스트/글자 그림자만 그린다. MenuActions.debug는 입력 조건에서 사용하지 않으며 F8 generation_open만 마우스 캡처/게임 입력/편집창 그리기를 제어한다. 명시 --debug-ui 옵션은 F3 텍스트만 켠다.

- F3는 weight600 정적 글꼴25.5px와8방향1픽셀 검정 외곽선으로 표시한다. F8 미리보기는 독립jthread 요청/완료 슬롯과main-thread staged texture upload를 연결한다. GenerationEditor는 ImGuiLifetime 이후 생성되어 먼저 소멸하며 swapchain의 ImGui backend 재생성 전에preview descriptor를해제한다. Renderer::create_texture의선택적staged 경로는렌더패스 전현재 프레임에 업로드하고 완료 후staging을정리한다. 상세범위/샘플링/수명계약은world.md를 따른다.

- 미리보기는 F8 본창과 별도 ImGui창으로 그리고 좌우table의설정/이미지 scroll영역을분리한다. 공통 noise_controls 선언은 ui/noise_controls.hpp, 구현은generation_editor.cpp에두어양쪽창이동일한draft/제한/도움말을공유한다. 해상도는스냅샷으로복사하며확대는표시만변경한다.

## 옵션 저장과 옥타브 편집 (2026-09-14, 미빌드)

- core/settings.*는 nlohmann JSON으로 settings.json의 schema_version1/render_distance 정수1..64를 읽고 쓴다. world_core에 등록하며 파일 크기는64KiB 제한이다. 누락 시12, 잘못된 파일은 main에서 로그/옵션 안내와 기본값(또는 CLI값)으로 처리하며 자동으로 덮어쓰지 않는다.
- main은 SDL 실행 파일 디렉토리에서 설정을 읽고 명시한 --render-distance를 우선한다. 메뉴/인게임 WorldSettings는 실제 값 변경에만 공유 거리를 즉시 적용하고 저장한다. 임시 sibling 파일에 쓰고 close를 확인한 뒤 Windows MoveFileExW로 교체한다. 실패해도 실행 중 거리는 유지하고 양쪽 옵션 도움말에 실패를 표시한다. 다음 값 변경 시 재시도한다. CLI만 지정하거나 메뉴를 여는 것으로 저장하지 않는다.
- package.ps1은 원래 실행 파일/에셋/셰이더 등을 복사하는 방식이므로 기존 out/Sandbox/settings.json과 worldgen.json을 그대로 보존한다. 설정 파일과 임시 파일은 gitignore에 추가했다.
- groundness.weights와 schema2/legacy 읽기, 2D SIMD 가중치/0제외, F8 공통 개별 가중치 UI와 옥타브 선택 미리보기는 world.md의 후속 계약을 따른다. shape의 gain 루프는 유지한다.

- 후속 3D 토글: GenerationConfig.shape_enabled와 schema3를 추가했다(v1/2 읽기 유지). 비활성 또는 진폭0은 shape 노드 없이 groundness 높이에서 표면과 블록을 직접 생성한다. F8 비활성 파라미터 보존과 재생성/저장 동작은 world.md를 따른다. 미빌드다.

- 수중 지층 후속 승인: 최고 고체부터 모래3/흙2/돌, 육지 잔디/흙3/돌이다. sand 고체와 아틀라스 재질6을 추가하고7타일 크기는 C++파일목록/GLSL textureSize로 처리한다. 상세 경계/최적화/에셋은 world.md와 assets.md를 따른다(미빌드).

## 조명과 핫바/하루 주기 (2026-09-14, 미빌드)

- world/lighting.*를 world_core에 등록했다. LightWorker는 실데이터9컬럼 snapshot과 그범위의편집을 받아 별도스레드에서 두채널 전파를 계산한다. WorldView는 lighting_columns의준비상태/순차적인편집묶음/컬럼공개/빛전용업로드를조정한다. 초기solve이후에는기존조명snapshot과LightChange를LightUpdate의제거·추가큐로처리한다. 주변밝기미준비시만초기계산으로대체한다. 청크지형생성/기하메싱worker와분리하며GPU/ImGui/Rml호출은main만수행한다.
- GPU에는 별도밝기SSBO binding1과면기하binding0을같은set에묶는다. buffer descriptor pool은set당2개의storage descriptor를예약한다. relight는기하자원을공유하고빛buffer/descriptor만교체하며실패경로와지연폐기에서이중해제를피한다. CPU면/이전빛배열은shared_ptr로지연콜백과공유한다.
- Renderer::begin_rendering은depth가있는월드패스에하늘clear색을받는다. push상수의기존미사용offset.w로낮강도를전달한다. 시간만변할때조명전파나면업로드는필요없다. 플레이어도같은조명필드를읽는다.
- 중앙5칸핫바/모래·glow설치/우상단제거는main과Rml에연결했다. glow용순백타일은아틀라스조립시에코드로만들며외부이미지의픽셀은수정하지않는다. 상세조명범위/메모리/갱신한계/시각주기는world.md를따른다.

- 후속 편집 최적화: WorldView::prepare는 직접 편집한 청크를 urgent 큐로 우선 처리하고 조명/신규 업로드보다 먼저 기하를 반영한다. LightResult는 변경 컬럼들의 불변 공유 snapshot을 한 묶음으로 반환한다. 완료 시 기준 조명을 순차 전진시키고 대기 편집을 이어 처리하여 전역 revision 재시작을 없앴다. 변경 LightChunk/halo만 copy-on-write·재패킹하며 별도의 영구9컬럼 조명 캐시는 두지 않는다. 상세 경계/동시 편집/대체 경로는 world.md의 증분 조명 절을 따른다.

- 후속 AO/조명 샘플링 분리: LightChunk는 불변 고체 마스크를 밝기와 별도로 보관한다. face_light는 비고체 샘플만 평균 내고 막힌 양 측면 너머의 대각선을 배제하며 AO 비트는 읽지 않는다. 증분/대체 solve와 main 완료 판정은 고체 여부만 변한 경우도 반영한다. GPU 구조나 AO 생성/셰이더 강도는 바꾸지 않았다.


## 로딩 작업 분리와 자원 재사용 (2026-09-14, 미빌드)

- world/lighting.*의 MeshLightWorker가 CPU 면 합치기/밝기 패킹/이전 값 비교를 담당한다. PackedMesh는 불변 geometry/values/LightChunk와 ChunkKey/ticket을 전달하며 WorldView는 ticket이 일치하는 결과만 업로드한다. 초기 컬럼 공개까지 완료 조건은 유지한다. 기하 편집의 halo→mesh 단계는 기존 main에서 처리하고 밝기 패킹은 긴급 worker 작업이다.
- Renderer 프레임 슬롯은 매핑된 staging page 목록을 보관하고 fence 후 사용 오프셋을 초기화한다. WorldView의 FacePool 공유 상태가 가용 set 수를 추적하며 지연 해제 콜백과 수명을 공유한다. 목적지 geometry/light buffer 및 shader descriptor ABI는 변경하지 않았다.
- WorldStream은 row interval 차집합과 데이터 링 참조 수로 범위를 갱신한다. 새 항목만 정렬/생성하며 재사용 데이터의 메싱 예약은 worker로 넘긴다. main 데이터 조회는 try_lock, pending은 atomic이다. request가 반환한 leaving 목록으로 WorldView도 실제 빠진 컬럼만 해제한다.
- 초기 LightWorker는 기존 정확한 이웃 조명을 고정 경계값으로 사용해 미준비 영역만 스캔/전파한다. 편집 fallback은 변경 전 캐시를 재사용하지 않는다. 자세한 순서/메모리/예산과 한계는 world.md의 로딩 개선 절을 따른다.
- F3는 VMA의 가벼운 heap 통계와 증분 면 메모리 합계를 사용한다. CPU 월드 준비 수치는 이전에 빠졌던 request/언로드도 포함한다. 새 파일이나 build dependency는 추가하지 않았으며 기존 world_core의 lighting.cpp에 CPU worker를 등록했다.


## 컬럼 단계 분산 (2026-09-20, 미빌드)

- 초기 조명은 WorldStream worker pool에서 여러 컬럼을 병렬 처리한다. DataColumn은 블록과 local_light를 불변 공유하며 BuiltColumn은 대상 경계까지 확정된 ColumnLight를 포함한다. 블록 생성은 한 컬럼 작업이고 내부 generate_chunk의 데이터 독립성은 유지한다.
- LightUpdate는 편집의 제거/추가 전파와 초기 경계의 추가 전파를 공유한다. 경계 모드는 원천값 초기화/제거를 수행하지 않고, 밝기가 부족한 인접 셀을 찾은 면만 시작점으로 삼는다. 초기 경계 결과는 작업마다 대상 컬럼만 반환해 동시 작업끼리 공유 상태를 쓰지 않는다. 상세 도달 범위/halo/캐시 조건은 world.md의 최신 절을 따른다.
- WorldView는 편집 없는3×3 지역에서 worker의 초기 조명을 사용하므로, 이전 incoming 컬럼 공개를 기다린 뒤 다음 컬럼의 조명을 시작하던 직렬 구간이 사라진다. 세션 편집 영역은 기존 LightWorker의 일관된 편집 기준으로 보정한다. GPU 제출은 main과 기존 soft budget을 유지한다.
