# 그래픽 구현 컨텍스트

## 2026-09-26 원거리 LOD 렌더링

현재 원거리 표현은 [세션 LOD](lod-2026-09-26.md)를 따른다. LOD 물도 기존 WaterEffects 반사/수면 재질과75%SSR 타깃을 공유한다. 이전 LOD 제외 기록은 과거 이력이다. 실제 블록 생성과 원형 근거리 컬럼 공개 규칙은 유지한다.

## 얼음 재질 (2026-09-25, 현재)

얼음은 정적인 독립 투명/반사 재질이다. GPU 순서는 opaque/ice/water이며 water firstInstance=solid_count+ice_count, 그림자 투명 단계는 solid_count부터 얼음+물을 처리한다. 기존 그림자 깊이 재사용을 유지한다. RGB 보존 PNG 알파 제거, Complementary 광택/프레넬, 분리 SSR, 광/깊이 처리와 한계는 [ice-material-2026-09-25.md](ice-material-2026-09-25.md).

## 고체 그림자 깊이 재사용 (2026-09-24, 현재)

- 사용자 `하고 비교해서 알려줘 실시`로 그림자 두 맵을 독립적으로 다시 그리던 경로를 교체했다. layer1에 고체/플레이어 깊이와 기존 shadow colour/shaft metadata를 먼저 기록하고, D32 깊이를 layer0에 복사한 뒤 물 면만 추가한다. 물 pass는 깊이와 색 attachments 모두 LOAD한다. 그림자 맵 두 장과 수식·필터·거리·해상도·매 프레임 갱신은 유지한다.
- 두 맵은 동일 크기/투영/왜곡/깊이 규약을 사용해야 한다. 현재 prepare에서 동일 행렬과 크기를 지정하며 ensure_images에서 크기 불일치를 거부한다. 미래에 cascade나 다른 해상도를 도입하면 이 복사 전제를 함께 재설계해야 한다.
- 고체 pass도 shadow.frag를 실행해 고체의 색0과 빛줄기용 높이 alpha를 기록한다. 복사 후 물이 더 가까운 픽셀에서만 해당 색/깊이를 대체한다. 고체 전용 fragment 없는 pipeline2개를 제거했고 플레이어를 그림자에 두 번 제출하지 않는다. 물 draw의 firstInstance는 mesh.solid_count다.
- copy 직전/직후 depth attachment↔transfer 레이아웃과 쓰기/읽기를 동기화하고, 색 attachment는 고체 쓰기→물 LOAD를 동기화한다. 모든 작업은 기존 graphics queue에서 수행하며 일반 프레임에 CPU 대기나 readback을 추가하지 않는다.
- GPU 계측은 shadow_solid/새 shadow_depth_copy/새 shadow_water로 나눈다. 과거 shadow_all+shadow_solid와 비교할 때 세 구간을 합쳐야 한다. 명시적 --capture-shadow-maps는 마지막 프로필 프레임 완료 후 raw 깊이2장/색2장을 저장한다. 비교 조건과 결과는 docs/shadow-reuse-2026-09-24.md를 따른다.

## 이미지 없는 태양·달·별 (2026-09-23, 현재)

사용자 `실시`로 shaders/celestial.glsl을 추가했다. 로컬 Complementary Unbound program/gbuffers_skybasic.glsl의 기본 SUN_MOON_STYLE2는 절차적 원반이며 stars.glsl 역시 절차적이다. 원본 달 무늬는 공용 noise.png를 쓰지만 여기서는 이미지 없는 moon_noise로 대체한다. 출처는 assets/licenses/Complementary.txt에 추가했다. 아래 과거 천체 미구현 설명보다 이 절이 우선한다.

- 태양은 env.solar.xyz, 달은 반대 방향이다. 원본 dot 임계값.9975/배율400, sqrt1=x(2-x), 태양 RGB(.9,.5,.3)×25와 보름달(.38,.4,.5)×4 및 높이/동굴 감쇠를 사용한다. 지평선 아래 부분은 추가로 부드럽게 잘라낸다. 달 표면은 궤도축 기반 로컬 좌표의3주파수 value noise로 고정하므로 카메라 이동이나 X/Z 순환에 따라 미끄러지지 않는다. 현재 날짜 체계/위상 순환은 없으며 보름달 고정이다.
- 별은 원본 반구 투영·격자규모204.8·세 난수 곱 임계값.7·청백색 팔레트를 참고한다. 원본 sin 난수/네모 픽셀 대신 정수 해시, 셀 내 위치/크기 변화,3×3 이웃의 둥근 별과 미분 footprint 필터를 쓴다. 수평 이동에 흔들리지 않는 월드 방향 좌표이며 원본 syncedTime 평행이동은 사용하지 않는다. 태양 고도-.12..+.02에서 밤 가시성을 전환하고 지평선/동굴/천체 근처를 감쇠한다. 낮 또는 OFF에는 별 루프를 생략한다. 새로운 날씨·별자리·시간 체계는 없다.
- 기존 HDR 하늘을 display-like 공간으로 변환해 원반/별을 합친 뒤2.2승으로 돌린다. atmosphere의 불투명 depth가 없는 픽셀에서만 천체 하늘을 그리므로 지형 가림을 유지한다. 이후 기존 구름 합성이 천체를 가리고 물 reflectionBackground에도 동일한 함수를 연결한다. 구름 자체 색/거리 안개는 기존 일반 하늘만 사용하여 태양 원반이 구름 음영에 복사되지 않게 했다. 기존 GGX 하이라이트·대기 glare·블룸/TAA와 함께 표시되며 원본과의 픽셀 동일성은 아니다.
- 옵션 대기·빛에 태양·달(sun_moon), 별(stars) 두 토글을 추가했다. 초기/구형 파일 모두ON, 서로 독립이며 atmosphere/clouds 스위치에도 독립이다. 두 메뉴에서 자동 binding/save를 공유한다. settings schema8로 저장하며 v1..7은 기존 모든 값을 유지하고 두 새 값만 기본ON으로 읽는다. 사용자가 옵션을 변경하기 전에는 파일을 다시 쓰지 않는다. 표시OFF여도 월드 조명은 그대로다. F4에서는 두 표시값 모두OFF다.
- Environment UBO464bytes의 기존 water_origin.z/w 여유값을 표시 플래그로 사용하며 xy는 기존 물 좌표다. 추가 이미지·descriptor·draw pass는 없고 shader17개를 유지한다. celestial.glsl을 CMake include 의존성에 등록했다. 화면/설정 변경 때 기존 TAA 이력 검증이 작동한다.

