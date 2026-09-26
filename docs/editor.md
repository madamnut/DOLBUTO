# 독립 월드 생성 편집기

2026-09-26 웹 스플라인 관계 편집: [연결 트리·제어점 표·경로·미리보기](editor-spline-relations-2026-09-26.md). 현재 중첩 구조를 명시적으로 표시하며 생성 규칙은 변경하지 않는다.

2026-09-26 최신 생성기: [주기적 Double Perlin 전환](periodic-double-perlin-2026-09-26.md). 아래 Simplex/격자/단순3D 설명은 이전 이력이다. 현재 schema10, 공유 Shift, 원본 중첩 Hermite, Blended3D를 사용한다.


## Minecraft 원본 비교 (2026-09-26)

왼쪽 Minecraft 원본 버튼은 별도 읽기 전용 minecraft.html을 연다. 일반26.2의DoublePerlin/64비트시드/Shift/중첩원본곡선을editor전용C++로평가하며게임world_core와저장schema는불변이다. 현재초안을사본으로읽어지도·원시곡선비교,부모창없으면확정설정fallback. 저장기능없음. 상세 [minecraft-reference-page-2026-09-26.md](minecraft-reference-page-2026-09-26.md).

## 단일 높이 곡선 제거 / schema9 (2026-09-26, 최신)

Groundness→높이 편집기와 이에 의존한 구형조합/스플라인 사용 전환을 제거. 현재복합스플라인만사용하며 /api/curve의height종류도제거. 저장에서 height_curve/terrain/splines.enabled 제외. schema5..8 복합스플라인ON 규칙은메모리에서이행하며파일자동수정없음. 아래 구형 높이곡선/schema8/모드전환설명은이력이다. 상세 [height-curve-removal-2026-09-26.md](height-curve-removal-2026-09-26.md).

## Snowcapped 편집 체계 (2026-09-26, 최신)

사용자 `실시`로 왼쪽 지형/노이즈/기후 전체 목록, 가운데 격자·곡선, 오른쪽 지도 구성으로 교체했다. 아래 지도 중심/스플라인 별도 modal 설명은 이전 이력이다. Snowcapped 원본 UI 배치·팔레트를 참고해 자체 API에 적응했으며 MIT 고지는 assets/licenses/Snowcapped.txt. 제어점·접선 드래그와 하위 단계 자체 축 미리보기, 칸복사/붙여넣기, 자동갱신을 추가했고 바이옴 규칙/실제 지형 계산식은 그대로다. 상세 [editor-snowcapped-2026-09-26.md](editor-snowcapped-2026-09-26.md).

## Groundness 개별 옥타브 간격 / schema8 (2026-09-25)

Groundness에만간격지수대신각옥타브의블록간격입력(4..131072,소수가능)을제공한다. 공통gain은Groundness가항상개별weights이므로웹에서표시하지않는다. 나머지노이즈/워핑은그대로다. count추가시마지막간격절반(최소4),weight0;줄일때배열을같이줄인다. API상태/구형파일불러오기는schema8과명시적spacings로정규화한다. 개별옥타브미리보기는선택한spacing을단일배열로유지하고seed_offset에원래옥타브index를더한다. 상세검증은groundness-spacing-2026-09-25.md.

## schema7 / 4D Simplex / 워핑 목록 (2026-09-25)

현재 저장 형식은 schema7이다. 노이즈 작업의 워핑 목록에서 추가/복제/삭제/이름 변경과 enabled/strength/NoiseSettings(옥타브별 가중치 포함)를 편집한다. 각2D신호와 기후에 적용 워핑 선택이 있고 없음도 가능하다. 목록 삭제 시 모든 참조를 비운다. id는 안정된 참조이며 이름 변경과 독립이다. 최대16개, 중첩워핑은없다. 워핑 전 미리보기는 임시config의 전체enabled만끈다. 실제 월드/확정파일은 자동변경하지 않는다. F8도같은schema/함수사용. 아래schema6·공유워핑구성은이력이며 자세한수식/검증은 simplex-transition-2026-09-25.md를 따른다.

AI 작업 컨텍스트. 사용자 안내는 루트 README의 게임 없이 월드 규칙 편집 절을 따른다.

## 승인 범위와 구조

2026-09-24 사용자 `실시`에 따라 게임 없이 규칙을 편집·미리보기·확정 저장하는 로컬 웹 편집기를 추가했다. 바이옴/식생의 배치 규칙은 아직 미합의이므로 추가하지 않았다. Snowcapped와 같은 표/곡선/지도 작업 흐름을 참고한 자체 UI이며 해당 사이트나 소스를 복제한 제품은 아니다.

