# 검증 기록

## 2026-09-27 DOLBUTO 로컬 교체

원본 DOLBUTO 전체 백업16821파일/5.321GiB 보존 및 목록·크기 대조, Git정보/설정/세이브 주요 파일 SHA256 확인. 새DOLBUTO의코드·에셋·문서·루트245파일은이관문서추가전sandbox와SHA256일치. 전체복사로그 실패0. 상세경로/복사캐시보관이력은 [이관 기록](migration-dolbuto-2026-09-27.md).

새빈build/release에서configure/Release전체빌드/패키징성공. CMake_HOME_DIRECTORY는DOLBUTO,컴파일러도DOLBUTO/.tools의clang-cl23.1.0. 로그 build/release/migration-configure.log와migration-build.log. SDL외부코드의SDL_last_event_id unused warning1개; 구성의선택적PkgConfig/LibUSB미발견은실패아님. 프로젝트컴파일오류없음.

격리된build/release/migration-inspection에새배포런타임복사. 사본만fps_limit60/LOD OFF, --world --debug-ui --render-distance 2 --frames 120 --validation --capture migration.png 실행exit0. 13컬럼공개/대기0, Vulkan errors0, UI issues0, texture failures0. 기존셰이더미사용정점출력경고14개. 자체캡처를view_image로읽어지형/F3/핫바표시확인. UI자동조작/합성입력/자동테스트/CTest없음. 같은사본의worldgen_editor --no-browser에서state schema10,HTML6944bytes,offset곡선257표본확인후API종료exit0.

패키징/실행후원본배포settings/worldgen SHA256은이관전과일치(이관기록참조). main/HEAD c9fd48b6a63f54e55a04cbf1e969a3541cc1ddc1/origin https://github.com/madamnut/DOLBUTO.git 유지. staged diff없음,commit/push수행없음. Git의대량삭제표시는기존프로젝트교체결과이며이전파일은전체백업에있다. sandbox원본도유지. 게임기능/명칭/규칙변경없음.

## 2026-09-26 웹 스플라인 연결 관계 편집

Release 증분 빌드·패키징 성공(`build/release/spline-relations-build.log`). 이번 변경은 app.js/index.html/style.css와 문서이며 C++/생성기/셰이더 변경 없음. runtime_assets 갱신, 전체 패키징 완료. `node --check assets/editor/app.js` 통과. 자동 테스트/CTest/컴퓨터 유즈/브라우저 자동화/합성 입력은 사용하지 않음.

격리된 `build/release/spline-relations-inspection`에서 배포 편집기를 `--no-browser`로 실행하여 기존 HTTP API를 수동 호출했다. index.html/app.js/style.css의 HTTP 응답과 배포 파일 바이트가 동일함. source/build/package 3개 위치의 해당 에셋 SHA256도 각각 동일. Offset/Factor/Jaggedness root는 10/5/3 제어점, 곡선은 각각257표본. Offset GND≈−0.16의 path[5]는 Smoothness축7제어점/257표본. 고정값 path[0]은 현재 사용자값−.444의 수평선. 전체 GND/E/W 단면 각각257표본, 별도 메모리 사본의 고정값−.21 수정은 validate에서 수락됐다. draft/publish는 호출하지 않았고 shutdown 후 exit0. 자세한 출력 `build/release/spline-relations-inspection/inspection.txt`.

설정 보존: 작업 전후 settings.json SHA256 `1597232D238F770788C3E86C69B4C331B3F0310CC30ED52DFD6E4A7558253D62`, worldgen.json `E6991E0A058F663EE92C22BC8CAFDC161F62605792BC649E28976E6C935FCDB8` 일치. 이전 작업 이후 사용자가 변경한 파일을 기준으로 보존했으며 이전 기록의 해시로 되돌리지 않았다.

소스 검토: 숫자 입력 onchange가 전체 DOM을 교체하면 blur 직후의 클릭을 잃을 수 있어 숫자 수정 시 입력·버튼을 유지하고 트리 문구/인접 입력 범위만 갱신한다. 구조 변경과 드래그 완료 때만 해당 폼 재생성. 응답은 canvas identity/requestId로 오래된 곡선 갱신·오류를 차단. 표·트리 탐색/키보드·드래그·반응형 배치의 실제 브라우저 동작 및 시각 확인은 수행하지 않았으므로 검증 완료로 주장하지 않는다. 게임 런타임은 변경하지 않아 이번 작업에서 추가 실행하지 않았다.

## 2026-09-26 바다·강·육지 바이옴

Release증분빌드 성공/컴파일경고없음(`build/release/biome-build.log`). src/world/biome.hpp 공용3종분류를F3기존표본에연결. 원본OverworldBiomeBuilder의해양/중앙계곡/G·E조건을검토해기후없는조건으로단순화. 원시W중앙구간을사용하므로PV접힌바깥꼬리는강이아님. 지형/물리/표면/기후노이즈와설정파일은변경없음. 자세한임계값과원본대응한계는 [바이옴](biomes-2026-09-26.md).

이전작업에서실행중인편집기잠금이있어동일한worldgen_editor.exe의build/out SHA256확인후유지하고,실제로변경된sandbox.exe와README만배포폴더에갱신했다. 나머지에셋/셰이더/런타임의소스변경없음. build/out sandbox.exe SHA256 5A7FDDCC406E129F2B2E837F41B2B762F4C30E3EB6068E73D08BC64794E3F9E2 일치. 작업시작시사용자worldgen은이전기록과달라졌으므로새baseline DB8141125D9BCC22D41C12B1E89BDE7AC85955B16582B40C5E4B0731C6DB43F9 그대로보존. settings SHA256 1AFD757B9FF404E8078718DEB3F0A15B1E975C328A0F66FFDBE9CBB332C8F09A 보존.

격리climate-debug-inspection에최종exe와현재worldgen사본갱신,기존FPS60/LOD OFF진단설정으로 --world --debug-ui --render-distance 2 --frames 120 --validation --capture 실행. biome.log/biome.png:exit0/13컬럼/Vulkan오류0/UI문제0/텍스처실패0,기존미사용출력경고14유지. 자체PNG에서기후아래 바이옴:육지 표시와레이아웃확인(G .4790/E −.0229/W .4972). 바다/강조건및경계는소스분기검토이며실제장면별이동검증을하지않았다. 자동테스트/CTest/CU/합성입력없음.

## 2026-09-26 F3 동서남북

Release증분빌드 성공,컴파일경고없음(`build/release/facing-debug-build.log`). 논리camera.yaw를remainder360으로정리하고동/남/서/북각90도구간표시;pitch/CameraMode/visual_rotation을참조하지않음. 비유한yaw는판정불가. 실제카메라/이동/설정값변경없음.

배포스크립트는게임exe복사후실행중인worldgen_editor.exe 잠금으로중단. 편집기build/out SHA256 994CF20AF5FB195489EF933BD7F2629C7B67C7FE8DFE8E41F8521FA5F344A73E 동일을확인해종료·교체하지않았다. 변경된README는수동복사완료,assets/shaders전체build/out바이트동일검토. 게임build/out SHA256 7D5DDE623CF2FA3E80B284C55250664510337FCAFC53487584860D7C531D8A43 일치. 기존settings SHA256 1AFD757B9FF404E8078718DEB3F0A15B1E975C328A0F66FFDBE9CBB332C8F09A/worldgen SHA256 63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 보존.

기존격리climate-debug-inspection의exe만갱신후 --world --debug-ui --render-distance 2 --frames 120 --validation --capture 실행. facing.log/facing.png:exit0/Vulkan오류0/UI문제0/텍스처실패0,기존미사용출력경고14. 자체PNG읽기에서시점아래북쪽(−Z)표시/한글/기존F3·핫바배치확인. 다른방향과앞3인칭은분기및논리yaw참조를소스검토했고실제회전조작은하지않았다. 자동테스트/CTest/CU/합성입력없음.

## 2026-09-26 F3 기후 9단계

사용자가 −1..+1 균등9등분을 선택. 경계는 ±1/9,±3/9,±5/9,±7/9의float표현이며 upper_bound로 정확경계가상위단계. 바깥유한값은끝단계,원래숫자유지;비유한값은판정불가. 기존F3 10Hz표본 values[5]/[6]에서두줄로표시하고추가노이즈계산없음. 실제바이옴선택/생성기/설정schema는변경하지않음.

Release증분빌드·패키징 성공, 컴파일경고없음(`build/release/climate-debug-build.log`). build/out sandbox.exe SHA256 F9F67C24E56148B1EC7DACB848CF832080BB378512406E2B03196FA97F6AD2E1 일치. 사용자settings SHA256 1AFD757B9FF404E8078718DEB3F0A15B1E975C328A0F66FFDBE9CBB332C8F09A,worldgen SHA256 63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 전후보존.

격리 build/release/climate-debug-inspection에복사한설정만FPS60/LOD OFF로변경. 기존CLI --world --debug-ui --render-distance 2 --frames 150 --validation --capture 로실행, exit0/13컬럼/Vulkan오류0/UI문제0/텍스처실패0. 기존미사용정점출력경고14유지. 자체캡처 climate.png에서 온도0.2642·난온대(6/9),강수량0.4514·습윤(7/9),한글폰트/기존F3배치/핫바와비중첩을이미지읽기로확인. 다른단계와외부/경계값은분류소스검토만수행. 자동테스트/CTest/CU/입력합성없음.

## 2026-09-26 플레이어 이동·보행 화면

Release 증분빌드/패키징 성공. `build/release/player-movement-build.log`, 최종 `player-movement-build-final.log` 모두 컴파일 경고없음. 최종 build/out sandbox.exe SHA256 `682E8742602980EE361503FF38608D10B7AD6782278B8EDC5CC42656D171F8BF` 일치. 사용자 settings `1AFD757B9FF404E8078718DEB3F0A15B1E975C328A0F66FFDBE9CBB332C8F09A`, worldgen `63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924` 전후 바이트보존.

로컬 Minecraft26.2 소스의 이동/보행 수식과 C++20TPS 단위변환/처리순서, 벽충돌 축속도 제거, 모드전환/텔레포트/재생성 초기화, 논리시선과 렌더카메라 분리, 메뉴 양쪽 토글과 저장 연결을 소스로 검토. 자세한 수식/호환범위는 [이동 기록](player-movement-2026-09-26.md).

격리 `build/release/player-movement-inspection` 런타임에 기존 설정/생성규칙을 복사하고 FPS60/LOD OFF만 진단용 변경. `--world --render-distance 2 --frames N --validation`로 일반 새 월드 진입. `legacy-on.log`는 schema9/흔들림 새기본ON/120프레임, `schema10-off.log`는 schema10/view_bobbing=false/60프레임. 둘 다 exit0,13컬럼/Vulkan오류0/UI문제0/텍스처실패0. 기존 미사용정점출력 Vulkan경고14는 남는다. 최종 변경은 player.hpp의 glm/geometric.hpp 직접 include 추가뿐이며 이 뒤 Release 재빌드·재패키징했다.

자동테스트/CTest/입력합성/컴퓨터유즈 없음. 프레임제한 실행은 이동 입력을 받지 않으므로 위 결과는 설정읽기·UI연결·정지상태 렌더·시작경로만 확인한 것이다. 보행/점프의 실제 조작감·움직이는 카메라·토글 클릭 왕복은 직접 플레이 검증하지 않았다.

## 2026-09-26 F4 LOD 단계 보기

Release 증분빌드·패키징 성공(`build/release/lod-debug-build.log`, 최종 `lod-debug-build-final.log`). 컴파일/셰이더경고없음. build/out sandbox.exe SHA256 5125F4F303136CCC584C5F2E19A07BD3657DB696BB2CE108187E3B770468C616 일치. 사용자 settings SHA256 8DDFC7D056E74C42AA16D4352789D832D28CB0D4ED6F11405408DBE6B5F95157, worldgen SHA256 63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 전후보존. src/shaders에서 wireframe/LINE_LIST/구형선파이프라인·12정점경로 제거를 조회확인했다.

격리 `build/release/lod-debug-inspection`에 사용자설정을 복사해 FPS60/LOD거리만 진단값으로 변경했다. 양축순환경계8191,8191컬럼,근거리4컬럼,동일사용자생성규칙,명시적CLI --validation/--profile-gpu/--capture/--lod-debug 사용. GUI키입력합성·CU·자동테스트·CTest 없음.

- debug.log/debug.png: F3+F4표시상태,LOD32,높이430/35°/-70°/12시,1840프레임,원거리3120/대기0. 실제청크회색/LOD0초록·1노랑·2주황 및단계별타일크기외곽선,범례위치검토. 범례폰트가약해이후SemiBold20으로보완.
- legend.log/legend.png: 최종바이너리,F3없음/F4상태,LOD16,높이330.580프레임,원거리708/대기0.독립우측범례의굵은흰글자·11색·기둥범위와타일외곽선을자체이미지로확인.
- normal.log/normal.png: 최종바이너리,디버그OFF,LOD16,높이230/35°/-12°/12시.580프레임,원거리708/대기0.일반물노멀·반사·조명·천체/구름경로와범례없음을확인.

세실행모두exit0/Vulkan오류0/UI문제0/텍스처실패0,공통정점인터페이스미사용출력경고14는남음. F4 실제키누르기/같은실행중왕복은검증하지않았으며 키바인딩→동일toggle함수→TAA이력무효화/색팔레트·효과복원은소스로검토했다. CLI는초기표시상태만설정하며생성·LOD선택을변경하지않는다. 캡처범위에서0..2단계를관찰했으며3..9단계는공유팔레트/셰이더인덱스와범례정합을소스로검토했다.


## 2026-09-26 LOD 확대 독립·물 반사

Release 증분빌드와 패키징 성공. 최종 컴파일/셰이더 경고없음(`build/release/lod-water-build-final.log`), 최초 configure 선택적 PkgConfig/LibUSB/rocprofiler 검색메시지는 오류아님. sandbox.exe build/out SHA256 EA92C093FFACE80423DA06A45B9F6746B3C6EEDB87834D680C33138AC28CD82C, worldgen_editor.exe 7ABA0DC3C8FF1A207AE559A6E3121B9AF42541DA6A502E3C7F68CE36D770F5C6 일치. LOD/물 셰이더9개 build/out 바이트동일. 사용자 settings SHA256 8DDFC7D056E74C42AA16D4352789D832D28CB0D4ED6F11405408DBE6B5F95157, worldgen SHA256 63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 작업전후보존.

`build/release/lod-water-inspection`에 복사한 런타임·설정만 사용. 기존 명시적 CLI validation/profile/capture 실행이며 자동테스트/CTest/입력합성/CU 없음. 모든 실행 exit0/Vulkan오류0/UI문제0/텍스처실패0; 기존 및 공통 정점 인터페이스 미사용출력 경고14. 실제로 Z를 누르지는 않았으며 LodCache::request/Request/선택/생성에 zoomed 또는 FOV 입력이 없는 것을 소스로 검토했다. 렌더 프러스텀과 수면 셰이딩의 픽셀발자국은 투영에 따라 달라져도 LOD 상세도 요청은 바뀌지 않는다.

- `on.log/on.png`: 일반49컬럼/LOD32완료,원거리3120,대기0. 육지 장면이므로 물 반사의 시각 근거로 사용하지 않는다.
- `water-on.log/water-on.png`: 양축 순환경계8191,8191컬럼/카메라230,yaw35,pitch-12/12시/LOD32/구름양30. 2140프레임,원거리3120/대기0. 실제수면과LOD수면에 노멀·하늘 반사가 이어지는 자체캡처 확인.
- `coast-on.log/coast-on.png`: 같은위치/LOD64/구름0. 3340프레임,원거리12742/대기20,표시429타일. 먼섬/해안의 거울상이LOD수면에 나오는 것을 이미지리더로 확인했다. 캡처 당시 포커스 상실에 따른 일시정지 메뉴/어두움이 포함됐으며 UI합성입력은 하지 않았다. 마지막 생성20컬럼이 남아 완전생성/정상플레이 고정성능 검증이 아님.
- `water-off.log/water-off.png`: 최종 배포동일바이너리,LOD16/카메라210/물효과enabled=false. 580프레임,원거리708/대기0,물효과 파이프라인 없이 기본LOD물/최종깊이 표시 경로 확인.

ON 캡처 이후 최종 C++ 변경은 물이 없는 타일의 물 패스 제출 생략 및 주석이며, 최종 OFF 경로에서 재실행했다. 화면 밖 지형은 SSR에 반사할 수 없고 기존 하늘/구름 보완을 사용한다. 원거리 조명/지형/해저는 축약 데이터이므로 정밀청크와 픽셀동일하지 않다. 이번 측정은 성능 전후비교가 아니다. 세부 구조는 [LOD](lod-2026-09-26.md)의 물 반사 연결 정정절.


## 2026-09-26 세션 원거리 LOD

Release 증분빌드·패키징 성공. 최종 컴파일/셰이더 빌드 경고없음 (`build/release/lod-build.log`). configure의선택적PkgConfig/LibUSB/rocprofiler검색메시지는실패조건아님. 빌드/배포sandbox.exe SHA256 39B8F026B870BBC08EDE3BCC8A639A26D35F4390098549D475B966C61877D0F5 및 worldgen_editor.exe 5FF7A7116D2CCF7148AFF611F1DD145F244225E7E33D2E9446CE61760463A4A9 일치. 사용자 settings SHA256 680113FEAA89CEAD3D5DB99D42CD733DD776405D623B12F0EDDE08EE93168197 / worldgen SHA256 63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 전후보존. 실행결과와한계의구조설명은 [세션 LOD](lod-2026-09-26.md).

격리런타임 `build/release/lod-inspection`, 기존CLI `--validation --debug-ui --render-distance 4 --profile-gpu ... --profile-view ... --capture ...`로실행. 사용자설정을복사한진단파일만schema9/LOD거리/60FPS/낮/구름양·일부품질조정. 입력합성/브라우저·컴퓨터유즈/자동테스트/CTest 없음. 생성기설정은사용자worldgen그대로. 로그/CSV/PNG파일은동폴더.

- `run.log/world.png`: LOD32컬럼 정지생성완료. 일반49컬럼,원거리생성3120,대기0,표시385타일. CPU52,758,608bytes/GPU10,342,144bytes. 초기렌더구현검증.
- `seam.log/seam.png`: 원점8191/8191컬럼(카메라131064/290/131064),LOD128. 일반49컬럼유지,원거리20424컬럼생성/592타일표시. 양축월드끝주변지형·수면을캡처로확인. 최초무한로딩없음. 전체128범위완료전캡처임.
- `budget.log/budget.png`: 같은순환경계128범위,원거리33543컬럼생성,세부캐시21529개퇴거,남은대기17801. CPU286,285,776bytes(약273MiB)/GPU26,430,272bytes(약25MiB). 타일682개표시. 분할업로드/캐시퇴거/지연GPU폐기포함버전. 메모리압박에서도계속원거리생성이진행됨을확인. 전체128범위가완료된결과는아님.
- `off.log/off.png`: schema9 LOD꺼짐,원거리생성0/표시0/GPU0. 이전캐시없으면원거리생성안함. 해당실행당시비활성상태에서도요청목록크기를대기로표시하는것을발견해최종코드에서는OFF대기0표시로정정함.
- `final.log/final.png`: LOD16다시켜고시작,708원거리컬럼생성후대기0. 일반청크49개유지.
- **최종배포동일바이너리** `delivery.log/delivery.png`: LOD16/원점1500,2000/카메라270,0,-18,9시. 939프레임/원거리708컬럼/대기0/표시216타일. CPU16,290,992bytes/GPU5,633,920bytes. 일반공개컬럼과같은level0 LOD는CPU에서도드로생략하는최종경로확인. 자체스크린샷을이미지리더로검토함.

모든실행exit0,Vulkan오류0/UI문제0/텍스처실패0. 셰이더인터페이스미사용출력등validation경고14는남아있다. CSV의steady는기존일반청크로딩기준으로원거리생성중샘플도포함하므로고정장면성능전후비교로사용하지않는다. 실제사용자입력으로이동/줌/옵션전환/편집/재생성을수행하지않았으며이경로들은소스로검토했다. 캐시경계의임의지형모든상황/거친수면전환의픽셀일치를보장하는검증은아니다.


## 2026-09-26 F3 RAM/전용 커밋

F3 우상단에 현재 프로세스 WorkingSetSize/PrivateUsage를 GiB 소수2자리로 표시한다. steady_clock 기준최대1초당1회조회, F3숨김시조회생략,실패시명시적조회실패표시를소스로확인했다. Microsoft 공식 PROCESS_MEMORY_COUNTERS_EX/GetProcessMemoryInfo 문서의필드의미및K32/Kernel32연결을확인했다. Release 증분빌드·패키징성공,경고없음(build/release/process-memory-build.log). 빌드/배포sandbox.exe SHA256일치. settings.json=680113FEAA89CEAD3D5DB99D42CD733DD776405D623B12F0EDDE08EE93168197, worldgen.json=63E4356C125F6B0CD088F549462AD8710DBFC3C5161E613C9D4CA2102C316924 전후보존. 실제게임의HUD시각검증은하지않았으며자동테스트/CU/합성입력없음.

## 2026-09-26 Z 망원 확대

Release 증분 빌드·패키징 성공, 빌드 경고없음. build/release/zoom-build.log. 배포 sandbox.exe와 빌드 출력 SHA256 일치. settings.json/worldgen.json 전후 SHA256 동일. 소스로 Z 현재 상태/메뉴·콘솔·F8·포커스 차단, 시야각·감도1/4, 1·3인칭 투영/근평면 충돌과 TAA 이력 초기화 연결을 검토했다. 키 해제 후 임시 확대만 해제하여 기본 설정을 복원한다. 실제 게임 조작·시각 확인은 하지 않았으며 자동테스트/CU/합성입력은 사용하지 않았다.

## 2026-09-26 기존 표면 규칙 제거

사용자 실시 승인으로 자연 생성의 잔디·흙·모래/해변/수중층 배치를 제거했다. terrain.cpp 일반 경로와 균일 청크 경로에서 돌/물/공기만 반환하는 것을 소스로 확인했다. sandy_surface와 표면 재질 함수/노이즈 분류는 제거, surface_y 탐색은 육지 스폰용으로 유지. Release 증분 빌드·패키징 성공(경고없음). source/build 배포 실행파일 해시일치. settings.json 및 worldgen.json의 전후 SHA256 동일. 게임 실행·시각 검증은 이번 작업에서 하지 않았고 자동테스트/CU/합성입력을 사용하지 않았다. 빌드 로그 build/release/surface-rules-removal-build.log.


## 2026-09-26 주기적 Double Perlin / 원본 중첩 지형

완료: Release 증분 빌드·패키징 성공. 최종 빌드 경고없음. 실행파일2개/편집기파일6개/배포기본JSON source-build-package 해시일치. runtime worldgen은명시적백업후schema10초기값전환,settings해시보존. 상세 [구현·수치·성능](periodic-double-perlin-2026-09-26.md).

격리 수동진단:12지도/Blended3D/Shift두번째단면의주기차0,6929입력×3스플라인최대오차1.67e-6,433웹곡선노드API확인,6컬럼표면·주기블록불일치0. JS구문/staticDOM ID정합확인. 합성높이미리보기는이전설정대비약32.5~37.3%느리고개별노이즈지도는빨라짐;동일파라미터연산비교아님. 실제게임격리CLI로49컬럼로딩·렌더·캡처후exit0,Vulkan오류0/UI문제0/텍스처실패0;기존shaderinterface경고14. 컴퓨터유즈/합성입력/자동테스트등록없음,브라우저UI조작/레이아웃은실검증하지않음.


