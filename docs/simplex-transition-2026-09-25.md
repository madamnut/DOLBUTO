# 4D Simplex와 개별 워핑 · 2026-09-25

AI 작업 컨텍스트. 사용자 `실시하고 초기값은 너가 알아서 넣어봐` 승인 범위: 3D를 제외한2D신호의 4D Simplex 전환, 복수 워핑 정의와 신호별 연결, 초기값, 전후 비교, Release 빌드·패키징.

## 구현과 좌표

- Groundness/Smoothness/Weirdness/Precipitation/Jagged 및 워핑 변위는 FastNoise2 Simplex4D를 사용한다. shape3D는 기존 PeriodicPerlin 그대로다. 밀도 수식·4블록 삼선형 보간·독립 청크·컬럼 공개/조명/메시 규칙은 바꾸지 않았다.
- X/Z는 double로131072에 정규화한 뒤 theta=2*pi*p/L, R=L/(2*pi)로 (R*cos(thetaX),R*sin(thetaX),R*cos(thetaZ),R*sin(thetaZ))에 임베딩한다. 노이즈 노드의 scale은2^(spacing_log2-octave), seed는 world_seed+seed_offset+octave(mod2^32). 각 옥타브는 FastNoise 출력 범위에 정규화된 weight를 곱하고 SIMD Add graph로 합성한다. weight0 노드는생략, 전체0은Constant0. 노이즈 출력에 별도의 관측 범위 재매핑/clamp는없다. 같은scale라도 Perlin과 Simplex의 시각적인 특징 크기/분포는 같지 않다.
- 변위는 원래 위치에서 두 독립 Simplex4D 신호를 계산한다. Z변위 seed는 X변위보다7936크다. 변위*strength를 X/Z에 더한 뒤 최종 노이즈의 토러스 좌표로 바꾼다. 하나의 신호의 모든옥타브는 같은 변위 사용. 재귀적/progressive/옥타브별 워핑은없다.
- terrain_inputs는 요청된 신호를 warp ID별로묶어서 변위2개와 토러스 좌표를 한 번만 준비한다. 원래좌표의 토러스도 batch 내공유. CPU thread_local SoA 버퍼와 최대4096항목의 unwarped 축 sin/cos cache 사용. warped double좌표는 float키로캐시하지않아 분수 정밀도 손실을 피한다. 여러 map API 요청 사이에서 임의의 변위결과를 전역 보관하지않는다. 생성기 graph는불변이고 worker간scratch를공유하지않는다.
- Temperature 원신호만 (R*cos(thetaX),R*sin(thetaX),canonicalZ+displacementZ,0)의 4D표본이다. X만순환, Z는직선. 위도띠는 warpedZ가 아닌 original canonicalZ로계산한다. belt=pow(.5-.5*cos(2*pi*z/L),latitude_power); 변동*belt*(1-belt)를더한다. 극과적도의온도고정/끝점연결유지. 워핑이Z축을월드밖으로옮겨도 중간에잘라붙이지않는다. 최종기온은기존대로모든Z를월드정규화한다.

## 초기값 / 저장

schema7: noise_2d="simplex4d_torus", warps=[{id,name,enabled,strength,noise}], 각NoiseSettings.warp는안정된ID또는빈문자열. 최대16개, id중복/없는참조/워핑의워핑/shape3D워핑은오류. 이름변경은ID에영향없고삭제시연결된신호를없음으로바꾼다.

| ID / 이름 | 간격 | 강도 | 옥타브/가중치 | seed_offset | 연결 |
|---|---:|---:|---|---:|---|
| land / 큰 지형 |2048|192|3 / 1,.5,.25|8191|Groundness, Precipitation|
| ridges / 능선과 계곡 |512|64|3 / 1,.5,.25|24239|Weirdness→PV|

Smoothness/Temperature/Jagged는워핑없음. 두워핑은ON. 강도는변위각성분의정규화노이즈배율이지 실제최대이동을고정하는범위가아니다. 기존신호의spacing/seed/weights,스플라인,3D파라미터는유지한다. 프리셋의지형형상수치까지재튜닝하지않는다.

- assets/worldgen/default.json 및 GenerationConfig 기본값에위워핑을넣었다.
- out/Sandbox/worldgen.json은승인된초깃값적용으로schema/워핑만바꿨다. 이전전체파일은 worldgen.before-simplex4d-2026-09-25.json.bak. settings.json 바이트동일. 기존seed/곡선/스플라인/노이즈/3D값의JSON동일성을확인했다.
- 구형1..6을읽을수있지만2D알고리즘은새것이므로동일지형을보존하지않는다. v6의단일domain_warp는legacy ID,1옥타브,기존간격/강도/ON상태,groundness/weirdness참조로이행한다. v1..5는워핑없음. 읽기만으로파일을쓰지않는다.
- F8/별도미리보기/독립편집기가같은C++설정과생성기를사용한다. 추가·복제·삭제·이름변경·신호별선택·개별옥타브가중치지원. 미리보기워핑전은임시사본의모든워핑을OFF. 기존Groundness개별옥타브보기는spacing/seed_offset을같이옮겨실제4D해당옥타브를표시한다. 저장/확정/재생성동작은기존대로분리한다.

## 확인 결과