- `src/editor/main.cpp`: Windows loopback HTTP 서버, 기존 world_core 링크. SDL/Vulkan/게임 월드/메시/조명 루프는 시작하지 않는다. CPU FastNoise2 SIMD와 TerrainGenerator를 게임과 공유한다.
- `assets/editor/index.html`, `style.css`, `app.js`, `map-view.js`: 외부 패키지 없는 한국어 웹 UI. 스플라인/노이즈 계산식을 JS에 복제하지 않고 지도·곡선·좌표 조회를 C++에 요청한다. JS의 색 팔레트는 표시 전용이다.
- `editor.bat`: 배포 위치의 exe 실행. 배포 폴더에서는 worldgen_editor.exe를 직접 실행한다. 실행 파일 디렉토리를 기준으로 읽고 저장하므로 현재 작업 디렉토리에 의존하지 않는다.
- 기존 CMake의 world_core 정적 라이브러리를 링크하고 별도 실행 파일을 생성한다. 현 프로젝트 구성 단계에는 Vulkan SDK 등 기존 게임 빌드 도구가 여전히 필요하다. 배포 편집기 실행에는 게임이나 개발 도구가 필요 없다.

## 저장과 게임 반영

시작 시 실행 파일 옆 worldgen.json을 읽으며 없으면 assets/worldgen/default.json을 사용한다. 기존 parse_generation_json / generation_json / save_generation_config를 통해 schema6 검증, 구형 설정 이행, 원자적 파일 교체를 공유한다. 미리보기 자체는 저장하지 않는다.

초안 저장은 worldgen.draft.json만 바꾼다. 확정 저장은 기존 worldgen.json을 worldgen-backups/worldgen-<timestamp>.json에 백업한 뒤 교체한다. 임시 파일은 공유 저장 함수의 .tmp 규칙을 따른다. 사용자 settings.json은 접근하지 않는다. 패키징은 두 JSON 및 백업을 덮어쓰지 않는다.

서버가 읽은 원문 파일의 FNV64 revision을 publish와 대조한다. 다른 프로그램이 파일을 바꿨으면 409로 거부하며, 사용자는 JSON 내보내기 후 최신 확정을 다시 불러온다. 폴더별 Windows mutex로 편집기 중복 실행을 막는다. 이 revision은 인증 수단이 아니며, 외부 프로그램의 정확히 동시 쓰기를 OS 차원에서 잠그는 것도 아니다. 실행 중 게임의 F8 저장과 웹 확정 저장을 동시에 수행하지 않는다.

게임 F8의 `확정 파일 불러오기`는 게임의 generation_path를 다시 읽어 UI 초안만 교체한다. 다음 프레임의 자원 변경 단계에서 처리한다. 현재 월드·플레이어·시각은 즉시 바뀌지 않고 사용자가 기존 재생성 버튼을 눌러 적용한다.

## 편집·미리보기

기존 지형/3D/기후 노이즈와 옥타브별 가중치, 공유 워핑, 순수 Groundness 높이 곡선, 기존 복합 지형 설정, C/E 표와 W/PV 중첩 Hermite를 편집한다. C/E 표 축은 -2..2, 각 24개 이하이며 실제 저장 유효성은 C++가 판정한다. 곡선 그래프의 가로축은 Weirdness이고 PV 노드도 W→PV 변환 후 값을 그린다. 높이 곡선만 Groundness가 가로축이다.

웹 프리셋 버튼은 offset/factor/jaggedness 세 표만 교체하고 splines.enabled를 켠다. 인게임 능선 버튼의 추가 squash/jagged 노이즈 변경까지 수행하는 버튼은 아니다. UI에 표만 바꾼다는 결과를 표시한다.

지도는 GenerationMap 12종이며 영역 좌표는 0..131072, 긴 변 32..1024 샘플이다(UI 선택 128/256/512/1024). X/Z 양끝을 포함하고 순환 좌표로 정규화한다. Groundness 개별 옥타브는 기존 F8와 같은 간격 감소·seed offset 증가 및 합성 기여 가중치 정규화를 쓴다. 워핑 전 체크는 미리보기 요청 복사본에서만 워핑을 끈다. probe는 지도 생성 당시 전체 설정의 원래 합성값 12종을 보여 주며 표시 픽셀 값은 별도로 제시한다.

base_height/effective_height는 3D 밀도를 적용하기 전 연속 기준 높이다. 청크의 4블록 밀도 보간·최종 표면·동굴/오버행/조명·블록 렌더링을 미리보기한 결과가 아니다. 온도/강수량은 지형 변경 없이 독립 표시한다. 지도 PNG는 계산 샘플 해상도로 내보낸다.