## Minecraft 26.2 원본 비교 페이지 (2026-09-26)

Release빌드·패키징성공. 배포두실행파일/편집기신규3파일·연결파일해시일치. 원본Java클래스직접호출과격리HTTP수치대조:6시드×8좌표×10값최대차1.11e-16,6,425곡선입력×3결과최대차2.37e-7. 10종비교지도/3축곡선/유효성오류400/정적파일200/JS구문·포맷·DOM누락중복0확인. settings SHA256 4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1 및worldgen E16C2EED246E6B85ECA968FCF5E2C98A7328CB309A1C60BDF03862DBB7DDB7DB로보존. 실제브라우저외관·입력/게임실행검증없음. 자동테스트/CTest/CU/합성입력없음. 상세 [minecraft-reference-page-2026-09-26.md](minecraft-reference-page-2026-09-26.md).

## MCP-Reborn 로컬 소스 준비 (2026-09-26)

upstream26.2/727d72ffc66bcdf1a8c16ee92b120db2eaa46e26을ref/MCP-Reborn에고정. 공식Adoptium JDK25다운로드SHA256검증,격리Gradle캐시/JDK21실행·JDK25도구체인으로setup성공(4분6초). Java7055파일및지형핵심파일/메서드확인. 루트Git제외/패키징ref미포함확인. 게임Release증분빌드성공,실행중인동일해시편집기/CRT만복사생략하여패키징완료. settings/worldgen해시보존. Minecraft실행/자동테스트/CTest/CU없음. 상세 [mcp-reborn-reference-2026-09-26.md](mcp-reborn-reference-2026-09-26.md).

## Groundness 단일 높이 곡선 제거 (2026-09-26)

구형 높이곡선/UI/설정/의존생성경로 제거 후 JS구문·포맷검사/Release빌드성공. 현재사용자규칙으로기존/신규편집기12종지도×2영역×64²표본응답바이트동일,현대파라미터모두동일. schema9왕복/폐기키제외/구형복합ON읽기/번들기본값/제거API·구형OFF오류확인. 실제게임UI·브라우저조작검증없음. 최초패키징은실행중인편집기잠금으로중단되었으나사용자종료확인후재개하여완료. 배포sandbox.exe/worldgen_editor.exe/app.js/default.json/README와빌드·원본해시일치. settings.json SHA256 4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1, worldgen.json E16C2EED246E6B85ECA968FCF5E2C98A7328CB309A1C60BDF03862DBB7DDB7DB로작업전후동일. [height-curve-removal-2026-09-26.md](height-curve-removal-2026-09-26.md).

## Snowcapped 방식 웹 편집기 (2026-09-26)

JS구문/포맷검사·Release빌드·패키징성공.정적ID누락/중복0.격리편집기 --no-browser에서정적4파일200,PV자체축·하위곡선·잘못된경로400·기존최종곡선4종·전체월드지도/schema8 API응답수동확인. 배포편집기실행파일/4개웹파일/README/Snowcapped고지가원본과동일. settings.json SHA256 4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1, worldgen.json E16C2EED246E6B85ECA968FCF5E2C98A7328CB309A1C60BDF03862DBB7DDB7DB로작업전후동일. 실제브라우저외관/입력조작은미확인. 자동테스트/CTest/CU/합성입력없음. [editor-snowcapped-2026-09-26.md](editor-snowcapped-2026-09-26.md).

## 인게임 명령 콘솔 (2026-09-26)

포맷 검사와 Release 컴파일·링크·패키징 성공. 소스 검토로 열기 Enter 제외/콘솔 독점 입력/Esc 옵션 중복방지/다음 프레임 명령 적용/인수 수·숫자·범위 검사/기록 경계와 작성중 입력 복원을 확인. 배포 실행파일·README 해시가 빌드/루트와 일치하고 settings.json/worldgen.json SHA256은 작업 전후 동일. 실제 게임 실행·콘솔 키 입력·시각적 배치 검증은 수행하지 않았으며 자동 테스트/CU/합성 입력을 사용하지 않았다. 상세 docs/console-2026-09-26.md.

## 시작 걷기 모드 (2026-09-26)

일반 육지 스폰 분기에 MovementMode::walk 지정. 소스 검토로 스페이스 두 번 전환과 명시적 프로필의 기존 플라이 조건 유지 확인. 포맷 검사/Release 증분 빌드/패키징 성공, 빌드·배포 실행파일 SHA256 일치. settings.json/worldgen.json SHA256 작업 전후 동일. 실제 게임 실행·입력 검증은 수행하지 않았으며 자동 테스트/CU/합성 입력은 사용하지 않았다.

## 육지 스폰·달력 (2026-09-26)

Release빌드성공.32개무작위스폰의실제생성발밑/몸공간/유체/위도검사불일치0,전해양탐색상한확인.자정·월말·연말·역행달력출력확인.일반CLI500프레임자체캡처에서육지/09:00/1일차/10800틱F3표시확인,Vulkan오류0/기존경고14. [spawn-calendar-2026-09-26.md](spawn-calendar-2026-09-26.md).

## 얼음 반투명·반사 (2026-09-25)

Release/GLSL 컴파일 성공. CPU 메시 분류·내부/halo 면·광 증분 대조 확인. 입력 합성 없는 임시 고정 재질 장면에서 물ON/OFF 각각80프레임 캡처, Vulkan오류0/기존경고14/UI문제0/텍스처실패0. RGB 보존·알파255 확인. fixture는 배포/게임 소스에 포함하지 않는다. 범위/한계는 [ice-material-2026-09-25.md](ice-material-2026-09-25.md).

## 얼음·눈 (2026-09-25)

Release빌드성공.16겹전체충돌·선택·메시·저장재적용대조불일치0.순환경계/서로다른높이착지/얇은눈→가득찬눈증분조명일치.격리CLI329프레임/오류0/기존경고14,8·9번아이콘캡처확인.직접쌓기플레이외관미확인. [snow-ice-2026-09-25.md](snow-ice-2026-09-25.md) 참조.

## 용암 (2026-09-25)

Release 빌드 성공. 독립 CPU 출력에서5틱스케줄/물매틱/재료별양보존/혼합차단/순환경계/지연재개,용암메시·광원·잔광제거·증분조명대조 확인. 격리게임CLI329프레임/오류0/기존경고14 및7번아이콘캡처. 실제용암설치외관미확인. [lava-2026-09-25.md](lava-2026-09-25.md) 참조.

## 한 단계 수면 연결 (2026-09-25)

Release 빌드 성공. 임시 CPU 출력 진단에서 청크/월드 순환 경계 높이 일치, 벽/큰 낙차/수직물 유지, 354,880꼭짓점 압축 해독 불일치0 및 삼각형 수중 보간 오차0 확인. 격리 평탄 수면 CLI329프레임/Vulkan오류0/기존경고14/자체캡처 확인. 실제 부분량 경사의 플레이 외관은 미확인. 상세는 [fluids-2026-09-25.md](fluids-2026-09-25.md) 최신절.

## 유체 정밀도256 및 물 설치 (2026-09-25)

Release컴파일및별도CPU출력진단에서sizeofFluid2/최대256보존,수평총16·미세균등화·하향256·세션재적용·메시/조명경계확인. 혼합영역200틱총량292463유지/범위초과0. 실제게임CLI329프레임/오류0/기존경고14,수면과6번아이콘자체캡처확인. 마우스설치플레이미확인. 상세는 [fluids-2026-09-25.md](fluids-2026-09-25.md) 최신절.

## 유체 틱 호출 위치 수정 (2026-09-25)

초기 물 흐름 구현의 호출이 구름키 이벤트 안에 있어 실제20TPS가 진행되지 않았음을 사용자 제보로 확인했다. `ㅇㅋ 실시` 승인 후 프레임 메인 루프의 in_world&&!paused 분기로 이동했다. 초기 CPU 진단/정적 수면 실행을 게임 내 흐름 확인으로 해석하면 안 된다. 수정본 Release 빌드와 격리CLI329프레임 실행 성공(Vulkan오류0,기존경고14,UI문제0). 호출 단일성/이벤트루프 외부를 소스로 확인. 상세는 [fluids-2026-09-25.md](fluids-2026-09-25.md).

## 유한 유체 확인 (2026-09-25)

Release 컴파일 및 별도 CPU 출력 진단:양18412가200틱후18412,초과0,active0. 수평/낙하/미로딩/순환경계,세션재적용,부분수면메시,조명분기 확인. 별도평탄해저 게임CLI329프레임/29컬럼/오류0으로수면캡처 확인. 경고14는외부레이어/기존shader미사용출력. 직접편집플레이/대규모유체성능은 미확인. [fluids-2026-09-25.md](fluids-2026-09-25.md)에범위·한계기록.

## 2026-09-25: Groundness 옥타브별 간격

격리편집기API에서전후24전체지도/8옥타브지도바이트동일. 소수/비정렬간격·16옥타브·가중합일치·저장왕복·잘못된설정11종거부확인. 사용자설정파일은바이트보존. Release빌드·패키징완료. 일반자동테스트/CU/합성입력없음. 상세: groundness-spacing-2026-09-25.md.

## 2026-09-25: 4D Simplex / 개별 워핑

Release빌드·패키징완료. 명시적torus_benchmark:960주기표본쌍/3D256표본보존/독립청크49,152블록동일/4worker일치/설정왕복과참조검사통과. 격리편집기API로12전체월드지도/구형이행/초안/확정저장확인,게임18회441컬럼완료. 일반자동테스트/CTest/합성입력/CU없음. 전체노이즈배치는바뀌므로전후이미지/게임로딩동일성을요구하지않는다. 성능·제약·파일해시는 [simplex-transition-2026-09-25.md](simplex-transition-2026-09-25.md).


## 고체 그림자 깊이 재사용 비교 (2026-09-24)

- 고체/플레이어 깊이·색 metadata를 한 번 기록하고 깊이를 복사한 뒤 물만 추가하도록 변경했다. 그림자 수식/거리/해상도/갱신 주기/사용자 설정은 유지한다. Release 빌드·패키징과 변경 C++ format 검사 성공.
- 별도 runtime에서4장면×전후2종×3회,14,400steady프레임을 측정했다. 그림자 GPU 평균39~45%, 전체 GPU 평균22~27% 감소. 육지 after의 긴 표본107ms도 제거하지 않았다. 세부 평균/중앙값/p95/실행 변동은 docs/shadow-reuse-2026-09-24.md를 따른다.
- 각 장면의 깊이2장/색2장, 총16개2048²맵이 바이트 단위로 동일했다. 최종 PNG는 TAA/history/dither 때문에 동일하지 않으며 정지 화면을 읽어 비교했다. 이동·태양 이동 동영상 검증은 하지 않았다.
- 성능24실행 모두exit0/오류로그0. 별도 validation+sync validation을 quality1/2/3에서 요청한3회도Vulkan errors0/SYNC-HAZARD0이다. 각 로그의 외부 레이어/미사용 shader 출력 관련 경고14개는 남아 있다. 일반 자동 테스트/CTest/합성 입력/CU는 사용하지 않았다.
- 최종 게임 SHA256 `1743CA2FBCBD7DF1671B00887809E54101DB5805FE9CF7BC68C443508B4A6AE7`를 배포했다. 사용자 설정/생성 규칙 해시는 작업 전과 동일하다. 원시 steady CSV/입력/로그/맵 해시/PNG/통계는 docs/benchmarks/shadow-reuse-2026-09-24-*에 보관한다.

## 그래픽 GPU 패스 계측 (2026-09-24)

- 사용자 `측정부터 해보자 실시`로 기본OFF인 `--profile-gpu`와 초기 장면 지정/수동 집계 도구를 추가했다. 그래픽 수식/렌더 알고리즘/품질은 바꾸지 않았다. 일반 자동 테스트/CTest/합성 입력/CU를 사용하지 않았다.
- 실제 RTX3080/1280×900/거리24/정오 고정에서5장면×3회 및 해안 효과ON/OFF5조건×3회, 총30실행/18,000steady프레임. 240프레임 예열 이후각600표본과1793컬럼완료를확인했고구간합/전체GPU시간일치,로그오류0,전부exit0이다. validation=0으로실행했으므로validation layer검증은아니다.
- 그림자 두map이GPU시간의약53~67%로최대다. 해안·산지의최종물표현0.57~0.67ms가SSR탐색0.17~0.21ms보다크다. 블룸은약0.17ms. 효과OFF차이는기능의종속경로/클럭변동도포함한다. 조건/분모/전체통계/제약은docs/gpu-profile-2026-09-24.md를따른다.
- Release증분빌드·패키징성공,변경C++format검사성공. 최종게임SHA256 `DBC1A1CC22F03B4C2815F196186A742FCC38C6433234BEB2F4A56F20840F8C6E`가빌드/계측runtime/배포에서동일하다. 계측OFF일반CLI120프레임실행도exit0이며프로필로그없음. 입력settings/worldgen의작업전후해시동일. 보고서/원시표본은docs/benchmarks/gpu-profile-2026-09-24-*에보존한다.

## 조명 최적화·독립 비교 (2026-09-24)

- 사용자 승인 범위의 경계/halo 생략·내부 재사용·빈 인공광 생략·연속 접근·밝기 bucket 큐를 적용했다. worker와 동기 비교 도구가 같은 update_column_lights를 사용한다. 명시적 SIMD/컬럼 공개 스케줄링/지형은 변경하지 않았다. 일반 자동 테스트/CTest/컴퓨터 유즈/합성 입력 없음.
- Release compile/link, lighting_benchmark와 world_benchmark 대상 빌드 성공. C++ clang-format --dry-run --Werror 성공. 비교 도구는 EXCLUDE_FROM_ALL, 기준 코드/카운터는 게임 배포에 포함되지 않는다.
- 최종 final-a/final-b 각123731712, 합계247463424개18³ halo 포함 샘플의 하늘빛·인공광·차폐 정보가 동결 기준과 정확히 일치. 빈 인공광 요약의 유효성/키/revision도 확인.6초기입력/9편집상황/부분 이웃/순환경계/Y양끝/48묶음 혼합 편집을 두 번 수행했다. 두 실행exit0. 초기에 거부되는 물 설치 fixture를 발견해 물 제거로 수정하고, 탐색 실행은 최종 통계에서 제외했다.
- 각 구현/상황당31회×2실행=62관측, 원시1860개. 평균/중앙값/p95와 큐 소비/업데이트/경계/halo 작업량은 docs/benchmarks/lighting-2026-09-24-*에 보관. 초기 조명 중앙값4.99~5.55배, 편집1.39~1.79배. 이것은 전체 게임 FPS 배율이 아니다.
- CPU 파이프라인 전후36실행에서 지역별 총 면 수 동일. 실제게임 거리24의 전후3쌍 모두1793컬럼 공개/fence 회수, face bytes13008216 동일,exit0/오류로그0. validation=0. 전체 공개 평균11.420→11.250초로 변화가 작아 공개 경로 병목은 유지된 것으로 판단한다. 자세한 범위/분모/변동 한계는 lighting-optimization-2026-09-24.md.
- 최종 Release 증분 빌드·패키징 완료(final-build.log). 실제 실행한 after.exe와 빌드/배포 sandbox.exe의 SHA256은 `C5D63D6F21F4C8796AC67748E471119275C3E76D49D78B1DC95673AEA4129211`로 일치한다. 사용자 worldgen/settings는 작업 전 해시를 유지했다. 원시 pipeline 실행별 CSV도 docs/benchmarks에 보존했다.

## 실제 청크 파이프라인 계측 (2026-09-24)

- 사용자 `실시`에 따라 기본OFF 타이머/CSV, 명시적 게임 `--profile-world`/`--profile-origin`, EXCLUDE_FROM_ALL의 world_benchmark, 통계 집계 스크립트를 추가했다. 알고리즘/저장 규칙/스케줄링/노이즈/스레드 수는 유지한다. 자동 테스트/CTest/합성 입력/컴퓨터 유즈는 사용하지 않았다.
- Release configure/build 및 별도 world_benchmark compile/link 성공. 변경 C++의 clang-format --dry-run --Werror 성공. 최종 일반 Release 증분 빌드·패키징 성공(world-profile-final-build.log). game/editor 배포 실행 파일이 빌드 실행 파일과 SHA256 일치.
- CPU 반경6 계측ON/OFF 각7회×3지역=42실행, 반경24 각3회×3지역=18실행. 전부exit0. 지역별 모든 실행에서 컬럼 수와 총 면 개수 동일. 반경6은113컬럼, 반경24는1793컬럼을 완주했다. 계측 오버헤드는 시간 변동과 겹쳐 정밀하게 분리하지 못했으며 이상값을 제거하지 않았다.
- 실제 게임은 별도 runtime 복사본에서 CLI로 반경6/24 각각3지역×3회=18실행. 전부exit0, 지정한 모든 컬럼 공개 및 관련 GPU submission fence 회수 확인. 로그 UI issues/texture failures/Vulkan errors/warnings0. Release validation=0이므로 validation layer 검증을 주장하지 않는다. 입력·이동·UI 조작을 합성하지 않았다.
- 첫 CPU 하네스 실행의 try_lock 실패를 잘못 오류로 취급한 부분을 수정했다. 최초 반경24 게임 계측은 기록 한도를 넘었으므로 희소 행 저장으로 변경하고 전 실험을 다시 실행했다. 두 실패한 탐색 실행은 최종 통계에 섞지 않았다. 이후 모든 최종 데이터가 완주 및 누락 없는 CSV 저장을 완료했다.
- 사용자 worldgen SHA256 `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings `F264819A6441F5C47B27920DF650A3721F29E2B9C9252F61FA260B3E23052A30` 작업 전후 동일. 입력 사본과 요약은 docs/benchmarks/world-profile-2026-09-24*에 보관. 상세 수치·분모·제약·원시 자료 경로는 world-profile-2026-09-24.md를 따른다.

## PSRD 비교 측정 (2026-09-24)

- 사용자 `ㄱㄱ`에 따라 tools/benchmarks/psrd의 정적2D PSRD AVX2 노드와 --psrd 성능/수치/이미지 경로를 추가했다. EXCLUDE_FROM_ALL의 별도 측정에만 연결하며 게임/편집기 생성 코드는 바꾸지 않았다. 일반 자동 테스트/CTest/합성 입력/컴퓨터 유즈 없음.
- 초기 빌드에서 Generator의 필수3D 가상함수 누락을 발견하고 명시적 미지원 예외로 수정했다. 이후 Release compile/link 성공, 최종 clang-format --dry-run --Werror 통과. psrd-final-build.log 및 psrd-formatted-build.log에 컴파일 warning/error 없음. 최종 서식 변경 후 재빌드했으며 동작 수정은 없었다.
- 실제 CPU0/P-core/AVX2에서 두 실행 모두 exit0, 총744관측. 원본 독립double 수식 최대차1.335174e-6, 검사 표본 X/Z 주기 이동 최대차0, 경계 중앙차분 최대차4.656613e-10. 진단이 타이밍 전에 완료되며 예외 발생 시 성능 측정을 중단한다. 검사 표본/시드/부동소수점 제약은 psrd-benchmark-2026-09-24.md를 따른다.
- matplotlib로 동일[-1,1] 범위/동일 물리 좌표의 회색조와 등고선을 생성하고 두PNG를 직접 읽어 확인했다. PSRD의 대비 증가와 삼각형/세로 방향 무늬가 보여 아티팩트가 없다는 결론은 내리지 않았다. 원시CSV744행/통계12조건 및 실행별 진단 파일을 docs/benchmarks에 보존했다.
- Release 증분 빌드·패키징 성공(psrd-package-build.log). 배포 game/editor 실행 파일 해시가 빌드와 일치한다. 사용자 worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings `63D1090D617BA362BB0EC5E0F188A7F3A6C832F68A582B108039E9EA83AC435D` 작업 전후 동일. 성능 실행 파일과 PSRD 소스는 게임 배포에 포함하지 않는다. 게임은 이번 작업에서 직접 실행하지 않았다.


## 사용자 요청 노이즈 벤치마크 (2026-09-24)

- 사용자 성능 측정 및 통계 제공 `실시`로 독립 noise_benchmark(EXCLUDE_FROM_ALL)를 추가했다. 일반 자동 테스트/CTest/게임 검증 경로를 복원하지 않는다. world_core/FastNoise 라이브러리는 기존 코드 그대로 링크한다. 생성 규칙과 복수 워핑은 변경하지 않았다.
- Release configure/build 및 명시적 noise_benchmark 대상 compile/link 성공, noise-benchmark-build.log에 컴파일 warning/error 없음. 소스 clang-format --dry-run --Werror 통과. i5-13400의 CPU0/P-core/AVX2 실제 선택을 기록하고1스레드로 실행했다.
- 25/256/65536샘플 ×1/6옥타브 ×5방법 ×31회 ×2실행 =1860관측. 두 실행 모두 exit0. 워밍업/반복 횟수 보정, 라운드별 순서 혼합, 출력 관찰 및 유한 checksum을 포함한다. 두 토러스 변환 구현은 모든 배치/옥타브에서 출력 최대 차이0. 첫 실행 시간 편차에 따라 동일 조건을 한 번 더 측정하고 이상값을 버리지 않고 두 실행을 모두 통계에 포함했다.
- 최신 규칙에 따라 Release 패키징 완료. 배포 game/editor 실행 파일은 빌드 해시와 일치하며 사용자 worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings `63D1090D617BA362BB0EC5E0F188A7F3A6C832F68A582B108039E9EA83AC435D` 작업 전후 동일. 측정 실행 파일은 일반 게임 배포에 포함하지 않았다.
- CPU 전체 사용률42%를 한 차례 관측했으며 백그라운드 부하/부스트 영향을 완전히 통제하지 않았다. 통계/원시CSV/재현조건/해석 한계는 noise-benchmark-2026-09-24.md 및 benchmarks/noise-2026-09-24-*.csv를 따른다. 게임 전체 생성 성능이나 시각 품질 측정이 아니다. 컴퓨터 유즈/합성 입력 없음.


## 독립 편집기 UI 개편 (2026-09-24)

- 사용자 `실시`로 지도 중심 UI, 노이즈/지형 조합/기후의 선택식 폼, 지도 휠/드래그/범위 입력, 동일 범위 변경 전후 비교, 넓은 스플라인 dialog와 한 단계씩 중첩 편집, 상단 저장 상태 및 파일 dialog를 추가했다. 지형 계산/저장 schema/사용자 설정은 변경하지 않았다.
- Release configure/compile/link 성공(editor-ui-build.log), 최종 JS 수정 후 runtime_assets 증분 빌드 성공(editor-ui-final-build.log). C++ 서버는 map-view.js 정적 제공 목록만 확장했다. node --check 두 파일 및 C++/JS clang-format --dry-run --Werror 통과. 초기 시작 코드 이동 때 reload 핸들러의 구문 오류를 발견해 수정하고 최종 구문 검사를 다시 통과했다.
- HTML 중복ID0, JS 정적 ID 대상 누락0, 인라인 이벤트/스타일 속성0. 초기 state 요청을 map-view.js 마지막으로 옮겨 두 스크립트 로딩 순서 경쟁을 방지했다. UI 파일/설정 다운로드 외 외부 리소스 요청은 추가하지 않았다.
- 게임/브라우저를 열지 않고 최종 worldgen_editor --no-browser 정상 실행. 네 정적 파일 GET 결과가 소스와 바이트 일치. 임시 요청 객체의 시드만 바꿔 같은 X/Z32768..49152 영역의 Groundness128² 두 지도를 요청: 크기 일치/모든 값 유한/16380개 샘플 차이. server shutdown exit0. 로그 editor-ui-inspection.log. 저장 API를 호출하거나 사용자 JSON을 바꾸지 않았다.
- Release 패키징 후 두 실행 파일/웹 에셋4종/사용자 README 해시 일치. 사용자 worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings `63D1090D617BA362BB0EC5E0F188A7F3A6C832F68A582B108039E9EA83AC435D` 작업 전후 동일.
- 컴퓨터 유즈/합성 입력/브라우저 자동화/자동 테스트를 사용하지 않았다. 브라우저 화면 배치, 휠/드래그/슬라이더/대화상자 포커스의 실제 상호작용은 직접 실행 검증하지 않았다. 계산 요청 검증을 브라우저 UI 검증으로 해석하지 않는다. 이번 변경은 게임 런타임 소스를 바꾸지 않아 일반 게임 실행을 반복하지 않았다.


## 독립 월드 생성 편집기 (2026-09-24)

- 사용자 `실시`로 CPU 전용 편집기, 한국어 웹 UI, 기존 생성 코드 미리보기, 초안/확정/백업, F8 확정 파일 불러오기를 추가했다. Release 증분 configure/build/link 성공, editor-final-build.log에 컴파일 warning/error 없음. 수정 C++4파일과 JS의 clang-format --dry-run --Werror, node --check 통과. HTML 중복ID0, JS 정적 ID 대상 누락0, 인라인 이벤트/스타일 속성0을 확인했다.
- 분리된 build/release/editor-inspection 복사본에서 --no-browser로 정상 서버를 실행하고 일반 HTTP 요청으로 state, preview, probe, curve, draft, publish, shutdown을 확인했다. 지도128² Groundness(-0.6744..0.6734), 온도(-1..0.9997), 강수량(-0.651..0.6227), 잔굴곡 기준높이(117.59..431.11) 모두 유한했다. 좌표 신호12종, 곡선257점 응답. 실제 사용자의 확정 파일에 쓰지 않았다.
- 복사본 초안 저장 후 확정 파일 원문 유지, 복사본 시드 변경 확정 후 백업1개, 오래된 revision409, 유효하지 않은 squash0 저장400, 무인증 API403, 종료0을 확인했다. 최종 실행 파일은 별도 한글 경로 editor-경로확인에서 파일 없음→기본값 fallback/revision missing, UTF-8 경로 응답, 잘못된 preset/curve400, 종료0을 확인했다. 자동 테스트 소스/CTest/재사용 검증 도구는 추가하지 않았다.
- 실행 파일 imports는 WinSock/bcrypt/Shell/Windows/CRT만 참조하며 SDL/Vulkan 런타임 의존성이 없다. API 설정/지도 계산은 실제 게임 world_core 링크이다. 브라우저 화면 렌더링·포인터/드래그·파일 다운로드·실제 기본 브라우저 자동 열기는 직접 검증하지 않았다. 컴퓨터 유즈/합성 입력/브라우저 자동 조작은 수행하지 않았다.
- 게임은 사용자 JSON 사본을 build/release/bin에 잠시 두고 일반 --world --render-distance12 --frames600 실행: exit0,287컬럼/160표시청크,UI issues0/texture failures0. 로그 editor-game-run.log. 외부 Vulkan validation은 켜지 않았으므로 엔진 오류0을 validation 확인으로 해석하지 않는다. F8 불러오기 버튼의 실제 클릭/재생성은 미검증이다. 임시 JSON은 finally에서 제거했다.
- 사용자 원본 보존 기준: worldgen SHA256 `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings `63D1090D617BA362BB0EC5E0F188A7F3A6C832F68A582B108039E9EA83AC435D`. Release 패키징 후 두 해시 일치, 두 실행 파일 및 웹 에셋3종의 source/build/package 일치, 배포 README 일치를 확인했다.


