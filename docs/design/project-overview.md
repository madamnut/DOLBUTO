# 프로젝트 개요

## 목표

DOLBUTO는 Vulkan/C++ 기반의 샌드박스 복셀 게임이다.
최종 방향은 마인크래프트 류의 블록 월드이며, 현재는 월드 생성, 청크 로딩, 메쉬 렌더링, 블록 편집, 저장/로드의 기초 시스템을 구현한 상태다.

## 구현 원칙

- 한 파일이나 한 함수에 모든 기능을 몰아넣지 않고 역할별로 분리한다.
- 구현은 최대한 단순하게 시작하고, 실제 병목이나 필요가 확인될 때만 확장한다.
- 과한 범용화, 미리 만든 대규모 시스템, 불필요한 외부 의존성은 피한다.
- 빌드는 항상 사용자가 직접 수행한다.

## 주요 구성

- 애플리케이션/입력/플레이어: [[player]]
- 클라이언트 구조/의존 방향: [[client-architecture]]
- 런타임 경로: [[runtime-paths]]
- Vulkan 렌더링: [[rendering]]
- 청크 로딩/작업 큐: [[chunk-system]]
- 지형 생성/피처 생성: [[world-generation]]
- 월드 저장/로드: [[save-load]]
- 블록 정의와 텍스처: [[block-data]]
- 디버그/프로파일링: [[debug-profiling]]
- 배포 설정: [[build-and-distribution]]

## 코드 구조

`main.cpp`는 로그 초기화, GLFW/window 생성, 최상위 예외 처리, `GameClient` 실행을 담당한다.
`src/game/GameClient.h/.cpp`는 클라이언트 런타임 오케스트레이터로서 메인 루프, 입력, 플레이어 상태, 월드 선택/생성, 월드/플레이어 상태 저장, UI action 처리를 담당한다.
현재 `Renderer`는 Vulkan/GPU 리소스와 화면 출력 경계를 맡고, 월드 런타임, 청크 저장/로드, UI, 오디오, gameplay 상태 조율은 `ClientRuntime` 계열 subsystem으로 분리되어 있다.
0.0.0.3에서는 이 구조를 기준으로 새 기능을 추가하되, renderer 중심 구조로 되돌아가지 않도록 유지한다.
클라이언트 계층과 의존 방향은 [[client-architecture]]를 기준으로 한다.

현재 분리된 타입 헤더:

```text
src/world/BlockData.h
src/world/WorldTypes.h
src/items/ItemData.h
src/renderer/TerrainTypes.h
```

`WorldTypes.h`는 청크 크기 상수, 청크 데이터, feature write, terrain job, 저장 snapshot, 런타임 청크, 월드 엔티티 같은 월드 런타임 타입을 담는다.

현재 분리된 데이터 로더:

```text
src/data/DataLoaders.h
src/data/DataLoaders.cpp
src/config/ConfigLoaders.h
src/config/ConfigLoaders.cpp
src/assets/PropModelLoader.h
src/assets/PropModelLoader.cpp
src/game/ClientContent.h
src/game/ClientContent.cpp
src/game/ClientFrame.h
src/game/ClientRuntime.h
src/game/ClientRuntime.cpp
src/game/ClientRuntimeState.h
src/game/ClientRenderRuntime.h
src/game/ClientRenderRuntime.cpp
src/game/ClientSceneLifecycle.h
src/game/ClientSceneLifecycle.cpp
src/game/ClientTerrainCompletionHandler.h
src/game/ClientTerrainCompletionHandler.cpp
src/game/ClientTerrainCoordinator.h
src/game/ClientTerrainCoordinator.cpp
src/game/ClientTerrainJobProcessor.h
src/game/ClientTerrainJobProcessor.cpp
src/game/ClientTerrainSceneRuntime.h
src/game/ClientTerrainSceneRuntime.cpp
src/game/ClientUiTypes.h
src/game/ClientWorldRuntime.h
src/game/ClientWorldRuntime.cpp
src/gameplay/BlockInteractionSystem.h
src/gameplay/BlockInteractionSystem.cpp
src/gameplay/ClientGameplayRuntime.h
src/gameplay/ClientGameplayRuntime.cpp
src/gameplay/PlayerInventory.h
src/gameplay/PlayerInventory.cpp
src/audio/AudioSystem.h
src/audio/AudioSystem.cpp
src/ui/ClientUiBridge.h
src/ui/ClientUiBridge.cpp
src/ui/UiSystem.h
src/ui/UiSystem.cpp
src/ui/InventoryUi.h
src/ui/InventoryUi.cpp
src/ui/RmlInput.h
src/ui/RmlInput.cpp
src/save/SaveFormat.h
src/save/SaveFormat.cpp
src/save/SaveSystem.h
src/save/SaveSystem.cpp
src/world/ChunkLoadSystem.h
src/world/ChunkLoadSystem.cpp
src/world/ChunkPrepareSystem.h
src/world/ChunkPrepareSystem.cpp
src/world/DroppedItemRuntime.h
src/world/DroppedItemRuntime.cpp
src/world/DroppedItemSystem.h
src/world/DroppedItemSystem.cpp
src/world/TerrainBuilder.h
src/world/TerrainBuilder.cpp
src/world/TerrainJobSystem.h
src/world/TerrainJobSystem.cpp
src/world/TerrainMesher.h
src/world/TerrainMesher.cpp
src/world/WorldRuntime.h
src/world/WorldRuntime.cpp
```

