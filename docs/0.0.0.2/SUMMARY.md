# 0.0.0.2 요약

## 전체 방향
- 0.0.0.2는 렌더러 중심으로 비대해졌던 클라이언트 구조를 runtime subsystem 중심 구조로 옮기는 버전이다.
- 목표는 기능을 무한히 쪼개는 것이 아니라, 각 파일과 시스템이 명확한 책임을 갖고 이후 GameServer 분리에도 걸림돌이 적은 형태를 만드는 것이다.
- 빌드는 사용자가 직접 수행하는 기준을 유지했다.

## 클라이언트 구조
- `main.cpp`는 GLFW/window 생성과 최상위 실행 진입점에 집중하고, 게임 실행 흐름은 `GameClient`가 맡도록 정리했다.
- `GameClient`는 `ClientRuntime` facade를 통해 world/gameplay/UI/render runtime을 조율하고, 렌더러 직접 의존을 줄였다.
- `ClientRuntime`, `ClientRenderRuntime`, `ClientRuntimeState`를 도입해 client runtime state와 renderer-facing adapter를 분리했다.
- `ClientWorldRuntime`, `ClientTerrainSceneRuntime`, `ClientTerrainCoordinator`, `ClientTerrainCompletionHandler`, `ClientTerrainJobProcessor`를 추가해 월드/청크 요청과 완료 처리를 단계별 책임으로 나눴다.

## 렌더러 구조
- `Renderer.cpp`는 제거했고, 기존 책임은 lifecycle, frame-loop, Vulkan core, bridge, render path 파일로 분리했다.
- `Renderer.h`는 public API와 최상위 소유/배선 중심으로 축소했다.
- Vulkan context/swapchain/pipeline/handle 상태는 `RendererVulkanContext`, `RendererSwapchain`, `RendererPipelines`, `RendererVulkanState`로 분리했다.
- 지형, 플레이어, 파티클, 드랍 아이템, 텍스트, 스프라이트, sky, viewmodel은 각각 전용 render path로 나눴다.
- gameplay, terrain runtime, diagnostics, audio, config, UI runtime, scene lifecycle glue는 bridge 파일로 분리해 렌더러 core에 다시 섞이지 않도록 했다.

## 월드와 청크
- 청크 생성은 한 번에 완성하는 방식에서 `TerrainSourceReady`, `LocalLightReady`, `LightResolved`, `Meshed`로 승급되는 pipeline으로 재정리했다.
- `ChunkData` 내부 책임을 source/block/light/derived cache 기준으로 분리했다.
- feature와 light 작업은 주변 청크를 직접 수정하지 않고 read-only 입력을 읽어 center 청크 산출물만 반환하도록 바꿨다.
- `ResolveFeatures`는 3x3 source 입력에서 center 청크에 닿는 feature만 적용하고 local skylight까지 계산한다.
- `ResolveLight`는 center local light와 동서남북 이웃 face만 읽어 boundary propagation을 수행한다.
- 저장 snapshot 복원과 derived cache 재구축 일부를 `ChunkPrepareSystem` worker로 옮겨 메인 스레드 load completion 부담을 줄였다.

## 지형 생성
- 기존 heightmap 흐름을 Groundness, Smoothness, Weirdness, PV, baseNoise, LUT 기반 terrain value 계산으로 정리했다.
- `SplineEditor`를 범용 float spline/LUT 편집 도구로 개선하고, DLSF binary LUT export를 사용했다.
- Groundness는 바다/해안/육지 성향과 baseline을 만들고, Smoothness는 baseNoise 영향력을 조절하도록 했다.
- PV는 최종 terrain value를 누르는 weight 축으로 연결해 강/골짜기 후보 표현을 실험할 수 있게 했다.
- temperature/precipitation/groundness 기반 biome classifier와 surface/subsurface rule을 추가했다.
- 새 월드 스폰은 seed 기반 랜덤 X와 `Z = 16384` 기준에서 `grass` 표면 column을 찾아 초기 위치로 저장하도록 했다.

## 조명과 하늘
- 청크 light 배열을 추가하고 packed `uint8_t`의 상위 4비트는 skylight, 하위 4비트는 block light로 사용하도록 했다.
- `SkyLightSystem`을 추가해 local skylight와 resolved skylight를 계산하도록 했다.
- `blocks.json`/`fluids.json`에 `lightAttenuation`을 추가해 빛 감쇠를 content data에서 읽도록 했다.
- `glowing_rock` 테스트 블록과 block light 전파를 추가했다.
- 시간대별 sky shader, sun/moon 표시, sky brightness curve를 추가했다.
- 지형뿐 아니라 플레이어, 1인칭 손, 손에 든 아이템, 드랍 아이템, 파티클도 위치 기준 light와 `skyBrightness`를 사용하도록 연결했다.

## 플레이어와 조작
- 플레이어 모델을 GLB 기반으로 교체하고, node transform을 사용해 머리 회전과 보행 모션을 적용했다.
- 1인칭 손과 손에 든 아이템 viewmodel 경로를 추가하고, 별도 viewmodel FOV와 depth 처리를 적용했다.
- sprint, sneak, fly 전환, FOV 옵션, sprint FOV, sneak edge guard, sneak camera smoothing, view bobbing을 추가했다.
- `PlayerMovementSystem`을 분리해 `GameClient::updatePlayer()`의 이동 규칙 부담을 줄였다.

## 화면과 게임플레이
- HUD chat input과 local slash command system을 추가했다.
- `/help`, `/pos`, `/seed`, `/tp`, `/time`, `/stat`, `/gamemode` 등 테스트/디버그 명령어를 추가했다.
- options 화면에 BGM/SFX volume, 조작 토글, FOV, view bobbing 설정을 추가하고 `settings.json`에 저장하도록 했다.
- HP/허기/갈증 스탯 게이지와 `PlayerStats`를 추가했다.
- `Survival`/`Sandbox` 게임모드를 추가하고, player save에 게임모드와 스탯을 저장하도록 확장했다.

## 저장과 호환성
- 저장/로드는 `SaveSystem`, `ChunkLoadSystem`, `ChunkPrepareSystem`으로 책임을 분리했다.
- light 배열, player stats, game mode 등 저장 데이터가 확장되었다.
- light 저장 포맷 변경 이후 legacy save 호환은 목표로 두지 않고, 기존 세이브 삭제 후 새 월드 생성 기준으로 진행했다.

## 문서
- `docs/design`의 client architecture, rendering, chunk system, world generation, save/load, player, UI, block data 문서를 현재 구조에 맞춰 갱신했다.
- 0.0.0.2 일일 로그 중 영어 중심으로 작성된 기록을 한글 작업 기록 형식으로 정리했다.
- 날짜 규칙에 맞지 않던 `2026-05-21-chunk-prepare.md`는 `2026-05-21.md`에 병합했다.