## 핫바 슬롯 스프라이트 교체 (2026-09-23)

- 사용자 새이미지 확인·적용실시로22×22 RGBA 원본을읽고기존슬롯PNG에교체했다. 원본4배규칙에따라88dp/전체880dp,아이콘64dp와선택72dp중앙정렬,bottom0을유지했다. 원본해시는assets.md참조.
- Release 증분빌드·패키징 성공(hotbar-sprite-build.log). 일반CLI600프레임/반경12/사용자JSON사본으로exit0,293컬럼/162청크(hotbar-sprite-run.log). hotbar-sprite.png를읽어새직각테두리/10칸/아이콘·선택정렬/하단고정을확인했다. UI/텍스처오류0. 외부Vulkan validation은별도활성화하지않았고자동테스트/합성입력/컴퓨터유즈는없다.
- source→build→package의슬롯·선택PNG/RCSS해시일치,배포exe는빌드와일치. 사용자settings/worldgen은작업전후동일하며임시JSON은finally에서제거했다. 키입력로직은변경하지않았다.

## 핫바 2배 확대·하단 고정 (2026-09-23)

- 사용자 `실시`로 슬롯48→96dp, 선택틀36→72dp, 아이콘32→64dp 및 inset2배, 핫바bottom20→0을 적용했다. 향후 UI스프라이트 원본1px→4dp 기본규칙을AGENTS.md와assets.md에기록했다. 이미지원본/입력/블록배치는변경하지않았다.
- Release 증분빌드·패키징 성공(hotbar-scale-build.log). 일반CLI600프레임/반경12/사용자설정사본으로정상종료(hotbar-scale-run.log),296컬럼/165청크. hotbar-scale.png를읽어1280×900화면에서전체10칸폭960/높이96,하단고정,아이콘과선택틀배율을확인했다. 엔진UI/텍스처오류0. 이번실행은별도외부Vulkan validation을활성화하지않았으므로엔진오류0을validation검증으로해석하지않는다. 자동테스트/합성입력/컴퓨터유즈없음.
- 배포RCSS/실행파일은빌드와해시일치. 사용자settings/worldgen은작업전후해시일치하며임시빌드설정은finally에서제거했다. 원본PNG를편집하지않았다. 작은창에서고정폭960보다가로가작으면잘릴수있으며별도반응형배율은이번요청범위에추가하지않았다.


## 10칸 핫바 검증 (2026-09-23)

- 사용자 `실시`로10칸/1~9·0/휠선택/이미지슬롯·선택틀/텍스처만표시를 적용했다. 설치등록5종을1~5에 유지하고6~0은빈칸이며 빈칸 설치는조기반환한다. 신규블록은등록하지않았다.
- Release 증분 빌드·링크·패키징 성공. build/release/hotbar-build.log에서 컴파일 경고/오류 없음. 변경 C++4파일 clang-format --dry-run --Werror 통과. RML 구문/중복ID/10개slot ID 확인. 숫자키 매핑/휠modulo10/빈칸 설치 방지/메뉴입력 차단은 소스로 검토했다.
- 사용자settings/worldgen사본을 build/release/bin에 임시로 두고 일반CLI --world --render-distance12 --frames600 --capture 실행,299컬럼/166표시청크/exit0. 외부Khronos synchronization validation 실제Error/Warning/VUID0, 엔진Vulkan/UI/텍스처오류0. hotbar-run.log와hotbar.png에기록했다. 캡처를읽어하단10칸/첫5블록아이콘/나머지빈칸/첫칸흰선택틀/번호·이름제거를확인했다.
- 키/휠/설치 입력을 합성하지 않았으므로 실제 입력 이동·빈칸 설치 상호작용은 직접 실행 검증하지 않았다. 자동테스트/컴퓨터유즈는 수행하지 않았다. 임시JSON은finally에서제거했다.
- 사용자settings `D27DDCEE2F6D0E20FDE08D79897A8BE074C68DE31EB668379DDC9FD687144E92`,worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D` 작업전후동일. 배포exe SHA256 `E0A26C6FCE0A7F275F6A13497215B3ADD71B26EF2A4C6577BE4107EEFA1B7984`, 빌드exe와일치. source→build→package의RML/RCSS/두PNG도일치한다. 에셋이동시원본해시보존은assets.md참조.


## 절차적 태양·달·별 검증 (2026-09-23)

- 사용자 `실시`에 따라 이미지 없는 천체·두 옵션·schema8을 구현하고 Release 증분 configure/build/link 및 tools/package.ps1을 완료했다. 로그 build/release/celestial-build.log의 shader/C++ 컴파일과 링크 성공, 컴파일 경고/오류 없음. C++4파일 clang-format --dry-run --Werror 및17개 SPIR-V spirv-val --target-env vulkan1.4 통과.
- 두 RML은 XML 파싱/중복ID/새 toggle ID 존재를 확인했다. 공통 GraphicsToggle의 표시/이벤트/저장 루프에 연결됨을 소스로 검토했다. v1..7은 새 값 기본ON, v8은 boolean 필수, 저장v8 경로를 소스로 검토했다. 실제 UI 토글/저장 상호작용은 수행하지 않았다.
- 실제 사용자 settings(schema7)/worldgen 사본을 build/release/bin에만 두고 --world --render-distance12 --frames600 --capture로 정상 실행했다. 외부 Khronos synchronization validation 활성화,299컬럼/166표시청크, exit0. 실제 Validation Error/Warning/VUID0, 엔진 Vulkan/UI/폰트/텍스처 오류0. 로그/캡처는 celestial-saved.log/png이며 이미지를 읽어서 기존 구름/지형/하늘 출력이 유지됨을 확인했다. 고정 시점에 천체 원반은 보이지 않는다.
- 프레임 제한 실행은 시작06시와 카메라가 고정이므로 밤의 별/달, 태양 원반의 실제 모양·이동·물 반사·구름 뒤 가림을 직접 시각 검증하지 못했다. 방향/깊이/합성순서는 소스 검토했다. 원본 게임과 화면 비교/픽셀 동일/성능 개선을 입증하는 검증이 아니다. 자동 테스트/CTest/합성 입력/컴퓨터 유즈는 추가·수행하지 않았다.
- 임시 settings/worldgen은 finally에서 제거했고 사용자 원본은 보존했다. 작업 전후 settings SHA256 `97743CA8743D1ABF458EDA809B42BEF80E7FEC55475131F8339403481FBFB5F9`, worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D` 일치.
- 배포 exe SHA256 `64207CA8BDF9BCA175DF45162003224537FCAF29825BE1A27B40D9A9BF16C569` (8,069,632bytes), 빌드exe/17개shader와일치. source→build→package UI/출처 및 루트README 복사 일치를 확인했다.


## Complementary 구름 이식 검증 (2026-09-23)

- 사용자 `구름 실시` 승인 범위의 밀도·형상·두께·음영·물 반사를 반영하고 Release 증분 빌드 및 패키징을 완료했다. build/release/complementary-cloud-build.log의22단계가 성공했으며 컴파일 경고/오류가 없다. 수정 C++2파일의 clang-format --dry-run --Werror와17개 SPIR-V의 spirv-val --target-env vulkan1.4를 통과했다.
- 일반 CLI --world/--render-distance12/--frames600/--capture 실행으로 세 경로를 확인했다. 외부 Khronos synchronization validation을 활성화했고 각 실행은 exit0, 실제 Validation Error/Warning/VUID0, 엔진 Vulkan/UI/폰트/텍스처 오류0이었다. validation 시작 INFO에 포함된 WARNING-CreateInstance-status-message 문자열은 실제 경고가 아니다.
- 사용자 settings/worldgen 사본: 품질2/높이640/양79/TAA OFF,298컬럼/166표시청크. complementary-cloud-saved.log/png에서 아래쪽 시점의 구름을 확인했다.
- 기존 생성 편집으로 가능한 임시 높이맵 설정: terrain/spline/shape OFF, groundness 간격128/4옥타브/가중치1,.5,.25,.125, 높이곡선[-1,80],[0,350],[1,450]. 품질3/높이320/양60/TAA ON,287컬럼/267표시청크. complementary-cloud-above.log/png에서 구름 위 시점과 전경 지형 가림을 확인했다.
- 같은 임시 높이맵의 곡선[-1,80],[0,180],[1,310], 품질1/높이320/양50/TAA OFF,297컬럼/303표시청크. complementary-cloud-water.log/png에서 아래쪽 구름과 수면 반사 경로를 확인했다. 세 캡처 모두 이미지로 직접 읽었다. TAA OFF 정지 캡처에는 표본 디더가 남을 수 있다.
- 임시 settings/worldgen은 build/release/bin에만 생성하고 실행 후 finally에서 제거했다. 사용자 파일은 변경하지 않았다. 자동 테스트/CTest/검사용 코드/합성 입력/컴퓨터 유즈는 사용하지 않았다. 제한 프레임 CLI는 시각/카메라가 고정이므로 애니메이션·이동 중 구름 관통·시간 변화·실제 월드 경계 통과·F4/리사이즈를 수동 확인한 것은 아니다. 원본 게임과 픽셀 비교나 성능 벤치마크도 아니다.
- 배포 exe SHA256 `784F278B2762282856DEC557840CAC981AF2DAB3ABC53A2E597E29BBC94E83D7` (8,069,120bytes), 빌드 exe 및17개 shader 해시가 배포와 일치한다. 양쪽 RML 구문/중복ID 검사와 source→build→package의 UI/출처 파일 일치를 확인했다.
- 사용자 settings SHA256 `D0114D58CF30255B06429D0C76FDA9DC0B40D0110F60DFFFBE9E874658A6C38A`, worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`가 작업 전후 일치한다.


## Complementary 물 전체 이식 검증 (2026-09-23)

- 사용자 `싹 맞춰봐 실시` 승인으로 water material/normal/parallax/GGX/SSR/거품/수중반사/합성후굴절을이식했다. 최신실시규칙에따라 Release 증분 configure/compile/link 및tools/package.ps1 완료. 빌드로그는build/release/complementary-water-build.log이며컴파일경고·오류없음. 공용fullscreen.vert참조를전부대조했고기존water_hiz shader와C++resource/pipeline참조는제거했다.
- 수정C++6파일의clang-format --dry-run --Werror,17개현재SPIR-V의spirv-val --target-env vulkan1.4 통과. 배포17개shader해시는빌드결과와일치. 양쪽RML구문/중복ID/거품표시문구검사통과.
- 정상CLI실행/외부Khronos synchronization validation: 저장된사용자설정사본+월드설정사본으로600프레임/반경12,299컬럼/166청크정상종료(complementary-water-saved.log). 해당사용자설정의TAA OFF/VSync ON 경로가실행됐다.
- 물표면이보이는일반월드설정(기존생성편집으로가능한groundness높이모드,shape/terrain/splines OFF,curve[-1,80],[0,180],[1,310],groundness간격128/4octaves)을빌드폴더에만임시저장하여실행했다. 기본그래픽/물모두ON,600프레임/반경12에서296컬럼/230청크정상종료(complementary-water-surface.log/png). 캡처에서넓은수면·물결·지형반사/물가를읽어서확인했다. 처음임시설정은spacing7에9octaves를유지해정상유효성검사에서거부되었고4octaves로고친뒤최종로그/캡처를갱신했다. 소스/사용자기본지형은변경하지않았다.
- SSR/구름/그림자/대기/안개/광선/블룸/TAA OFF,물depth/waves/refraction만ON인조합도600프레임/반경8로정상실행(197컬럼/184청크,complementary-water-refraction.log/png). 굴절단독최종합성경로와반사미생성descriptor경로에validation오류없음. 임시settings/worldgen은finally에서삭제했고현재빌드폴더에남아있지않다.
- 위세최종실행의외부validation Error/Warning/VUID 0,엔진Vulkan/폰트UI/텍스처오류0. 자동테스트/CTest/새검사용소스/합성키입력/컴퓨터유즈는사용하지않았다. 프레임제한CLI는시각/카메라고정이므로수중진입·애니메이션움직임·근접물가·낮GGX/시간변화·옵션상호작용·F4/resize의수동시각검증은하지않았다. 원본Minecraft와화면비교/픽셀일치/성능개선의증거로이기록을해석하지않는다.
- 배포exe SHA256 `9236C31643F4D8E54BDADE4A941855ED6DCC0A4E267386E1689420FE7D80596F` (8,069,120bytes),빌드exe와일치. 사용자settings `FDE84C715042F0FAA5364F2C58D1E40D7512D17876772F11172888CBA2BF7D97`,worldgen `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`는작업전후일치한다. sourceassets→buildassets→package의UI/원본텍스처/출처파일복사를확인했다.


## Complementary 조명 이식·Release 빌드 (2026-09-23)

- 사용자 `실시` 승인 범위로 원본 조명/왜곡 그림자/Scene Aware 갓레이/수중 처리/하늘·안개·구름 조명/블룸·톤/TAA를 연결했다. 현재 대응과 한계는 graphics.md 최신 절을 따른다. 기존 그림자20Hz/cascade와 롤백된 자체 갓레이 보강은 현재 경로가 아니다.
- `tools/build.ps1 -Preset release` 증분 configure/GLSL·C++ 컴파일/링크 성공. 마지막 로그 `build/release/complementary-port-build.log`에 컴파일 warning/error 없음. 선택 의존성 탐색 안내는 이전과 동일하다. 수정 C++/헤더5개 clang-format --dry-run --Werror 통과.18개 SPIR-V 모두 spirv-val --target-env vulkan1.4 통과. RML 양쪽18개 checkbox/18개 option-text,중복ID0을 읽기 검토했다.
- 일반 월드 실행 `--world --render-distance 12 --frames 600`: 정상 종료0,299컬럼/표시164청크. `complementary-port-final.log` 및 자체 PNG 캡처를 확인했다. SDK1.4.341.1 외부 Khronos validation + synchronization validation 활성화를 확인했으며 Validation Error/Warning/VUID/SYNC-HAZARD0,UI issues0/texture failures0이다. Release 내장validation=0 카운터와 별도로 외부 로그를 검색했다.
- 효과OFF 경로: 사용자 파일을 복사한 일시적인 build/release/bin/settings.json에서 그래픽/물/TAA를 모두OFF로 두고 일반 실행4컬럼·240프레임을 확인했다. 정상종료0,49컬럼,외부 validation 오류/경고0,UI/texture0. 임시 설정은 종료 후 제거했고 기존 사용자 설정은 수정하지 않았다. 테스트 소스/CTest/합성 입력을 추가하지 않았다.
- `tools/package.ps1` 완료 후 배포 exe로 기존 사용자 schema6/settings/worldgen을 읽는4컬럼·240프레임 일반 실행 확인. 정상종료0,49컬럼,외부 validation 오류/경고0,UI/texture0. `complementary-port-disabled.log`, `complementary-port-packaged.log`에 기록했다. 여러 실행의 마지막 프레임GPU시간은 장면/로딩 상태가 달라 성능 비교 수치로 쓰지 않는다.
- 빌드/배포 exe SHA256 모두 `358C6ADE8D85F3E69FE2F7E399352F689E673E328A122A32762D6C6C3EE341AC`,18개SPIR-V 배포 불일치0. 작업 시작부터 패키징·배포 실행 후까지 out/Sandbox/settings.json `B4758D87415731F33A20AF4D8776A4EE60B4922FE66D8E55958EDFD4A0C7FD6C`,worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D` 동일.
- 컴퓨터 유즈/UI 자동 입력은 사용하지 않았다. CLI 자체 캡처 이미지만 읽었다. 프레임 제한 일반 실행은 시작06시·정지 카메라이므로 실제 태양 이동·카메라 이동 중 TAA 안정성,수중 진입/전환,옵션 클릭/F4/리사이즈,장면별 갓레이 외형은 직접 조작 검증하지 못했다. 원본 Minecraft와 동일 장면 비교도 수행하지 않았으므로 픽셀 동일/모든 떨림 해결을 주장하지 않는다. 사용자 시각 확인과 제보에 따른 후속 조정 대상으로 구분한다.

## 갓레이 보강 롤백·Release 빌드 (2026-09-22)

- 사용자 `방금 한 위 작업 롤백실시`로 직전 갓레이4종을 모두 되돌렸다.8/12/20 균일 표본, 카메라 하늘빛에 의한 계산 조건/전체 감쇠, 경계 구름 전용 fallback, 경로길이×0.00065 산란식을 복구했다. 공기 전용 비교/근거리 집중/공통 scene_volume 함수는 제거했다. 옵션 글자 수정과 그림자20Hz·해상도/표면PCF는 유지한다.
- 최신 사용자 방침(`실시`에 자동 빌드 포함, 명시적 생략만 예외)을 AGENTS.md에 반영했다. tools/build.ps1 -Preset release 증분 configure/셰이더·C++ 컴파일/링크가 성공했고 컴파일 경고·오류는 없다. 선택 의존성 PkgConfig/LibUSB/rocprofiler-sdk 미발견 안내는 기존과 같다.
- 수정 C++의 clang-format --dry-run --Werror와14개 SPIR-V의 spirv-val --target-env vulkan1.4 검사가 통과했다. 소스에서 추가 함수 제거 및 기존 품질값/감쇠/합성 경로 복원을 확인했다. 이번 롤백에서는 게임 실행·Vulkan 런타임 검증·자동테스트·컴퓨터 유즈는 하지 않았다.
- tools/package.ps1 완료. build/release/bin과out/Sandbox 실행 파일 SHA256은 `6C47D4AF6BCA1BCD9C4496872CC1F875460E6F736FA2D6BE2F910495A8B618FC`로 일치한다(8,057,856bytes).14개 shader도 빌드/배포 간 일치한다. 패키징 직전/직후 settings.json `B4758D87415731F33A20AF4D8776A4EE60B4922FE66D8E55958EDFD4A0C7FD6C`, worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`는 동일하며 사용자 설정을 직접 수정하지 않았다.
- 아래 갓레이 보강 기록은 취소된 구현 이력이며 현재 코드의 기능이나 미완료 작업으로 해석하지 않는다.

## 근거리 갓레이 가림 보강 (2026-09-22, 미빌드 후 롤백된 이력)

- 사용자 제보와 후속 `실시` 범위로 shaders/environment.glsl·volume.frag·atmosphere.frag 및 SceneEffects의 품질별 표본 수를 수정했다. 카메라 하늘빛의 전체 감쇠를 제거하고 공간 표본의 가림을 사용한다. 첫24블록의 간격은 낮음1.6/보통0.96/높음0.6이며 이후 제곱 분포다. 산란계수0.0012/거리 감쇠0.004 및 구간별 적분 가중치를 사용한다.
- 소스 대조: 반해상도 주 패스와 원본 해상도 경계 fallback 모두 scene_volume을 호출하며 후자는 구름8표본과 같은 품질의 빛줄기를 포함한다. 가장 가까운 표면에서 최종 구간을 잘라 뒤쪽까지 적분하지 않는다. shadow volume 밖/직접광0/효과OFF의 조기반환과 원거리 fade0, 근거리/원거리 cascade 혼합 및 기존 sky/fog/underwater 경로를 검토했다. UBO·descriptor·이미지 할당/설정 schema 변경은 없다.
- 공기 표본의2×2 그림자 조회와 지형·플레이어의4×4 PCF를 분리했다. 최대24/40/64표본이며 단일cascade 깊이 조회 예산96/160/256회, 혼합 구간은 해당 표본의2배다. 일반 구름 그림자 조회는 광선 중앙1회로 유지한다. 이는 소스상 예산이며 GPU 시간 개선을 측정한 것은 아니다.
- 수정 C++의 clang-format --dry-run --Werror 통과. GLSL 호출/정의 순서, 품질값24/40/64 전달, 기존CMake environment.glsl 의존성을 읽기로 확인했다. GLSL/C++ 컴파일·게임실행·Vulkan 검증·패키징·자동테스트는 하지 않았다. 사용자 금지에 따라 컴퓨터 유즈도 하지 않았다.
- out/Sandbox 실행파일 SHA256 `5DEAAC494C1439142E9B0CAE1D71A2F6E2E6299A3286B3FD1777F056F04DD50A`, settings.json `95B80B1F98BE53E342F58C696F2306D631BBD7B02A3A4FF526D70C73D6367AD6`, worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`는 작업 전후 동일하다. 기존 실행파일에는 이번 갓레이 변경이 없다.
- 다음 명시적 빌드 시 shader 컴파일 및 실제 근거리 블록/동굴 입구/윤곽 경계/빛줄기·그림자OFF/F4/구름 조합과 GPU 비용 확인이 남는다. 원거리 얇은 그림자 누락, 반해상도 한계,20Hz 가시성 변화의 외형은 이번 소스 검토만으로 해결을 보장하지 않는다.

