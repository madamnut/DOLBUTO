# 개발 환경 컨텍스트

## 2026-09-25: 토러스 전환 명시적 진단

EXCLUDE_FROM_ALL인 torus_benchmark는 이번 승인에 한해 실행한 진단/성능비교도구다. 일반빌드·게임시작·CTest에연결하지않는다. `torus_benchmark worldgen.json results.csv`로3D보존/주기/독립생성/설정과2D성능을확인한다. 동일지형을생성하는구현비교가아니므로게임로딩전후차이에지형/메시변화가포함된다. 자세한조건은simplex-transition-2026-09-25.md를따른다.


AI 전용 문서. 루트 README는 사용자 실행 안내용이다.

## 명시적 그래픽 GPU 계측 (2026-09-24)

- 후속 그림자 재사용 비교로 `--capture-shadow-maps <절대 디렉토리>`를 추가했다. --profile-gpu의 마지막 제출 뒤 깊이2장/색2장의 raw 데이터를 저장한다. `--validation`은 명시적 Vulkan validation 요청이며 일반 Release 기본OFF는 유지한다. 성능 비교 중에는 validation을 켜지 않는다. shadow_compare.py와 상세 재현은 docs/shadow-reuse-2026-09-24.md를 따른다.

- 일반 빌드/실행에는 계측을 켜지 않는다. `sandbox.exe --profile-gpu <절대 CSV 경로> --profile-origin 7040 5504 --profile-view 220 -90 -22 12 --profile-samples 600 --seconds 120`로 GPU 패스별 시간을 기록한다. view는 초기 카메라 Y/yaw/pitch/게임 시각(시간)이며 키보드/마우스 입력을 합성하지 않는다.
- 모든 컬럼 공개/조명 대기 해소/업로드 없는 상태에서 240프레임 예열 후 표본을 수집한다. 로딩 표본은 CSV의 steady=0, 정식 표본은1이다. `--capture <절대 PNG 경로>`는 마지막 측정 프레임을 자체 캡처한다. 시간 제한 전에 필요한 표본이 없으면 실패로 처리한다.
- `tools/benchmarks/gpu_profile.py`는 별도 런타임/설정 사본을 사용하는 수동 실행용 측정 도구다. CTest/일반 자동 테스트로 연결하지 않는다. CPU 프레임 시간과 GPU 시간의 차이 및 패스 구분은 docs/gpu-profile-2026-09-24.md를 따른다.

## 도구와 재현

- C++20, LLVM Clang 23.1.0의 clang-cl, Windows SDK 및 MSVC STL, CMake 3.26 이상, Ninja.
- 현재 확인한 CMake는 4.3.1-msvc1, Ninja는 1.13.2다. 이 도구는 선택한 빌드 방식에 맞는 기존 설치를 재사용한다. 에디터 UI에 의존하지 않는다.
- Vulkan 코어 요구 버전은 1.4. volk 및 헤더는 1.4.350으로 맞췄다. SDK는 glslc와 개발 검증 레이어를 제공한다. SDK 1.4.341.1의 glslc를 사용했다.
- `tools/setup.ps1`은 LLVM을 `.tools`에 준비하고, `dependencies.lock.json`의 고정 커밋 아카이브를 SHA-256 검사 후 `.cache/deps`에 푼다. 라이브러리 원본 코드를 모두 읽을 수 있다.
- `.tools`, `.cache`, `build`는 Git 추적 대상이 아니다. 생성 파일이나 절대 PC 경로를 소스 설정에 넣지 않는다.
- 의존성 변경 시 락 파일을 갱신하고 재검증한다. `.cache` 안을 수정한 것만으로는 다른 체크아웃에 재현되지 않는다. 필요한 원본 수정은 버전 관리되는 패치로 보존해야 한다.
- `tools/environment.ps1`이 vswhere로 Windows C++ 환경을 찾고 현재 프로세스에만 개발 경로를 설정한다.

## 실행 명령

조명 개선 전후 비교는 명시적 `cmake --build --preset release --target lighting_benchmark` 후 `build/release/bin/lighting_benchmark.exe out/Sandbox/worldgen.json <출력 폴더>`를 사용한다. 동결 기준과 블록별 밝기/차폐를 비교하고15상황의 시간을 기록한다. 게임의 테스트 모드나 CTest가 아니며 자동 실행하지 않는다. 구현·fixture·통계 조건은 `lighting-optimization-2026-09-24.md`를 따른다.