이번 정지 CLI 캡처는 시작 시각06시/고정 시점이다. 밤의 별·달, 천체를 향한 시선과 시간 이동·물 반사의 실제 모양을 직접 확인했는지는 verification.md에 구분한다.

## Complementary Unbound 구름 형상 이식 (2026-09-23, 현재)

사용자 `구름 실시`로 기존 자체 셀/워핑 구름 밀도를 원본 mainClouds.glsl, unboundClouds.glsl, cloudColors.glsl 및 commonFunctions의 Noise3D 방식으로 교체했다. 아래 조명/물 절의 기존 구름 밀도 유지 설명보다 이 절이 우선한다. 출처는 assets/licenses/Complementary.txt에 추가했다.

- shaders/clouds.glsl이 밀도/구름 그림자/광선 적분을 담당하고 environment.glsl에서 포함한다. 품질1은 원본 noise.png의 B 채널 2D 두 옥타브(persistence .6), 품질2/3은 R 채널 두 슬라이스를 보간하는 Noise3D 네 옥타브(.5)다. 좌표는 옥타브마다3배, 바람은.5배이며 정규화 합의 제곱과 원본 높이8승 임계값을 사용한다. 기존 자체 구름 밀도/워핑은 제거했다.
- 원본 .00006/.00012 주파수를 8/131072 및16/131072로 맞춰 X/Z 순환을 유지한다. cloud.w는 기존 초 단위 대신 time×cloud_speed×16/131072의 정규화 위상(mod24)이고 낮은 품질은.5배다. 위상24는 각 옥타브와 그림자 /3에도 연결된다. 기존 visual_time 자체의 장시간 wrap 동작은 변경하지 않았다.
- 품질1/2/3 전체 층 두께는22/32/36블록, 적분 간격은16/32/16블록이다. 물 반사는 원본 non-DEFERRED1처럼 두께22/간격64이며 동일한 노이즈를 사용한다. 기존 높이는 층 중심이며 양0은 즉시 생략, 양50%는 원본 amount1, 100%는2다. 기존 옵션과 키/저장schema7을 보존한다.
- 원본 밀도×8의 표본 alpha, 가산 불투명도와 누적 전 alpha를 쓰는 색 혼합, alpha>.9의 조기 종료, 높이 음영·전방 산란·silver lining·낮/밤 팔레트·거리 안개를 이식했다. 수평4000블록 제한과 방향별 원거리 감쇠를 사용한다. 최대256회 루프로 모든 품질의 제한 범위를 처리한다. 품질1 첫 표본의 높이 노이즈도 유지한다.
- 구름 그림자는 기존 원본8탭 방식에 정규화된 바람 위상을 연결했다. 이는 실제 입체 밀도를 적분한 구름 그림자맵이 아니라 원본의 별도2D 근사다. 그림자OFF 때 사용하는 물 빛무늬의 기존 hash/noise/feature는 caustic_*로 이름만 분리하여 유지한다.
- 이 게임의 합성에 맞춰 출력 RGB를 alpha로 미리 곱한다. 지형/수면 깊이는 엄격한 적분 상한으로 쓰며 위에서 볼 때도 앞 지형을 투과하지 않는다. 반해상도 경계의 원해상도 재계산도 동일한 품질을 쓴다. 메타데이터는 첫 양수밀도 거리를 기록한다. 원본의 연속 skyFade 대신 sky/반사 여부의 이진 대응, 자체 깊이/좌표/합성 경로를 사용하므로 픽셀 동일 출력은 아니다. 카메라 하늘빛과 그림자맵으로 원거리 밀폐 공간 가림을 검사한다.
- 맑은 Overworld/기본 크기 기준이며 비·오로라·성운은 추가하지 않는다. 물 반사의 밤 팔레트는 원본 반사 경로대로 지상 light 팔레트를 사용한다. UBO464bytes/descriptor/이미지 수와17개 shader 구성을 유지하며 새 텍스처는 없다. 저장된 사용자 settings/worldgen은 변경하지 않는다.

검증과 정지 캡처의 한계는 verification.md의 최신 절을 따른다.

## Complementary 물 재질 전체 이식 (2026-09-23, 현재)

`싹 맞춰봐 실시` 승인으로 이전 조명 이식에서 남겨둔 물 표면 경로를 교체했다. 이 절은 아래 조명 절의 "물 SSR/굴절은 기존 시스템" 설명과 과거 물가 색/수중 굴절 전용 방침보다 우선한다. 참조는 로컬 ComplementaryUnbound의 water.glsl, reflections.glsl, reflectionBackground.glsl, refraction.glsl, ggx.glsl이다. 저작자/출처를 assets/licenses/Complementary.txt에 추가했다.