## 옵션 글자·그림자20Hz 및 Release 빌드 (2026-09-22)

- 사용자 `실시하고 빌드까지 해` 승인으로 양쪽 옵션 체크박스 글자를 span flex 자식으로 감싸고, 그림자 캐시를50ms 주기로 변경했다. 이전 시간축 가시성 필터·RG32F history·MRT·물 shader 변형은 제거했다. 기본 근거리64×64/4096²·원거리512×512/2048², PCF/좌표안정화/schema6은 유지한다. UBO352bytes, 프레임당9sets/34samplers, 모든 프레임이 공유하는 D32 한 쌍(기본80MiB)을 대조했다.
- `tools/build.ps1 -Preset release` 증분 configure/GLSL·C++ 컴파일/링크가 성공했다. 컴파일 경고/오류는 없으며 선택 의존성 PkgConfig/LibUSB/rocprofiler-sdk 미발견 안내는 기존과 같다. 수정 C++/헤더7개의 clang-format --dry-run --Werror 및14개 SPIR-V의 spirv-val --target-env vulkan1.4 검사가 통과했다. 자동테스트/CTest/합성 입력 경로는 추가하거나 실행하지 않았다.
- RML 파싱에서 양쪽 각각17개 체크박스/17개 텍스트 wrapper, main73/world81개 ID의 유일성과 label 연결을 확인했다. computer-use로 시작 화면만 관찰한 뒤 사용자가 물리 Escape로 중단했고 `컴퓨터유즈는하지마`라고 명시했다. 이후 UI 자동 조작은 하지 않았다. 옵션 페이지의 글자 표시·줄바꿈·실제 클릭 동작은 화면으로 확인하지 못했다.
- 일반 실행 경로 `--world --render-distance 4 --frames 600`을 Windows UI 자동 조작 없이 실행하여 정상 종료코드0,49컬럼 로딩,UI issues0/texture failures0을 확인했다. SDK1.4.341.1의 외부 Khronos validation과 synchronization 검증 활성화를 로그에서 확인했다. shadow-20hz-world.log와시작메뉴 shadow-20hz-runtime.log에서 Validation Error/Warning은 각각0개다. Release 자체 validation=0 카운터와 외부 검증 결과를 구분한다.
- 월드 실행은 시작위치/06시 정지 상태다. 공유맵의 반복 갱신·읽기/쓰기 barrier 경로는 실행됐지만 태양이 실제 이동하는 동안의 시각적 떨림·카메라 이동·편집·F4/품질 전환은 직접 확인하지 못했다. 이 기록은20Hz가 모든 떨림을 제거했다는 증거나 GPU 성능 측정이 아니다.
- tools/package.ps1 완료. build/release/bin과out/Sandbox의 exe8,057,856bytes는 SHA256 `5DEAAC494C1439142E9B0CAE1D71A2F6E2E6299A3286B3FD1777F056F04DD50A`로 일치한다.14개 SPIR-V·양쪽RML·options.rcss·README도 일치한다. 패키징 직전/직후 사용자 settings.json `2F97F80B763D0B15DA7B26E8514D566A757455A83E217B70E2E18D8659D665DC`, worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`가 동일하다. 작업 중 사용자가 실행한 게임에서 변경된 설정은 현재값 그대로 보존했고 설정 파일을 직접 수정하지 않았다.

## 그림자 구간 분리·시간축 안정화 (2026-09-21, 소스 반영·미빌드)

- 사용자 태양 방향 고정 시 떨림 소멸 확인 뒤 근거리64×64/4096²·원거리512×512/2048²와 시간축 필터링 제안에 `실시`를 받았다. 각 구간 독립D32·snap, 경계혼합, 기존 수신면 PCF의 깊이범위 한정과 지형/플레이어 가시성 이력을 적용했다. 구름/물/수중 빛무늬·빛기둥은 이번 수정 범위가 아니다.
- 소스 검토: CPU/GLSL 환경 UBO512bytes의 기존offset 보존과 추가행렬352/416·이동480·시간축496을 대조했다. 환경 sampler3개와 프레임당 descriptor9sets/35samplers, 두 해상도의 viewport/texel 계산, RG32F MRT/법선ID/거리 검증, 두 프레임history 비중첩 읽기·쓰기 및 해제 경로를 확인했다. GPU validation을 실행한 것은 아니다.
- 시간축 이력은 단일 불투명 패스에 함께 기록한다. 현재깊이·법선/플레이어 구분/이전4픽셀의 수신면 거리, PCF 범위클램프와80ms 응답을 사용한다. 스트리밍/편집/재생성/빠른 시각변화/슬롯 불연속 등은 이력을 무효화한다. 모든PCF 표본이 같은 곳은 history 조회를 생략한다. 시간누적이 실제떨림을 완전히 없앤다고 검증하지 않았다.
- C++/헤더7개 clang-format --dry-run --Werror 통과. main/world RML을 XML로 읽어73/81개 ID 중복0 및 label 미연결0, 그림자 반경 입력4개 기본256을 확인했다. CMake shader 산출물은15개이며 world_water.frag.spv는 world.frag의WATER_PASS 변형이다. 의존성/로딩명/패키징의 전체shaders 복사 경로를 소스로 확인했다.
- settings schema6은 기존파일의 그림자 반경/품질만256/2로 메모리 이행하며 다른 옵션을 보존한다. 사용자파일은 쓰지 않았다. 확인한 out/Sandbox/settings.json SHA256 `96ECF6BDB9C0F851E0DD8947F67B37A7029A6BF56CF30F0D0E80DC49FBEA487F`, worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`는 검토 중 동일하다. 기존 배포exe도 `1A53201BC94EEBA337843C43D213F6364DDFAC8F2CFF918636508CC5BA30C152` 그대로다.
- 빌드/GLSL·C++ 컴파일/게임실행/패키징/자동테스트는 하지 않았다. 다음 명시적 빌드 요청 때 컴파일·Vulkan core/sync검증과구름0/태양이동 정지시점·이동/F5·시각조절·편집·로딩·화면크기/품질 변경의 외형/잔상/비용을 확인해야 한다. 기존06시 고정시점의 이전 GPU검증 기록은 이번 변경의 증거가 아니다.

## 물가·수중 효과·지형 그림자 보정 빌드 (2026-09-21 08:08 KST)

- 사용자 `싹 실시하고 빌드까지 실시` 승인 범위로 물가 색, 실제 물 접촉 지형 면의 공통 빛무늬, 수중 안개/개별 옵션(schema5), 그림자 비교/필터를 변경했다. 메싱 bit28/면4bytes, 환경 UBO352bytes/물 UBO224bytes를 소스와 static_assert로 대조했다. 상세는 [graphics.md](graphics.md).
- Release 증분 빌드의 GLSL/C++ 컴파일·링크가 성공했다. 최초 Vulkan 검증 실행에서 swapchain acquire 뒤 layout 전환의 WRITE_AFTER_READ와 물 shader의 DemoteToHelperInvocation 미활성화를 발견했다. acquire wait와 전환 srcStage를COLOR_ATTACHMENT_OUTPUT으로 연결하고 필요한 코어 기능을 장치 선택/생성에서 확인·활성화한 뒤 증분 재빌드했다. 최종 컴파일 경고/오류는 없다. configure의 선택 의존성 미발견 메시지는 기존과 같았다.
- C++/헤더11개 clang-format --dry-run --Werror,14개 SPIR-V의 spirv-val --target-env vulkan1.4 검사가 통과했다. RML XML 조회에서 main73/world81개 ID의 중복 없음,8개 물 제어항목과label 연결을 확인했다. 자동 테스트 파일/CTest/합성 입력은 추가하거나 실행하지 않았다.
- 최종 Release 실행은 `--world --render-distance 4 --debug-ui --frames 600 --capture .../build/release/water-shadow-review.png`로 완료됐다. 정상 게임 경로의49컬럼 로딩, UI issues0/texture failures0과 저장 화면의 기본 월드·물·구름·HUD를 확인했다. 그림자 품질/효과는 배포 사용자 설정이 아닌 기본 설정이었다.
- SDK1.4.341.1의 VK_LAYER_KHRONOS_validation을 이 실행의 환경변수로 활성화했고, build/release/validation-settings/vk_layer_settings.txt로 core와synchronization 검증 로그를stdout에 받았다. 최종 water-shadow-validation-final.log는 layer active/synchronization active를 명시하며 Validation Error/Warning 항목이 각각0개다. Release 앱의 validation=0 및 자체 카운터는 외부 layer 상태를 반영하지 않으므로 검증 증거는 외부 로그다. 최초 실패 로그는 water-shadow-validation.log에 남겼다.
- 이 실행의 화면은 시작 위치/06시 정지 상태다. 실제 수중 렌더 경로·수면 출입/3인칭·개별 옵션 조합, 낮 시간에 구름0으로 정지한 장면의 그림자 출렁임 개선 정도, 움직이는 빛무늬 외형과 GPU 비용은 아직 직접 확인하지 못했다. 코드를 임시 변경하거나 자동 입력 경로를 추가해 이를 확인한 것으로 보고하지 않는다. 단일 GPU 시간/FPS는 성능 측정값으로 사용하지 않는다.
- tools/package.ps1 완료 후 build/release/bin과out/Sandbox 실행 파일8,056,320bytes의 SHA256은 `1A53201BC94EEBA337843C43D213F6364DDFAC8F2CFF918636508CC5BA30C152`로 일치했다.14개 SPIR-V, 양쪽UI RML과README도 원본과 일치했다. 기존 사용자 settings.json `2B29F3934D86D5A1BFDC0877E8FFB5F2098EDE53942EF7D710BA350897072305`, worldgen.json `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`는 작업 전후 동일하다.

## 반사 교차·구름 부피·수중 굴절 및 Release 빌드 (2026-09-21 07:51 KST)

- 사용자 `실시하고 빌드까지 실시` 승인으로 SSR 면 교차/고립 누락 보정, 구름의 3D cellular 덩어리·워핑·지역별 두께와 음영, 실제 카메라 수중 판정과 물 밖 장면 굴절을 반영했다. 상세는 [graphics.md](graphics.md). 아래 미빌드로 기록된 그림자·옵션·구름 가림·75% 반사 해상도 변경도 이번 빌드에 포함된다.
- 변경 C++/헤더6개에 clang-format을 적용하고 `--dry-run --Werror`를 통과했다. `tools/build.ps1 -Preset release`의 증분 configure/GLSL·C++ 컴파일/링크와 `tools/package.ps1`이 종료 코드0으로 완료됐다. 컴파일 경고·오류는 없었다. configure의 선택 의존성 PkgConfig/LibUSB/rocprofiler-sdk 탐색 메시지는 빌드를 막지 않았다. 자동 테스트/CTest는 추가하거나 실행하지 않았다.
- build/release/bin 및 out/Sandbox 실행 파일은8,054,272bytes이며 SHA256 `526897E99DB10E105E1CAA28BD581913834909582011310032E82E303B50F0A8`로 일치한다.14개 SPIR-V도 모두 일치한다. 패키징 전후 사용자 settings.json의 SHA256은 `F651F8DF523C305B7FF637FE33EB59BB37420BC6190D2A553EB727DBE3101FCB`, worldgen.json은 `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`로 동일하다. 이전 기록 이후 사용자가 바꾼 설정을 이번 빌드 기준값으로 보존했다.
- 정상 실행 경로로 `sandbox.exe --world --render-distance 4 --debug-ui --frames 120 --capture C:/Users/PC/Desktop/YD_Unity/sandbox/build/release/graphics-review.png`를 실행했다. RTX3080/Vulkan1.4/IMMEDIATE에서 종료 코드0,120프레임,49컬럼 로딩, UI issues0/texture failures0을 확인했고 저장 화면을 열어 기본 월드·하늘·구름·HUD 렌더링을 확인했다. Release의 validation은OFF였으므로 로그의 Vulkan errors/warnings0을 validation 통과로 해석하지 않는다.
- 이 짧은 실행은 시작 위치의 화면 확인이며 반사 점 감소 정도, 수중 진입/이탈·3인칭 카메라·굴절 옵션 조합, 구름 위/아래·이동 시 외형, 그림자 안정성과 창 크기 변경은 아직 수동 확인 대상이다. 출력된 단일 GPU 시간이나 시작 직후 FPS를 성능 측정으로 사용하지 않는다. 화면 밖 SSR 한계는 유지하며 새 구름의 실제 GPU 비용은 미측정이다.

## 물 SSR 75% 해상도 (2026-09-21, 소스 반영·미빌드)

- 사용자 `실시`로 반사 색/metadata/깊이 버퍼와 viewport/scissor/UBO를 축별ceil(3×화면크기/4)로 맞췄다. 1920×1080→1440×810,2560×1440→1920×1080,3840×2160→2880×1620이며 비정수 크기는 올림한다. 중간 정수 곱 오버플로를 피하는 dimension-dimension/4 식을 사용한다.
- 소스에서 공유 reflection_extent_의 생성3개/viewport/scissor/UBO 전달과 셰이더 sizes.zw 사용 경로를 검토했다. 전체 해상도 water_footprint의 고정2배를 실제X/Y 비율로 바꿨고 SSR 자체는1배를 유지한다. 장면 snapshot/Hi-Z 및 구름·빛줄기/블룸 경로는 수정하지 않았다.
- C++/헤더2개 clang-format --dry-run --Werror를 통과했다. 빌드/컴파일/실행/자동 테스트/패키징은 하지 않았고 out/Sandbox도 수정하지 않았다. 이전 그래픽 보정과 함께 실제 외형·GPU 비용·창 크기 변경 확인이 남아 있다.

## 그림자·구름·SSR·옵션 보정 (2026-09-21, 소스 반영·미빌드)

- 사용자 `ㅇㅋ 싹 실시` 범위로 그림자/구름/단축키/넓은 옵션/물 가림/SSR 점 보정을 소스와 문서에 반영했다. 상세 구조는 [graphics.md](graphics.md). 아래06:59 빌드는 이 후속 수정을 포함하지 않는다.
- 변경 C++/헤더7개에 clang-format을 적용하고 `--dry-run --Werror`를 통과했다. RML XML 파싱에서 main72개/world80개 ID가 고유하고 label 연결,5개 분류, 기존 제어항목, settings.hpp의7스위치/8정수범위와 입력쌍을 대조했다. C++4개와 변경 GLSL6개의 구분자도 읽기 전용으로 확인했다. 이는 컴파일/동작 검증이 아니며 자동 테스트 파일/CTest를 추가하지 않았다.
- 코드 검토: 프레임별 descriptor 수(장면9sets/33samplers, 물5samplers+Hi-Z), UBO크기320/224 유지, SSR MRT2개/metadata binding5 생성·전환·해제, 사전/사후 합성set 분리, 물 색 이후 depth-only의 LOAD/STORE와 구름 깊이 클리핑, volumeOFF/F4 우회와 최종 bloom/tone 입력을 대조했다. 같은 프레임에서 이미 사용한 descriptor를 바꿔 끼우지 않는다.
- 입력/저장 검토: Esc 전체옵션→일시정지→게임, 시작 메뉴 옵션→메뉴, 옵션 중 입력 차단, 구름 키 단일/반복/동시 중립/키해제 latch/종료 flush,0..100 clamp, CLI 거리값 보존을 소스로 확인했다. 구름 키를 누르는 매 프레임 디스크 저장을 하지 않는다.
- out/Sandbox/settings.json SHA256 A61EB8EF6D57FBB2E9CAB6020C6D992584D922BBE36F63585BE28A29C77FEEB6 및 worldgen.json 964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D는 직전 기록과 동일하다. 배포 실행 파일·셰이더·에셋도 이번에 수정하지 않았다.
- configure/빌드/C++·GLSL 컴파일/게임 실행/패키징/자동 테스트는 수행하지 않았다. 다음 명시적 승인 뒤 빌드와 Vulkan validation, 낮/밤·이동·기준점 전환 시 그림자 떨림, 구름 위/아래와 고체·수면 가림, 물가·비껴보기·반사 경계·수중·F4, 구름0/100·키 반복·저장복원·메뉴 전환·창 크기/DPI를 수동 확인해야 한다. 개선 정도와 GPU 비용은 미측정이며 SSR 화면 밖 지형/구름 적분의 한계는 남는다.

## 그래픽 통합 Release 빌드 (2026-09-21 06:59 KST)

- 사용자 `빌드 실시` 승인으로 Release 증분 빌드와 `tools/package.ps1`을 실행했다. 새 GLSL을 포함한 셰이더 컴파일과 C++ 컴파일/링크가 성공했다.
- 최초 컴파일에서 scene_effects.cpp의 VkImageAspectFlagBits 조건식이 VkImageAspectFlags 초기화 목록으로 축소 변환되는 오류2곳을 발견했다. 명시된 VkImageAspectFlags 지역 변수로 전달하도록 수정하고 clang-format 검사 후 다시 빌드했다. 최종 컴파일/링크에는 경고·오류가 없었다. configure의 선택 의존성 PkgConfig/LibUSB/rocprofiler 탐색 메시지는 빌드를 막지 않았다.
- out/Sandbox/sandbox.exe(8,035,840bytes)와14개 SPIR-V가 build/release/bin 결과와 SHA-256 일치하는지 확인했다. 기존 out/Sandbox/settings.json 및 worldgen.json도 빌드 전후 SHA-256이 일치한다.
- 게임 실행·화면 확인·GPU validation·성능 측정·자동 테스트는 하지 않았다. 아래 미빌드 기록은 당시 이력이며, 이번 기록은 컴파일/링크·패키징만 갱신한다.

## HDR와 그래픽 옵션 통합 (2026-09-21, 미빌드)

- 사용자 `실시` 범위의 그래픽 통합을 소스에 반영했다. 구현과 알려진 한계는 [graphics.md](graphics.md). configure/C++·GLSL 컴파일/빌드/게임 실행/자동 테스트/패키징은 하지 않았다.
- 변경 C++/헤더11개에 clang-format을 적용하고 `--dry-run --Werror`가 통과했다. 이는 형식 검사이며 컴파일 결과가 아니다.
- 두 RML을 XML로 읽어 중복ID, label 연결,7개의 그래픽 스위치/8개 정수 입력쌍/3개 새 물 효과의 기본ON·이름을 settings 표와 대조했다. CMake에 등록된 셰이더 파일과 공유 include 경로/구분자 개수를 확인했다. 임시 조회로만 수행했으며 테스트 소스/CTest를 추가하지 않았다.
- CPU/GLSL uniform의320bytes 환경(flags256)/224bytes 물(extras192,trace208), descriptor set 배치와 pool 개수, HDR attachment/snapshot 형식, 프레임별 리소스/resize와 종료, shadow→sample·depth→sample→depth·water snapshot·bloom→tone의 레이아웃과 바인딩을 소스로 검토했다. 업로드 timestamp가 shadow를 포함하지 않도록 분리했고 반복 begin_rendering에서도 한 번만 기록한다.
- 실제GPU validation, 새 셰이더 컴파일 성공 여부, 낮/밤 외형·그림자 접점·구름 경계·물 경계, 재시작 설정복원, F4/F2/F11/해상도 변경, 효과ON/OFF 조합과 프레임/VRAM 비용은 아직 실행 검증하지 않았다. 다음 명시적으로 승인된 빌드/실행에서 확인할 대상이다. 사용자 배포 설정 파일과 실행 파일에 쓰기 작업은 하지 않았다.

## 물 반사 왜곡·거리·블록 접점 보정 (2026-09-21, 미빌드)

- 사용자 `실시`로 반사 방향 노멀30%, 렌더 거리 연동16..1024블록/Hi-Z 최대128회 탐색, 깊이·수면 평면에 따른4탭 보간과 경계만 원래 해상도 추적을 소스에 반영했다. 고체/물 접점 대상이며 메시/물 높이·Fresnel/흡수·옵션과 저장 설정은 변경하지 않았다.
- 변경 C++/헤더3개를 clang-format 적용 후 `--dry-run --Werror`로 확인했다. CMake의 새2개 셰이더와 공유 include 의존성, 실행 시 shader 경로, 기존 패키징의 shaders 디렉토리 복사를 읽어 대조했다. 소스 검토이며 컴파일 검증이 아니다.
- CPU/GLSL UBO208bytes/trace offset192, mip 생성 개수와 descriptor pool 용량, 단일 mip 입출력 view, 단계별 color-write→fragment-read barrier, frame slot 재사용/resize/종료 해제, 물 렌더 시작 시 descriptor/push/viewport 복원을 소스로 검토했다. 홀수 크기 마지막 행/열의 포함과 광선 clip/셀 경계 진행/128회 한도, signed-distance 평면 검사/부족한 샘플 보충, SSR OFF 경로도 읽어 확인했다.
- configure/C++·GLSL 컴파일/빌드/게임 실행/자동 테스트/패키징은 수행하지 않았다. 실제 반사 거리·접점 외형·깜빡임·추가 GPU 시간은 미검증이다. 다음 승인된 빌드/실행에서 가까운 블록 접점, 비스듬한 먼 수면, 창 크기/F11/홀수 해상도, 렌더 거리/옵션 전환과 Vulkan validation을 확인해야 한다. 현재 실행 파일에는 이번 변경이 반영되지 않았다.

## 비스듬한 시선의 물 반사 강화 (2026-09-21, 미빌드)

- 사용자 `실시`로 water.frag의 반사 곡선 지수5→3만 조정했다. F0=.02, 깊이 흡수, SSR 추적/보간, 옵션과 저장 파일은 유지했다. 소스의 해당 식과 전후 합성 경로를 읽어 확인했으며 configure/셰이더 컴파일/빌드/실행/자동 테스트/패키징은 하지 않았다. 기존 실행 파일의 반사 곡선은 다음 빌드 전까지 그대로다.

## 물 높이·깊이/잔물결/SSR 옵션 (2026-09-21, 미빌드)