명시적 청크 계측(일반 실행에서는 비활성): `sandbox.exe --world --profile-world <절대 CSV 경로> --profile-origin <컬럼 X> <컬럼 Z> --render-distance 24 --seconds 60`. 최초 요청 범위가 모두 공개되면 종료한다. `--seconds`는 중단 상한이며, CSV의 request/published 개수가 같아야 완주다. 기록 경로의 부모 디렉토리는 미리 준비한다. 사용자 입력을 합성하지 않는다. 원본 설정 보존을 위해 실행 파일·assets·shaders·runtime DLL과 worldgen/settings의 복사본을 별도 폴더에서 실행할 수 있다.

CPU 파이프라인만 측정: `cmake --build --preset release --target world_benchmark` 후 `world_benchmark.exe <worldgen.json> <출력 디렉토리> [반경=6] [반복=7]`. 바다/완만한 육지/굴곡 지역을 자동 선택해 기본값으로 반경6의113컬럼을 계측 ON/OFF 각7회 처리한다. 반경24/각3회는 끝에 `24 3`을 붙인다. GPU/업로드 대신 소비자가 완료 컬럼을 바로 받아 공개하며 잠금 경합 때 재시도한다. CTest나 일반 빌드에 연결되지 않는다. `python tools/benchmarks/summarize_world_profile.py <출력 디렉토리>`로 상세 CSV의 통계를 계산한다. 반복 실행은 출력 파일을 덮어쓰므로 보존할 결과는 별도 디렉토리에 둔다.

최신 AGENTS 규칙에 따라 `실시`/`ㄱㄱ`/`ㅅㅅ`는 수정 후 빌드·패키징을 포함한다. 해당 작업에서 빌드하지 말라고 지정한 경우에만 생략한다. 읽기 전용 조사와 구현 제안만으로 빌드하지 않는다.

| 작업 | PowerShell 명령 |
|---|---|
| 최초 준비 | `./tools/setup.ps1` |
| 더블클릭 배포 빌드·패키징 | 루트 `build.bat` |
| 디버그 도구 준비 | `./tools/setup-diagnostics.ps1` |
| 개발 빌드 | `./tools/build.ps1 -Preset dev` |
| 최적화+계측 빌드 | `./tools/build.ps1 -Preset profile` |
| 배포용 빌드 | `./tools/build.ps1 -Preset release` |
| 실행 | `./tools/run.ps1 -Preset profile` |
| 코드 정적 검사 | `./tools/analyze.ps1 -Preset profile` |
| Vulkan 프레임 캡처 | `./tools/capture.ps1 -Preset profile` |
| Tracy 기록 | `./tools/profile.ps1 -Preset profile` |

`dev`는 Vulkan 검증과 Tracy를 켠다. `profile`은 최적화와 디버그 심볼, Tracy를 켜고 Vulkan 검증은 끈다. `release`는 최적화하며 두 계측 기능을 끈다. 프로젝트 소스만 강한 경고 및 정적 검사를 적용하고, 외부 라이브러리 경고를 프로젝트 결함과 혼동하지 않는다.

`build.bat`는 자기 디렉토리로 이동하여 PowerShell 7에서 `tools/build.ps1 -Preset release`를 실행하고, 성공한 경우에만 별도 PowerShell 프로세스에서 `tools/package.ps1`을 실행한다. 첫 실패의 종료 코드를 보존하고 성공/실패 모두 pause로 결과 창을 유지한다. PowerShell은 PATH의 pwsh.exe → Program Files의 PowerShell/7 → 현재 사용자 `.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe` 순으로 찾는다. `build.bat --check`는 선택된 경로와 PowerShell 버전만 확인하고 빌드·테스트·패키징·pause 없이 종료한다. ExecutionPolicy Bypass는 해당 자식 프로세스에만 적용하며 사용자/시스템 정책을 저장 변경하지 않는다. 최초 setup/SDK 설치는 자동 수행하지 않는다. bat 파일 작성 승인은 실제 빌드 실행 승인이 아니다.

