# 개발 환경 컨텍스트

## 일반 지형 nonempty 청크 목록 (2026-10-01)

Resident::nonempty_meshes는 mesh.count>0인 세로 청크 인덱스의32비트 마스크다. incoming의 업로드 완료 후 Resident 생성 시 초기화하고 accept_meshes의 resident geometry 교체 직후 해당 비트를 set/clear한다. relight는 geometry/count를 그대로 보존하므로 갱신하지 않는다. uploaded_chunks_는 준비 완료 여부이며 empty도 포함하므로 이 마스크와 혼용하지 않는다. 퇴거/재생성은 Resident와 마스크를 함께 제거한다. 주 화면 루프는 countr_zero/최하위비트 제거로 기존 cy 오름차순을 유지한다. 시야·선택 블록·고체/물/얼음 분기 및 기존 참조 수명은 그대로다. 그림자 루프는 이번 변경 범위에서 제외했다.

상세 계측을 켜면 매 컬럼의 전체 mesh.count로 기대 마스크를 만들고 일치 여부를 확인한다(성능 계측 OFF 경로에는 없음). near_chunk_slots는 이제 실제 방문한 nonempty 슬롯 수이며 이전 조사에서32×컬럼이던 정의가 실제 방문량으로 이어진다. 표집 scan은 마스크 순회 비용이 된다. 세부 계측 시간에는 이 추가 검증 비용이 포함되므로 전후 성능 비교에는 세부 옵션을 끈다.

## 일반 지형 세부 계측 (2026-10-01)

`--profile-near-detail`은 `--profile-draws`에 의존하며 near 전용 추가 CSV 필드를 활성화한다. 미사용 시 stride0/나머지0이며 기존 총 CPU 시간 및 GPU CSV는 유지한다. constexpr로 별도 생성한 상세 루프에서 published 컬럼 인덱스 modulo32와 프레임 phase를 맞춰 순환 표집한다. 고정 장면에서640steady는 각 컬럼20회 표집이다. 일반 실행 루프에는 세부 청크 카운터/타이머가 없다.

- sample_scan은 표집 컬럼의 내부32슬롯 순회/빈 메시 검사와 루프·일부 카운터 비용이다. 외부 map 순회, 컬럼 base 좌표 계산, hypothetical 컬럼 AABB 판정, 초기 바인딩은 제외된다.
- sample_cull은 비어 있지 않은 청크의 상대 좌표 및 기존 frustum 판정, sample_record는 통과 청크의 선택 블록/명령 기록/물·얼음 목록과 drawn/triangle 카운터다. 타이머·세부 카운터 비용이 일부 포함되며 보정하지 않는다. 실제 물·얼음 별도 패스는 포함하지 않는다.
- sample 값은 표집된 컬럼들의 원시 시간 합이다. 고정640표본의 평균×32는 추정치일 뿐, 기본 루프 시간을 정확히 분해한 값이나 최적화 절감 예상량이 아니다. 옵션 OFF 총 CPU 측정과 함께 해석한다.
- 전체 slots/nonempty/culled/visible 및 rejected_columns/rejected_column_nonempty를 기록한다. 후자는 높이512의 컬럼 AABB로 가상 선별하며 실제로 skip하지 않는다. rejection_conflicts는 해당 컬럼에서 기존 청크 시야 판정을 통과한 청크 수로, 고정 fixture에서0인지 확인한다. 이 계측 자체는 새 컬링 도입이 아니다.

## LOD 그리기 목록 캐시 (2026-10-01)

- LodRenderer::prepare에서 새 coverage 마스크와 중심을 기존 값과 비교한다. active scene 교체 또는 coverage 변화가 있을 때만 rebuild_draw_meshes를 호출한다. 불변 프레임에서는 그리기 패스마다 map 조회와 level0 근거리 중복 판정을 반복하지 않는다.
- 목록은 active scene 순서를 유지하는 `{LodKey, const Mesh*}` 벡터다. 포인터는 meshes_의 std::map 노드로 삽입에 의해 무효화되지 않는다. collect는 active/pending scene의 키를 보존하며 새 active 목록 재구축 뒤에만 실행한다. clear/shutdown에서는 캐시를 먼저 비운다. 미완성 pending scene을 그리기 목록에 섞지 않는다.
- 시야/그림자 거리 컬링은 여전히 record에서 현재 카메라/행렬로 매번 계산한다. water_only의 재질 검사도 같은 메시에서 수행한다. coverage 판정식, 컬링 판정식, 그리기 순서, shaders, part 분할, GPU 버퍼의 deferred 파괴는 바꾸지 않는다.
- CPU CSV 끝에 world_prepare_cpu_ms를 추가했다. WorldView::prepare가 이미 기록하던 값을 복사하며 새로운 타이머는 없다. 목록 재구축 비용을 그리기 구간에서 준비 구간으로 옮겨 숨기지 않도록 전후 모두 이 항목을 기록한다. 세 CPU 구간의 합은 전체 프레임 시간이 아니다.