- 사용자 `실시` 승인 범위로 물 메시 bit27, 공용 정점 셰이더의7/8 높이, 물 전용 snapshot/반사/합성 경로와 옵션 저장을 소스에 반영했다. 이전 Release 실행 파일/패키지와 사용자 worldgen/settings는 수정하지 않았다.
- 변경 C++/헤더11개를 clang-format 적용 후 `--dry-run --Werror` 검사했다. 두 RML을 XML로 읽어 main27개/world32개 ID가 각각 고유하며 네 물 옵션 ID가 모두 존재함을 확인했다. CMake의 새 cpp/두 frag/shared include 의존성과 UI 이벤트 등록·제거·설정 적용 경로를 대조했다. 이는 소스/문서 구조 확인이며 컴파일 검증은 아니다.
- 소스 검토: bit27은 water+위쪽air에서만 세팅하며 기존AO/밝기 비트와 겹치지 않는다. 아래쪽 꼭짓점은 유지하고 윗면/옆면 상단은 동일하게 낮춘다. F4도 같은 정점 경로다. settings v1/v2 기본ON, v3 네boolean 검증, 전체OFF 때 하위값 보존·재활성, 메뉴/인게임 공유·자동 저장 및 명시 render-distance 보존을 확인했다.
- GPU 경로를 소스로 검토했다: dynamic rendering 종료 후 색/깊이 복사와 레이아웃 전환, 원래 깊이 보존/LOAD, 반사 패스의 독립 깊이, 불투명 가림, set0/1과push 호환, set2의192byte uniform/std140 일치 확인용 static_assert, frame_slot별 이미지/UBO와 flush, resize 시 idle/descriptor 재생성, 종료 시 face layout보다 먼저 효과 해제. 실제 Vulkan validation을 실행하지는 않았다.
- 전체OFF/모든하위OFF/F4/보이는물없음에서 효과 패스를 건너뛰며 잔물결만ON일 때 장면 복사가 없는 것을 분기로 확인했다. 반사는 절반 가로·세로 해상도/최대48단계/첫교차6회세분화/96블록 한도로 구현했다. 화면 밖·불안정 교차의 하늘 fallback, 거리 기반4탭 합성, 수면 뒷면의 흡수 거리, 조명/시각 및X/Z순환 물결 좌표를 검토했다.
- configure/빌드/C++·GLSL 컴파일/게임 실행/자동 테스트/패키징은 수행하지 않았다. 다음 승인된 빌드에서 Vulkan validation과 창 크기·F11·F2·F4, 옵션 조합/저장복원, 바다·강·청크 및순환 경계, 수면 위/아래·밤·동굴·발광·3인칭 반사, 화면 가장자리와 가려진 물체를 수동 확인해야 한다. 반사 품질/색감/깜빡임/실제 GPU 비용은 아직 측정하지 않았다.

## 해변 표면 규칙 이후 Release 빌드 (2026-09-21)

- 사용자 `빌드도 해줘` 승인으로 기존 release 빌드 디렉토리에서 `tools/build.ps1 -Preset release`를 실행했다. 증분 configure/build와 최종 실행 파일 링크가 종료 코드0으로 완료됐다. 이어 `tools/package.ps1`도 종료 코드0으로 완료되어 `out/Sandbox/sandbox.exe`를 갱신했다.
- 빌드 원본/패키지 실행 파일은 각각7,950,848bytes이며 SHA-256이 동일하다. 패키징 전후 `out/Sandbox/worldgen.json`과 `settings.json`의 해시가 같아 사용자 설정 보존을 확인했다.
- 컴파일 경고/오류는 출력되지 않았다. configure의 PkgConfig/LibUSB/rocprofiler-sdk 미발견 메시지는 선택 의존성 탐색 결과이며 빌드를 중단하지 않았다. 게임 실행·시각적 확인·자동 테스트는 수행하지 않았다. 아래 각 변경의 미빌드 표기는 당시 기록이며 현재 소스는 이번 Release 빌드에 포함된다.

## 노이즈 조건의 모래 해변 (2026-09-21, 미빌드)

- 사용자 `실시` 범위로 `terrain.cpp/.hpp`의 표면 재질 규칙과 관련 문서를 수정했다. 두 소스 파일의 clang-format 적용 후 `--dry-run --Werror` 검사를 통과했다. 새 자동 테스트는 추가하거나 실행하지 않았다.
- 소스 검토에서 해변 후보는 표면 윗면 Y=192..196이고 Groundness [-.2,.1]/Smoothness>=0을 모두 요구하는 것을 확인했다. 최고 고체 y=190은 기존 수중, y=191은 해변 후보 시작, y=195는 마지막 후보, y=196은 제외다. 고체 없음(-1)은 후보에서 제외된다. 주변 X/Z 데이터 조회를 추가하지 않고 각 후보의 블록 중심 좌표를 기존 생성기에 배치 전달한다.
- 컬럼 작업의 타일1회 생성/32청크 재사용, 단독 청크의 동일 타일 생성, 3D 활성/비활성 양쪽의 재질 함수 호출을 대조했다. 모래 깊이0..2/흙3..4와 균일 돌 빠른 경로의 최대 깊이4가 일치하고, 기존 육지의 흙 깊이3과 공기·물 배치는 유지한다. 이는 소스 경로 검토이며 실제 청크를 실행 생성한 검증은 아니다.
- 빌드/configure/컴파일/게임 실행/패키징을 하지 않았으며 실행 파일과 사용자 worldgen/settings는 수정하지 않았다. 다음 승인된 빌드에서 해수면 접점·Y196 경계·세로 청크 경계·월드 순환 경계·3D OFF/ON을 수동 확인해야 한다. 모래 분포/해변 폭/추가 생성 비용은 미측정이다.

## C/E별 PV 완전 제외 (2026-09-21, 미빌드)

- 변경C++/헤더3개clang-format --dry-run --Werror통과. 저장JSON읽기에서변경은splines.offset/factor/jaggedness만임을확인했다. 노드offset1674/factor1464/jaggedness48(총3186),최대깊이3,파일672418bytes로기존16384노드/깊이8/4MiB한도안이다. C=.25/1양쪽행에서표별상수E목록을읽어공통완전제외구간을확인했다. 이수치는현재사용자저장표기준이며새프리셋의컴파일/실행검증이아니다.

- 사용자최종정정후실시로능선프리셋상수칸작성과현재저장3표를변경했다. 원본/다채로운프리셋및밀도수식은유지한다. 쓰기전최신사용자schema6/워핑ON9/512/압축.2를읽었고백업후splines만수정했다. 백업은out/Sandbox/worldgen.before-regional-pv.json.bak다.
- 소스검토: factor의C=.25/1확장은원래마지막행복제로동일함수를유지한후상수화한다. 표기준복사본에서W±.45평균을얻어순서의존을없앴다. offset/factor/jaggedness모두대상빈칸을상수로채운다. 고정C/E에서양끝상수가되는공통구간은C>=.25와E[-.35,-.1]또는E>=.4다. 다른구간에서는기존표보간으로W영향이남거나되돌아온다.
- 새자동테스트는추가하거나실행하지않고빌드/configure/컴파일/게임실행/패키징도수행하지않았다. 다음실행에서F3 C/E/PV변화와강/고원분포를확인하고,다음승인빌드에서능선프리셋재불러오기/저장/표의상수칸편집을확인해야한다. 현재메모리초안은재시작전설정을반영하지않는다. 시각적효과·성능은미검증이다.


## 그라디언트 완화·Jaggedness 범위 (2026-09-21, 미빌드)

- `싹 실시`로 기본/능선버튼/현재저장 squash=.2, 추가산악 factor증폭제거, Jaggedness E감쇠끝+.4를 반영했다. 아래 과거 저장파일미변경 기록과 달리 이번 저장파일 수정은 승인 범위다.
- 저장 JSON 구조를 읽어 factor PV wrapper8개를 확인했고 첫3자식 동일(배율1), 좌표6개와 접선0을 확인한 뒤 해당 wrapper만 제거했다. 백업은 out/Sandbox/worldgen.before-relaxed-gradient.json.bak. 변경 전후 비교에서 최상위 splines/squash, 표 내부 factor/jaggedness만 달라짐을 확인했다. schema5/offset/시드/기타설정은 그대로다. 저장값은 코드와 같은 E가중치로 작성했고 float 반올림 차이는 허용한다.
- 저장된 표의 노드수는 offset4554/factor1048/jaggedness72로 총5674다. 변경한 에셋JSON squash=.2를 읽어 확인했다. 분기상C/E/PV보호구간0·E-.1와.2의새양수가중치·global0.4→0.2의gradient절반·factorwrapper제거를 소스로 검토했다.
- 자동테스트·빌드·컴파일·게임실행·패키징은 수행하지 않았다. 다음 실행에서 기존 저장파일 로딩, 같은 위치의F3원신호/Jaggedness/최종압축, 강바닥·동굴/돌출/물/컬럼경계, 새빌드에서프리셋불러오기와저장을 수동확인해야 한다. 낮은압축으로3D영향범위와생성할복잡도가늘수있으며 실제속도/외형은미검증이다.


## 공유 워핑·F3·시간 조절 (2026-09-21, 소스 반영·미빌드)

- 대기 작업5종에 대한 사용자 `실시` 범위만 소스에 반영했다. out/Sandbox의 사용자 worldgen/settings/실행 파일은 수정하지 않았고, 기존 사용자 worldgen이schema5·워핑 필드없음인 것을 읽어 확인했다. 배포용 default.json은schema6·warp OFF로 변경했다.
- 생성 경로 소스 검토: 4블록 프로파일에서 terrain_inputs의 한 변위쌍을 g/w가 재사용하고 모든 옥타브는 변형 좌표를 입력받는다. smoothness/shape/jagged/기후에는 원좌표를 전달한다. 개별 지도와 옥타브 미리보기에도 같은 helper를 사용한다. 꺼짐·강도0에서 warp 노드 생성/샘플링을 생략하며 주기적 변위와 주기적 g/w 합성으로 X/Z경계를 이어간다. 지표검색·실제블록생성은 기존 프로파일을 공유한다. 실제 심리스 화면이나 생성 성능을 측정한 것은 아니다.
- 저장·UI 소스 검토: schema1..6 읽기, v1..5 OFF, schema6 warp값 검증/쓰기/signature 반영, 에셋/importer 기본값 일치. 전후 비교는 request.before_warp에만 저장하며 초안을 수정하지 않는다. 결과 이미지의 이전 요청 판정과 출처표시에 비교 스위치를 포함한다. F8 값변경→재생성→저장 분리는 유지한다.
- F3 현재 생성기/실제 플레이어 좌표 참조,100ms캐시/signature 갱신, 숨김 시 진단 샘플링없음, FPS첫줄/좌우구역/검정외곽선·흰색/좁은창 세로배치를 소스로 검토했다. Factor원값과 최종압축을 구분하고 3D비활성·스플라인구형모드를 표기한다. world.rml을XML로 읽어28개ID가모두고유하고flight-help가없는것을 확인했다.
- 시간 소스 검토: bracket키 양방향±1200tick/s·동시0·음수자정보정, UI/포커스복귀release latch, 재생성시각유지. 정상20tick/s와 물리20TPS루프를 구분했다. daylight의05/08/18/21경계·08~18상수1·21~05상수0과 낮판정[06,20)을 검토했다. 블록광/AO/조명전파를 변경하지 않으며 렌더의 같은 sun값을 하늘색·월드표면에 적용한다.
- 변경C++/헤더10개 clang-format --dry-run --Werror 통과. 에셋JSON/HUD XML읽기 및 importer Python ast 문법읽기 완료. importer를 실행하거나 외부자료를 재가져오지 않았다. 새자동테스트는 추가하거나 실행하지 않았다.
- 빌드/configure/컴파일/게임실행/패키징은 하지 않았다. 다음 승인된 빌드에서 워핑OFF·강도0·ON,각지도/개별옥타브전후비교,월드X/Z경계,저장/재실행,창크기별F3가독성·F8미적용초안구분,시간키단독/동시/자정/입력차단·복귀,밝기상수구간과발광블록·수중·동굴을 수동 확인해야 한다. 시각적 결과·성능은 미검증이다.

## 산악 능선·급경사 프리셋 (2026-09-21, 소스 반영·미빌드)

- 사용자 `실시`로 별도 프리셋과 F8 버튼을 추가했다. 기존 원본·내륙 프리셋, 에셋 default.json, out/Sandbox의 사용자 설정과 실행 파일은 수정하지 않았다.
- 소스/수식 검토: offset의 정상 이동은0이고 어깨 이동은음수, 정상 접선은양수다. PV<=-.76 구간의 offset/factor 변형은항등이며 mountain_detail은0이다. 수정 행의 E=-.1 제어점 존재를 원본 자료에서 확인하여 평야 쪽 offset/factor로 변경이 번지지 않는 것을 검토했다. 잔굴곡은 C/E의0칸을 명시해 sparse clamp 확장을 막는다. GUI는 세 표와 jagged 주파수/옥타브/가중치/gain만 대입하고 모드를 켠다. 재생성/저장 경로는 기존과 같다.
- 원본 JSON 트리의 복제 수를 읽기 전용으로 계산했다: offset4554노드, factor2776노드, jaggedness48노드, 총7378노드/최대 깊이3. offset/factor의 수정 칸은 각각8개다. 이는 정적 자료 검토이며 C++ 실행이나 자동 테스트가 아니다.
- 변경 C++/헤더3개의 clang-format --dry-run --Werror 검사를 통과했다. 선언/구현/호출과 한국어 도움말·문서의 설정 범위를 대조했다. 기존 등록된 terrain_presets.cpp를 사용하므로 빌드 대상 추가는 없다.
- 빌드·실행·자동 테스트·패키징은 하지 않았다. 다음 승인된 빌드에서 프리셋→미리보기/재생성, 같은 시드의 평야·강 중심·산악 비교,3D/잔굴곡 스위치, 반복 클릭, 저장/재실행, 높이512 제한과 편집기 반응 속도를 수동 확인해야 한다. 시각적 개선 및 성능을 실행 검증한 것으로 취급하지 않는다.

## 내륙 다양화 스플라인 (2026-09-21, 소스 반영·미빌드)

- 사용자 `실시`로 원본 기반의 별도 내륙 프리셋과 F8 불러오기 버튼을 추가했다. 원본 초기화 버튼, 내장 원본·에셋 default.json, out/Sandbox의 사용자 설정은 변경하지 않았다. 실제 기본값 고정은 사용자가 기본값 저장을 눌렀을 때다.
- 소스/수식 검토: 원본 함수의 상수와 접선에 동일 affine 변환을 적용한다. 외부 W의 두 중심 제어점은 원본 자식으로 동일하므로 |W|<=.08에서 원본 함수가 유지된다(부동소수점 반올림 차이 가능). C<=-.1 offset 행은 그대로이고, jaggedness의 추가 E열도 먼저 원본 마지막 값으로 연장한다. 노이즈/기후/높이배율/shape 스위치는 버튼에서 대입하지 않는다. 강 보호는 원본 프리셋 기준이며 사용자 편집 표를 병합하지 않는다.
- 원본 JSON의 트리를 읽고 구조상 복제 수를 계산한 결과 신규 프리셋은3727노드/깊이3이다. 원본 에셋과 내장자료의 일치를 확인했다. 이는 정적 자료 검토이며 C++ 생성기 실행이나 자동 테스트가 아니다.
- 변경 C++/헤더3개의 clang-format --dry-run --Werror 검사를 통과했다. 새 소스의 CMake 등록, 프리셋 API 선언·호출 및 버튼 안내의 일치를 확인했다.
- 빌드·실행·자동 테스트·패키징은 하지 않았다. 다음 승인된 빌드에서 새 버튼→미리보기/재생성, 기존 시드/배율 유지, 반복 불러오기, 저장·재실행, 원본 중심 강과 내륙 다양성, height_scale256에서 높이512 상한에 닿는 지역을 수동 확인한다. 시각적 개선/성능은 아직 미검증이다.

## 이동 중 근거리 우선 로딩 (2026-09-21, 소스 반영·미빌드)

- 사용자 제보와 `실시` 승인으로 작업 heap/halo 우선순위 상속, 근거리8컬럼 예약, 거리순 전달/공개 및 GPU 준비/공개 상태 분리를 반영했다.
- 소스 경로 검토: 최초 요청과 기존 halo의 재활용, 데이터/내부 조명/경계/32메시 완료, CPU 슬롯 반환과 공개 pending 감소의 분리, 선두 미완료 시 뒤 결과 대기, 이동·반경 변경 시 순서 갱신을 확인했다. 공개 선두의 데이터를 만드는 작업에는 공개 조건이 없어 순환 대기를 만들지 않는다.
- 이동으로 작업 범위 밖이 된 active 체인은 revision 취소로 슬롯을 반환한다. 이전 worker 결과가 같은 RenderColumn의 새 체인에 쓰이지 않으며, 범위 이탈 취소·같은 좌표 재진입은 기존 상태 객체 취소와 함께 적용한다. 대기 결과와 요청의 큰 자원 폐기는 잠금 밖에서 처리한다.
- GPU 전송 중 이동 시 완료 컬럼을 hidden resident로 전환한 뒤 다음 컬럼을 받아 근거리 공개 대기와 incoming 보유 사이 교착을 피한다. hidden 상태의 이웃 편집/면 밝기 갱신, 드로우/표적/충돌 제외, 언로드와 재생성의 자원·visible/pending 수명 경로를 검토했다.
- 변경한 stream/world_view의 C++·헤더4개는 clang-format 형식 검사를 통과했다. 대기 상태·거리순 맵·작업 revision의 접근은 scheduler mutex 안에 있고, worker의 잠금 밖 취소 확인은 atomic만 읽는다. 이전 FIFO/완료 큐 참조가 남지 않았고 pending 감소는 공개 또는 미공개 범위 이탈에만 남아 있는 것을 소스로 확인했다.
- 빌드·게임 실행·GPU 검증·패키징·자동 테스트를 수행하지 않았다. 다음 승인된 빌드에서 초기 로딩, 빠른 왕복 이동/방향 전환, 순환 경계, 로딩 중 반경 확대·축소와 F8 재생성, 이웃 블록 편집을 겹쳐 확인해야 한다. 느린 근거리 컬럼 때문에 외곽이 대기하는 것은 의도한 동작이다. 성능 향상과 화면의 빈틈 감소를 실행 검증한 것으로 보고하지 않는다.

## 다변수 스플라인 / jaggedness (2026-09-20, 소스 반영·미빌드)

- 사용자 `실시` 범위로 실제 지형 계산, 원본 offset/factor/jaggedness 초기 자료, F8 표/재귀 곡선 편집, 지도 확장, schema5를 반영했다. 기존 실행 폴더 worldgen/settings와 실행 파일은 수정하지 않았다.
- 소스 검토: 원본 C/E 접선0을 확인하고 sparse grid로 변환, W/PV 재귀 제어점/기울기와 빈 칸 의미를 보존했다. 내장 JSON과 에셋 JSON을 대조했다. 새 height/squash 계산을 표면 검색과 블록 밀도에 함께 사용하며, 위치별 범위는 양수 gradient4배에도 보수적이다. 모든 노이즈의 주기와4블록 최솟값/기후 분리를 소스에서 검토했다.
- 저장 검토: v1..4는 새 모드 OFF로 복원, schema5는 표·재귀 값·jagged 설정을 저장한다. 구조/유한수/축순서/깊이/노드수/파일 크기 제한과 원자적 교체를 유지했다. 구형 gain 및 explicit weights 경로를 확인했다.
- 변경 C++/헤더15개에 clang-format --dry-run --Werror 통과. 내장/에셋 스플라인 JSON 일치, 프리셋 최소 제어점 간격0.01·최대 자식 깊이2·범위 위반0개, 모든 CMake 소스 경로 존재를 읽기 전용으로 확인했다. 빌드·게임 실행·GPU 검증·새 자동 테스트·패키징은 수행하지 않았다. 지형 모양이나 성능을 실행 검증한 것으로 취급하지 않는다.
- 다음 승인된 빌드에서 프리셋 불러오기→재생성, 표의 행/열/빈칸/중첩점 편집, 저장 후 재실행, 구형 사용자 설정 복원, 잔굴곡/3D 스위치, 지표 재질과 경계 연속성, F8 지도 종류/범위/최신 요청 반영을 수동 확인한다.

## 옵션 시야각·FPS·VSync (2026-09-20, 미빌드)

- 승인된 옵션3종을 메뉴/Esc 양쪽 UI와 settings.json schema2에 연결했다. 기존 v1 거리 복원, 신규 기본값, 타입·범위 검증, 실패 안내와 원자 저장, CLI 거리의 실행 한정 우선순위를 소스로 검토했다. 사용자 settings.json/worldgen.json과 실행 파일은 변경하지 않았다.
- FOV가 카메라 투영/프러스텀과3인칭 충돌 여유에 함께 반영되고 재생성/시점 변경에 유지되는 경로를 확인했다. FPS 제한 대기는 실제 프레임 작업 시간을 제외하고 적용하며 VSync ON에는 건너뛴다. 기존20TPS 누적 물리 코드는 수정하지 않았다.
- VSync 변경은 다음 begin_frame의 idle/swapchain 재생성으로 연기한다. FIFO와 기존OFF 대체 모드, 초기 생성 중복 재생성 방지, 이미지 수 변경 시 ImGui 재초기화 경로를 검토했다. 실제 드라이버별 전환은 미검증이다.
- 의존성 소스에서 체크박스 checked 변경의 동기 change 이벤트, text Enter의 linebreak change, blur, disabled 처리와 SDL_DelayPrecise 선언을 확인했다. UI 갱신 재진입 방지, 부분 숫자 입력 보류, 유효하지 않은 입력 복원, VSync 활성 중 FPS 입력 차단을 검토했다.
- C++/헤더7개 clang-format --dry-run --Werror 통과. 두 RML 문서의 XML 파싱, ID 중복/참조, label 대상과 stylesheet 존재를 읽기 전용으로 확인했다. 변경 소스/style UTF-8 및 내부 include 경로도 확인했다. 이는 컴파일·RmlUi 실행·화면 검증이 아니다.
- 빌드/configure/컴파일/게임 실행/패키징/자동 테스트는 수행하지 않았다. 다음 명시적 빌드 후 메뉴/Esc 입력·재시작 복원·VSync 연속 전환/전체화면·FPS 상한/물리 속도·시야각별 컬링과3인칭 벽 회피·작은 창/배율별 스크롤을 수동 확인해야 한다.

## 지형 조합·온도·강수량 지도 (2026-09-20, 미빌드)

- 사용자 실시 및 계속 요청 범위에서 groundness/smoothness/weirdness→PV 높이 조합, 지역 압축, Y192 강·계곡 초기값, 독립 기후2종과 F8 편집/미리보기/저장을 소스에 반영했다. Minecraft의 구성 개념을 참고한 자체 지형 합성이다.
- 읽기 전용 검토: periodic Perlin2D의 X 마스크 유지/Z 선택적 마스크와3D 기존 마스크, 전역4블록 격자에서 높이/압축 보간, surface 검색과 블록 생성의 같은 밀도식, 보수적 uniform 범위, 청크 독립성을 대조했다. temperature/precipitation 샘플링 호출은 지도 조회에만 있으며 profile/블록 생성에서 사용하지 않는다.
- 온도 수식을 검토했다. Z65536 적도와 Z0/131072 한랭대에서 지역 노이즈 항이0이고 양 끝 값과 기울기가 연결된다. 원본 온도 노이즈의 Z 격자에는 주기 마스크를 적용하지 않는다. 최종 좌표 조회는 단일 순환 월드에 맞게 정규화한다. 수치 실행 검사는 하지 않았다.
- JSON v1..3 지형 조합 비활성 호환 경로와 v4 새 필드/곡선 범위 검증, 코드 초기값과 배포 프리셋 필드, F8 프리셋의 시드/기후 보존, 재생성/저장의 기존 역할을 대조했다. 배포 JSON 문법을 읽기 전용으로 확인했다. 실행 파일 옆 사용자 worldgen.json/settings.json은 변경하지 않았다.
- UI는 지도별 노드 선택, groundness 전용 개별 옥타브, 이미지별 범례/좌표 값, 전체 월드 범위, 비동기 요청 취소 및 오래된 이미지 표시를 검토했다. 적도/양 끝 온도가 같은 경우에도 ImGui의 동일 min/max 비제한 동작으로 유효 범위를 벗어나지 않도록 보정했다.
- 변경 C++/헤더/inl12개의 clang-format --dry-run --Werror 통과. 소스/문서/프리셋17개를 UTF-8로 읽고 내부 include 존재 및 JSON 파싱을 확인했다. 자동 테스트는 추가/실행하지 않았다. configure/컴파일/빌드/셰이더 컴파일/게임 실행/패키징을 하지 않았으며 기존 실행 파일은 갱신하지 않았다.
- 다음 명시적인 빌드 후 컴파일, F8 곡선/지도 전환/전체 월드 표시, 구형·새 설정 로드/저장, 재생성 시 위치 유지, 순환 경계, 산·평지·강의 모양과 생성 성능을 수동 확인해야 한다. 초기 프리셋은 시각적 검증 전이며 실제 강 연결과 물 배치는 파라미터에 따라 달라진다.