clang-tidy에서 Vulkan의 signed enum 비트 연산, 출력 구조체 초기화, 유사 파라미터 경고는 잡음이 많아 제외했다. null 역참조 등 Clang analyzer 진단은 실패로 처리한다.

## 실행·진단 인터페이스

자동 테스트는 사용자 승인으로 모두 제거했다. CTest 프리셋/대상과 build.ps1의 Check 옵션은 없다. build.bat는 빌드 성공 후 바로 패키징한다. 아래 캡처·계측 기능과 실제 실행 오류 처리는 유지한다.


- `--world --seed 1337 --render-distance 12`: 메뉴를 거치지 않고 첫 월드 진입. 시드와 컬럼 반경을 지정한다.
- `--debug-ui`: 처음부터 F3 진단 텍스트 표시. F8 생성 편집창은 열지 않는다.
- 후속 소스에서 인게임 Esc는 어두운 옵션 오버레이를 열고 닫는다. 게임 복귀/시작 화면 버튼과 공유 렌더 거리를 제공하며 Tab 설정 토글은 제거했다. Esc 종료도 제거했다. 옵션 동안 월드 렌더/스트리밍은 계속하고 카메라/편집 입력은 차단한다. 이 변경은 미빌드다.
- F2는 실행 파일 옆 screenshots 폴더에 UI 포함 PNG를 저장하고, F4는 월드의 선만 표시하는 모드를 전환한다. 두 기능은 소스 반영/미빌드 상태다. 일반 키 이벤트의 repeat는 제외한다.
- F3 진단 텍스트의 FPS는 함께 표시하는 직전 루프의 프레임 간격(ms)을 사용한 순간 값(`1000 / frame_ms`)이다. 평균 처리는 하지 않으며, 간격이 0 이하이면 `--`를 표시한다.
- FPS와 프레임 간격 표시는 0.25초마다 최신 단일 프레임 샘플로 함께 갱신하고 그 사이에는 유지한다. 프레임 간격은 소수점 셋째 자리(ms)까지 표시한다. 패널을 닫아도 표시용 샘플은 갱신하며, 렌더링 속도 자체를 제한하지 않는다.
- `--frames N`, `--seconds N`: 제한 실행. `--capture path.png`는 지정 프레임 종료 시 GPU에서 읽은 화면을 PNG로 저장한다.
- `--rdc path`: RenderDoc 프로그램 API로 프레임을 캡처한다. 필요한 DLL 및 레이어 환경은 `tools/capture.ps1`이 자식 프로세스 범위로 구성한다. 관리자 권한이나 시스템 레이어 등록 변경이 필요하지 않다.
- RenderDoc 1.46 및 Tracy 0.14.1 실행 도구도 공식 배포본의 SHA-256을 고정했다. `.tools`의 GUI 도구로 캡처를 열 수 있다.
- Tracy는 localhost로만 연결하며, CPU 구간과 프레임 마커 및 GPU 시간 수치가 기록된다. 시스템 전체 추적은 끈다. GPU 작업별 Tracy 타임라인은 아직 구현하지 않았다.
- VSync는 기본 OFF다. 표면이 지원하는 IMMEDIATE → MAILBOX → FIFO 순서로 선택하며, 창 크기 변경 시에도 같은 정책을 적용한다. MAILBOX/FIFO 대체 모드는 동기화된 표시이므로 OFF로 표기하지 않는다. 실제 모드는 로그와 F3 진단 텍스트에서 확인한다.
- 프레임 간격에는 CPU 작업과 대기가 포함된다. GPU 시간은 Vulkan timestamp query로 측정한다. VMA 수치는 해당 allocator의 사용량이며 드라이버·ImGui를 포함한 전체 GPU 메모리가 아니다.

## 라이선스

의존성 원본 라이선스는 `.cache/deps`에서 확인할 수 있다. `tools/package.ps1`은 실행 폴더와 외부 라이선스를 `out`에 모은다. Noto Sans KR의 OFL은 폰트와 함께 보관한다. FreeType은 FTL 조건으로 포함한다. 프로젝트 자체 라이선스는 사용자가 아직 정하지 않았다.