## 주 화면 지형 그리기 계측 (2026-10-01)

- `--profile-gpu gpu.csv --profile-draws draws.csv`로 CPU/호출 계측을 함께 켠다. GPU CSV 스키마는 유지한다. draw CSV는 frame/steady/해상도/렌더거리/컬럼/LOD타일/LOD대기 및 near/lod 각각 cpu_ms/draws/descriptor_binds/vertex_binds/pushes를 저장한다. 명시적 옵션 없이는 새 clock 호출/카운터 증가/CSV 누적을 하지 않는다.
- DrawStats는 주 화면 고체 지형만 계측한다. near_cpu_ms는 world_view.cpp의 파이프라인·atlas 연결부터 컬럼/청크 순회·컬링·push/descriptor/draw·투명체 목록 구성까지 포함한다. lod_cpu_ms는 LodRenderer::draw의 파이프라인/환경 연결부터 record의 타일 순회·컬링·메시 조회·분할 버퍼별 draw까지 포함한다. per-draw 타이머 대신 구간당 시작/종료만 재며 두 시간은 겹치지 않는다. CPU 소요시간에는 OS 스케줄링 지연도 들어간다.
- GPU 큐 제출/대기, 업로드, 그림자/물/얼음 별도 패스, 플레이어와 UI는 위 CPU 구간 밖이다. CPU와 GPU 시간은 실행이 겹치므로 더해서 FPS로 해석하지 않는다. GPU opaque_terrain_player에는 플레이어도 포함되므로 CPU 합과 범위도 다르다.
- 그리기 수는 vkCmdDraw 실제 호출 위치에서 센다. near descriptor는 atlas1+그리는 청크당1, push는 청크당1; LOD descriptor는 환경1+coverage1, push는 컬링 통과 타일당1, vertex bind/draw는 그릴 면이 있는 part당1이다. LOD 타일 수와 호출 수를 혼동하지 않는다. 물/그림자 호출은 같은 record를 재사용하지만 진단 카운터에 합산하지 않는다.
- draw 옵션의 steady 조건은 기존 near ready 조건에 LOD pending0/paused false/memory_limited false/최신 scene revision 업로드 완료를 추가한다. 그 상태240프레임 후 측정한다. 메모리 예산 때문에 완료할 수 없는 경우 성공으로 오인하지 않고 시간 상한에서 샘플 부족으로 exit1, 부분 CPU/GPU CSV를 보존한다. 기존 GPU 단독 실행의 ready 조건은 바꾸지 않는다.
- CPU CSV는 GPU/월드/PNG와 같은 정규화 경로를 거부하며 GPU 옵션 누락·초기 LOD 디버그와의 조합도 거부한다. 시간 제한 및 미완료 오류는 기존 GPU 프로필 정책을 공유한다. CPU 저장 실패는 exit1이다. 창 모드도 사용 가능하지만 비교는 고정 시점 headless/validation OFF로 수행한다.

## Vulkan 헤드리스 실행 (2026-10-01)