- 물결: WATER_STYLE3, speed1.10, size100%, bump1.25, small.75/medium1.70/big2.00. 원본 cloud-water RG 노멀을4크기로 합성하며 alpha 높이4회(회당2샘플) 시차, 시야각·하늘빛 진폭, 안쪽 반사 방향 보정을 사용한다. 원본처럼 반사 추적에는 노멀80%를 쓰고 하늘 반사는 전체 노멀을 쓴다. 131072 순환을 위해 가장 느린 주파수.004를524/131072로 대응하고 나머지는 정수배다. camera UBO는32블록 위상 대신 월드주기XYZ와 실제 animation seconds를 쓴다. 미해상 물결은 footprint.25..2에서 감쇠한다. 원본의 texture mip chain/미분 샘플링과는 이 부분이 다르다.
- 색: 고정 온대 물 tint(.2470588,.4627451,.8941176)에 원본 다항식 sqrt1=x(2-x), .375 배율과 RGB(1,.85,.8), noise coloring을 쓴다. 물 안개1-exp(-thickness*.075), alpha=.98*(.25+.75*fog), 동굴 물 밝기 보정2.5-sqrt2(fog)-.5*sky를 적용한다. 예전 채널별 exp 흡수는 제거했다. 반사전 alpha는 기하 Fresnel^4로1에 접근하고 반사 비율은(.85*Fresnel^3+.15)*reflectMult다. 여기의 Fresnel은1-dot(V,N)이며 기존 F0.02 식이 아니다.
- 원본 GGX area-light(GetNoHSquared radiusTan.01, roughness(.35)^4, F0.05, specular/(.125*specular+1))와 medium/small 노멀 하이라이트를 사용한다. 원본 polynomial sqrt3를 실제 sqrt로 잘못 바꾸지 않는다. 0나눗셈/음수sqrt 방어를 추가했다. 빛 무늬의 wind1.10*.035 및 광선 투과 tint의 원본 sqrt1도 맞췄다.
- 물가: 수면과 opaque 바닥의 수직 차이, 원본 noise²*1.6 임계값, 밝기/수면fraction 조건으로 거품을 만든다. 색(.9,.95,1.05), 반사 감소1-foam을 적용한다. JSON foam 키를 보존하며 UI는 "물가 거품"이다. 원본 기본 흰 거품이 이번 전체 이식의 적용 범위다.
- SSR: 원본 quality2의30단계/6회 세분화, 거리·Fresnel 시작점, Bayer64+TAA golden-ratio jitter, .8노멀, 화면 가장자리/밝기별 confidence를 이식했다. 입력은 Vulkan0..1 깊이와 이미HDR인 장면이다. 기존75% RGBA16F/RG32F+동일면 보간/둘러싸인 누락 보정/얇은 물가 원해상도 재탐색은 유지한다. 화면 밖/clamped 픽셀·6회미수렴·수면반대편 교차는 추가로 거부하며 렌더거리~1024에 제한한다. 원본 packed previous RGB 대신 현재 opaque snapshot을 쓰므로 저장·시간차/경계 검사 차이가 있다. SSR의 화면 밖 정보 한계는 남는다.
- Hi-Z 트레이서/이미지/밉/descriptor/패스는 제거했다. world.vert가물SSR와수면을그리며 공용 fullscreen triangle은 fullscreen.vert로 명명한다. WaterEffects는프레임당 1set/4sampler/1UBO(224bytes), opaque colour/depth,75%reflection/metadata/depth만 보유한다. binding0색/1깊이/2반사/3UBO/5metadata이고4는빈칸이다. 예전 Hi-Z 비용약21MiB/프레임@1920×1080 및 프레임별mip draw가없다. 실제 전체GPU 개선량은 측정하지 않았다.
- 물 diffuse/반사/alpha 혼합은 원본 gamma-like 공간에서 처리한 뒤2.2승으로HDR 출력한다. 불투명 snapshot을 반드시 확보하며 물 pass 하드웨어 blend는OFF다. 앞물은 같은 opaque snapshot을기반으로뒤물출력을덮는다. 아래면/여러겹 투명체를독립재귀합성하지않는기존단일수면한계가있다.
- 수중에서도 원본 reflectMult=.5, 방향음영비활성, 약한GGX와 수면 반사를 사용한다. 별도 수중 왜곡 전용 함수는 제거했다. enabled OFF면 수중 표면은 배경을 그대로 전달한다. 수중 안개/갓레이는 기존 원본 조명 경로를 이어 쓴다.
- 굴절: atmosphere.frag phase1/3에서 수면·반사까지 합성된 색 전체를원본 composite1처럼샘플한다. noise RB,강도2*.02*fov/(3+distance),view-depth-gap을쓴다. 가장가까운물depth와opaque보존depth가다른픽셀만대상이며후보및최종위치의깊이차도검사한다. 이로써전경블록과물밖화면을끌어오지않는다. env.view.w를굴절활성으로재사용(UBO464bytes유지)하며구름/광선/수중안개가모두OFF여도굴절용물depth+합성은유지한다. 위쪽굴절은depth+waves,수중굴절은waves가필요하며기존옵션의존성을유지한다.

저장schema7/사용자 settings.json·worldgen.json은 변경하지 않는다. 반사환경의 구름 밀도 형상, 날씨/바이옴의 고정입력, 태양·달 형상 부재는 이전 조명 이식과 같다. 전체 원본 Minecraft/Iris 출력과 픽셀 일치를 주장하지 않는다. 수중 이동/시간 변화/낮 태양 하이라이트/물가 아주 근접한 시각 확인은 수행 여부를 verification.md에서 구분한다.

## Complementary Unbound 조명 경로 (2026-09-23, 현재)

사용자가 로컬 ref/ComplementaryUnbound의 조명 수식·기본값·순서를 이식하는 제안에 `실시`로 승인했다. 아래 기존 2026-09-22 설명은 이전 구현 이력이며 현재 경로는 이 절을 우선한다. 출처는 assets/licenses/Complementary.txt. 사용자 settings.json/worldgen.json은 직접 변경하거나 패키징으로 덮어쓰지 않는다.

### 원본 대응