2026-09-07, Windows x64 / NVIDIA GeForce RTX 3080에서 개발 기반을 검증했다. 장거리 월드 성능 측정은 아직 아니다.

## 컬럼 생성·내부 조명·경계 전파 분산 (2026-09-20, 미빌드)

- 사용자 컬럼 단위 생성 정정과 실시 승인으로 WorldStream을 column/local_light/connect_light/mesh 작업으로 변경했다. 컬럼32청크를 한 번에 생성·게시하고 내부 조명을 worker별 scratch로 계산·캐시한다. 실제 한 겹 이웃의 내부 결과가 준비되면 공유 면의 밝기 차이에서 시작하는 추가 플러드 필로 중앙 컬럼 및 halo를 확정한다. 완료된 조명과 메시를 함께 전달하므로 이전 컬럼의 main 업로드가 다음 컬럼 초기 조명의 시작을 막지 않는다.
- 소스 검토: waiting은 pump에서만 해제하고 active8 슬롯은 경계 작업부터32메시 완료 큐 소비까지 유지한다. 컬럼 생성 도중 취소 확인, 단계 게시 전 취소 토큰 검사, 재요청의 별도 상태 객체, 취소된 ready의 카운터, stop_token에 의한 조명 루프 중단, 기존 링 데이터의 reschedule 경로를 대조했다. worker의 Column/LightInput snapshot은 공유 불변이며 이후 단계가 미완성 배열을 읽지 않는다.
- 조명 소스 검토:16×512×16 직사광/발광 초기값, 고체 차단/물 감쇠, 두 채널 내부 큐, 경계 양방향 비교와 다음 이웃까지의6방향 전파, 순환 좌표, X/Z 한 겹16칸과 최대14칸 양의 간접광 도달 범위를 대조했다. 내부 halo의 가장자리 복제는 임시값으로만 쓰며 대상32개 halo/고체 마스크를 실제9컬럼으로 확정한다. 기존 편집 제거·추가 경로는 기본 reset_sources=true를 유지하고 경계 연결만 원천 초기화를 건너뛴다.
- 편집 소스 검토: 주변3×3의 모든 편집을 확인해 base 조명 단축 경로를 제한한다. AO 영향 셀만 확인하는 affected 비트를 조명 판정에 재사용하지 않았다. 세션 편집 지역 재로딩과 초기 경계 대체 계산도 local/connect 알고리즘으로 바꾸고, 편집 snapshot의 블록과 재사용 조명 기준을 맞췄다. 로딩 중 편집은 기존 light_changes/업로드 재예약/컬럼 공개 대기를 유지한다.
- F3에 최근 컬럼의 초기 조명 시간을 추가했다. 해당 컬럼 내부+경계 작업 합으로, 이웃 내부 계산/큐 대기/편집 조명 대체 비용을 포함하지 않는다. 실제 로딩 지연시간이나 처리량 측정 결과는 아니다.
- C++10개 파일 clang-format --dry-run --Werror 통과. 코드/문서13개 파일 UTF-8과 내부 include 참조를 읽기 전용으로 확인했다. 빌드/configure/컴파일/셰이더 컴파일/게임 실행/패키징/자동 테스트는 수행하지 않았다. 기존 실행 파일과 저장 설정/에셋은 변경하지 않았다.
- 다음 승인된 빌드 후 최초/이동/반경 변경/F8 재생성 시 컬럼 공개와 완료 여부, 경계 동굴·수중·발광/차광 편집과 재로딩, 순환 경계, 종료·재생성 중 취소를 수동 확인해야 한다. 컬럼 내부 조명 캐시만큼 RAM이 추가되며, 같은 경계 비교가 인접 대상 작업에서 반복될 수 있다. 편집 지역 초기 보정은 기존 LightWorker에서 직렬 처리한다. 실제 성능 향상과 결과 일치는 아직 실행 검증 전이다.

## 대기 컬럼 활성화 경합 수정 (2026-09-20, 미빌드)

- 사용자 청크 생성 이상 제보 후 소스에서 발견한 예약 누락 경로를 실시 승인으로 수정했다. active8 제한으로 waiting 중인 컬럼이 take_ready 이후 데이터 완료 경로에서 일부 청크만 예약하며 active가 되면, 후보 큐 처리에서 active라는 이유로 나머지 준비된 청크 재검토를 건너뛸 수 있었다. 사용자 화면의 직접 원인이라는 실행 재현은 아직 없다.
- waiting 컬럼의 schedule_locked 진입을 슬롯 여유와 무관하게 차단했다. 후보 큐 소비자인 pump_locked만 waiting을 해제하고 동일 잠금 안에서32청크를 재검토한다. 아직 준비되지 않은 이웃은 이후 데이터 완료 경로에서 예약하고 queued 비트로 중복을 막는다.
- 변경한 stream.cpp의 clang-format --dry-run --Werror 검사를 통과했다. 이는 형식 검사이며 컴파일이나 실행 검증이 아니다.
- 소스 흐름에서 슬롯 해제→데이터 완료→pump 순서, 슬롯 해제→pump→데이터 완료 순서, retained 데이터 재예약, 취소된 후보 건너뛰기와 동일 좌표 재요청의 별도 상태 객체를 검토했다. 새 자동 테스트는 추가하지 않았다. 빌드·게임 실행·패키징은 수행하지 않았으며 실행 파일은 갱신하지 않았다. 다음 승인된 빌드 후 초기 로딩 완료, 이동·반경 변경·재생성을 수동 확인해야 한다.

## 로딩 중 프레임 비용 개선 (2026-09-14, 미빌드)

- 사용자 전체 제안 승인 `ㄱㄱ` 범위로 면 밝기 worker 이동, 프레임 staging page 재사용, 디스크립터 풀 가용 수 추적, F3 가벼운 메모리 조회, 로딩 범위 차집합/잠금 축소, 초기 조명 known 이웃 재사용을 반영했다. 저장 렌더 거리36과 사용자 worldgen/settings JSON, 기존 실행 파일/에셋은 변경하지 않았다.
- CPU 소스 검토: face_light 호출은 MeshLightWorker에만 남고 WorldView 업로드/relight에는 면 순회가 없다. 고체→물 면 순서/solid_count와 이전 값 비교를 worker로 옮겼다. 작업 ticket 폐기, 같은 청크 재편집/언로드/재진입, 오래된 빛으로 먼저 준비된 기하의 최신 빛 재예약, 빈 청크 생략,32청크 공개 조건과 빛 전용 geometry 동일성 검사를 대조했다. 긴급 요청/완료 결과는 일반 로딩보다 먼저 처리하며 worker는 GPU를 호출하지 않는다.
- Vulkan 소스 검토: Frame fence 이후 staging cursor 초기화, nonCoherentAtomSize와4바이트 정렬, flush 범위/VkBufferCopy.srcOffset, 현재 프레임별 페이지 분리, 페이지 증설 실패 해제, 종료 fence/allocator 순서를 확인했다. 실제 목적지 버퍼의 기존 shader-read barrier/지연 해제를 유지했다. FacePool 할당 감소/지연 free 증가, 가용0 건너뛰기, 풀 실패 후 반복 호출 억제, WorldView 수명 밖 callback의 shared 상태를 확인했다.
- 스케줄링 소스 검토: 이전/현재 원형 구간의 교집합·비교집합·반경 변경·큰 이동·순환 좌표, 데이터 링 참조 수0전환, 유지 컬럼의 데이터 의존성, 취소된 ready/작업의 active/pending 카운터를 대조했다. main 전체 큐 정렬과 take_ready 내 pump를 제거했고 retained 데이터 재예약은 worker에서 한다. 조회 try_lock과 원자적 pending, 취소 결과 해제의 잠금 밖 수명을 확인했다. 기하8컬럼 active 제한은 유지한다.
- 초기 조명 소스 검토: known 컬럼은 현재 편집 기준 상태의 정확한 완료 결과만 받으며 그 내부 스캔/COW/전파를 생략한다. unknown 셀에만 경계 빛을 주입하고 known 값에는 쓰지 않는다. 재사용 없는 최초 요청과 편집 fallback도 동일 함수의 빈 known으로 처리한다.0/1인 비전파 셀의 불필요한6이웃 검사와 경계 주입 순서, 출력 halo의 기존 고체 마스크/Y경계를 검토했다. 실제 값 일치/빛 누출은 실행 검증 전이다.
- F3 VMA 상세 순회를 제거하고 설치된 VMA 헤더의 vmaGetHeapBudgets/vmaGetMemoryProperties 시그니처 및 statistics 의미를 대조했다. 면 메모리 합계는 공개/교체/언로드/재생성에서 증분 유지한다. CPU 월드 준비 수치는 request·언로드를 포함하며 전체 프레임 또는 GPU 실행 시간과는 다르다.
- C++9개 파일 clang-format --dry-run --Werror 통과. 코드/문서12개 파일 UTF-8과 내부 include 경로를 읽기 전용으로 확인했다. 빌드/configure/컴파일/셰이더 컴파일/게임 실행/패키징/자동 테스트는 하지 않았다. 이 기록은 소스 검토이며 실행 성능 개선을 측정한 결과가 아니다.
- 다음 승인된 빌드 후 거리1/12/36/64 초기 로딩, 한 컬럼씩/대각선 이동, 거리 확대·축소·순환 경계, 로딩 중 연속 설치/파괴·발광 제거, F8 재생성/메뉴 왕복을 수동 확인해야 한다. F3 켬/끔 CPU/GPU 시간과 로딩 완료 시간, staging 예약 메모리의 최고 사용량, descriptor 재사용 시 validation 오류를 확인한다. 초기 대규모 상태 추가/개별 메싱/목적지 GPU 할당은 여전히 main 비용이며 strict2ms 상한은 아니다. 초기 scratch48×512×48 할당과 미준비 이웃 계산, 편집 fallback 비용도 남는다.

## AO 유지·고체 조명 샘플 제외 (2026-09-14, 미빌드)

- 사용자 AO와 조명 독립 처리 제안 및 실시 승인으로 face_light에서 AO 비트 분기를 제거했다. 고체 배치로 계산하는 기존 vertex_ao/삼각형 대각선/셰이더 AO 강도는 수정하지 않았다.
- 소스 검토로 면 외부/측면/대각선의18³ 좌표, 유효 샘플 수1~4 평균, 어두운 비고체0 유지, 양 측면 고체일 때 대각선 제외, 편집 직후 유효 샘플0개 대체 처리를 확인했다. 평균에서 고체0과 발광 고체 내부값을 제외하며 발광은 기존 주변 전파와 자체 흰색 표현을 유지한다.
- 별도 고체 bitset의 초기 생성/균일 압축, 증분 편집의 밝기 불변·고체 여부 변경, 이웃 halo 갱신, 대체 solve 비교 및 main 완료 판정을 대조했다. GPU ABI/버퍼 크기/셰이더는 변경하지 않았다.
- C++3개 파일 clang-format --dry-run --Werror 통과. 코드/문서6개 파일 UTF-8과 내부 include 경로를 읽기 전용으로 확인했다. 빌드/configure/컴파일/셰이더 컴파일/실행/패키징/자동 테스트는 하지 않았다. 사용자 JSON과 실행 파일, 에셋은 수정하지 않았다.
- 다음 승인된 빌드 후 균일한 야외 조명 아래의 한쪽/양쪽 막힌 모서리, 어두운 동굴에서의 설치·파괴, 물/발광 블록 주변과 청크·순환 경계를 수동 확인해야 한다. 실제 화면에서의 개선과 성능은 미확인이다.

## 편집 메시 우선 처리·증분 플러드 필 (2026-09-14, 미빌드)

- 사용자 `ㅇㅋ 일단 해보자 실시` 승인으로 직접 편집 청크 우선 갱신, 준비되지 않은 이웃을 건너뛰는 처리, 조명/신규 업로드와의 예산 공유를 반영했다. 물리틱을 기다리는 새 경로는 넣지 않았다. 실제 입력→화면 반영 지연은 아직 측정하지 않았다.
- 조명은 캐시가 준비된3×3 합집합에서 before/after 편집 묶음을 제거·추가 큐로 처리한다. 소스 검토로 수정 수직선의 직사광 차이, 다중 광원 제거/복구, 동일 좌표 왕복 편집 병합, 같은 광학 특성 재질 생략, 공기/물/고체,0/511 및 X/Z 순환 경계 인덱스를 대조했다. 결과가 없는 변경도 빈 LightResult로 작업 완료를 알린다.
- 작업 중 추가 편집은 다음 묶음으로 남기고 완료 기준 상태를 받아들인다. 전역 revision 재시작 제거, 초기 solve보다 편집 우선, 언로드 결과 폐기와 재생성 stop/join을 검토했다. 미준비 주변 밝기는 기존 full solve 대체 경로를 사용하고 실제 데이터 미준비는 제출 대기한다. 이 경로에서는 이전의 큰 계산 비용이 남는다.
- 변경 청크만 copy-on-write하고 halo를 참조하는 최대8개청크 조명 패킹을 예약한다. 최종 값이 같으면 기존 snapshot을 재사용하며, 추가 큐 중복은 청크별 bitset으로 억제한다. main은 바뀐 LightChunk만 빛 업로드 대상으로 넣고 예정된 기하 교체의 기존 메시에는 중복 업로드하지 않는다. 초기 미공개 컬럼도 빛 업로드 완료 후 공개한다. GPU ABI/셰이더/동기화 자원 변경은 없다.
- C++6개 파일의 clang-format --dry-run --Werror 통과. 코드/문서10개 파일의UTF-8과 내부 include 경로를 읽기 전용으로 확인했다. 이 항목들은 컴파일 또는 게임 동작 검증이 아니다.
- 빌드/configure/컴파일/셰이더 컴파일/게임 실행/패키징/자동 테스트는 하지 않았다. 저장 JSON/기존 실행 파일/에셋은 수정하지 않았다. 다음 승인된 빌드 뒤 여러 발광 블록 중 하나 제거, 지붕 막기/열기, 수면 인접 편집, 청크·순환 경계, 연속 클릭과 로딩 중 편집, 작은 렌더 거리 대체 경로, 거리 변경/F8 재생성 후의 정확성과 반응 속도를 수동 확인해야 한다. 새 자동 테스트는 추가하지 않았다.

## 두 채널 조명·핫바·하루 주기 (2026-09-14, 미빌드)

- 사용자 실시 승인 범위인 햇빛/블록광 전파, 물감쇠/고체차단, 흰색발광블록15, 모래/발광설치, 중앙하단5칸핫바/휠, 우상단상태제거, 28800틱하루/06:00시작/하늘색·햇빛변화를 소스에 반영했다. 태양/달은 추가하지 않았다.
- 정적검토: LightWorker의9컬럼 완성snapshot/편집부분복사/worker내copy-on-write,16칸여유와최대14칸간접전파,전높이스캔/수중감쇠/불투명/복수광원,두채널4비트packing/모서리샘플좌표범위를검토했다. CPU결과revision/동일키중복예약방지/언로드/재생성stop-join과최초컬럼공개대기를확인했다. 조명계산은영향컬럼영역재계산이며영구월드증분제거큐방식은아니다.
- Vulkan source검토: set1 binding0/1,풀당2descriptor,같은solid→water면순서,별도빛buffer업로드후vertex storage읽기barrier,빛교체시기하유지/예외중빛단독retire/기존descriptor지연폐기/종료시두buffer해제를확인했다. shader재질7흰색/물5/8타일256×32,96바이트push유지와offset.w용도를대조했다. 실제shader컴파일/validation실행은하지않았다.
- UI/시간 source검토:1~5 매핑,부분휠누산/순환/FLIPPED/입력차단,기존도움말과중앙핫바간위치분리,우상단태그·CSS·main갱신제거,F3시각/대기표시,50ms틱증가/자정순환/메뉴정지/재생성시간유지를확인했다. 하루주기는기존물리프레임누산상한을따르므로지속긴프레임에서는현실시간과차이가날수있다.
- C++14개파일 clang-format --dry-run --Werror 통과. 변경파일24개의UTF-8/내부include경로,world.rml XML파싱/중복ID/5개슬롯/에셋경로,lighting.cpp의CMake등록을확인했다. 기존저장JSON/실행파일/외부텍스처는변경하지않았다.
- 빌드/configure/컴파일/셰이더컴파일/게임실행/패키징/자동테스트는수행하지않았다. 다음승인된빌드후낮동굴/수중/여러발광설치및제거/연속편집/청크·월드경계/로딩중편집/거리변경/F8재생성/게임종료,핫바휠과UI스크롤분리,24분주기와시각정지/태양·달없음을수동확인해야한다. 초기조명은1worker에서컬럼별지역solve를수행하므로원거리로딩처리량과편집지연의실측/후속최적화가필요하다. 화면가독성·빛수치·성능은실행검증전이다.

## 수중 모래3층/흙2층 (2026-09-14, 미빌드)

- 사용자 실시 승인으로 수중 최고 고체부터 모래3/흙2/돌을 적용했다. 모래 원본을 실행 폴더에서 assets로 복사하고 바이트/SHA-256 일치를 확인했다. 나머지 전달 텍스처는 연결하지 않았다.
- 소스 검토: 수면 경계 surface_y190/191, 깊이0..2/3..4/5이상 분기, 지층이 청크 경계를 넘을 때 uniform 돌 생략 조건, 3D 활성/비활성 공통 재질 함수, sand의 고체/AO/충돌/레이캐스트/편집 등록을 확인했다. 아틀라스7타일/재질6과shader textureSize 사용, 기존 물재질5 보존을 확인했다.
- C++4개 파일의 clang-format --dry-run --Werror 통과. 소스/셰이더/문서10개의 UTF-8, 내부 include 및 아틀라스7개 파일 경로를 확인했다. 빌드·셰이더 컴파일·게임 실행·패키징 및 자동 테스트는 수행하지 않았다.
- 다음 승인된 빌드 후 수중 모래/흙층과 육지 경계, 양 생성 모드, 모래 충돌/파괴/AO 및 물 투명도·아틀라스 타일 표시를 수동 확인해야 한다.

## 가중치 초기화 경고 정리 (2026-09-14, 수정 후 미빌드)

- 사용자가 전달한 빌드 로그에서 release 링크와 out/Sandbox 패키징 성공을 확인했다. 실행 검증 자료는 아니다. 같은 로그에 NoiseSettings의 weights 초기화 누락 경고가 두 소스 위치에서 반복 출력됐다.
- 후속 실시 승인으로 GenerationConfig.shape와 parse_noise 반환값에 빈 weights 배열 {}를 명시했다. 기존 빈 vector 초기화와 동작은 동일하다. 선택 의존성 탐색 안내나 빌드 설정은 변경하지 않았다.
- 변경 두 C++ 파일의 초기화 목록을 검토하고 clang-format --dry-run --Werror 통과를 확인했다. 수정 후 빌드/컴파일/실행/패키징이나 자동 테스트는 수행하지 않았다. 실제 컴파일 경고 제거 여부는 다음 승인된 빌드에서 확인한다.

## 3D 노이즈 사용 전환 (2026-09-14, 미빌드)

- 사용자 실시 승인으로 F8 체크박스/비활성 파라미터 보존, schema3 shape_enabled 저장과 v1/v2 기본true 읽기, 3D 비활성 시 groundness 전용 생성 경로를 추가했다. 실제 저장 JSON 파일은 수정하지 않았다.
- 소스 검토: shape 노드 생성/구성과 실제 SIMD 호출이 비활성 상태에서 생략됨을 확인했다. 표면 탐색의 직접 높이 판정, 청크의3D격자 없는 채우기, uniform 최적화/지층/수면 경계, 셀 중심=기준 높이일 때 밀도0 처리, 세계 높이0/512와 스폰 상한을 검토했다. 체크박스 변경 후 재생성/저장/되돌리기와 기존 파라미터 보존, 미리보기 독립성도 검토했다.
- 변경 C++6개 파일의 clang-format --dry-run --Werror 통과. 소스/문서11개의 UTF-8과 내부 include 경로를 확인했다. 자동 테스트 추가/실행, 빌드/configure/컴파일/게임 실행/패키징은 하지 않았다.
- 다음 승인된 빌드 후 3D 끄기→재생성→켜기, 설정 저장/재시작, 구형 파일 불러오기, 해수면/잔디·흙 층, 위치·각도 유지, 진폭0 및 상단/하단 높이를 수동 확인해야 한다. 실제 성능과 화면 결과는 미검증이다.

## 렌더 거리 저장과 옥타브별 가중치/미리보기 (2026-09-14, 미빌드)

- 사용자 실시 승인으로 settings.json 자동 저장/시작 복원, CLI 우선값, 저장 실패 시 옵션 안내를 연결했다. core/settings.cpp의 CMake 등록과 양쪽 Rml 도움말 대상 ID를 확인했다. 저장은 값 변경 시에만 수행하며 임시 파일/close 확인/Windows atomic replace를 사용한다. package.ps1의 복사 범위를 읽어 사용자 설정 파일을 덮어쓰지 않는 경로임을 검토했다.
- schema1 gain 읽기 → 개별 float 가중치 → schema2 weights 쓰기 경로를 소스로 검토했다. 옥타브 수/배열 길이/비음수/유한성 검증, 모두0과 큰 가중치의 double 합, generator의 설정 정규화/불변 공유, SIMD0기여 제외와 주기/시드 인덱스 유지, 기존3D gain 경로를 확인했다. 기존 사용자/배포 JSON 파일은 수정하지 않았다.
- F8 두 창의 공유 배열 크기 조절/새 가중치0, 원신호/정규화 기여분/합성 선택, 프리뷰 요청의 선택 스냅샷과 stale 판정, 단일 옥타브 간격/시드 오프셋32비트 래핑, 빠른 선택 변경의 기존 revision 취소 경로를 소스로 검토했다. 가중치0인 옥타브 원신호도 확인 가능하며 전체 가중치0의 기여분은0으로 채운다.
- 변경 C++/헤더/inl12개 파일의 clang-format --dry-run --Werror 통과. 변경 파일21개의 UTF-8 및 내부 include 경로를 확인했다. 새 자동 테스트는 추가하지 않았고 빌드/CMake configure/컴파일/게임 실행/패키징은 수행하지 않았다.
- 다음 승인된 빌드 후 실제 실행 간 렌더 거리 복원, 메뉴/월드 공유, 저장 권한 오류 안내, CLI 우선값, 기존 JSON 불러오기/새 JSON 저장 후 재시작, 옥타브별 미리보기 전환/제로 가중치/모든0/최대8겹/옥타브 감소, 재생성·저장·프리뷰 독립 동작을 수동 확인해야 한다. 실제 성능과 시각 결과는 미검증이다.

## 연속 점프와 잔디/흙 표면 (2026-09-13, 미빌드)