- `DOLBUTO.exe --headless`는 SDL EVENTS만 초기화하고 Renderer::initialize(nullptr, ...)로 직접 Vulkan 인스턴스/그래픽 큐를 만든다. surface 확장·present 지원·VK_KHR_swapchain은 요구/활성화하지 않는다. Vulkan 1.4와 기존 렌더 기능 요구는 유지한다.
- BGRA8 UNORM COLOR_ATTACHMENT|TRANSFER_SRC VMA 이미지 2장을 사용한다. 각 frame fence를 기다린 뒤 동일 슬롯의 이미지를 재사용하며 acquire/present semaphore와 PRESENT 레이아웃 전환은 생략한다. 이미지 뷰 파괴 후 VMA 이미지를 해제한다. 일반 창의 swapchain/resize/present 경로는 그대로 유지한다.
- RmlUi는 타이머·로그용 headless SystemInterface, ImGui는 DisplaySize/DeltaTime을 직접 제공한다. SDL 입력/플랫폼 백엔드는 창 모드에서만 사용하며 실제 UI 렌더러·WorldView·HDR/후처리·캡처·GPU query 경로는 공유한다.
- 해상도 1280×900, DPI1, 월드 자동 진입. 저장 설정을 읽지만 VSync/FPS 제한은 메모리에서만 끄며 사용자 파일을 저장하지 않는다. headless만 지정 시 300프레임, 제한 없는 PNG 캡처는 기존100프레임, GPU 계측은 기존600 steady samples/120초를 사용한다. 시간 제한 캡처는 한 마지막 프레임, world profile 완료 캡처는 완료 프레임에 저장한다.
- 사용자용 실행 예시는 루트 README의 헤드리스 절. `--profile-gpu`와 `--profile-view 193.6 -90 -22 12`를 조합하면 현재 돌 평지에서 지형이 보이는 고정 시점을 얻는다. 시드/설정/해상도/위치를 맞춰 비교하되 headless 시간에 presentation은 포함되지 않는다. steady 판정은 기존 근거리 컬럼/조명/업로드와 240프레임 워밍업 기준이며 LOD 전체 완료를 별도로 보장하지 않는다. 현재 자연 물이 없는 평지는 물 효과의 시각 검증용 fixture가 아니다.

## 새 PC 준비 진입점 (2026-10-01)

- build.bat → Windows PowerShell 5.1 호환 setup.ps1 → 사용자 동의 → 도구 준비 → PowerShell 7의 build.ps1 -NoDownload → package.ps1. 직접 build.ps1 호출도 setup의 동일한 선택으로 연결된다.
- toolchain.ps1은 네트워크/파일 쓰기 없이 버전 및 실제 도구·SDK 파일을 검사한다. setup -Check / build.bat --check는 준비됨 0, 부족함 1. 거절 또는 -NoDownload로 준비 불가 시 2. 기타 실패 1. --check/--no-download는 pause하지 않는다.
- PowerShell 부족 시 7.6.5 ZIP을 프로젝트에 설치한다. SHA-256은 공식 GitHub release asset digest. LLVM 23.1.0과 의존성 lock/hash는 기존 값을 유지한다. PowerShell >=7, CMake >=3.26, Ninja >=1.10, VS >=2022, Windows SDK >=19041, Vulkan header >=1.4.341을 재사용한다. LLVM은 정확한 버전과 lld-link를 요구한다.
- Build Tools 2022의 VC workload, x64/x86 C++ 도구, CMake 프로젝트 도구 및 Windows SDK 22621을 공식 bootstrapper로 추가한다. 기존 Build Tools 2022가 있으면 modify, 없으면 해당 제품을 추가 설치한다. 타 VS 제품은 변경하지 않는다. 2022 서비스 버전은 고정되지 않는다. SDK/설치 캐시는 Microsoft 공용 위치에도 생긴다.
- Vulkan SDK 자동 설치 버전은 1.4.341.1. Microsoft/LunarG 설치 EXE는 유효한 Authenticode 및 게시자 이름 확인 후 관리자 권한으로 실행한다. 라이선스 자동 동의와 시스템 변경을 확인 문구에 명시한다. 재부팅 반환 시 자동 재부팅/후속 빌드 없이 중단한다.
- 다운로드 helper는 동의 후에만 로드한다. 임시 파일을 검증 후 캐시에 반영한다. 불일치/불완전 의존성 소스는 캐시 안에서 backup-GUID로 보존하고 새로 준비한다. marker는 압축 해제 성공 후 작성한다.
- environment.ps1은 같은 감지 결과를 재사용하고 개발 환경 및 PATH/VULKAN_SDK를 현재 프로세스에만 적용한다. 설치 직후 새 SDK 환경 변수도 레지스트리 기반 환경값에서 재탐색한다.
- Windows x64 지원이며 macOS/Linux/ARM64 자동 준비는 미지원. Windows 기본 tar.exe 필요. 진단 도구 setup-diagnostics.ps1은 별도의 명시적 실행용으로 유지한다.
- 공식 설치 명령 근거: https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022 및 https://vulkan.lunarg.com/doc/view/1.4.341.1/windows/getting_started.html . PowerShell ZIP: https://learn.microsoft.com/en-us/powershell/scripting/install/install-powershell-on-windows .

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