- `shaders/complementary.glsl`: clear Overworld/SHADER_STYLE4의 lightAndAmbientColors, skyColors/GetSky, mainLighting의 기본 고체 경로, shadowSampling, volumetricLight를 Vulkan 좌표와 이 게임의 입력으로 옮겼다. 고정 screenBrightness=.5, 하늘 입력(.47,.65,1)×태양 고도 밝기, 물 색(.2470588,.4627451,.8941176), 수중 fog 입력(.01960784,.01960784,.2), 비/번개/특수 바이옴/포션=0이다. 호스트 sunAngle의 의미는 Iris 공식 문서(https://shaders.properties/current/reference/uniforms/world/) 및 CelestialUniforms.java(https://github.com/IrisShaders/Iris/blob/1.21.1/common/src/main/java/net/irisshaders/iris/uniforms/CelestialUniforms.java)와 대조했다.
- 게임06..20시는 sunAngle0..0.5, 나머지는0.5..1에 연결한다. 원본 common.glsl의 sunAngle→timeAngle 조각 함수와 GetSunVector의 곡선, sunPathRotation=-40°, noon/night/sunVisibility/shadowTime 곡선을 적용한다. 실제 tick과 `[ ]` 입력은 유지한다. 이전 표시용 낮/밤 최대밝기 plateau는 새 방향광 수식으로 대체하며 world.daylight() 자체는 기존 물의 보조 입력 등에 남아 있다.
- 지형/플레이어는 원본 방향별 명암, 색을 가진 주변광/직접광, sky lightmap의 smoothstep, 블록광의8승+선형→2.25승 곡선, 최소 동굴광, AO 재매핑 및 제곱합/제곱근 혼합을 쓴다. 기존 sky/block0..15 전파·면 light4bytes·AO2bits 계산은 그대로다. 최종 표시 곱셈만 교체한다. 발광 테스트 블록은 기존 흰색HDR8이다. 텍스처/조명을 곱한 원본 표시공간→2.2승 변환에 맞춘다.
- 그림자는 **매 렌더 프레임** 두 깊이를 갱신한다. 동일한 직교 투영에서 [0]은 고체+물+플레이어, [1]은 불투명 고체+플레이어다. 근/원거리 cascade가 아니다. `1-25.6/distance`의 방사형 XY 왜곡과 Z압축.2를 vertex/receiver에 동일 적용한다. 원본 거리 기반 normal bias와 어두운 모서리의 블록 중심 보정,2쌍의 ±회전 PCF/프레임 지터/TAA를 연결한다. 깊이 비교는 하드웨어 선형 comparison sampler. 바깥은 sky^8로 연결한다. CPU 컬럼→청크 그림자 프러스텀 컬링을 유지한다.
- 그림자 기본 거리192, 품질1/2/3=1024²/2048²/4096²다. D32×2 + RGBA8×2(표면 투과색/빛줄기 투과색+높이)를 공유한다. 기본64MiB, 품질3=256MiB. 색맵은 원본처럼8bit에 높이를 저장한다. 물 그림자의 caustics/수중 광선 투과는 원본 noise.png와cloud-water.png를 사용한다. assets/textures/effects에 출처를 명시해 복사했다. 좌표 주기를 월드131072에 맞추기 위해 노이즈 주파수는 가장 가까운 정수 반복수로 대응한다.
- Scene Aware는1×1 GPU 이력에 보관한다. 약15Hz에 그림자 중앙25표본의 평균높이6블록 조건으로+1/255 또는-2/255, 화면 위쪽5점 중4점이 하늘이면-3/255다. 데이터가 없는 경우0나눗셈을 막는다. 이전 프레임 값을 광선에 적용하며 CPU readback은 없다.
- 갓레이는 원본 균일 view-depth raymarch, near1, 최대거리mix(max(renderDistance,96)×.55,80,sceneFactor), 방향·고도·시간·거리 가중치와 원본 색을 사용한다. 품질1/2/3은 평상시6/10/15, 강한 장면12/20/30, TAA OFF는2배다. 원본처럼 가림은 depth texelFetch 비교×65536과 불투명 depth/물 투과색으로 판정한다. 최종 합성 단계에서 원본 화면 해상도로 계산하므로 구름 반해상도 경계 fallback에 빛줄기가 빠지지 않는다. 카메라 하늘빛으로 전체 갓레이를 꺼 버리지 않는다.
- Opaque 깊이를 별도 D32에 보존하고 물 깊이와 함께 사용한다. 물 뒤 ray sample에 투과 감쇠를 적용하며 수중의 색×(.80,.87,.97)×.85와 빛줄기×((.80,.87,.97)×.71)^2, 물 안개1-exp(-(distance/48)^2)를 적용한다. 물 위 SSR/굴절/7/8 형상은 기존 시스템이다. 그림자ON일 때 기존 Voronoi caustic 가산을 중복 적용하지 않는다. 그림자OFF에서는 기존 물 접촉 무늬가 fallback이다.
- 하늘은 원본 상/중/하 팔레트, 일몰·지평선·glare·동굴 가림·banding dither다. 대기안개는 원본 거리/고도 식(고도기준192.1), 동굴안개→대기안개→border fog 순서다. 구름은 기존 순환 밀도/형상/높이·양 조절을 유지하면서 원본 cloudColors, 높이/전방 산란/거리안개 수식을 사용한다. 구름 그림자는 원본 noise8탭이다. 50% 양을 기준값으로 두고0%에서는 완전히 끈다.
- 블룸은 threshold 없이 원본HDR을8단계 mip으로 축소하고 lod2..8을 각각7×7 이항 커널로 흐린다. 원본의7개 가중치/원경 감쇠/.14합과12% mix를 적용한다. 동굴/밤/수중 bloomFog 배율은 mip0을 만들 때만 원본색에 곱해 원본의 pre-multiply 후 base만 나누는 순서와 대응한다. 원본 packed8bit tile 대신 별도RGBA16F 이미지를 사용한다. 그림자/블룸의 저장 및 mip 재표본화 차이로 비트 동일 출력은 아니다.
- 톤은 원본 수정 Lottes: exposure1, contrast1.05, hdrMax8, mid.25, dark lift, white path1, dark desaturation.25, saturation1/vibrance1이다. 이후 전체 화면 TAA를 수행한다. 원본8프레임 jitter×.125, Catmull-Rom(c=.7),8이웃 AABB clipping, smoothing3(.35/.2/.7) motion blend를 사용한다. 추가로 거리 이력 불일치, 화면밖/NaN, 설정/카메라 큰 이동/수면 전환/태양 급변/편집·메시 교체/재생성/리사이즈를 검증한다. UI는 TAA 뒤에 그린다. F4는 jitter/효과/TAA blend/톤 곡선을 모두 우회한다.

### 리소스·설정·범위

- `SceneEffects::Uniform`464bytes: 기존352bytes + previous_camera64 + temporal/cycle/view48. flags256/hydro320/previous352/temporal416/view448를static_assert한다. Environment set은 world/player/shadow2, water3, fullscreen1이다. binding0UBO,1/2comparison depth,3표면 그림자색,4이전 scene-factor,5빛줄기 그림자색,6raw depth,7/8reference textures다. post image layout은8bindings다.
- Frame별 scene/composite, 반해상도 cloud volume/metadata, opaque-depth 복사, toned/historyRGBA16F(이력alpha=거리),1×1scene-factor,8mips/8bloom을 가진다. shadow depth/color4개는 공유하고 큐 barrier로 직렬화한다. 프레임당23sets를 사용하며 pool은24sets/196samplers/1UBO 여유로 예약한다. history read/write slot이 같아지는 메뉴 복귀는 다른 slot을 택하고 해당 프레임 blend를 무효화한다. 화면 크기/그림자 품질 재할당 때만idle하며 매 프레임readback/idle은 없다.
- `settings.json` schema7: graphics.taa=true 추가. v1..6은 TAA ON, 구형 기본 shadow_distance256+quality2 조합만192로 메모리 이행한다. 다른 사용자 범위/효과 값과worldgen은 보존한다. 시작·인게임 옵션의 대기·빛에 TAA 토글을 추가했다. 그림자 설명에서20Hz/cascade를 제거했다.
- 이식 범위는 합의된 조명 경로다. Minecraft의 전체 재질/식생/바이옴별 색/날씨/포션/차원/SSAO/FXAA/별·천체 형상은 새로 구현하지 않는다. 기본 입력 대응, 기존 구름 모양과 물 SSR, 높이/시간/주기적 월드 차이 때문에 원본 게임 화면과 픽셀 동일함을 보장하지 않는다. 동작 검증과 사람의 시각 비교는 docs/verification.md에 구분한다.

2026-09-22 사용자 `방금 한 위 작업 롤백실시`에 따라 직전 갓레이 보강4종을 전부 되돌렸다. 아래는 복구된 기존 갓레이 방식이며, 이전 옵션 글자 수정과 그림자맵20Hz 갱신은 유지한다. 최신 사용자 규칙에 따라 롤백 후 Release 빌드·패키징을 완료했다. 검증 결과는 verification.md를 따른다.

## 범위와 사용자 설정

- Esc는 게임으로 돌아가기/옵션/시작 화면으로의 작은 일시정지 메뉴를 연다. 옵션 버튼은 화면92%×88%(최대폭1200dp)의 설정창을 열며 시작 메뉴에서도 같은 구성을 쓴다. 왼쪽 화면/물/구름/그림자/대기·빛 분류, 오른쪽 스크롤, 고정 제목/뒤로가기로 나눈다. 전체 옵션의 Esc는 일시정지 메뉴로, 다음 Esc는 게임으로 돌아간다. 시작 메뉴 옵션에서는 시작 메뉴로 돌아간다. RmlUi 양쪽 설정 ID/값/의존 관계는 유지한다.
- `-`/`=`는 구름 양을 한 번에1% 조절하고 약0.35초 뒤 초당20% 반복한다. 양쪽 동시 입력은 중립이다. 게임 캡처 중에만 동작하고 Esc/F8/입력 포커스 복귀 때 키를 놓아야 다시 작동한다. UI의 같은 값을 갱신하고 현재 양을 알린다. 키 해제/메뉴 진입/정상 종료 시 기존 원자적 설정 저장을 한 번 수행하여 매 프레임 디스크 쓰기를 피한다. CLI 렌더 거리 우선값을 함께 저장하지 않는다. 구름 스위치를 자동으로 켜지는 않는다.
- `WaterSettings`: enabled/depth/waves/ssr/refraction/foam/caustics/underwater_fog 모두 최초ON. foam 키는 호환성을 위해 유지하되 UI는 물가 색 강조로 변경했다. 수면 위 굴절/물가 색은 depth가 필요하며, 굴절은 waves도 필요하다. 지형 면의 caustics와 수중 안개는 depth와 독립이고 enabled를 따른다. 수중 굴절은 enabled/waves/refraction을 따른다. 전체OFF는 수면 위에서 기본 반투명 물, 수중에서는 왜곡/안개 없는 시야이며7/8 형상은 유지한다. 비활성 항목의 선택값을 보존한다.
- `GraphicsSettings`: clouds/cloud_shadows/shadows/atmosphere/fog/shafts/bloom 최초ON. cloud_shadows는 clouds, shafts는 shadows가 필요하다. 하늘 근사OFF도 구름을 독립 표시한다. 효과OFF 시 관련 숫자 입력을 비활성화하고 값을 보존한다.
- 기본값과 범위: 원거리 그림자 반경256(32..512), 근거리 투영면 전체폭64 고정. 품질1은근거리2048²/원거리1024², 품질2(기본)는4096²/2048², 품질3은4096²/4096²다. 따라서 기본 원거리 전체폭은512다. 구름/빛줄기 품질2(1..3), 구름 중심 높이640(320..1024), 양50%(0..100), 이동 속도4블록/초(0..20), 블룸12%(1..40). 숫자는 슬라이더 또는 Enter/포커스 이탈로 적용한다.
- `settings.json` schema6. 기존 shadow_distance 키는 원거리 반경이다. v1..5는 승인된 새 그림자 프리셋(반경256/품질2)으로 메모리에서 이행하며 나머지 사용자 옵션을 보존한다. v6은 사용자가 조정한 반경/품질을 복원한다. 파일은 시작 시 쓰지 않고 옵션 변경 때 원자적으로 저장한다. v5부터의 water.underwater_fog와 v1..4의 해당 항목ON 이행은 유지한다. 렌더 거리 CLI 우선값을 다른 옵션 변경으로 저장하지 않는다.
- 양쪽 옵션의 체크박스 설명을 `.option-text` span으로 감싼다. RmlUi flex의 직접 텍스트 자식이 표시되지 않는 문제를 피하고, 남은 너비를 받아 긴 설명은 줄바꿈한다. 기존 label/for와 입력 ID·상태 연동을 유지한다. 사용자 settings/worldgen은 패키징으로 덮어쓰지 않으며 VSync 기본OFF를 유지한다.

## 렌더 순서와 리소스

`SceneEffects`가 월드 표시를 담당한다. 메싱은 고체 면의 실제 이웃 물 접촉을 bit28로 추가 기록하며 스트리밍/블록/밝기 전파와 기존 비트는 유지한다.

1. frame fence 대기 뒤 환경 UBO352bytes를 갱신한다. 그림자ON이고50ms 갱신 기한 또는 강제갱신 조건에 도달했을 때 공개된 고체 청크와 플레이어를 두 직교 shadow cascade에 그린다. 갱신 사이에는 마지막 그림자맵과 생성 당시의 행렬을 재사용한다. 카메라 프러스텀이 아니라 각 shadow volume으로 컬럼 전체를 먼저 거르고 통과한 컬럼의 청크를 다시 컬링한다.
2. RGBA16F scene A에 불투명 월드/플레이어를 그린다. PCF 그림자 가시성을 직접광에만 적용하며 시간축 누적이나 MRT history 출력은 없다. atlas linear 변환, sky/block/AO 결합 및 발광 블록HDR8은 유지한다.
3. D32를 sampled read로 전환하고 scene A→B에 하늘과 지형 안개만 합성한다. 깊이를 attachment로 복귀시키고 LOAD로 물 색 패스를 시작한다. 물의 opaque snapshot/Hi-Z/굴절에는 구름이나 수면 깊이가 섞이지 않는다.
4. B에 물 SSR/굴절/표면 색을 그린다. 이후 전경 volume 또는 수중 안개를 사용할 때 보이는 물 메시를 depth-only로 한 번 더 그려 D32에 가장 가까운 물 표면을 남긴다. 기본 물 표현에서도 적용하며 수면7/8은 같은 world.vert를 재사용한다. 고체·물 뒤 구름은 이 깊이에서 잘린다.
5. 구름 또는 빛줄기ON일 때 가로·세로 절반 크기의 volume RGBA16F와 metadata RG32F(고체 또는 물까지 거리/첫 구름 거리)를 계산한다. B→A의 별도 합성은 깊이 경계4탭을 검사하며 부족한 경계는8표본 구름만 다시 구한다. 이때 하늘/안개를 다시 씌우지 않는다. 수면 위에서 volume이 모두OFF면 물 깊이와 후속 합성을 건너뛰고 B를 최종색으로 쓴다.
6. 최종색에서 블룸ON일 때 절반 크기부터5단계 별도RGBA16F 이미지를 축소·필터링한다. 첫 단계soft threshold, 이후9탭 필터다. 톤 pass는3개의 크기를 혼합하고 고정 노출의 filmic shoulder와 linear→sRGB를 기존UNORM swapchain으로 보낸다. 디스플레이 HDR 출력 모드는 아니다.
7. UI/F3/F8/핫바/알림은 톤 매핑 뒤에 그린다. F2는 이 최종 화면을 저장한다.

수중에서는3번에서 불투명 깊이에 따른 구름/빛줄기를 먼저 만들어 하늘·안개와 B에 합성한다(phase2). 그 B를 물 snapshot으로 사용해 바깥 구름도 굴절된다. 수중 안개ON이면 물 색 이후 depth-only 패스가 가장 가까운 물 출구/지형 깊이를 남기고, phase3에서 B→A에 해당 물 통과 거리의 흡수/산란색을 한 번 적용한다. 안개OFF는 B를 최종색으로 사용한다. 수면 위는 phase0→물→phase1 순서를 유지한다.

- F4는 그림자/구름/안개/빛줄기/블룸을 사용하지 않고 톤 곡선도 우회해 LOD 단계 색을 유지한다. scene A→B→swapchain 전달은 남는다.
- 환경 descriptor는 world/player set2, water set3, fullscreen set1이다. binding0 UBO352bytes, binding1 근거리sampler2D, binding2 원거리sampler2D다. world/water set0 atlas와 set1 면/밝기 SSBO 및96byte vertex push는 유지한다. 물 set2의6개 binding과 UBO224bytes, SSR75% RGBA16F/RG32F는 변경하지 않았다.
- SceneEffects는 진행 중2프레임 각각 scene A/B, volume/metadata,5개 bloom과 uniform/descriptor를 소유한다. D32 근거리/원거리 그림자맵은 모든 프레임이 공유하는 한 쌍이다. 화면 크기/품질 변경 시 idle 후 재생성하고 캐시를 무효화한다. 그래픽 큐의 ALL_GRAPHICS barrier로 이전 프레임의 읽기→새 깊이 쓰기→후속 읽기를 연결하며 일반 그림자 갱신에 CPU/GPU idle 대기는 추가하지 않는다.
- pre/post 합성 descriptor set은 별개다. 프레임당 environment1+pre1+post1+tone1+bloom5=9sets, sampler34개, UBO352bytes(flags256/limits304/hydro320/water_origin336)다. 지형·플레이어 색 출력은 단일 attachment이며 일반 물도 같은 world.frag를 재사용한다. 시간축용 RG32F history·추가 descriptor·행렬·world_water 변형은 제거했다. 구름 volume의 RG32F metadata는 별개 기능으로 유지한다.
- HDR/depth 포맷의 attachment/sampled/linear/blend/transfer 요구 기능은 사용 경로에 맞춰 확인한다. snapshot은 같은HDR형식끼리 복사한다. 모든 barrier는 dynamic rendering 밖에서 수행한다. 종료 시 water→scene→월드 pipeline/layout→face layout 순서로 해제한다. 물 fragment의 discard가 요구하는 shaderDemoteToHelperInvocation 기능을GPU 선택/생성에서 확인·활성화한다. swapchain 최초 layout 전환의 srcStage는 acquire semaphore wait와 같은COLOR_ATTACHMENT_OUTPUT으로 지정한다.
- Renderer::finish_uploads가 첫 shadow 이전에 업로드 시간측정 끝을 기록한다. HDR와 최종 톤 pass 때문에 query3을 중복 쓰지 않는다. 기존 F3 전체GPU 시간은 새 효과를 포함하며 청크/삼각형 카운터는 월드·물/SSR/수면 깊이 draw 기준(그림자 중복 draw/전체화면 삼각형 제외)이다.

## 효과 방식과 한계

- 낮06..20은 태양 방향, 밤은 그 반대편의 약한 빛 방향을 쓴다. 지평선에서 직접광을 감쇠한다. 기존05..08/18..21 전환 및08..18/21..05 밝기 기준,20TPS/하루28800틱/시작06시/메뉴에서 시각 정지는 유지한다. 태양·달 원판/형상은 없다.
- 그림자 투영면 근거리 반경32/원거리 기본반경256. double 행렬과512블록 기준점·재설정 시 격자위상 보존을 유지하고 각 구간 자체 해상도로 snap한다. 근거리의정규화경계.65.. .95에서 원거리와smoothstep 혼합하고 원거리.88..1에서 소멸한다. 수신면 평면을 실제4×4 tent PCF texel에 투영하고 비교깊이를0..1로 한정한다. normal offset=texel폭×.08, softness=max(2e-7, texel폭×.06×depth행길이), receiver bias=softness2배 및 caster constant1.25/slope0을 유지한다.
- steady_clock 기준50ms마다 새 그림자맵을 만들며 새 결과는 해당 프레임부터 바로 사용한다. 태양 방향과 표시 조명은 매 프레임 계산한다. 갱신 사이에는 이전 깊이를 새 빛 행렬과 섞지 않고, 캡처 당시의 double 행렬에 순환 월드의 카메라 이동량만 보정한다. FPS가20보다 낮으면 표시 프레임당 최대1회이며 밀린 갱신을 반복하지 않는다. 물리틱 루프를 변경하지 않는다.
- 최초/화면크기·품질·반경 변경/그림자 재활성화/F4 복귀/마지막 캡처에서 카메라8블록 이상 이동/블록편집·공개된 메시 교체/재생성은 즉시 갱신한다. 신규 컬럼 공개·언로드와 일반 플레이어 이동·빠른 시각 조절은 다음50ms 갱신에서 반영한다. 따라서 강제 갱신 상황에는20Hz를 넘을 수 있다. 장면 색·그림자 가시성의 시간 보간/누적은 없다.
- 공유 그림자D32는 기본80MiB, 품질1=20MiB, 품질3=128MiB다. 이전 시간축 소스 대비 기본80MiB와1920×1080에서약31.64MiB의 history를 절약한다. 갱신 프레임의 고해상도 그림자 비용과 회전하는 격자의 표본 변화는 남으므로20Hz만으로 떨림이 완전히 없어진다고 보장하지 않는다. 실제 시각적 안정성과 GPU 비용은 별도 관찰 대상이다.

- 구름은 X/Z131072 주기를 유지하는 smooth cellular 덩어리와 value noise를 조합한다. 셀마다 위치·반지름이 다른 compact lobe를3×3×3 이웃에서 더하여 Voronoi 경계선 없이 연결하고,2개의 저주파 신호로XYZ를 워핑한다. value noise는5차 보간, 옥타브는 정수 shear와 서로 다른 offset을 써서 축 정렬을 줄인다. 모든 축의 hash가 주기적이므로 Y를 섞는 shear에서도 X/Z 순환은 유지된다.
- 전체 구름 범위는 중심-144..+256, 지역 하부는중심-144..-80, 두께는192..336블록이다. 하부와 두께는X/Z 신호에서만 정하고Y는 envelope/3D 덩어리에 사용한다. 하부 진입/상부 둥근 감쇠와 덩어리 안 밀도, 두 방향 광선 표본(40/120블록)·상하 환경광으로 부피감을 표현한다. 셀 덩어리는ray footprint64..160에서 평균값으로, 미세 침식은24..80에서0으로 감쇠하여 미해상 노이즈 비용/아티팩트를 제한한다.
- 구름 양0은 계산을 끈다. 양100은 날씨 분포의 임계값을 최소로 하지만 모든 고도/방향의 완전 불투명을 뜻하지 않는다. 최대4096블록 ray 범위, 품질16/24/40 march, 투과율.02 조기종료, 반사 하늘8표본을 유지한다. 구름 그림자는중심-48/+80의 밀도 표본으로 근사한다. 속도 변경 시 바람 위상을 재계산하여 위치가 바뀔 수 있다. 기본 설정 위치의 화면만 확인했으며 상부/이동/노이즈 격자 완화 정도는 추가 수동 확인이 필요하다.
- 대기는 방향/고도/시각에 따른 하늘색·노을·넓은 광원 glow의 근사다. 물리 Rayleigh/Mie 다중 산란 적분은 아니다. 거리/높이 안개는 카메라 하늘빛 가시성으로 억제한다. 광선별 동굴 입구 전이를 정확히 추적하지는 않는다.
- 빛줄기는 min(원거리그림자반경,256) 범위에8/12/20 균일 shadow 표본을 적분한다. 기존 terrain_shadow의 표면용4×4 PCF를 법선0으로 조회한다. 카메라 하늘빛이0.01 이하이면 생략하고, 누적 밝기에 카메라 하늘빛·광원세기·방향함수·경로길이·0.00065를 곱한다. 구름 그림자는 구간 중앙에서1회 구한다. 반해상도/경계 보간을 유지하며 볼륨 자체는 시간축 누적하지 않는다.
- 직전 근거리 집중 표본/공기 전용2×2 비교/카메라 감쇠 제거/경계 빛줄기 재계산/지수 감쇠 강화는 사용자 롤백 요청으로 모두 제거했다. 따라서 가까운 얇은 가림을 놓치거나 그늘 안에서 전체 효과가 약해지고 윤곽 fallback에서 빛줄기가 빠질 수 있는 기존 한계도 복구된 상태다.
- 2026-09-21 후속 `실시`로 물 SSR 색/성공도·metadata·가림 깊이를 가로·세로75%로 변경했다. 각 축은ceil(3×화면크기/4),1920×1080에서는1440×810이다. 반사 viewport/scissor와 UBO sizes.zw도 같은 reflection_extent_를 사용한다. 전체 해상도 물 표면의 노멀 footprint는 sizes.xy/sizes.zw로 보정하여 SSR 표본과 맞추고 홀수 크기의 X/Y 반올림 차이를 반영한다. 장면 색/깊이 snapshot 및 Hi-Z는 원본 해상도, 구름/빛줄기는 절반, 블룸은 기존5단계다. 설정 키나 품질 옵션은 추가하지 않았다. 이번 승인 빌드에 포함했다.
- SSR은 지형 hit 색/성공도와 수면·hit 거리를 분리한다. Hi-Z 범위에 원근 깊이의 보수적 두께를 주고, 최종 픽셀은 깊이4이웃 중 연속된 면의 두 변을 복원해 광선과 교차시킨다. 복셀/플레이어의 축 정렬 면과95% 이상 일치할 때만 축으로 보정하여 먼D32 정밀도 흔들림을 줄인다. hit의 실제 투영이 해당 픽셀±.65 안에 있어야 하며 이웃 없는 얇은 실루엣은 제한된 두께 검사로 대체한다. 깊이점 하나와 정확히 일치해야 했던 조건을 제거했다.
- 반사 업샘플은 수면 평면/방향/불투명 가림을 검사한다. 거절/예산소진은3개 이상의 거리 일치 이웃으로 복구하고, 빈 탐색(-1)도 같은 물 평면의 지형 hit가 좌/우/상/하를 둘러싸며4개 이상이면 고립된 구멍으로 복원한다. 이웃 선택은 가장 가까운 hit를 기준으로 하여 반복 순서에 따른 지형 선택 편향을 줄인다. 실제 하늘 경계는 둘러싸임 조건에서 제외되며 화면 밖 지형은 여전히 만들 수 없다.75% coverage가 부족한 물 경계만 원본 해상도로 다시 추적한다. 추가 RG32F metadata는 같은75% 크기다. 추적128회 예산은 유지한다. 완전한 disocclusion 복원이나 시간 누적은 구현하지 않았다.
- SSR의30% 노멀 변형은 비스듬한 시선일수록 최대70% 더 줄인다. 반사가 실패하면 실제 수면 지점에서 같은 구름층과 하늘 함수를8샘플로 반사한다. 화면 밖 지형은SSR에 포함할 수 없다. Hi-Z 최대128회/렌더 거리 최대1024블록과 기존 접점 평면 보간은 유지한다.
- 수중 판정은 플레이어가 아니라 실제 렌더 카메라의 loaded_block과 윗셀을 쓴다. 위가 공기이면 y+.875, 아니면y+1 아래를 물로 판정하여3인칭/순환 경계/기본 수면과 맞춘다. 미공개 데이터는 수중으로 간주하지 않는다.
- 수중 물 fragment는 나가는 경계만 표시하며 수면 자체 tint·흡수·SSR/Fresnel·물가 색 경로를 건너뛴다. 지형 빛무늬는 불투명 패스에서, 카메라의 물 통과 거리는 후속 phase3에서 처리한다. 바깥 장면을 무색으로 투과하고 refraction/waves ON이면 작은 UV 변위를 적용한다. 같은 출구 평면의 mask와 불투명 깊이를 검사하므로 수중 지형을 굴절 영상으로 끌어오지 않는다. 이 경우 Hi-Z/반사 광선은 생략하지만 굴절용75% mask와 장면 복사는 필요하다. 반사/mask shader의 early_fragment_tests를 제거하여 버린 안쪽/가려진 면이 깊이를 선점하지 않게 했다. 중첩된 여러 수역을 물리적으로 추적하는 다중 굴절은 범위 밖이다.
- 수면 위의 구름 합성은 카메라→수면 전경 가림을 처리한다. SSR 지형 hit까지의 모든 구름 감쇠는 별도로 적분하지 않는다.
- 굴절은 물결 노멀·물 두께로UV를 이동하고 후보의 수면 mask/평면/불투명 깊이를 검사한다. SSR을 꺼도 깊이·잔물결·굴절ON이면 75% 해상도 물 mask를 그리며 반사광선/Hi-Z는 계산하지 않는다. 경계에서는 원래UV로 복귀한다.
- 물가 색은 기존 수직 간격0.12..0.65와 완만한 파형 마스크에 밝은 회색 대신 물 텍스처/깊은 청록색의55% 밝기를 혼합한다. 기존 foam 키를 재사용한다. 빛 무늬는 물 셰이더의 교차 사인파/재투영 경로를 제거하고 고체 world.frag 조명의 직사광에 적용한다. 따라서 물 밖의 굴절/SSR snapshot과 수중에서 같은 수신면/좌표가 보인다. 플레이어에는 별도 빛무늬를 추가하지 않았다.
- bit28은 is_solid(block) && halo의 노출면 바깥 셀이 water일 때만 기록한다. 추가 청크 요청/버퍼/면 크기 증가 없이 기존 halo와4byte 면 데이터를 사용한다. 발광 블록은 기존 자기 발광을 유지한다. 자연 수면Y191.875 아래의 젖은 면만 빛무늬 후보이며, 공기 동굴/육지는 제외한다. 깊이는 고정 수면 기준 근사이므로 편집으로 생긴 낮은 별도 물 출구의 깊이를 별도로 추적하지 않는다.
- 빛무늬는3×3셀의 움직이는 feature point에서 F2-F1 능선을 구하고 저주파2신호로 워핑한다. CPU의4096주기 카메라 원점과 셀2048주기로X/Z월드131072 순환이 이어진다. 표본 footprint .15.. .7에서 감쇠하고 해상되지 않으면9셀 계산을 생략한다. 깊이 exp(-depth×.055)/최대48블록, 기존 수신면 하늘빛·직사광·지형/구름 그림자를 따르며 야간에는 끈다. 원거리 추가 텍스처/전역 광자 추적은 없다.
- 수중 안개는 카메라→가장 가까운 지형/물 출구까지 거리에서 첫.75블록은 보존하고 최대256블록으로 제한한다. RGB흡수계수(.10,.037,.023)와청록(.009,.064,.076), 기존 시각·카메라 하늘빛에 따른 산란색을 적용한다. 추가 이미지 없이 기존A/B·depth를 재사용한다. 중간에 여러 수역을 통과하는 광선 전체를 추적하지 않으며 최초 물 구간의 근사다. 대기 안개 옵션과 독립이다.
- 공간/시간 순환, 근거리 그림자, 반해상도 볼륨, 고정 표본 예산으로 작업량을 제한한다. 전체 그래픽ON의 실제GPU비용은 미측정이다. 이번 소스 변경은 수중 빛무늬/빛기둥을 포함하지 않는다.

참고: 로컬 `ref/ComplementaryUnbound`의 물/구름/그림자/안개/블룸 구조를 읽고 일반적인 개념을 참고했다. 원본 코드나 텍스처를 복사하거나 런타임 의존성으로 추가하지 않았다.