현재 서버는 단일 요청을 순차 처리한다. 큰 지도 계산 중 다른 요청은 대기하며 게임 성능과는 독립적이다. 다중 사용자/인터넷 서비스는 범위 밖이다. 초안 되돌리기는 메모리 내 최대16개 스냅샷이며 종료 후 복구는 명시적 초안 저장 파일을 사용한다.

## 로컬 HTTP 규약

기본 127.0.0.1 임의 포트, 선택 --port, 브라우저 생략 --no-browser. BCrypt 난수 토큰을 첫 URL fragment로 전달하고 JS sessionStorage에 보관한 후 주소에서 지운다. API에는 X-Editor-Token 및 정확한 Host를 요구하고 Origin이 있으면 동일 출처만 허용한다. CORS를 열지 않는다. 정적 파일은 index.html/app.js/map-view.js/style.css 네 개만 허용한다. CSP self, no-store, nosniff, frame-ancestors none을 보낸다. 헤더16KiB, 본문5MiB, 연결 읽기/쓰기 제한시간5초를 적용한다.

- GET state / draft: 파싱된 설정과 파일 revision 조회.
- POST validate / draft / publish: 설정 검증 / 초안 저장 / 백업 후 확정 저장.
- POST preview: config, kind, range, resolution, octave, weighted, before_warp. 응답은 Windows little-endian float32 두 개(width,height) 다음 행 우선 raw 값 배열이다.
- POST curve / probe / preset: 공유 C++ 함수의 곡선257점 / 좌표 신호 / 세 표 초기값.
- POST shutdown: 현재 응답을 전송한 뒤 종료. 탭만 닫아서는 서버를 종료하지 않는다.

정적 페이지에는 토큰을 삽입하지 않는다. 프레임/외부 사이트는 API를 사용할 수 없지만 동일 사용자 PC의 악성 프로그램에 대한 보안 경계로 주장하지 않는다.


## UI 개편 (2026-09-24)

노이즈/지형 조합/기후의 세 작업과 신호별 선택을 사용한다. 평소에는 큰 지도와 선택한 신호 폼만 보이며 스플라인은 native dialog의 표/그래프 두 열이다. 중첩 노드를 재귀적으로 전부 렌더하지 않고 nodePath breadcrumb에 해당하는 한 단계만 편집한다. 그래프는 하위 노드 자체의 로컬축 그래프가 아니라 선택 C/E 칸 전체의 Weirdness 단면으로 명시한다. 원래 설정 수식과 schema6은 변경하지 않는다.

map-view.js는 지도 제스처/계산 상태/비교/좌표 조회를 담당한다. app.js의 폼/저장과 공유 상태를 초기화한 다음 map-view.js 끝에서 state 요청을 시작한다. 두 스크립트 로드 사이 네트워크 응답 순서에 의존하지 않는다. 서버의 정적 허용 목록에 map-view.js를 추가했다.

지도 휠은 포인터 위치를 중심으로 영역을 확대하며 drag는 블록 좌표 범위를 이동한다. 범위는 0..131072 안으로 제한하고 제스처 최소 폭은16이다. 시작/끝 직접 입력의 서버 범위는 기존 규약을 따른다. 드래그 중 기존 raster를 새 범위에 맞춰 표시하고 놓으면 재계산한다. 연속 휠 요청은180ms 합치며 계산 중이면 최신 요청만 이어 실행한다. 새 요청이 생기면 이전 응답을 공개하지 않는다. config 변경은 수동 갱신이며 표시 세대/범위/지도/비교 기준이 다르면 명확한 stale 표시를 낸다.

비교 기준은 최초 불러온 확정 규칙 또는 pin한 config 사본이다. baseline과 현재 config를 동일한 범위/종류/해상도/옥타브/워핑 조건으로 C++에 각각 요청한다. 한 canvas에서 좌우 clip 합성하고 슬라이더로 경계를 움직인다. baseline에 없는 개별 옥타브는 안내 후 비교를 거부한다. 좌표 probe는 클릭한 비교 쪽의 config를 사용한다. PNG는 현재 작업 raster의 원래 샘플 해상도로 내보내며 UI/비교 합성은 포함하지 않는다.

저장 상태는 현재/saved/published 설정 서명을 구분한다. draft 저장 후 게임 미확정 상태를 표시하고 publish 후 동일 상태를 표시한다. JSON 편집을 폼에 반영하지 않았으면 작업/확정 저장을 막고 안내한다. 헤더는 고정하고 파일 가져오기/JSON/종료는 별도 dialog에 둔다. 폼 변경은 파일에 자동 쓰지 않는다. 브라우저 자동화나 합성 입력으로 검증하지 않는다.
