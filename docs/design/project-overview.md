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
현재 `Renderer`는 Vulkan 렌더링뿐 아니라 월드 런타임, 청크 저장/로드, UI, 오디오, 아이템 렌더링까지 넓은 책임을 아직 많이 가진다.
0.0.0.2에서는 `GameClient`를 기준으로 전체 흐름과 렌더링 책임을 나누는 방향으로 정리를 진행한다.
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
src/game/ClientRuntimeFacade.h
src/game/ClientRuntimeFacade.cpp
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

`DataLoaders`는 `assets/data/blocks.json`과 `assets/data/items.json`의 정의 파싱만 담당한다.
`ConfigLoaders`는 `config/world.json`과 `config/render.json`의 설정 파일 읽기와 값 검증/클램프를 담당한다.
`PropModelLoader`는 소품 모델 `.glb` 파싱, `.dpm` 변환/검증, 렌더링용 소품 quad 로드를 담당한다.
`ClientContent`는 block/item 정의, 텍스처 layer 이름, block drop의 item key 해석, prop model binding을 로드하고 Renderer/Vulkan 타입에 의존하지 않는다.
`ClientRuntimeFacade`는 `GameClient`가 사용하는 scene/gameplay/UI/render 경계 API를 제공하고 `Renderer` 직접 의존을 감춘다.
`ClientSceneLifecycle`은 game scene load/unload 순서, active world 설정, gameplay reset, terrain scene runtime start/stop, save flush를 조율하고 Renderer 전용 작업은 hook으로 호출한다.
`ClientTerrainCompletionHandler`는 terrain/chunk-load 완료 결과를 해석해 runtime 설치, 저장 snapshot enqueue, feature 전파, mesh retry/install 판정을 수행하고 Renderer에는 dropped-item tracking refresh와 GPU mesh install 대상만 반환한다.
`ClientTerrainCoordinator`는 render/mesh/full/featuring 요청 cascade, feature finalize job queue, mesh retry queue, chunk-load 완료 뒤 ticket 재개를 조율한다.
`ClientTerrainJobProcessor`는 `BuildFeaturing`과 `FinalizeFeatures` terrain job을 처리하며 `TerrainBuilder`를 호출해 초기 청크 데이터, heightmap, tree feature write 생성/반영을 수행한다.
`ClientTerrainSceneRuntime`은 terrain scene load request, terrain/chunk-load/save worker lifecycle, completed terrain work drain, pending unload의 world/save 처리를 묶어 Renderer 밖에서 조율한다.
`ClientUiTypes`는 `GameClient`와 렌더러/UI bridge 사이에서 주고받는 클라이언트 표시 DTO를 담는다.
`ClientWorldRuntime`은 클라이언트 월드 런타임 전환 계층이며 `WorldRuntime`, `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`, terrain request set, unload queue, load order, active world 상태를 소유한다.
terrain render/mesh/full/featuring ticket 설정, chunk-load 필요 판단, BuildFeaturing/FinalizeFeatures/BuildChunkMesh job 생성 조건은 `ClientWorldRuntime`이 제공하고, 요청 cascade와 job enqueue 조율은 `ClientTerrainCoordinator`가 담당한다.
terrain/chunk-load 완료 큐 drain, stale 완료 결과 저장/무시/설치 판정, pending unload 후보 관리, 저장 snapshot 생성/복원, feature slot 전파도 `ClientWorldRuntime`이 담당한다.
`ClientTerrainSceneRuntime`은 `ClientTerrainCoordinator`가 만든 terrain job을 worker에 전달하고 scene lifecycle을 조율한다.
`Renderer`는 GPU mesh 설치, 렌더 데이터 detach 같은 렌더러 의존 작업만 수행한다.
`BlockInteractionSystem`은 블록 좌표 변환, 플레이어 충돌 범위 판정, 블록 레이캐스트, 블록 파괴 진행 상태를 담당한다.
`ClientGameplayRuntime`은 클라이언트 gameplay 전환 계층이며 `PlayerInventory`, `DroppedItemRuntime`, block breaking 상태를 소유하고 블록 상호작용, 드랍 아이템, 인벤토리 조작을 Renderer/Vulkan 타입 없이 조율한다.
`PlayerInventory`는 플레이어 인벤토리 슬롯, 임시 커서 스택, 스택 병합, 클릭/Shift-click/핫바 교환 규칙을 담당한다.
`AudioSystem`은 OpenAL device/context, 효과음 buffer/source pool, 음악 source와 OGG/WAV 재생 상태를 소유한다.
`ClientUiBridge`는 `UiSystem`과 `ClientGameplayRuntime` 사이에서 hotbar/inventory RML 조립, tooltip/cursor 갱신, slot hit test, world list 표시 변환, UI 입력 기반 인벤토리 조작을 담당한다.
`UiSystem`은 RmlUi 초기화/종료, context, 문서, 버튼 이벤트 수신, 문서 표시 상태, 문서 element 갱신을 소유한다.
`InventoryUi`는 인벤토리/핫바 슬롯 좌표, 아이템/툴팁 RML 문자열, 툴팁 위치 계산을 담당하는 표시 helper다.
`RmlInput`은 GLFW 키/수정키 상태를 RmlUi 입력 값으로 변환한다.
`SaveFormat`은 region 청크 payload 직렬화/역직렬화, LZ4 block encode/decode, 저장 좌표 래핑 helper를 담당한다.
`SaveSystem`은 save worker, 저장 큐, pending snapshot, clean revision cache, region header cache, region file IO, 저장/로드 카운터를 소유한다.
`ChunkLoadSystem`은 chunk-load worker, snapshot load 요청 큐, 완료 큐, 중복 요청 추적을 소유한다.
`DroppedItemSystem`은 드롭 아이템 엔티티 생성, 청크 소유권 helper, 병합, 물리 tick, 플레이어 pickup 판정을 담당한다.
`DroppedItemRuntime`은 클라이언트 런타임의 드랍 아이템 entity id 할당, 청크별 드랍 아이템 추적, 블록 드롭/수동 드롭 생성 연결, pickup/raycast, tick/update 조율을 담당하며 Renderer/Vulkan 타입에 의존하지 않는다.
`TerrainBuilder`는 높이맵, 초기 청크 블록/유체/기후 데이터, tree feature write 생성과 feature write 반영을 담당한다.
`TerrainJobSystem`은 terrain worker thread, terrain job 큐, terrain 완료 큐를 소유한다.
`TerrainMesher`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 담당한다.
`WorldRuntime`은 runtime chunk map, chunk key/좌표 helper, runtime block 조회/수정, dirty marking, chunk derived cache 갱신, terrain 완료 결과의 runtime chunk 상태 설치를 소유한다.
`Renderer` 구현은 `src/renderer/Renderer.cpp`, `src/renderer/RendererUi.cpp`, `src/renderer/RendererDroppedItems.cpp`로 나뉘어 있다.
프레임 렌더링 입력은 `src/renderer/RendererFrame.h`의 `RendererFrame` DTO로 묶어 `GameClient`에서 전달한다.
`src/renderer/RendererGpuResources.h/.cpp`는 texture/image/buffer, one-time command, mipmap 생성, descriptor set 업데이트 같은 Vulkan GPU 리소스 helper를 담당한다.
`src/renderer/RendererAssetStore.h/.cpp`는 `ClientContent`가 제공한 asset 이름을 바탕으로 Vulkan texture array, 기본 UI/sky/player texture, item sprite mesh, prop render mesh를 생성하고 수명을 관리한다.
`src/renderer/SpriteRenderPath.h/.cpp`는 sprite pipeline push constant, texture descriptor bind, screen-space sprite draw primitive를 담당한다.
`src/renderer/ScreenPresentation.h/.cpp`는 sky sprite, scene composite, climate overlay, crosshair, fallback menu background/buttons/text, debug text draw timing을 담당한다.
`src/world/ClimateSystem.h/.cpp`는 climate seed, tileable climate noise sampling, chunk climate population, temperature/precipitation 값 계산을 담당하며 Renderer/Vulkan 타입에 의존하지 않는다.
`src/renderer/ClimateOverlayTextureBuilder.h/.cpp`는 `ClimateSystem`을 입력으로 temperature/precipitation overlay RGBA pixel 데이터를 생성한다.
`src/renderer/DebugOverlayText.h/.cpp`는 hardware/performance/terrain/debug 표시 문자열과 text batch dirty 상태를 소유한다.
`src/renderer/TerrainGeometryBuilder.h/.cpp`는 block 정의, texture layer, prop mesh를 입력으로 받아 solid/cross/prop terrain CPU mesh를 생성하며 Vulkan 타입에 의존하지 않는다.
`src/renderer/RendererTerrainMeshBridge.h/.cpp`는 `TerrainGeometryBuilder`와 `TerrainMesher`를 연결해 chunk mesh와 edited subchunk mesh의 CPU 조립을 담당한다.
`src/renderer/TextRenderPath.h/.cpp`는 font atlas 생성, text vertex buffer, text batch 구성, debug/menu text draw submission을 담당한다.
`src/renderer/TerrainRenderPath.h/.cpp`는 terrain render chunk storage, render chunk 설치/교체/retire 규칙, retired mesh cleanup, packed quad 변환, terrain GPU upload, terrain vertex descriptor set 생성, solid/fluid terrain mesh draw loop와 terrain frustum culling을 담당한다.
`src/renderer/PlayerMeshRenderPath.h/.cpp`는 player mesh 파일 로드, player vertex/index buffer 생성, 매 프레임 player vertex 갱신, player indexed draw와 buffer 수명을 담당한다.
`src/renderer/ParticleRenderPath.h/.cpp`는 블록 파괴 파티클과 파괴 오버레이 렌더링 상태, host-visible particle vertex/index buffer, 파티클 갱신과 draw path를 담당한다.
`src/renderer/DroppedItemRenderCollector.h/.cpp`는 dropped item 렌더 후보 수집, 청크 frustum culling, 거리 culling, stack count별 시각 복제본 생성을 담당한다.
`src/renderer/DroppedItemRenderPath.h/.cpp`는 dropped item의 아이템 스프라이트 GPU mesh, persistent instance buffer, instance 업로드, item id별 batch draw를 담당한다.
`src/renderer/ItemSpriteMeshBuilder.h/.cpp`는 아이템 텍스처 alpha에서 드랍 아이템용 extruded sprite mesh를 생성한다.
`RendererDroppedItems.cpp`는 `DroppedItemRuntime` update 호출, 렌더 후보 수집 입력 조립, push constant 준비, `DroppedItemRenderPath` draw 호출만 담당한다.
텍스처 배열과 Vulkan buffer/image 생성은 `VulkanResourceManager`를 통해 수행하고, content 정의 해석은 `ClientContent`, GPU asset 생성/해제는 `RendererAssetStore`가 담당한다.
`Renderer`는 오디오 초기화/종료, listener 갱신, 음악 씬 분류, 효과음 발생 시점만 `AudioSystem`에 전달한다.
`Renderer`는 RmlUi의 Vulkan `RenderInterface`, UI geometry 업로드, texture load/release, render command 연결을 담당하고 UI 표시 변환과 인벤토리 입력 처리는 `ClientUiBridge`에 위임한다.
`Renderer`는 블록 상호작용 입력을 `ClientGameplayRuntime`에 전달하고 gameplay 결과에 따른 mesh 재생성, particle/sound 실행을 담당한다.
`Renderer`는 runtime chunk와 terrain/save/load 시스템을 직접 소유하지 않고 `ClientWorldRuntime`이 소유한 `WorldRuntime`과 worker system을 참조하는 전환 단계에 있다.
저장할 runtime chunk snapshot 생성, terrain 완료 결과 snapshot 저장 enqueue, snapshot에서 runtime chunk 복원, load-state incoming feature merge는 `ClientWorldRuntime`이 담당한다.
terrain 요청 cascade, chunk-load 완료 후 ticket 재개, feature finalize/mesh 재시도 queue는 `ClientTerrainCoordinator`가 담당한다.
terrain/chunk-load 완료 결과의 save/install/ignore/retry 판정 흐름은 `ClientTerrainCompletionHandler`가 담당한다.
terrain scene load request, worker start/stop, completed work drain, pending unload의 world/save 처리 흐름은 `ClientTerrainSceneRuntime`이 담당한다.
`Renderer`는 terrain job callback에서 render-dependent `BuildChunkMesh` 경계에 한해 `RendererTerrainMeshBridge`를 호출한다.
terrain 완료 결과의 runtime 상태 설치는 `WorldRuntime`에 위임하고, solid/cross/prop CPU mesh 생성은 `TerrainGeometryBuilder`, chunk/edited subchunk CPU mesh 조립은 `RendererTerrainMeshBridge`, GPU mesh buffer 설치, render chunk 교체/폐기, edited subchunk 교체, terrain draw loop는 `TerrainRenderPath`가 담당한다.
fluid subchunk mesh 생성과 불투명 블록 판정 연결은 `RendererTerrainMeshBridge`가 `TerrainMesher` 호출 안에서 처리한다.
climate 규칙과 chunk climate 채우기는 `ClimateSystem`이 담당하고, climate overlay pixel 생성은 `ClimateOverlayTextureBuilder`가 담당한다.
screen-space presentation은 `ScreenPresentation`이 담당하고, sprite draw primitive는 `SpriteRenderPath`, debug/menu text state는 `DebugOverlayText`, text atlas와 vertex upload는 `TextRenderPath`가 담당한다.
`GameClient`는 `ClientRuntimeFacade`를 통해 클라이언트 런타임과 렌더러 경계를 호출한다.
game scene load/unload의 전체 순서와 정책은 `ClientSceneLifecycle`이 담당하고, `Renderer`는 device idle, terrain render data destroy, particle clear, UI refresh 같은 renderer/UI 경계 hook만 제공한다.

## 좌표계

- `X+`: 동쪽
- `Z+`: 북쪽
- 블록 좌표의 기준은 블록의 아래 중앙이다.
- 플레이어 좌표 `POS`는 발밑 중앙이다.
- 1인칭 눈 위치는 플레이어 `Y + 1.5625`다.

## 현재 주의점

- 현재 버전은 `0.0.0.2`이다.
- `docs/design`은 확정된 설계만 남기는 공간이고, 실험 과정은 `docs/0.0.0.2` 날짜 로그에 기록한다.
- Release 경로와 Debug 경로가 다르므로 경로 관련 작업은 [[runtime-paths]] 기준을 따른다.