## 주기적 생성기 추가 (소스 반영, 빌드 대기)

- lock에 FastNoise2 v1.1.1, upstream이 요구한 FastSIMD 커밋, nlohmann/json v3.12.0를 추가했다. 새 체크아웃은 setup으로 모든 pinned 소스를 준비한다. FastNoise2 원본 CPM 설정 대신 프로젝트 cmake/fastnoise가 로컬 소스를 직접 연결한다. CMake configure에는 FastSIMD feature probe 컴파일이 있으므로 별도 빌드 승인 없이 configure하지 않는다.
- FastSIMD는 RELAXED 경로를 사용하며 C++ 검증/설정/UI 코드 전체에 fast-math를 켜지 않는다. `periodic_perlin.inl`의 Generator.inl→BasicGenerators.inl include 순서는 필수이며 clang-format 정렬에서 제외한다. upstream 원본 수정은 없다.
- assets/worldgen/default.json은 배포 기본값이다. 실행 폴더 worldgen.json은 F8 사용자 저장값이며 패키징 시 보존한다. build/release/bin과 out/Sandbox는 별도 실행 위치이므로 각자의 override를 가진다. 이 파일을 다른 실행 폴더에 복사하면 설정을 옮길 수 있다. 블록 편집 저장은 아니다.
- 패키징에 FastNoise2/FastSIMD/nlohmann-json의 MIT 라이선스를 포함한다. 사용자 프로젝트 자체 라이선스는 여전히 미정이다.

- 2026-09-14 소스 변경(미빌드): 옵션 렌더 거리는 실행 파일 옆 settings.json에 자동 저장하고 재실행에 복원한다. --render-distance는 실행 한정 우선값이며 옵션 직접 변경 시 저장된다. 패키징은 기존 settings.json/worldgen.json을 보존한다. 생성 설정은 v1 읽기 호환을 유지하며 다음 게임 내 저장부터 v2 groundness.weights를 기록한다. shape.gain은 유지한다.

- 같은 날짜 후속 3D 토글로 생성 설정 저장 형식은 v3로 확장했다. v1/v2는 3D 켜짐으로 읽고 v3은 shape_enabled boolean을 필수로 저장/복원한다. 실제 저장 파일은 게임 내 저장 시에만 변경한다(미빌드).

## 독립 생성 편집기

Release 빌드에 `worldgen_editor` 대상이 포함되며 package.ps1은 실행 파일과 assets/editor를 배포한다. 루트 editor.bat는 out/Sandbox/worldgen_editor.exe를 실행한다. `--no-browser`는 브라우저 자동 열기만 생략하고 `--port N`은 로컬 포트를 지정한다(기본 임의 포트). 별도 Node/npm 설치나 웹 빌드는 필요 없다. 구현과 파일 규약은 [editor.md](editor.md)를 따른다.


## 선택적 노이즈 성능 측정

PSRD 비교는 같은 대상의 `build/release/bin/noise_benchmark.exe --psrd <새출력폴더>`로 명시 실행한다. 정적2D SIMD PSRD/기존 주기적 Perlin의31회 성능 측정 및 원본/주기 진단,1024² 원시 float 지도를 출력한다. 일반 빌드/게임에는 실행되지 않는다. 분석 스크립트는 tools/benchmarks/psrd/summarize.py이며 numpy/matplotlib은 보고서 작성에만 필요하다. 조건과 결과는 psrd-benchmark-2026-09-24.md를 따른다.

2026-09-24 사용자가 별도 성능 측정 실행과 통계 표를 승인했다. `noise_benchmark`는 EXCLUDE_FROM_ALL이며 일반 빌드·패키징·게임 실행/CTest에 포함하지 않는다. Release configure 후 `cmake --build --preset release --target noise_benchmark`로 만들고 `build/release/bin/noise_benchmark.exe <output.csv>`로 명시 실행한다. 출력 경로가 존재하면 CSV를 덮어쓰므로 새 결과 경로를 사용한다. AVX2와 Windows 단일 프로세서 그룹을 사용하는 현 측정 환경용이다. 재측정은 조건/기계 정보를 함께 기록한다. 결과와 해석 한계는 noise-benchmark-2026-09-24.md를 따른다.