- 사용자 실시 승인으로 Space held 착지 재점프와 최고 고체 잔디/그 아래3칸 흙/나머지 돌을 소스에 반영했다. 최고 고체 위가 물이면 표면도 흙이다. 기존 모드 전환/UI 입력 억제, 밀도/공기/물/해수면/저장 설정은 유지한다.
- 정적 검토: Space의 빠른 누름 요청, held 상태, 키 해제, 두 번 누르기와 reset 경로를 확인했다. 표면 탐색의 격자 정렬/보간 순서를 청크 밀도식과 대조하고, Y=0/511 및 표면190/191의 수면 조건, 깊이3/4와 청크 uniform 조기 반환, 캐시 없는 독립 생성 경로를 검토했다. 모든 최고 표면은 다른 청크 데이터를 읽지 않고 컬럼당 한 번 계산해 공유한다.
- 변경 C++4개 파일의 clang-format --dry-run --Werror가 통과했고 변경 소스/UI/문서9개 파일의 UTF-8을 확인했다. 자동 테스트는 추가/실행하지 않았다. 빌드·컴파일·게임 실행·패키징은 하지 않았다.
- 다음 승인된 빌드 후 Space를 누른 채 연속 착지, 빠른 탭/두 번 누르기/F8·Esc 복귀, 수중 흙/해수면 접촉 잔디, 수직 청크 경계의3층 흙, 동굴/공중섬과 월드 상단 지층을 수동 확인해야 한다. 추가 표면 탐색의 실제 생성 시간은 미측정이다.

## F3 굵은 글꼴과 독립 미리보기 창 (2026-09-13, 미빌드)

- 사용자 캡처에서 F3 얇은 흰 획/과도한 검정 외곽선 가독성 문제를 확인했다. 원본 NotoSansKR.ttf의fvar축은wght100..900/기본100이었다. 사용자 `ㅅㅅ` 승인으로 굵기600 static instance를추가해 F3전용25.5px ImFont로사용하고 검정외곽선은8방향1픽셀로줄였다. 기존가변폰트는수정하지않았다.
- fonttools4.59.0을.cache/font-tools에설치해원본에서staticfont를변환했다. tools/bake_debug_font.py에재현방법을남겼으며게임빌드단계에추가하지않았다. 글꼴변환은실행했지만게임빌드/실행은하지않았다.
- 미리보기를별도 ImGui창으로분리했다. F8본창과같이열고닫되미리보기자체닫기와본창의다시열기버튼을제공한다. 왼쪽에공유시드/groundness/범위/해상도/랜덤요청, 오른쪽에표시결과/확대/이미지를배치하고각영역스크롤및창이동/크기변경을지원한다. 해상도256/512/1024/2048은요청snapshot에포함하고확대0.25~4는표시만변경한다.
- 정적확인: C++변경6개파일의clang-format검사,UTF-8/내부include경로,글꼴변환script AST파싱을확인했다. 새글꼴은usWeightClass600/fvar없음과main.cpp비ASCII문자coverage를확인했고원본글꼴SHA-256은기존문서값과일치했다. 두창의shared draft/공통noise controls,창Begin/End/child/table수명,해상도스냅샷/곱셈범위/이전이미지판정과viewport기준확대계산을소스검토했다.
- 빌드·컴파일·게임실행·패키징은하지않았다. 다음승인된빌드후F3실제획/외곽선/F8폰트유지,두창배치/이동/크기변경/닫기/재열기,편집값공유,모든해상도와확대/스크롤/화면맞춤,고해상도계산취소/종료와GPU자원수명을수동확인해야한다. 자동테스트는추가하지않았다.

## F3 글자 가독성과 F8 이미지 미리보기 (2026-09-13, 미빌드)

- 사용자 `실시` 승인으로 F3 글꼴을 기존1.5배 크기로 bake하고8방향2픽셀 검정 외곽선 위에 흰 글자를 그린다. 배경/입력 영역을 만들지 않으며 F8 글꼴 크기는 유지한다.
- GroundnessPreview 소스와 CMake 등록을 추가했다. 기본0..16384 양축, 시작/끝0..131072 범위 검증, 시드/크기 유지 랜덤 위치 및 초안 랜덤 시드, 비동기 최신 요청 우선 계산, 진행률과 이전 결과 표시, 표시 시드/범위/min/max/계산 시간/표본 hover를 연결했다. 긴축512표본, 원신호 -1..1 고정 흑백 매핑을 사용한다. 이미지 자체는 높이곡선/3D shape를 적용하지 않는다.
- CPU worker는 불변 TerrainGenerator groundness SIMD 함수를 재사용하며 GPU/ImGui는main에서만 접근한다. 새 이미지 업로드는 Renderer::create_texture의선택적staged 경로로현재 프레임 렌더 전에 기록한다. 완료 후기존 이미지/descriptor를정리하며 ImGui backend 재생성 시binding해제/재등록, 종료시stop/join·GPU자원정리를 연결했다. 기존텍스처호출은기본동기업로드를유지한다.
- 정적 확인: 변경 C++7개 clang-format 서식, UTF-8/프로젝트 include 경로, 새cpp CMake 등록을 확인했다. 기존 NotoSansKR cmap에서 변경 소스의비ASCII302종을 모두 확인했다. 로컬 ImGui PushFont크기/ImTextureRef/AddTextureAPI와 FastNoise PositionArray의tail 처리를 검토했다. 요청 스냅샷/수명/최신revision 비교, 범위차 연산과비율, 원시값 매핑, begin_frame→preview upload→world/UI렌더 순서를 소스로 확인했다.
- 빌드·CMake configure·컴파일·게임 실행·패키징은 하지 않았다. 실제가독성/이미지표시/계산속도/스레드·GPU수명은 미검증이다. 다음 승인된 빌드 후 F3외곽선/1.5배와F8기존크기, 기본/좁은/비정방형/월드전체범위, 잘못된 범위, 연속요청 취소와랜덤버튼, 표시결과·초안불일치, F8닫기/메뉴왕복, F11/창resize로backend재생성, 계산중종료, 저장/재생성독립성을 수동 확인한다. 자동테스트는 추가하거나실행하지 않았다.

## F3 진단 텍스트와 F8 생성 편집 분리 (2026-09-13, 미빌드)

- 사용자 `실시` 승인으로 F3는 좌상단 표시 전용 진단 텍스트, F8은 노이즈/곡선/재생성/저장 편집창 토글로 분리했다. 일반 실행 시 둘 다 숨김이며 기존 --debug-ui 명시 옵션은 텍스트만 표시한다.
- main의 진단 창을 background draw list 텍스트로 교체했다. 배경/테두리/입력 영역이 없고 글자 그림자로 가독성을 보완한다. 기존 수치를 묶어 표시하며 FPS는 기존 단일 프레임 샘플/250ms 표시 주기를 유지한다. 생성 편집 제목과 HUD/README/AI 문서의 단축키를 갱신했다.
- debug 표시 상태는 게임 입력 조건에서 제거했다. generation_open만 게임 조작/마우스 캡처/ImGui 입력 배분을 제어하며 F8 열기·닫기 프레임도 게임 입력을 막는다. F3는 Space 두 번 기록/F5/이동/편집을 중단하지 않는다. 메뉴 전환은 F8을 닫으며 Esc 옵션 우선순위를 유지한다.
- 정적 확인: 변경 C++2개 clang-format 검사, UTF-8/내부 include 경로, HUD XML/중복ID 확인을 통과했다. 로컬 ImGui 헤더의 draw list/TextBuffer API, F3 표시 상태의 입력 참조 제거, F8 전환과 메뉴/옵션/캡처 조건, 기존 저장/재생성 호출 보존을 소스로 검토했다.
- 빌드·컴파일·게임 실행·패키징은 하지 않았다. 실제 글자/그림자/창 크기별 줄바꿈, F3를 켠 상태의 이동·점프·블록 편집, F3/F8 독립 토글, F8 그래프/도움말/저장/재생성 및 Esc/메뉴 복귀/마우스 캡처는 다음 승인된 빌드 후 수동 확인해야 한다. 자동 테스트는 추가하지 않았다. 아래 F3 편집/입력 차단 관련 기록은 당시 단축키의 이력이며 현재는 F8이다.

## 걷기/플라이 전환과 중력·점프 (2026-09-13, 미빌드)

- 사용자 `실시` 승인으로 Space0.3초 이내 두 번으로 fly/walk를 전환한다. 비반복 SDL 키 이벤트의 ns timestamp를 사용하며 한 쌍을 소비한 뒤 기록을 비운다. 시작fly, 전환 시 실제/표시 위치 및 카메라/F5 방향은 유지하고 수직 속도만 초기화한다. 두 번째 누름의 상승은 키를 놓을 때까지 억제한다.
- 20 TPS 걷기 중력/지면 단발 점프,4.5/Shift7 이동과8 점프 속도/24 중력/60 종단 속도를 추가했다. 기존 swept AABB의 축별 충돌 결과를 반환하여 수직 속도를 정리하고 실제 고체 지지면만 접지로 인정한다. 미공개 셀은 통과·낙하를 막지만 점프 지지면으로 인정하지 않는다. 수영/부력/낙하 피해는 추가하지 않았다.
- main의 게임 입력 허용 범위에 Space press/release를 연결하고 UI/메뉴/포커스 차단 및 재생성 시 탭 기록/미소비 점프를 초기화한다. 기존 위치 보간/F5/지형 편집을 공유한다. HUD 모드/조작 안내, F3 접지·수직 속도와 문서를 갱신했다.
- 변경 C++5개 파일의 clang-format 서식 검사, UTF-8/내부 include 경로, world.rml XML·중복ID·참조ID를 확인했다. 로컬 SDL 이벤트 헤더에서 timestamp가SDL_GetTicksNS 기반 나노초임을 확인했고, 반복 키 제외/타임스탬프 역전 방어/한 번 점프 소비/중력 적분/축별 충돌/모드 전환 위치 불변을 소스 검토했다. 이전 미구현 설명을 현재 문서에서 제거했다.
- 빌드·셰이더 컴파일·게임 실행·패키징은 하지 않았다. 컴파일/실제 조작·물리 동작은 미검증이다. 다음 승인된 빌드 후 빠른/느린 두 번 누름, 키 누름 유지, 지면 점프·공중 재점프 거부·천장·착지·절벽, 플라이 복귀, 미로딩 낙하 정지, F5/월드 경계, 메뉴/F3/Esc/포커스 복귀 및 재생성 위치 보존을 수동 확인해야 한다. 자동 테스트를 추가하거나 실행하지 않았다.

## F5 뒤·앞3인칭과6블록 카메라 (2026-09-13, 미빌드)

- 사용자 `플레이어로부터 6블럭거리 실시` 승인으로1인칭→뒤3인칭→앞3인칭 순환과 눈높이 기준6블록 기본 거리를 소스에 반영했다. 앞3인칭 렌더 시선만 반전하고 플레이어 yaw/pitch/이동/블록 편집 방향을 분리했다.
- 가까운 화면 영역을 포함하도록 복셀을 확장하여 눈부터 카메라까지 선분 충돌로 거리를 제한한다. 고체와 미공개 셀에서 당기고 물은 통과한다. 렌더 전 컬럼/편집/화면비를 반영하며 로딩 중심은 플레이어를 따른다. 메뉴/F3/Esc 입력 차단과 키 반복 무시를 유지한다. 시점 전환/재생성은 플레이어 위치나 각도를 바꾸지 않는다.
- 변경 C++4개 파일의 clang-format 검사와 UTF-8/프로젝트 include 경로 확인을 통과했다. 소스에서 모드3개 순환, 렌더/플레이어 방향 분리, 클릭 사거리 기준, prepare→카메라 동기화→선택→렌더 순서 및 X/Z 주기 처리를 검토했다.
- 빌드·컴파일·게임 실행·패키징은 하지 않았으며 자동 테스트도 추가하지 않았다. 다음 승인된 빌드 이후 F5 순환/키 누름 유지, 앞뒤 몸체/와이어프레임, 벽·천장·대각 모서리·물·미로딩 경계, 창 화면비와 F11 변경, 벽 편집 후 거리 복원, 월드 경계, 메뉴/F3/Esc와 재생성 후 위치·각도·모드 보존, 모든 시점의6블록 편집 사거리를 수동 확인해야 한다.

## 흰색 플레이어와 몸체 충돌 (2026-09-13, 미빌드)

- 사용자 `ㅅㅅ` 승인으로0.7×0.7×1.75블록 몸체와 동일한 AABB를 추가했다. 발바닥 중앙을 기준으로 위치를 관리하고, 기존 자유 비행 입력은20 TPS 축별 sweep으로 연결했다. 고체/미공개 셀 진입을 막고 실제·이전·표시 몸체와 겹치는 블록 놓기를 거부한다. 중력과 점프는 추가하지 않았다.
- 기존1인칭 연결을 유지했다. 눈높이는1.6블록이며 초기 카메라 위치와 재생성 시 위치/각도를 보존한다. 일반1인칭에서는 몸 안쪽 면을 컬링하므로 흰색 몸체가 시야를 가리지 않는다. 3인칭은 추가하지 않았다. F4는 몸체 선을 포함한다.
- 새 player 셰이더2개/CMake 등록,6면 procedural draw와 물 이전 깊이 기록, pipeline 생성/정리를 추가했다. 실제 틱 사이의 가장 가까운 주기 위치를 보간하고 표시용 sweep으로 모서리 관통을 제한한다. 생성기나 사용자의 worldgen.json은 수정하지 않았다.
- 정적 검토: 변경 C++5개 파일의 clang-format 검사, 프로젝트 내부 include 경로, 전체6개 shader 소스 등록/존재, UTF-8 디코딩을 확인했다. 소스에서 CPU/GLSL push 크기96바이트와 슬롯 의미, 기존 면 winding 재사용, pipeline 성공/실패 수명, 주기 경계·축별 이동·재생성 상태 보존 경로를 검토했다.
- 빌드·셰이더 컴파일·게임 실행·패키징은 하지 않았다. 타입/링크/실제 화면과 충돌 동작은 검증하지 않았다. 다음 승인된 빌드 후 고속 벽 이동/미끄러짐, 천장·바닥·모서리, 월드 순환 경계, 로딩 경계, 재생성 겹침·위치 보존, 몸 안 블록 놓기 거부,1인칭/F4/물 깊이, Esc/F3/포커스 상실 후 복귀를 수동 확인한다. 자동 테스트는 추가하지 않았다.

## F3 생성 파라미터 한글 도움말 (2026-09-13)

- 사용자 `실시` 승인으로 생성 편집창의 항목/버튼 이름을 한글화하고 각 파라미터, 곡선 편집 및 재생성/저장 버튼에 hover 도움말을 추가했다. 값의 의미, 증감 효과와 적용 시점을 설명한다. JSON 키/설정값과 생성 알고리즘은 변경하지 않았다.
- 기존 NotoSansKR.ttf를 ImGui 기본 폰트로 연결했다. 로컬 Vulkan 백엔드의 RendererHasTextures 및 동적 텍스처 업로드 경로를 확인했고 폰트 cmap에서 변경된 두 소스의 비ASCII 문자 261종이 모두 존재함을 확인했다. 변경 C++ 파일의 clang-format 서식 검사도 통과했다.
- 빌드·실행·패키징은 하지 않았다. 실제 한글 렌더링, 창 크기별 배치, 마우스/키보드 도움말 및 폰트 atlas 업로드는 다음 승인된 빌드 이후 수동 확인이 필요하다. 기존 실행 파일은 갱신되지 않았다.

## 재생성 시 위치·시점 유지 (2026-09-13)

- 사용자 `ㅅㅅ` 승인으로 WorldView::regenerate의 스폰 높이 계산과 카메라 좌표 대입을 제거했다. 위치 X/Y/Z와 yaw/pitch는 유지하며 새 지형 내부에 겹쳐도 자동 이동하지 않는다. 최초 월드 진입의 스폰 계산은 유지한다.
- 변경 파일 서식 검사와 재생성 함수의 카메라 변경 제거/최초 스폰 유지 참조 확인을 완료했다. 빌드·실행은 하지 않았다.

## 자동 테스트 제거 (2026-09-13)

- 사용자 실행의 build/release/Testing/Temporary/LastTest.log에서 컴파일 뒤 기존6개 검사 중5개 통과, vulkan_world_smoke의30초 시간 초과를 확인했다. 로그는 중단 stage를 남기지 않아 원인을 확정하지 못했다. 높아진 스폰/작은 테스트 반경에서 drawn_chunks 조건이 충족되지 않았을 가능성을 설명했고, 이후 사용자는 수정 대신 모든 테스트 제거를 요청했다.
- `테스트 다 빼버려` 및 `ㄱㄱ` 승인으로 tests의 CPU 소스4개, CMake 테스트 대상/등록, testPresets, main.cpp의 두 smoke 모드/합성 입력/단계 판정/테스트 전용 카운터를 제거했다. build.ps1의 Check와 ctest 호출을 제거하고 build.bat는 build→package로 변경했다.
- 설정 저장/불러오기, 실제 게임 입력, F3 생성 편집/재생성, 렌더링, 프레임 제한 캡처와 성능 계측, 오류 처리/validation은 유지한다. 테스트 제거를 기존 시간 초과 문제 해결이나 게임 실행 통과로 기록하지 않는다.
- 기존 dev/profile/release 빌드 폴더의 루트 CTestTestfile.cmake도 삭제하여 과거 테스트 등록이 남아 실행되지 않게 했다. 실행 파일/로그/객체 등 과거 산출물은 기록으로 유지한다.
- 이 작업은 빌드·CMake configure·실행·패키징을 하지 않았다. 전체 소스 서식 검사, 테스트 코드/등록/옵션/카운터 잔여 참조 검사, 프리셋 JSON 파싱, quoted include 경로, 변경된 PowerShell 문법 확인을 통과했다. 아래 테스트 및 빌드 대기 절들은 당시의 작업 이력이며 현재 자동 테스트 실행 지침이 아니다. 기존 build/out의 산출물·로그도 현재 소스와 구분한다.

## 빌드 대기: 순환 Perlin 월드와 F3 생성 편집 (2026-09-13)

- 사용자 `실시` 승인 범위로 FastNoise2 v1.1.1/FastSIMD 주기적 2D·3D Perlin SIMD 확장, 131072² 단일 X/Z 순환 좌표, groundness PCHIP 그래프와 돌 지형, F3 초안/재생성/JSON 기본값 저장을 소스에 반영했다. Y192 수면과 4블록 보간은 유지한다. 원본 simplex 파일과 호출은 제거했다.
- 빌드·CMake configure·셰이더 컴파일·CTest·테스트 실행 파일·게임 실행·패키징은 하지 않았다. 새 소스의 컴파일, 실제 지형/입력/저장 UI, SIMD 경로/성능은 아직 검증되지 않았다. 기존 build/out 바이너리는 이 작업으로 갱신하지 않았다.
- 실행한 정적 확인: 전체 src/tests C++ clang-format 검사, lock의 13개 source-id 일치와 새 의존성3개 아카이브 SHA-256 일치, 기본 JSON 파싱·범위/주기 확인, CMake 내부 소스/quoted include 경로와 CTest6개 등록, RML XML·중복ID·스타일 경로, 패키징 라이선스 존재와 package.ps1 PowerShell 문법 파싱. 이 확인은 C++ 타입/링크 또는 실행 검증이 아니다.
- 소스 검토에서 수정: custom SIMD 구현은 Generator.inl→BasicGenerators.inl 순서가 필수라 include 정렬을 막았다. ImGui 수동 숫자 입력에도 범위를 강제하여 shift/index가 범위를 벗어나지 않게 했고, 비유한 값은 적용 전에 거부한다. 저장 파일의 정수 overflow와 잘못된 curve/settings를 검증한다.
- 추가/수정한 회귀 소스(미실행): world_generation은 곡선 제어점/방향/오버슈트, JSON 왕복과 유니코드 경로 저장·교체·잘못된 저장 시 이전 값 보존, SIMD tail을 포함한 2D/3D 양축 주기와 Y 비순환, 경계 주변 값/기울기, canonical chunk 동등성, 타일 설정 불일치 거부/불변 생성기 간 격리/고정 수면을 검사한다. world_rules는 독립 Perlin 생성·돌 재질·반대편 실제 halo와 canonical stream을, block_edits는 경계 편집/공유면/대각선AO dirty/시선을 검사한다. core_rules에는 wrap/delta 경계를 추가했다.
- world GPU smoke는 기존 메뉴/옵션 왕복 뒤 새 설정으로 재생성하고 양축 경계를 넘는 stage18로 확장했다. 재생성은 begin_frame 전에 실행하며 shader/pipeline/atlas를 유지하고 buffer/descriptor는 제출 완료 후 지연 해제한다. 기존 stream을 cancel/join하여 old ready 결과가 새 stream으로 넘어오지 않는다. 모든 smoke는 플레이어의 worldgen.json을 읽거나 쓰지 않는다.
- 다음 명시적 빌드에서 CTest6개와 SIMD 링크/호출을 우선 확인한다. 추가 수동 확인: F3 마우스 해제와 게임 입력 차단, 그래프 드래그/추가/삭제/숫자/스크롤, Regenerate의 편집 초기화와 Save의 다음 실행 기본값, 잘못된/쓰기 불가 설정 경로, 양축 경계에서 파괴·놓기·AO·타깃·물·F4, 재생성 반복 중 CPU/GPU 자원 수명, 장거리 생성/메모리/프레임 비용. 성능 향상 수치는 아직 주장하지 않는다.

## 빌드 대기: Esc 게임 옵션과 Tab 설정 제거 (2026-09-13)

- 사용자 `실시` 승인으로 인게임 Esc는 게임 화면 위 어두운 옵션 오버레이를 연다. Esc/게임으로 돌아가기 버튼으로 닫고 시작 화면으로 버튼으로 메뉴에 돌아간다. 렌더 거리는 시작 옵션과 공유한다. 시작 화면 Esc 종료 및 Tab 설정 토글을 제거했다. 기존 창 닫기 요청(SDL_EVENT_QUIT)은 유지한다.
- paused 상태와 마우스 캡처를 분리했다. 옵션 중 카메라/재질 선택/블록 편집을 막고, 전환 프레임의 나머지 게임 입력도 막는다. 포커스 상실은 옵션을 연다. 옵션 위에는 ImGui 입력/그리기가 겹치지 않으며 게임 prepare/render는 계속해 거리 변경을 반영한다.
- 기존 world smoke는 옵션 개방 후 거리 6회 변경, Esc 닫기/Tab 비동작/복귀 버튼/시작 화면/재입장(stage 15)까지 소스를 갱신했다. UI smoke는 시작 메뉴 Esc 비종료를 추가했다. 빌드·셰이더 컴파일·CTest·실행·패키징은 하지 않았고 변경된 테스트도 미실행이다.
- main.cpp 서식 검사와 RML XML 파싱, ID 중복/필수 옵션 ID/스타일시트 경로 확인을 통과했다. 구 render-settings 패널과 Tab 설정 안내·토글, Esc 종료 분기가 src/UI/README에서 제거됐음을 확인했다. SDLK_TAB의 남은 참조는 비동작 확인용 스모크 입력이며 종료 대입은 quit 버튼/SDL_EVENT_QUIT뿐이다.
- 다음 승인된 빌드에서는 기존 CTest 5개, 옵션의 전체 화면 어둡게 처리/중앙 배치, 실제 WASD·마우스·블록 클릭 차단과 복귀 클릭 통과 방지, 시작 옵션 Esc/시작 화면 Esc, 거리 공유/월드 재입장과 편집 보존, 포커스 상실·F11·F3 패널과의 입력 우선순위, DPI/창 크기를 확인해야 한다. 소스 검사로 화면이나 입력 동작을 검증했다고 보고하지 않는다.

## 빌드 대기: 시작 메뉴와 옵션 개편 (2026-09-13)