- EXCLUDE_FROM_ALL torus_benchmark를명시적으로빌드·실행. CTest/일반자동테스트/시작검사등록없음.
- 기본설정과6신호모두같은워핑으로연결한설정에서12지도×40위치×2구성=960쌍의X/Z주기와음수좌표일치. .03125블록 간격의 X경계양옆 원신호차이최대.00122944; Z경계도.08이하진단통과. 모든지도끝점만같은것으로연결을판단하지않았다.
- 온도적도/극고정,단독map와terrain_inputs동일,4worker배치동일,워핑OFF와미지정동일,전체0가중치0,삭제후참조유효,없는ID거부,JSON왕복확인.
- 3D shape256표본은기존PeriodicPerlin의직접계산과바이트동일. (0,0)/(8191,8191)의6높이청크에서독립생성과공유tile생성49,152블록동일.
- 격리된전후편집기를 --no-browser로실행. 실제HTTP API로전체월드384²의Groundness/Smoothness/PV/Temperature/Precipitation/EffectiveHeight를양쪽에서계산. 전체유한값,네변끝점일치확인. 새로운설정의구형이행/초안저장·불러오기/확정·불러오기를격리폴더에서확인. 게임폴더의확정파일을API검증용으로쓰지않았다.
- 전체384²점표본의기준높이(잔굴곡포함,3D미포함) 해수면미만비중38.808%→38.631%,평균높이215.271→213.158. Groundness표준편차.21237→.20133. 노이즈값분포의변화가있고같은시드의지역배치는달라진다. coarse 표본비중이지정확한블록별바다면적이아니다. 전체지도그림은고주파옥타브가픽셀보다작아aliasing이있을수있으며아티팩트소멸의증거로삼지않는다.
- node --check와변경C++ clang-format --dry-run --Werror 통과. 실제브라우저/F8입력조작은사용자금지에따라하지않았고시각적UI배치는자동검증하지않았다. 지도와게임자체캡처PNG는읽어확인했다.

## 성능 (동일PC Release)

256개16×16격자,각구현2000배치×11라운드교대. 최초라운드는예열로별도보존하고10라운드중앙값. OS/CPU클럭고정없음. 기존Perlin은직접2D호출,새것은실제generator의좌표준비/공유버퍼를포함. 노드생성/설정파싱은타이머밖. 캐시는예열됨.

| Groundness 계산 | 중앙값 ns/표본 | 256표본당 |
|---|---:|---:|
| 기존Perlin2D,워핑없음 |5.045|.00129ms|
| 새Simplex4D,워핑없음 |34.640|.00887ms|
| 새Simplex4D,land워핑포함 |90.552|.02318ms|

단순노이즈샘플은더비싸다. 마지막행은알고리즘전환뿐아니라3옥타브변위2개와추가삼각함수비용을포함하므로순수4D/2D비율이아니다. 게임FPS비율로해석하지않는다.

실제게임은전후동일3좌표,렌더반경12(표시441/데이터545컬럼),동일settings/카메라/정오,각3회교대실행. --profile-world + --profile-gpu 24steady표본,240예열,초회화면캡처. 총18실행전부exit0/표시441컬럼/런타임오류0. 성능실행의Vulkan validation은OFF이고새Vulkan자원변경없음.

| 기존지역이름 / 컬럼좌표 | 전체공개중앙값 전→후(ms) | 컬럼생성평균의실행별중앙값 전→후(ms) | 2D+warp 작업스레드누적중앙값 전→후(ms) |
|---|---:|---:|---:|
| coast / 7040,5504 |1564.686→947.676|1.569→1.302|1.136→5.225|
| land / 3456,1152 |969.391→1410.916|3.659→1.729|1.128→5.226|
| hills / 8064,3456 |1276.550→1097.318|1.637→1.449|1.193→7.165|

이름은기존지형기준이며변경후land가바다가되는등형상이달라졌다. 따라서전체시간증감을알고리즘만의성능효과라고주장하지않는다. 새2D타이머는좌표준비·분류까지포함하고warp하위시간을exclusive로빼서중복합산하지않는다. 누적5~7ms는병렬worker경과시간합이며전체로딩벽시계추가시간이아니다. 이번표본에서노이즈가전체생성병목으로바뀌지는않았다.

## 빌드 / 보관

Release빌드·패키징완료. 처음추가한NoiseSettings.warp의누락초기화경고는멤버기본초기화로해결했고최종변경C++빌드경고0. 보존검사Python이Windows기본cp949로한글JSON을읽다실패한것은UTF-8지정후재확인통과. 원본파일손상없음.

- sandbox.exe (빌드=배포): 221656688e9aecd54866c03e831d13c90e090454f80a43bedc2ae6c58cbbe2a7
- worldgen_editor.exe (빌드=배포): 88385ccf27b952e8c676dd501bbdd3f9563fa5e70cfccac2f02afc552ff878cf
- 새worldgen.json: e16c2eed246e6b85eca968fcf5e2c98a7328cb309a1c60bdf03862dbb7ddb7db
- 상세결과: docs/benchmarks/simplex-transition-2026-09-25-results.zip
- build/release/simplex-transition-2026-09-25에전후바이너리/원본설정/소스사본/API비교스크립트/게임비교스크립트/원시CSV/지도/캡처를보관한다. UI자동화/합성입력/컴퓨터유즈없음. 새조명·렌더링최적화나바이옴/식생은추가하지않았다.