`DataLoaders`는 `assets/data/blocks.json`, `assets/data/fluids.json`, `assets/data/items.json`의 정의 파싱만 담당한다.
`ConfigLoaders`는 `config/world.json`과 `config/render.json`의 설정 파일 읽기와 값 검증/클램프를 담당한다.
`PropModelLoader`는 소품 모델 `.glb` 파싱, `.dpm` 변환/검증, 렌더링용 소품 quad 로드를 담당한다.
`ClientContent`는 block/fluid/item 정의, 텍스처 layer 이름, block drop의 item key 해석, prop model binding, light attenuation table을 로드하고 Renderer/Vulkan 타입에 의존하지 않는다.
`ClientFrame`은 `GameClient`가 한 프레임 렌더링에 넘기는 카메라, 플레이어, overlay, debug, screenshot, world tick 입력을 담는 client 계층 DTO다.
`ClientRuntime`은 `GameClient`가 사용하는 scene/gameplay/UI/render 경계 API를 제공하는 클라이언트 런타임 진입점이다.
public API는 `render()`, `scene()`, `gameplay()`, `ui()`, `diagnostics()` access로 나누어 `GameClient` 호출부에서 역할이 드러나게 한다.
`ClientRuntimeState`는 selection, world/render config, diagnostics, gameplay/world/terrain scene lifecycle, content, UI bridge, audio를 묶는 클라이언트 런타임 상태이며 `ClientRuntime`이 소유한다.
`ClientRuntime`은 Renderer/GPU가 필요 없는 collision query, block selection state, inventory snapshot, UI action/input query, selected block/climate text를 `ClientRuntimeState`에서 직접 처리한다.
`ClientRenderRuntime`은 현재 전환 단계의 렌더링 런타임 adapter이며 `Renderer` 직접 의존, renderer bridge 접근, `RendererFrame` 변환, mesh/particle/sound/viewport가 필요한 호출을 `GameClient`와 `ClientRuntime` 밖으로 숨긴다.
`ClientSceneLifecycle`은 game scene load/unload 순서, active world 설정, gameplay reset, terrain scene runtime start/stop, save flush를 조율하고 Renderer 전용 작업은 hook으로 호출한다.
`ClientTerrainCompletionHandler`는 terrain/chunk-load 완료 결과를 해석해 runtime 설치, 저장 snapshot enqueue, 다음 단계 job 재검사, mesh retry/install 판정을 수행하고 Renderer에는 dropped-item tracking refresh와 GPU mesh install 대상만 반환한다.
`ClientTerrainCoordinator`는 render 요청에서 target status 범위를 설정하고, frontier scheduling으로 feature/light/mesh job queue와 chunk-load 완료 뒤 재검사를 조율한다.
`ClientTerrainJobProcessor`는 `BuildTerrainSource`, `ResolveFeatures`, `ResolveLight` terrain job을 처리하며 `TerrainBuilder`와 `SkyLightSystem`을 호출해 초기 청크 데이터, 3x3 feature resolve와 local skylight cache, 4방향 face 기반 skylight resolve를 수행한다.
`ClientTerrainSceneRuntime`은 terrain scene load request, terrain/chunk-load/save worker lifecycle, completed terrain work drain, pending unload의 world/save 처리를 묶어 Renderer 밖에서 조율한다.
`ClientUiTypes`는 `GameClient`와 렌더러/UI bridge 사이에서 주고받는 클라이언트 표시 DTO를 담는다.
`ClientWorldRuntime`은 클라이언트 월드 런타임 전환 계층이며 `WorldRuntime`, `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`, terrain request set, unload queue, load order, active world 상태를 소유한다.
terrain render/mesh/source/local-light/light ticket 설정, chunk-load 필요 판단, `BuildTerrainSource`/`ResolveFeatures`/`ResolveLight`/`BuildChunkMesh` job 생성 조건은 `ClientWorldRuntime`이 제공하고, target status 설정과 frontier job enqueue 조율은 `ClientTerrainCoordinator`가 담당한다.
terrain/chunk-load 완료 큐 drain, stale 완료 결과 저장/무시/설치 판정, pending unload 후보 관리, 저장 snapshot 생성/복원도 `ClientWorldRuntime`이 담당한다.
`ClientTerrainSceneRuntime`은 `ClientTerrainCoordinator`가 만든 terrain job을 worker에 전달하고 scene lifecycle을 조율한다.
`Renderer`는 GPU mesh 설치, 렌더 데이터 detach 같은 렌더러 의존 작업만 수행한다.
`BlockInteractionSystem`은 블록 좌표 변환, 플레이어 충돌 범위 판정, 블록 레이캐스트, 블록 파괴 진행 상태를 담당한다.
`ClientGameplayRuntime`은 클라이언트 gameplay 전환 계층이며 `PlayerInventory`, `DroppedItemRuntime`, block breaking 상태를 소유하고 블록 상호작용, 드랍 아이템, 인벤토리 조작을 Renderer/Vulkan 타입 없이 조율한다.
`PlayerInventory`는 플레이어 인벤토리 슬롯, 왼손 슬롯, 임시 커서 스택, 클릭/Shift-click/핫바 교환 규칙을 담당한다.
`AudioSystem`은 OpenAL device/context, 효과음 buffer/source pool, 음악 source와 OGG/WAV 재생 상태를 소유한다.
`ClientUiBridge`는 `UiSystem`과 `ClientGameplayRuntime` 사이에서 hotbar/inventory RML 조립, tooltip/cursor 갱신, slot hit test, world list 표시 변환, UI 입력 기반 인벤토리 조작을 담당한다.
`UiSystem`은 RmlUi 초기화/종료, context, 문서, 버튼 이벤트 수신, 문서 표시 상태, 문서 element 갱신을 소유한다.
`InventoryUi`는 인벤토리/핫바 슬롯 좌표, 아이템/툴팁 RML 문자열, 툴팁 위치 계산을 담당하는 표시 helper다.
`RmlInput`은 GLFW 키/수정키 상태를 RmlUi 입력 값으로 변환한다.
`SaveFormat`은 region 청크 payload 직렬화/역직렬화, LZ4 block encode/decode, 저장 좌표 래핑 helper를 담당한다.
`SaveSystem`은 save worker, 저장 큐, pending snapshot, clean revision cache, region header cache, region file IO, 저장/로드 카운터를 소유한다.
`ChunkLoadSystem`은 chunk-load worker, snapshot load 요청 큐, 완료 큐, 중복 요청 추적을 소유한다. worker는 region IO와 `SaveSystem::load` 호출만 담당한다.
`ChunkPrepareSystem`은 chunk-prepare worker와 준비 큐를 소유한다. 저장 snapshot을 `RuntimeChunk`로 복원하고 derived cache를 재구축해 메인 스레드의 load 완료 처리 비용을 줄인다.
`DroppedItemSystem`은 드롭 아이템 엔티티 생성, 청크 소유권 helper, 드롭 아이템끼리의 물리 충돌, 물리 tick, 플레이어 pickup 판정을 담당한다.
`DroppedItemRuntime`은 클라이언트 런타임의 드랍 아이템 entity id 할당, 청크별 드랍 아이템 추적, 블록 드롭/수동 드롭 생성 연결, pickup/raycast, tick/update 조율을 담당하며 Renderer/Vulkan 타입에 의존하지 않는다.
`Biome`은 temperature, precipitation, groundness 기반 5단계 biome band 분류와 biome 이름 조회를 담당한다.
`TerrainBuilder`는 높이맵, 초기 청크 블록/유체/기후 데이터, TerrainSourceReady source checkpoint, 3x3 source view 기반 tree feature resolve를 담당한다.
`TerrainJobSystem`은 terrain worker thread, terrain job 큐, terrain 완료 큐를 소유한다.
`TerrainMesher`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 담당한다.
`WorldRuntime`은 runtime chunk map, chunk key/좌표 helper, runtime block 조회/수정, dirty marking, chunk derived cache 갱신, terrain 완료 결과의 runtime chunk 상태 설치를 소유한다.
`Renderer` 구현은 `src/renderer/RendererLifecycle.cpp`, `src/renderer/RendererLocalResources.cpp`, `src/renderer/RendererSceneDraw.cpp`, `src/renderer/RendererVulkanContext.cpp`, `src/renderer/RendererSwapchain.cpp`, `src/renderer/RendererPipelines.cpp`, `src/renderer/RendererFrameLoop.cpp`, `src/renderer/RendererGameplayBridge.cpp`, `src/renderer/RendererTerrainRuntimeBridge.cpp`, `src/renderer/RendererSceneLifecycleBridge.cpp`, `src/renderer/RendererConfig.cpp`, `src/renderer/RendererAudio.cpp`, `src/renderer/RendererDiagnostics.cpp`, `src/renderer/RendererClimateOverlay.cpp`, `src/renderer/RendererUi.cpp`, `src/renderer/RendererRmlUiBackend.cpp`, `src/renderer/RendererDroppedItems.cpp`로 나뉘어 있다.
`src/renderer/RendererLifecycle.cpp`는 Renderer 생성/해제 순서와 queue family complete 판정을 담당한다.
`src/renderer/RendererLocalResources.cpp`는 text render path, UI buffers, particle/dropped-item render path buffers, selection line buffer, player mesh 생성 hook을 담당한다.
`src/renderer/RendererSceneDraw.cpp`는 terrain/player/particle/selection draw helper와 draw용 matrix helper를 담당한다.
`src/renderer/RendererTypes.h`는 renderer 구현 파일들이 공유하는 queue family, UI vertex/push/geometry, selection line vertex, terrain push constant 타입을 담는다.
`src/renderer/RendererVulkanMethods.inc`, `src/renderer/RendererRenderMethods.inc`는 C++ member function 선언 제약 때문에 `Renderer` class private section에 포함되는 구현 선언 fragment이며, `Renderer.h` 본문은 public API와 소유/배선 구조를 중심으로 유지한다.
`src/renderer/RendererVulkanContext.cpp`는 Vulkan instance/surface/device/queue/command pool/query pool과 hardware 정보 수집을 담당한다.
`src/renderer/RendererSwapchain.cpp`는 swapchain, image view, render pass, depth/scene target, framebuffer, swapchain recreate/cleanup을 담당한다.
`src/renderer/RendererPipelines.cpp`는 descriptor set layout, descriptor pool, sampler, shader module 로드, sprite/UI/terrain/particle/item/selection pipeline 생성을 담당한다.
`src/renderer/RendererVulkanState.h`는 Vulkan instance/device/swapchain/pipeline/command/sync/query/RmlUi buffer handle 상태를 `RendererVulkanState`로 묶는다.
`src/game/ClientRuntimeState.h`는 client runtime 전환 상태를 selection, world/render config, diagnostics, gameplay/world/terrain scene lifecycle, content, UI bridge, audio 묶음으로 보관한다.
`src/renderer/RendererFrameLoop.cpp`는 swapchain image acquire/present, per-frame fence/semaphore 흐름, command buffer 기록, screenshot readback/BMP 저장, command buffer/sync object 생성을 담당한다.
`src/renderer/RendererGameplayBridge.h/.cpp`는 block selection/edit/breaking, pickup/drop, inventory snapshot, block lookup/collision helper, gameplay 결과에 따른 mesh/particle/audio 반영을 담당하는 `RendererGameplayBridge`를 담는다.
`src/renderer/RendererTerrainRuntimeBridge.h/.cpp`는 loaded chunk 갱신, terrain load request, terrain job completion, pending unload, retired terrain chunk 처리, edited mesh rebuild, terrain stats 갱신을 담당하는 `RendererTerrainRuntimeBridge` 객체를 담는다.
`src/renderer/RendererSceneLifecycleBridge.h/.cpp`는 scene load/unload hook 조립과 renderer-specific scene lifecycle callback 연결을 담당하는 `RendererSceneLifecycleBridge` 객체를 담는다.
`src/renderer/RendererConfigBridge.h/.cpp`는 content/GPU asset load, world/render config load, height LUT load를 담당하는 `RendererConfigBridge` 객체를 담는다.
`src/renderer/RendererAudioBridge.h/.cpp`는 audio init/shutdown, listener update, music scene selection, gameplay sound trigger를 담당하는 `RendererAudioBridge` 객체를 담는다.
`src/renderer/RendererDiagnosticsBridge.h/.cpp`는 selected block/climate/debug/performance/VRAM text를 담당하는 `RendererDiagnosticsBridge` 객체를 담는다.
`src/renderer/RendererClimateOverlay.cpp`는 climate overlay texture 생성을 담당한다.
`src/renderer/RendererUiRuntimeBridge.h/.cpp`는 RmlUi 초기화/종료, UI frame render 호출, UI input forwarding, inventory/world list UI 갱신, UI action/input value 조회를 담당하는 `RendererUiRuntimeBridge` 객체를 담는다.
`src/renderer/RendererRmlUiBackend.h/.cpp`는 RmlUi `RenderInterface`, UI geometry upload, UI texture load/generate/release, scissor 상태를 담당하며 `Renderer`는 이 backend를 `UiSystem`에 주입한다.
프레임 렌더링 입력은 `src/game/ClientFrame.h`의 `ClientFrame` DTO로 묶어 `GameClient`에서 `ClientRuntime`으로 전달한다.
`ClientRenderRuntime`은 `ClientFrame`을 `src/renderer/RendererFrame.h`의 `RendererFrame`으로 변환해 Renderer에 전달한다.
`src/renderer/RendererGpuResources.h/.cpp`는 texture/image/buffer, one-time command, mipmap 생성, descriptor set 업데이트 같은 Vulkan GPU 리소스 helper를 담당한다.
`src/renderer/RendererAssetStore.h/.cpp`는 `ClientContent`가 제공한 asset 이름을 바탕으로 Vulkan texture array, 기본 UI/sky/player texture, item sprite mesh, prop render mesh를 생성하고 수명을 관리한다.
`src/renderer/SpriteRenderPath.h/.cpp`는 sprite pipeline push constant, texture descriptor bind, screen-space sprite draw primitive를 담당한다.
`src/renderer/ScreenPresentation.h/.cpp`는 sky sprite, scene composite, climate overlay, crosshair, fallback menu background/buttons/text, debug text draw timing을 담당한다.
`src/world/ClimateSystem.h/.cpp`는 climate seed, tileable climate noise sampling, chunk climate population, temperature/precipitation 값 계산을 담당하며 Renderer/Vulkan 타입에 의존하지 않는다.
`src/renderer/ClimateOverlayTextureBuilder.h/.cpp`는 `ClimateSystem`과 terrain debug sample을 입력으로 temperature/precipitation/terrain noise overlay RGBA pixel 데이터를 생성한다.
`src/renderer/DebugOverlayText.h/.cpp`는 hardware/performance/terrain/debug 표시 문자열과 text batch dirty 상태를 소유한다.
`src/renderer/TerrainGeometryBuilder.h/.cpp`는 block 정의, texture layer, prop mesh를 입력으로 받아 solid/blend/cross/prop terrain CPU mesh를 생성하며 Vulkan 타입에 의존하지 않는다.
`src/renderer/RendererTerrainMeshBridge.h/.cpp`는 `TerrainGeometryBuilder`와 `TerrainMesher`를 연결해 chunk mesh와 edited subchunk mesh의 CPU 조립을 담당한다.
`src/renderer/TextRenderPath.h/.cpp`는 font atlas 생성, text vertex buffer, text batch 구성, debug/menu text draw submission을 담당한다.
`src/renderer/TerrainRenderPath.h/.cpp`는 terrain render chunk storage, render chunk 설치/교체/retire 규칙, retired mesh cleanup, packed quad 변환, terrain GPU upload, terrain vertex descriptor set 생성, solid/blend/fluid terrain mesh draw loop와 terrain frustum culling을 담당한다.
`src/renderer/PlayerModelLoader.h/.cpp`는 player GLB의 node, mesh primitive, vertex/index, animation channel 데이터를 읽어 파트별 플레이어 모델 source data와 animation clip data를 만든다.
`src/renderer/PlayerMeshRenderPath.h/.cpp`는 player GLB 모델 로드, player vertex/index buffer 생성, 매 프레임 GLB 상태별 animation pose 기반 transform buffer 갱신, player indexed draw와 buffer 수명을 담당한다.
플레이어 머리 회전은 `ClientFrame`/`RendererFrame`의 head yaw/pitch 값을 animation pose 적용 뒤 `Head` 제어 node transform에 추가 적용하는 방식으로 처리한다.
`src/renderer/ParticleRenderPath.h/.cpp`는 블록 파괴 파티클과 파괴 오버레이 렌더링 상태, host-visible particle vertex/index buffer, 파티클 갱신과 draw path를 담당한다.
`src/renderer/DroppedItemRenderCollector.h/.cpp`는 dropped item 렌더 후보 수집, 청크 frustum culling, 거리 culling, 렌더 instance 생성을 담당한다.
`src/renderer/DroppedItemRenderPath.h/.cpp`는 dropped item의 아이템 스프라이트 GPU mesh, persistent instance buffer, instance 업로드, item id별 batch draw를 담당한다.
`src/renderer/ItemSpriteMeshBuilder.h/.cpp`는 아이템 텍스처 alpha에서 드랍 아이템용 extruded sprite mesh를 생성한다.
`RendererDroppedItems.cpp`는 `DroppedItemRuntime` update 호출, 렌더 후보 수집 입력 조립, push constant 준비, `DroppedItemRenderPath` draw 호출만 담당한다.
frame acquire/submit/present와 command buffer 기록은 `RendererFrameLoop.cpp`가 담당하고, gameplay/terrain/scene/debug bridge는 별도 translation unit에 둔다. 이전 `Renderer.cpp`는 제거되었고, lifecycle/local resource/scene draw 책임은 각각 이름 있는 translation unit에 둔다.
텍스처 배열과 Vulkan buffer/image 생성은 `VulkanResourceManager`를 통해 수행하고, content 정의 해석은 `ClientContent`, GPU asset 생성/해제는 `RendererAssetStore`가 담당한다.
`Renderer`는 오디오 초기화/종료, listener 갱신, 음악 씬 분류, 효과음 발생 시점만 `AudioSystem`에 전달한다.
`RendererUiRuntimeBridge`는 RmlUi runtime 호출과 `ClientUiBridge` 입력/표시 변환을 묶고, `RendererRmlUiBackend`는 RmlUi의 Vulkan `RenderInterface`, UI geometry 업로드, texture load/release, render command 연결을 담당한다.
`Renderer`는 블록 상호작용 입력을 `ClientGameplayRuntime`에 전달하고 gameplay 결과에 따른 mesh 재생성, particle/sound 실행을 담당한다.
`Renderer`는 runtime chunk와 terrain/save/load 시스템을 직접 소유하지 않고 `ClientRuntime`이 소유한 `ClientRuntimeState` 안의 `ClientWorldRuntime`과 worker system을 참조하는 전환 단계에 있다.
저장할 runtime chunk snapshot 생성, terrain 완료 결과 snapshot 저장 enqueue, snapshot에서 runtime chunk 복원은 `ClientWorldRuntime`이 담당한다.
terrain target status 설정, chunk-load 완료 후 주변 frontier 재검사, feature/light/mesh 재시도 queue는 `ClientTerrainCoordinator`가 담당한다.
terrain/chunk-load 완료 결과의 save/install/ignore/retry 판정 흐름은 `ClientTerrainCompletionHandler`가 담당한다.
terrain scene load request, worker start/stop, completed work drain, pending unload의 world/save 처리 흐름은 `ClientTerrainSceneRuntime`이 담당한다.
`Renderer`는 terrain job callback에서 render-dependent `BuildChunkMesh` 경계에 한해 `RendererTerrainMeshBridge`를 호출한다.
terrain 완료 결과의 runtime 상태 설치는 `WorldRuntime`에 위임하고, solid/blend/cross/prop CPU mesh 생성은 `TerrainGeometryBuilder`, chunk/edited subchunk CPU mesh 조립은 `RendererTerrainMeshBridge`, GPU mesh buffer 설치, render chunk 교체/폐기, edited subchunk 교체, terrain draw loop는 `TerrainRenderPath`가 담당한다.
fluid subchunk mesh 생성과 불투명 블록 판정 연결은 `RendererTerrainMeshBridge`가 `TerrainMesher` 호출 안에서 처리한다.
climate 규칙과 chunk climate 채우기는 `ClimateSystem`이 담당하고, climate overlay pixel 생성은 `ClimateOverlayTextureBuilder`가 담당한다.
screen-space presentation은 `ScreenPresentation`이 담당하고, sprite draw primitive는 `SpriteRenderPath`, debug/menu text state는 `DebugOverlayText`, text atlas와 vertex upload는 `TextRenderPath`가 담당한다.
`GameClient`는 `ClientRuntime`를 통해 클라이언트 런타임 경계를 호출하고, 역할별 access인 `render()`, `scene()`, `gameplay()`, `ui()`, `diagnostics()`를 통해 호출 의도를 드러낸다.
`CommandSystem`은 HUD 채팅 입력에서 제출된 slash-prefixed text를 파싱하고, 메시지와 GameClient가 적용할 로컬 액션을 반환한다.
현재 로컬 명령어는 도움말, 위치/시드 조회, 플레이어 텔레포트, world tick set/add만 담당하며, 청크 블록 수정처럼 runtime/mesh rebuild 경계가 필요한 명령은 별도 runtime API가 정리된 뒤 추가한다.
`ClientRuntime`은 현재 렌더러 의존 호출을 `ClientRenderRuntime`에 위임한다.
game scene load/unload의 전체 순서와 정책은 `ClientSceneLifecycle`이 담당하고, `Renderer`는 device idle, terrain render data destroy, particle clear, UI refresh 같은 renderer/UI 경계 hook만 제공한다.

## 좌표계

- `X+`: 동쪽
- `Z+`: 북쪽
- 블록 좌표의 기준은 블록의 아래 중앙이다.
- 플레이어 좌표 `POS`는 발밑 중앙이다.
- 1인칭 눈 위치는 플레이어 `Y + 1.5625`다.

## 현재 주의점

- 현재 버전은 `0.0.0.3`이다.
- `docs/design`은 확정된 설계만 남기는 공간이고, 실험 과정은 `docs/0.0.0.3` 날짜 로그에 기록한다.
- Release 경로와 Debug 경로가 다르므로 경로 관련 작업은 [[runtime-paths]] 기준을 따른다.