- 사용자 `실시` 승인으로 텍스처 소개 화면을 없애고 중앙에 싱글플레이/옵션/게임 종료를 세로로 배치했다. 옵션에는 공유 렌더 거리와 돌아가기를 넣었다. 월드 최초 진입은 현재 옵션 값으로 생성하며 메뉴 복귀 후 재입장은 기존 WorldView를 사용한다. Esc는 옵션에서 시작 메뉴, 월드에서 시작 화면, 시작 화면에서 종료다.
- 기존 GPU 스모크 소스를 새 UI에 맞췄다. UI smoke는 옵션 버튼/±/돌아가기, HUD 거리 공유, resize/F3, 양수 크기·화면 경계·세로 순서를 검사한다. world smoke는 메뉴 싱글플레이 클릭으로 시작한다. 빌드·CTest·게임 실행·패키징은 하지 않았으며 수정된 스모크도 미실행이다.
- main.cpp 서식 검사, 메뉴/HUD/알림의 XML 파싱·ID 중복·스타일시트 경로, 필수 옵션 ID 및 싱글플레이/옵션/게임 종료의 문서 순서를 확인했다. 제거한 갤러리/텍스처 선택/상태 버튼 참조가 src·UI·README에 남아 있지 않다. 로컬 RmlUi 소스로 range 변경 이벤트 동작과 사용한 flex/box-sizing 속성을 확인했다. 실제 레이아웃이나 컴파일 검증은 아니다.
- 다음 승인된 빌드에서는 기존 CTest 5개 및 실제 메뉴 배치, 마우스/키보드 옵션 조절, 옵션 Esc·돌아가기, 입장 전 거리 적용, 월드→메뉴→옵션→재입장의 값/편집 유지, 종료 버튼, F2/F3/F11, 최소 창 크기와 DPI 배치를 확인해야 한다.

## 빌드 대기: F2 스크린샷 / F4 와이어프레임 (2026-09-13)

- 사용자 `실시` 승인 범위에 두 단축키, 화면 알림, UI 도움말 및 문서를 반영했다. 빌드·셰이더 컴파일·CTest·게임 실행·패키징은 하지 않았다.
- 변경 C++ 5개 파일의 clang-format 검사, 메뉴/월드/알림 RML XML 파싱, UI ID 중복 및 스타일시트 경로 확인을 통과했다. 사용 중인 SDL 시간 API와 RmlUi 알림의 focus/pointer-events 설정은 고정된 로컬 의존성 선언을 확인했다. 이는 컴파일 또는 화면 검증이 아니다.
- F2는 UI까지 그린 프레임을 실행 파일 옆 screenshots 폴더에 시간 이름 PNG로 저장한다. 경로 중복 접미사, UTF-8 실행 경로/네이티브 파일 스트림, BGRA 채널 변환, 저장 실패 후 게임 지속, 프레임 생략 시 요청 유지 경로를 소스 검토한다. CLI 캡처 실패는 성공 처리하지 않는다.
- F4는 실제 AO 대각선을 포함한 삼각형의 세 모서리를 LINE_LIST로 그린다. 기존 GPU 면 데이터 재사용, 양쪽 shader specialization, 생성/실패 시 파이프라인 해제, 물 포함 단일 선 패스와 기존 UI 패스 분리를 소스 검토한다.
- 다음 승인된 빌드에서는 C++/GLSL 및 기존 CTest 5개, 실제 F2/F4 입력과 키 반복 방지, F3를 켠 캡처, F11/창 크기 변경 뒤 PNG 크기·색·알파, 한글 경로와 저장 불가 폴더, 알림의 입력 통과, 일반/선 모드 왕복과 물·음의 면·AO 대각선, Vulkan 수명 오류를 확인해야 한다. 기존 GPU 스모크만으로 새 단축키나 선의 화면 결과가 검증됐다고 간주하지 않는다.

## 빌드 대기: 강 생성 제거

- 사용자 `일단 강부터 없애고 생각해보자. 실시` 승인으로 강 노이즈/domain warp/폭 계산과 강·계곡 절삭을 제거했다. TerrainSite는 기준 높이만 보관하며 밀도·빠른 균일 판정·스폰 높이에서 강 절삭 상한을 없앴다. 바다·Y=192·진폭32·그라디언트·4블록 보간은 유지한다.
- world_rules에서 특정 강 좌표에 물이 반드시 존재한다는 가정을 제거하고, 제어된 해저 높이로 물 채움과 수면 상한을 검사하도록 수정했다. 새 테스트를 실행하지 않았다. 빌드·CTest·게임 실행·패키징도 하지 않는다. 사용자에게 보이는 지형 결과는 다음 빌드 후 확인해야 한다.
- 소스 확인 완료: 변경한 C++ 서식 검사 통과, src/tests의 river/warp/기존 site_node 참조 제거 확인, sea_level=192·density_amplitude=32·density_step=4와 물 배치 조건 유지 확인. 생성기와 관련 문서를 함께 갱신했다.

## 이전 소스 작업: 3D 지형·강·물·이웃 데이터 후 메싱

- 후속 사용자 `실시`로 루트 `build.bat`를 추가했다. release 빌드·CTest 성공 후에만 패키징하고, 실패 코드를 유지하며 완료 창을 닫지 않는 실행 래퍼다. 이번 작업에서는 bat/빌드/테스트/패키징을 실행하지 않았으며 경로·분기·텍스트 형식만 검토했다.
- 이후 사용자가 더블클릭 시 PowerShell 7 탐색 실패를 보고했다. 이 PC의 PowerShell 7.6.5는 Codex runtime 전용 경로에 있어 일반 Explorer 환경의 PATH/Program Files 탐색으로 발견되지 않았다. 현재 사용자 Codex runtime 경로 fallback과 `--check`를 추가했다. PATH를 Windows 시스템 디렉토리로만 제한한 자식 실행에서 `build.bat --check`가 해당 PowerShell을 찾아 버전 7.6.5, 종료 코드0을 반환했다. 실제 빌드·CTest·패키징은 실행하지 않았다.

- 사용자 `실시` 및 `계속해` 승인으로 월드 좌표 4간격의 3D 심플렉스 보간/수직 그라디언트, 2D 노이즈 강과 계곡, 고정 수면 Y=192의 물 생성·렌더링을 소스에 추가했다. 청크 데이터는 독립 생성하고, 경계 면/AO는 필요한 이웃 데이터가 준비된 뒤만 계산한다.
- 2D 타일 → 개별 청크 데이터 → 개별 청크 메시로 스케줄을 분리했다. 대각선 포함 데이터 전용 외곽 링, 준비된 후보 큐, 최대8개 메싱/완료 컬럼, immutable snapshot과 개별 취소 상태를 적용했다. 편집 메시도 준비된 실제 halo+변경 기록을 사용하며 절차 경계 샘플을 제거했다.
- C++/GLSL 컴파일, CMake configure, CTest, 게임 실행, clang-tidy는 하지 않았다. 기존 build/out 실행 파일·화면·테스트 로그는 이전 높이맵 생성기다. 이번 소스 검토/서식/참조 검사를 실제 동작·성능 검증으로 보고하지 않는다.
- 빌드 없는 확인 완료: clang-format 검사, CMake 내부 소스 경로, 월드 아틀라스 6개 텍스처 경로, RML XML 파싱, water PNG SHA-256 보존, 제거한 terrain_height/terrain_block/build_column 및 이전 아틀라스 폭 참조가 src/shaders/tests에 남지 않았는지 확인했다. 생성·메싱 대기/취소 상태, immutable halo와 편집 합성, 물 draw 구간을 소스에서 검토했다.
- world_rules를 독립 생성/순서·공유 캐시, simplex 범위·연속성, 고정 수면, 고체-물 면/AO, 준비된 대각선 이웃 AO 이음새, 누락된 데이터 거부, immutable 복사, 스트리밍 완료·취소 검사로 수정했다. block_edits는 실제 준비된 이웃 snapshot으로 재로딩·경계 편집을 검사하고 수중 고체 시선 선택도 검사하도록 수정했다. 테스트 수는 여전히5개이며 모두 새 소스에 대해 미실행이다.
- 다음 승인 빌드에서 C++ 타입/동기화와 world.frag 6타일 연결, 물 firstInstance/알파/깊이/양면 패스, 컬럼 공개·반경 변경·취소, 경계/AO/편집, 강이 막히지 않는지와 고정 수면을 확인한다. 투명 물 정렬은 청크 단위이며 수중/교차 표면의 정확한 정렬은 보장하지 않는다. 새 생성기의 지형 모양·생성 시간·메시 양·RAM/VRAM은 미측정이다.

## 마지막 빌드: 블록 편집·F11·수평 WASD (2026-09-07)

- 사용자 `빌드실시` 승인으로 dev/release C++·GLSL 빌드를 완료했다. 최종 각 CTest 5개, 총 10개 통과. `build/dev/block-edit-final-check.log`, `build/release/block-edit-final-check.log` 참조. profile은 갱신하지 않았다.
- `block_edits`의 시선/사거리, 음수 좌표, 미로딩 중단, 편집 재로딩·복원, 높이 경계, 이웃 면 제거·복구 및 AO dirty 범위 검사가 통과했다. 기존 UI/월드 GPU 스모크도 통과했다. dev Vulkan error 0, 기존 로더 warning 4, UI 문제/텍스처 실패 0. `build/dev/world-smoke.png`에서 새 블록 선택 슬롯과 안내의 배치를 확인했다.
- 첫 release 월드 스모크는 예상 컬럼 수 불일치로 1회 실패했다(`block-edit-check.log`). 진단 문구를 보강한 뒤 같은 테스트 10회는 재현 없이 통과했다. 소스 검토에서 RmlUi Update가 prepare 이후 거리를 바꾸는 순서 문제를 발견하여 해당 프레임은 완료 판정을 미루도록 수정했다. 최초 실패와의 인과관계를 재현으로 확정한 것은 아니다. 최종 코드에서 release 월드 스모크 5회 연속 통과(`block-edit-final-repeat.log`).
- 실제 마우스로 파괴·놓기, 대상 테두리, F11 왕복/창 크기 복원, pitch ±89° 수평 이동은 이번 자동 스모크의 검사 범위가 아니다. CPU 편집 검사와 셰이더 빌드 성공을 실제 입력 검증으로 확대 해석하지 않는다. 편집 중 이웃 컬럼 업로드, 대량 편집·장시간 부하는 추가 검증 대상이다.
- `out/Sandbox` 실행 파일·에셋·셰이더·README를 갱신하고 release 빌드와 실행 파일 SHA-256 일치를 확인했다. 해당 실행 파일로 기본 반경 12 월드를 10,000프레임 실행하여 441컬럼, UI 문제/텍스처 실패 0을 확인했다. `build/release/block-edit-package-run.log`, `block-edit-world.png` 참조. 화면에서 지형·재질 선택·안내를 확인했다. release의 검증 레이어는 꺼져 있다.

## 이전 소스 작업 기록: 블록 파괴·놓기

- 사용자 `실시` 승인으로 좌클릭 파괴/우클릭 놓기, 숫자 1/2/3 재질 선택, 6블록 시선 판정, 대상 테두리 및 선택 HUD를 소스에 추가했다. 컬럼 재로딩 시 세션 편집을 반영하고 경계 면/AO에 영향을 받는 개별 청크만 재메싱한다.
- 컴파일·셰이더 빌드·CTest·게임 실행은 하지 않았다. 기존 실행 파일과 아래 검증 기록에는 이번 변경이 포함되지 않는다. 소스 검토, C++ 서식 검사, RML XML 파싱, UI ID 중복/필수 ID, 에셋·소스 경로 확인을 완료했다. 렌더링 결과를 확인한 것은 아니다.
- `tests/edit_tests.cpp` 및 CTest `block_edits`를 추가했다(소스상 총 5개). 사거리 끝점, 음수/모서리 시선, 미로딩 중단, 편집 재로딩·원상복구, 수직 범위와 이웃 면/AO 갱신 범위를 검사하도록 작성했으며 미실행이다.
- 다음 승인된 빌드에서 C++/GLSL push constant 96바이트와 새 varying 연결, 기존 4개 및 새 CPU 테스트를 확인한다. 실제 좌우 클릭/재질 선택, 메뉴 입력 분리, 선택 테두리/HUD, 편집 후 거리 축소·확대, 이웃 컬럼 업로드 중 반복 편집, Vulkan 자원 해제를 추가로 확인해야 한다. 기존 GPU 스모크만으로 편집 기능이 검증됐다고 보고하지 않는다.
- 편집 재메시는 메인 스레드의 soft budget을 사용한다. 한 청크 작업과 저장된 편집을 새 컬럼 데이터에 적용하는 작업은 도중에 중단하지 않으므로 2ms를 넘을 수 있다. 대량 편집 부하 성능은 미측정이다.

## 이전 소스 작업 기록: F11·수평 WASD

- 사용자 `실시` 승인으로 F11 창/전체화면 토글, yaw 기준 수평 WASD, 입력 안내를 변경했다. Space/Ctrl 수직 이동은 유지한다.
- 로컬 SDL3 헤더의 API/전체화면 이벤트와 입력 경로를 검토했다. 빌드·실행 요청은 없어 컴파일이나 실행 검증을 하지 않았다. 다음 빌드에서 전체화면 왕복/창 크기 복원과 pitch ±89°에서도 WASD 높이·속도가 유지되는지 확인해야 한다.
- 기존 실행 파일과 아래 검증 기록에는 이 변경이 포함되지 않는다.

## 이전 빌드: AO·버텍스 풀링·인게임 렌더 거리 (2026-09-07)

- 사용자가 `실시`로 승인한 범위: 그리디 제거, 노출 면별 복셀 AO, 압축 버텍스 풀링, 인게임 거리 설정 및 빌드.
- dev/release C++·GLSL 빌드 성공, 각 CTest 4개(총 8개) 통과. `build/dev/ao-pulling-check.log`, `build/release/ao-pulling-check.log` 참조.
- CPU: 분리된 노출 면 수, 재질/방향/좌표 패킹, AO 점유 조합 8개와 대각선 선택, 컬럼 경계 AO 연속성, 면 방향, 거리 확대/축소/재요청을 확인했다.
- GPU 통합 검사: SDL 클릭 4회와 슬라이더 방향키 2회로 거리 변경 6회, 반경 1/2/3의 5/13/29컬럼 로딩·해제를 확인했다. 최종 `smoke stage=8`, dev Vulkan error 0, 기존 로더 warning 4, UI 문제/텍스처 실패 0. `build/dev/world-smoke.png`, `build/release/world-smoke.png`가 갱신됐다.
- 기본 반경 12를 dev 검증 레이어로 3초 실행하여 441컬럼 공개를 확인했다. Vulkan error 0, warning 4. 기록: `build/dev/ao-full-world.log`.
- 배포 폴더 갱신 후 10,000프레임 실행과 실제 화면 확인 완료. `build/release/ao-settings.png`, `build/release/ao-pulling-run.log`. 계단 모서리의 AO와 거리 슬라이더/버튼/HUD를 확인했다.
- 기본 시드/반경 12에서 공개된 면 데이터는 1,048,680바이트(약 1 MiB)다. 면당 4바이트이며 전체 GPU 메모리/descriptor/CPU 청크 데이터는 포함하지 않는다. 이전 그리디 버전 대비 FPS 향상이나 메모리 감소를 측정한 비교 벤치마크는 아니다.
- clang-tidy 종료 0, analyzer error 없음. `build/dev/ao-analysis.log`에 크기가 고정되거나 루프 범위가 제한된 정수 곱의 widening 경고 8개가 남는다. 경고를 숨기거나 정적 검사가 무진단이라고 보고하지 않는다.
- profile은 갱신하지 않았다. 최대 반경 64의 전체 로딩 성능, 장시간 연속 이동, 슬라이더 마우스 드래그 수동 검증은 별도다. 거리 설정은 현재 실행 중 유지하며 파일 저장은 구현하지 않았다.

아래는 이전 버전의 검증 기록이다.

## 첫 월드 빌드 검증 (2026-09-07)

- 첫 언덕 월드, 청크 데이터/greedy mesh, 비동기 생성과 컬럼 공개, GPU staging 업로드, depth/culling, 자유 비행과 메뉴/HUD 연결을 소스에 추가했다. LOD/편집/물리는 이번 구현에 포함하지 않는다.
- 사용자 요청 후 dev/release에서 C++ 및 월드 셰이더 빌드 성공. 각 CTest 4개, 총 8개 통과. 기록은 `build/dev/world-check.log`, `build/release/world-check.log`와 각 `Testing/Temporary/LastTest.log`다.
- dev 월드 검사: 13컬럼 로딩, 카메라 이동과 창 크기 변경, 새 영역 13컬럼 준비 및 그리기 성공. 종료 해제까지 Vulkan error 0, 기존 로더 warning 4, UI 문제/텍스처 실패 0. `build/dev/world-smoke.png`를 직접 확인했다.
- 배포 폴더 `out/Sandbox`를 갱신하고 반경 12의 441컬럼 월드를 10,000프레임 실행했다. 종료 시 308청크 draw, UI 문제/텍스처 실패 0. `build/release/world.png`에서 언덕 지형·재질·HUD를 확인했다. 실행 기록: `build/release/world-run.log`. 이 수치는 장거리 성능 벤치마크가 아니다.
- profile은 재빌드하지 않았다. 마우스 수동 조작·메뉴 재입장·최소화 복원·지속 이동 부하 검증은 남아 있다. 아래 기존 개발 기반/계측 기록과 구분한다.

## 완료

- Clang 23.1.0으로 dev / profile / release 세 프리셋 빌드 성공.
- 각 프리셋에서 CTest 2개 통과: 좌표 경계 검사, 실제 SDL3/Vulkan/RmlUi/ImGui 통합 실행. 총 6개 테스트 실행 성공.
- 스모크 실행: 100프레임, 텍스처 카드 2회 선택, 창 크기 2회 변경, 상태 버튼 및 F3 토글, 주요 UI 요소가 화면 안에 배치되는지 검사, 마지막 프레임 PNG 저장.
- dev에서 종료 자원 해제까지 Vulkan validation error 0, RmlUi 오류/경고 0, PNG 로딩 실패 0.
- dev의 Vulkan warning 4개는 설치된 Epic 중복 오버레이 및 Bandicam/Overwolf 레이어의 API 버전 관련 로더 메시지다. 시스템 설정을 수정하거나 경고를 숨기지 않았다.
- clang-tidy 종료 코드 0, 프로젝트 코드 진단 없음. 외부 헤더 경고는 필터링되어 집계 메시지만 남는다.
- RenderDoc 1.46으로 실제 `.rdc` 생성 및 내장 썸네일 추출 성공.
- Tracy 0.14.1로 `.tracy` 생성 및 CSV 내보내기 성공. Game UI update/render, Debug UI render, 프레임 및 renderer 구간이 기록됨을 확인했다.
- UI PNG를 직접 확인했다. 초기 RmlUi 기본 display 문제를 수정한 뒤, 한글 텍스트/배치/다섯 텍스처/디버그 패널을 확인했다.
- `out/Sandbox`에 release 실행 파일, 에셋, 셰이더, Microsoft CRT 파일과 외부 라이선스를 모으고 해당 실행 파일을 별도로 실행하여 화면 저장 성공.

## 로컬 산출물 (Git 제외)

| 경로 | 내용 |
|---|---|
| `out/Sandbox/sandbox.exe` | 사용자 실행 파일 |
| `build/dev/final-tests.log` | 개발 검증 결과 |
| `build/profile/final-tests.log` | 최적화 계측 빌드 검증 결과 |
| `build/release/final-tests.log` | 배포 빌드 검증 결과 |
| `build/profile/analysis.log` | 정적 검사 결과 |
| `build/release/smoke.png` | 게임 UI 화면 |
| `build/release/debug-ui.png` | 게임 UI + 개발 패널 화면 |
| `build/profile/captures/sandbox_capture.rdc` | RenderDoc 프레임 |
| `build/profile/captures/renderdoc.png` | RenderDoc에서 추출한 썸네일 |
| `build/profile/captures/sandbox.tracy` | Tracy 기록 |
| `build/profile/captures/zones.csv` | 기록 구간의 CSV 확인 |

## 범위와 제한

- 월드/청크 빌드와 제한 실행을 확인했다. LOD/물리는 미구현이며 편집은 dev/release 빌드와 CPU 검사를 완료했다. 장거리 성능 측정은 수행하지 않았다. 기존 메뉴 화면의 FPS나 GPU 시간을 월드 성능으로 보고하지 않는다.
- 실제 GPU 테스트는 이 PC에서 수행했다. 다른 GPU/드라이버/운영체제 검증은 아직 없다.
- RmlUi 고급 필터·마스크·레이어 합성은 현재 자체 UI 렌더러에 미구현이다. 상세 범위는 `architecture.md` 참조.
- profile/release는 검증 레이어가 꺼져 있다. 이 모드에서 error 카운터 0은 Vulkan 검증을 수행했다는 뜻이 아니다.

## FPS 표시 갱신 간격 변경 검증 (2026-09-07)

- 순간 FPS와 프레임 간격의 표시 갱신을 0.25초 간격으로 변경했다. 같은 최신 단일 프레임 샘플을 사용하며 평균 처리는 없다. 프레임 간격 표시는 소수점 셋째 자리까지 확장했다.
- 사용자의 명시적 요청 후 release 빌드와 CTest 2개 통과. 기록: `build/release/fps-refresh-check.log`.
- `out/Sandbox` 갱신 후 개발 패널을 켜고 10,000프레임 실행 성공. `build/release/debug-ui.png`에서 FPS와 소수점 셋째 자리의 프레임 간격 표시를 확인했다. 갱신 간격은 소스에서 검토했으며 별도 시간 계측은 하지 않았다.
- 실행 기록: `build/release/fps-refresh-run.log`. UI 문제/텍스처 실패 0, IMMEDIATE 유지. release 검증 레이어는 꺼져 있으며 dev/profile은 이번에 재빌드하지 않았다.

## 순간 FPS 표시 검증 (2026-09-07)

- F3 상태 패널에 순간 FPS 표시(`1000 / frame_ms`, 0 이하 간격 보호)를 추가했다. 사용자 요청에 따라 이동 평균 대신 표시 중인 프레임 간격을 사용한다.
- 사용자의 명시적인 빌드 요청 후 release만 빌드하고 CTest 2개 통과. 기록: `build/release/fps-check.log`.
- `out/Sandbox` 갱신 후 100프레임 실행 성공. `build/release/debug-ui.png`에서 FPS와 프레임 간격 표시를 직접 확인했다. UI 문제/텍스처 실패 0, IMMEDIATE 유지. 실행 기록: `build/release/fps-run.log`.
- dev/profile은 이번에 재빌드하지 않았다. release는 Vulkan 검증 레이어가 꺼져 있으므로 이번 실행은 새 Vulkan validation 검증 기록이 아니다.

## VSync 기본 OFF 변경 검증 (2026-09-07)

- dev/profile/release 재빌드 및 각 CTest 2개, 총 6개 통과. 기록은 각 `build/<preset>/vsync-check.log` 및 `Testing/Temporary/LastTest.log`에 있다.
- RTX 3080에서 최초 생성과 창 크기 변경 후 모두 `IMMEDIATE (VSync off)` 선택을 확인했다. dev Vulkan error 0, 기존 로더 warning 4, UI 문제/텍스처 실패 0.
- `out/Sandbox` 실행 파일을 갱신하고 100프레임 실행했다. 개발 패널에 실제 표시 모드를 출력하며 `build/release/debug-ui.png`도 갱신했다.
- MAILBOX/FIFO 대체 경로는 이 장치에서 실행하지 않았다. 위 RenderDoc/Tracy 산출물은 VSync 변경 전 개발 기반 검증 기록이다.
