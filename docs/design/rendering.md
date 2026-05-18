# 렌더링

## 현재 방식

월드는 레이마칭이 아니라 메쉬 기반으로 렌더링한다.
청크 데이터에서 보이는 면을 만들고, 그 결과를 Vulkan 버퍼에 올려 그린다.

## 렌더링 대상

- 지형 블록 메쉬
- 플레이어 스킨 메쉬
- 선택 블록 검정 테두리
- 하늘, 해, 달, 크로스헤어
- 디버그 텍스트

## 지형 메쉬

지형은 그리디 메싱을 사용한다.

- 보이지 않는 면은 생성하지 않는다.
- 큐브형 블록은 가능한 면을 합친다.
- AO 패턴이 다른 면은 무리해서 합치지 않는다.
- 식물 같은 `cross` 렌더 타입은 X자 스프라이트 형태로 만든다.
- 청크 메싱은 주변 8청크 정보를 사용해 경계면을 처리한다.

## GPU 데이터

지형 메쉬는 최종적으로 `PackedTerrainQuad` 단위로 압축된다.

- CPU 메싱 결과는 임시 `TerrainVertex`/index 형태로 만들어진다.
- 업로드 직전에 quad record로 변환한다.
- terrain vertex shader가 SSBO에서 quad record를 읽어 6개의 가상 vertex를 생성한다.
- terrain은 index buffer 없이 `vkCmdDraw`를 사용한다.
- player는 별도 vertex/index 경로를 유지한다.

렌더링 지형 데이터 타입은 `src/renderer/TerrainTypes.h`에 둔다.
현재 포함 타입은 `TerrainVertex`, `PackedTerrainQuad`, `TerrainMesh`, `TerrainBuildData`이다.
`src/renderer/RendererGpuResources.h/.cpp`는 `Texture`, buffer upload region, texture/image 생성, render target 생성, mipmap 생성, one-time command buffer, buffer upload, texture descriptor set 업데이트를 담당한다.
`src/game/ClientContent.h/.cpp`는 block/item 정의와 텍스처 layer 이름을 Renderer/Vulkan 타입 없이 로드한다.
`src/renderer/RendererAssetStore.h/.cpp`는 `ClientContent`를 입력으로 받아 sky/UI/player/terrain/fluid/item texture, item sprite mesh, prop render mesh를 생성하고 해제한다.
`src/renderer/SpriteRenderPath.h/.cpp`는 sprite pipeline의 push constant 구성, descriptor bind, 6-vertex draw primitive를 담당한다.
`src/renderer/ScreenPresentation.h/.cpp`는 sky sprites, scene color target composite, climate overlay, crosshair, fallback menu, debug text 호출을 조율한다.
`src/world/ClimateSystem.h/.cpp`는 climate seed, tileable climate noise sampling, chunk climate population, temperature/precipitation 계산을 담당한다.
`src/renderer/ClimateOverlayTextureBuilder.h/.cpp`는 temperature/precipitation과 terrain noise overlay texture에 업로드할 RGBA pixel 데이터를 생성한다.
`src/renderer/DebugOverlayText.h/.cpp`는 debug 표시 문자열, 해상도/FPS cache, text batch dirty 상태를 소유한다.
`src/renderer/TerrainGeometryBuilder.h/.cpp`는 solid/cross/prop terrain CPU mesh 생성을 담당하며 Vulkan 타입에 의존하지 않는다.
`src/renderer/RendererTerrainMeshBridge.h/.cpp`는 `TerrainGeometryBuilder`와 `TerrainMesher`를 연결해 chunk mesh와 edited subchunk mesh의 CPU 조립을 담당한다.
`src/renderer/TextRenderPath.h/.cpp`는 font atlas texture 생성, host-visible text vertex buffer, glyph layout, outline/fill text batch draw를 담당한다.
`src/renderer/TerrainRenderPath.h/.cpp`는 terrain chunk render data, render chunk 설치/교체/retire 규칙, retired terrain mesh 수명, packed terrain quad 변환, terrain GPU buffer upload, terrain vertex descriptor set 생성, solid/fluid terrain mesh draw 순회와 terrain frustum culling을 담당한다.
`src/renderer/PlayerMeshRenderPath.h/.cpp`는 player mesh 파일 로드, player vertex/index buffer 생성, 매 프레임 player vertex 위치 갱신, player indexed draw와 buffer 수명을 담당한다.
`src/renderer/ParticleRenderPath.h/.cpp`는 블록 파괴 파티클 상태, 파괴 오버레이 quad 생성, 파티클 수명/단순 terrain 충돌 갱신, host-visible particle vertex/index buffer 업로드, particle draw path를 담당한다.
`src/renderer/DroppedItemRenderCollector.h/.cpp`는 드랍 아이템 청크 frustum culling, 거리 culling, stack count별 시각 복제본 생성, `DroppedItemRenderPath::RenderInstance` 목록 생성을 담당한다.
`src/renderer/DroppedItemRenderPath.h/.cpp`는 드랍 아이템 로컬 스프라이트 mesh 타입, GPU static vertex/index buffer, persistent instance buffer, instance 업로드, item id별 batch draw를 담당한다.
`src/renderer/ItemSpriteMeshBuilder.h/.cpp`는 아이템 텍스처 alpha를 읽어 드랍 아이템용 extruded sprite mesh를 생성한다.
`src/renderer/RendererVulkanContext.cpp`는 Vulkan instance, surface, physical/logical device, queue, command pool, timestamp query pool, hardware debug 정보 수집을 담는다.
`src/renderer/RendererSwapchain.cpp`는 swapchain, image view, render pass, depth/scene target, framebuffer, recreate/cleanup을 담는다.
`src/renderer/RendererPipelines.cpp`는 descriptor layout/pool, sampler, shader module, sprite/UI/terrain/particle/item/selection pipeline 생성을 담는다.
`src/renderer/RendererVulkanState.h`는 Vulkan instance/device/swapchain/pipeline/command/sync/query/RmlUi buffer handle 상태를 `RendererVulkanState`로 묶는다.
`src/game/ClientRuntimeState.h`는 `ClientRuntime`이 소유하는 client runtime 전환 상태를 selection, world/render config, diagnostics, gameplay/world/terrain scene lifecycle, content, UI bridge, audio 묶음으로 보관한다.
`src/renderer/RendererLifecycle.cpp`는 Renderer 생성/해제 순서와 queue family complete 판정을 담는다.
`src/renderer/RendererLocalResources.cpp`는 text render path, UI buffers, particle/dropped-item render path buffers, selection line buffer, player mesh 생성 hook을 담는다.
`src/renderer/RendererSceneDraw.cpp`는 terrain/player/particle/selection draw helper와 draw용 matrix helper를 담는다.
`src/renderer/RendererTypes.h`는 renderer 구현 파일들이 공유하는 queue family, UI vertex/push/geometry, selection line vertex, terrain push constant 타입을 담는다.
`src/renderer/RendererVulkanMethods.inc`, `src/renderer/RendererRenderMethods.inc`는 `Renderer` class private section에 포함되는 구현 선언 fragment이며, `Renderer.h` 본문은 public API와 소유/배선 구조를 중심으로 유지한다.
`src/renderer/RendererUiRuntimeBridge.h/.cpp`는 RmlUi 초기화/종료, UI frame render 호출, UI input forwarding, inventory/world list UI 갱신, UI action/input value 조회를 담당하는 `RendererUiRuntimeBridge` 객체를 담는다.
`src/renderer/RendererRmlUiBackend.h/.cpp`는 RmlUi `RenderInterface`, UI geometry upload, UI texture load/generate/release, scissor 상태를 담고, `RendererUi.cpp`는 이 backend를 `UiSystem`에 연결한다.
`src/renderer/RendererDroppedItems.cpp`는 `DroppedItemRuntime` update 호출, 렌더 후보 수집 입력 조립, push constant 준비, `DroppedItemRenderPath` draw 호출만 담는다.
`src/renderer/RendererFrameLoop.cpp`는 frame acquire/submit/present, command buffer 기록, screenshot readback/BMP 저장, command buffer/sync object 생성을 담는다.
`src/renderer/RendererGameplayBridge.h/.cpp`는 block selection/edit/breaking, pickup/drop, inventory snapshot, block lookup/collision helper, gameplay 결과의 mesh/particle/audio 반영을 담당하는 `RendererGameplayBridge`를 담는다.
`src/renderer/RendererTerrainRuntimeBridge.h/.cpp`는 loaded chunk 갱신, terrain load request, terrain job completion, pending unload, retired terrain chunk 처리, edited mesh rebuild, terrain stats 갱신을 담당하는 `RendererTerrainRuntimeBridge` 객체를 담는다.
`src/renderer/RendererSceneLifecycleBridge.h/.cpp`는 scene load/unload hook 조립과 renderer-specific scene lifecycle callback 연결을 담당하는 `RendererSceneLifecycleBridge` 객체를 담는다.
`src/renderer/RendererConfigBridge.h/.cpp`는 content/GPU asset load, world/render config load, height LUT load를 담당하는 `RendererConfigBridge` 객체를 담는다.
`src/renderer/RendererAudioBridge.h/.cpp`는 audio init/shutdown, listener update, music scene selection, gameplay sound trigger를 담당하는 `RendererAudioBridge` 객체를 담는다.
`src/renderer/RendererDiagnosticsBridge.h/.cpp`는 selected block/climate/debug/performance/VRAM text를 담당하는 `RendererDiagnosticsBridge` 객체를 담는다.
`src/renderer/RendererClimateOverlay.cpp`는 climate overlay texture 생성을 담는다.
`src/game/ClientFrame.h`는 `GameClient`가 한 프레임 렌더링에 넘기는 카메라, 플레이어, overlay, debug, screenshot, world tick 입력을 `ClientFrame` DTO로 묶는다.
`src/game/ClientRuntime.h/.cpp`는 `GameClient`가 호출하는 클라이언트 런타임 진입점이고, `render()`, `scene()`, `gameplay()`, `ui()`, `diagnostics()` access로 역할별 호출 표면을 제공한다.
Renderer/GPU가 필요 없는 collision query, block selection state, inventory snapshot, UI action/input query, selected block/climate text는 `ClientRuntimeState`에서 직접 처리하고, 렌더러 의존 작업은 `ClientRenderRuntime`에 위임한다.
`src/game/ClientRenderRuntime.h/.cpp`는 `Renderer`를 소유하고 scene/gameplay/UI/render 호출을 현재 renderer bridge 객체로 연결하는 전환기 adapter다.
`src/renderer/RendererFrame.h`는 `ClientRenderRuntime`이 Renderer 호출 직전에 변환하는 renderer 내부 경계 DTO다.
드롭 아이템 생성/병합/물리 tick/pickup 판정은 `src/world/DroppedItemSystem.h/.cpp`가 담당한다.
드롭 아이템 entity id, 청크별 추적, spawn/drop/pickup/raycast/update 조율은 `src/world/DroppedItemRuntime.h/.cpp`가 담당한다.
`src/world/TerrainMesher.h/.cpp`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 맡는다.
solid/cross/prop subchunk mesh 생성은 `TerrainGeometryBuilder`가 담당하고, render chunk storage 조작, Vulkan upload, terrain render data 수명, terrain mesh draw loop는 `TerrainRenderPath`가 담당한다.
chunk mesh와 edited subchunk mesh의 CPU 조립은 `RendererTerrainMeshBridge`가 담당하고, `Renderer`는 결과를 `TerrainRenderPath` 설치 API로 전달한다.
player mesh는 terrain chunk mesh와 별도 indexed vertex buffer 경로이며 `PlayerMeshRenderPath`가 소유한다.
fluid subchunk mesh 생성은 `TerrainMesher`가 맡고, 불투명 블록 판정은 `Renderer` callback을 사용한다.
프레임 루프와 Vulkan command recording은 `RendererFrameLoop.cpp`의 책임이며, gameplay/terrain/scene/debug bridge는 별도 translation unit으로 분리한다. 이전 `Renderer.cpp`는 제거되었고, lifecycle/local resource/scene draw 책임은 이름 있는 translation unit에 둔다.
초기 청크 지형 생성과 feature 반영은 `src/world/TerrainBuilder.h/.cpp`로 분리되어 있다.

## 메모리

지형 최종 버퍼는 `DEVICE_LOCAL` 메모리에 둔다.
업로드는 staging buffer를 통해 수행한다.

이 구조는 idle 상태의 GPU 읽기 성능을 우선한 결정이다.

## 텍스처

- 블록 텍스처는 `assets/data/blocks.json`에 등록된 텍스처만 texture array에 넣는다.
- 블록/아이템 정의와 텍스처 layer 이름은 `ClientContent`가 만들고, 실제 Vulkan texture array는 `RendererAssetStore`가 만든다.
- 기본 블록 텍스처는 `assets/textures/block`에 있다.
- 수동 mip 파일은 `assets/textures/block/mip`에 둔다.
- 없는 mip 파일은 실행 중 생성해서 mip 폴더에 저장한다.
- mip 전환은 shader에서 카메라 거리 기준으로 처리한다.
- `mipDistanceScale = 1.0`일 때 64블록 단위로 mip 단계가 바뀐다.

## 카메라 상대 렌더링

월드, 플레이어, 청크, 저장 좌표는 월드 공간에 유지한다.
씬 렌더링은 큰 래핑 X/Z 좌표에서 float 정밀도 흔들림을 줄이기 위해 투영 전에 카메라 상대 좌표를 사용한다.

- CPU 게임플레이 좌표는 이미 사용하는 곳에서 `double`을 유지한다.
- 지형 메시 정점은 월드 좌표로 저장한다.
- 지형, 플레이어, 파티클, 드랍 아이템, 선택 표시 vertex shader는 각 월드 공간 정점에서 `cameraPosition.xyz`를 뺀다.
- 씬 view matrix는 카메라 회전만 사용하고 translation은 0으로 둔다.
- 지형 frustum culling은 청크 AABB에서 렌더 카메라 위치를 뺀 뒤 검사한다.
- fragment mip 거리는 `distance(camera, worldPosition)` 대신 카메라 상대 위치의 길이를 사용한다.

## 블록 파괴 오버레이

블록 파괴 금 텍스처는 UI asset이 아니라 월드 렌더링 오버레이다.
저장 위치는 다음과 같다.

```text
assets/textures/block/breaking/destroy_stage_0.png
...
assets/textures/block/breaking/destroy_stage_9.png
```

플레이어가 블록 파괴를 유지하면 선택된 큐브 블록은 현재 파괴 진행도에 따라 10개 오버레이 단계 중 하나를 렌더링한다.
오버레이는 지형 texture array를 사용하는 블록 공간 quad로 방출하며 mip level 0을 강제한다.
파괴 중에는 타격 면에서 작은 블록 텍스처 파티클이 고정 간격으로 생성된다.

## 텍스트 렌더링

디버그 텍스트와 fallback 메뉴 텍스트는 `TextRenderPath`가 렌더링한다.
`TextRenderPath`는 FreeType으로 font atlas를 만들고, 텍스트 batch를 outline/fill vertex로 변환한 뒤 sprite pipeline에 업로드한다.
`DebugOverlayText`는 hardware/performance/terrain/debug 표시 문자열과 dirty 상태를 관리한다.
`Renderer`는 FPS, swapchain extent, 성능 샘플, terrain 통계 같은 입력 값을 전달하고 메뉴 항목 표시 타이밍만 조율한다.

## 화면 프레젠테이션

swapchain render pass 위에 얹는 2D presentation은 `ScreenPresentation`이 조율한다.
scene color target 합성, sky sprite, crosshair, climate overlay, fallback menu 배경/버튼/text, debug text draw 호출은 이 계층에 둔다.
실제 sprite descriptor bind와 push constant draw는 `SpriteRenderPath`가 담당한다.

temperature/precipitation overlay texture pixel은 `ClimateOverlayTextureBuilder`가 `ClimateSystem` 샘플링 결과로 생성한다.
groundness/smoothness/weirdness/PV overlay texture pixel은 같은 builder가 `TerrainBuilder`의 mode별 terrain debug noise 결과로 생성한다.
생성된 texture draw는 `ScreenPresentation`이 수행한다.

## 소품 렌더링

`renderType = "prop"` 블록은 일반 지형 메시 경로 안에서 렌더링한다.

- 블록 데이터는 `prop.model`로 `.dpm` 모델을 선택한다.
- 블록 데이터는 `prop.texture`로 블록 texture array layer 하나를 선택한다.
- `.dpm`은 quad 위치, UV, normal을 저장하며 magic 값과 version field는 없다.
- 시작 시 없거나 오래된 `.dpm` 파일은 대응하는 `.glb` 파일에서 다시 생성한다.
- `.glb` 변환 중 원본 triangle pair는 다시 quad로 병합한다.
- `.glb` 파싱, `.dpm` 변환/검증, 렌더링용 quad 로드는 `src/assets/PropModelLoader.h/.cpp`가 맡는다.
- subchunk meshing 중 prop quad는 블록 위치에 추가된다.
- prop quad는 원본 모델 winding에 의존하지 않도록 양면으로 방출한다.
- `randomOffset` prop 블록은 렌더링되는 X/Z 메시 위치만 중심에서 최대 `0.2`블록까지 오프셋한다.
- 작은 회전 prop geometry와 모델 UV island가 보존되도록 packed terrain 위치와 UV는 1/256 정밀도를 사용한다.

## 컬링

- 프러스텀 컬링을 적용한다.
- 지형 메쉬는 청크/서브청크 렌더 데이터 기준으로 draw한다.
- 와이어프레임은 F4로 토글한다.

## 관련 문서

- [[chunk-system]]
- [[block-data]]
- [[debug-profiling]]

## 유체 렌더링

유체는 블록 지형과 분리된 subchunk mesh로 렌더링한다.
현재 렌더링되는 유체는 `water`이다.

비유체 지형 mesh 이름은 `solidSubchunks`이다.
여기서 `solid`는 유체 반대편의 지형 경로를 뜻하며 cube 블록, `cross` 블록, `prop` 블록을 포함한다.

- 텍스처: `assets/textures/fluid/water.png`
- 런타임 설정: `config/render.json` -> `fluid.water.alpha`
- 수동 유체 mip 텍스처는 아직 사용하지 않는다.
- `amount = 0` 또는 `id = 0`은 렌더링하지 않는다.
- 윗면 amount 높이는 10단위 올림으로 `0.08`~`0.8`블록에 매핑한다.
- 위에 다른 물 셀이 있는 물 셀은 `1.0`블록 높이로 렌더링한다.

블록 지형, 블록 선택 표시, 플레이어 메시를 먼저 그리고, 그 다음 같은 scene pass에서 유체 mesh를 그린다.
인접 유체가 같거나 더 높은 높이에 도달하면 내부 유체 face는 생략한다.
유체 mesh 생성은 `fluidSubchunkCounts` 값이 `0`인 subchunk를 건너뛴다.
유체 렌더링은 alpha blending을 켜고 depth write를 끈 별도 `fluidPipeline_`을 사용한다.
일반 terrain pipeline은 opaque/cutout 블록 렌더링을 위해 non-blend 상태로 유지한다.
유체는 depth test를 유지하므로 블록, cutout 지형, 선택 외곽선, 플레이어가 scene depth buffer를 통해 유체를 가릴 수 있다.
유체 pipeline은 `fluid.frag`를 사용한다.
`fluid.frag`는 fluid texture array를 샘플링하고 render config의 고정 alpha 값을 적용한다.
`config/render.json` 파일 읽기와 값 검증은 `src/config/ConfigLoaders.h/.cpp`의 `config::loadRenderConfig`가 맡는다.
물 normal mapping, Fresnel alpha, depth absorption, SSR은 현재 렌더러에 포함되어 있지 않다.

## 블록 파괴 파티클

블록 파괴는 수명이 짧은 런타임 파티클을 생성한다. 파티클은 저장하지 않는다.
파티클 상태와 GPU buffer 수명, draw command 방출은 `ParticleRenderPath`가 소유하고, `Renderer`는 블록 정의 확인, texture layer 선택, 현재 파괴 상태와 terrain collision callback만 전달한다.

- 트리거: 블록 제거 성공.
- 개수: 파괴된 블록당 24개.
- 형태: terrain view matrix와 같은 right/up basis를 사용하는 카메라 정면 billboard quad.
- 텍스처: 파괴된 블록의 대표 block texture layer.
- UV: 텍스처 내부의 결정론적 랜덤 4x4 sub-tile.
- 수명: `0.45 ~ 0.75`초.
- 크기: `0.10 ~ 0.16`블록.
- 중력: `22`.
- 충돌: solid terrain cell에 대한 단순 바닥 충돌, 약한 bounce, 강한 X/Z friction.
- Pipeline: 기존 block texture array를 사용하는 전용 particle graphics pipeline.
- Depth test는 켜고 depth write는 끈다.

씬 그리기 순서는 블록, 유체, 플레이어, 블록 파괴 파티클, 선택 외곽선 순서다.

## 드랍 아이템 렌더링

드랍 아이템은 전용 item pipeline으로 렌더링한다.
아이템 스프라이트에서 만든 로컬 extruded mesh는 시작 시 정적 vertex/index buffer에 한 번 업로드한다.
로컬 extruded mesh 생성은 `ItemSpriteMeshBuilder`가 담당하고, 결과 타입은 `DroppedItemRenderPath::ItemSpriteMesh`이다.
프레임마다 CPU가 아이템 쿼드 정점을 다시 펼치지 않고, 드랍 아이템 위치/회전/텍스처 layer만 담은 instance buffer를 갱신한다.
instance buffer는 persistent mapping 상태로 유지해 매 프레임 `vkMapMemory`/`vkUnmapMemory`를 반복하지 않는다.
정적 mesh GPU buffer, instance buffer, instance 업로드, 정렬, item id별 batch draw는 `DroppedItemRenderPath`가 소유한다.
`DroppedItemRuntime`은 드랍 아이템 runtime 상태와 tick을 갱신하고, `DroppedItemRenderCollector`는 월드 엔티티를 순회해 렌더 후보 `RenderInstance` 목록을 만들며, `RendererDroppedItems.cpp`는 카메라/pipeline/texture 정보를 `DroppedItemRenderPath`에 전달한다.
렌더 후보 수를 줄이기 위해 다음 컬링을 적용한다.

- 런타임은 드랍 아이템을 가진 청크별 카운트와 전체 드랍 아이템 수를 캐시한다.
- 드랍 아이템 렌더링은 `runtimeChunks` 전체가 아니라 드랍 아이템을 가진 청크 목록만 순회한다.
- 청크 AABB가 카메라 프러스텀 밖이면 해당 청크의 드랍 아이템은 모두 건너뛴다.
- 청크가 프러스텀 안에 있어도, 개별 드랍 아이템 위치가 카메라에서 48블록보다 멀면 렌더링하지 않는다.
- 거리 판정은 보간된 드랍 아이템 위치와 카메라 위치 사이의 3D 거리 제곱으로 처리한다.
- 드랍 아이템 스택은 count에 따라 1~4개의 시각 복제본으로 렌더링한다.
- 시각 복제본 수는 count `1`, `2~16`, `17~48`, `49~99` 구간에 따라 각각 1, 2, 3, 4개다.
- 복제본은 기존 드랍 아이템 두께인 `0.05`블록 단위로 Y 오프셋을 쌓고, 작은 XZ 오프셋과 Y 회전 차이를 둔다.
- 같은 아이템 mesh를 사용하는 instance는 item id 기준으로 정렬한 뒤 batch draw한다.

이 컬링은 렌더링 후보만 줄이며, 드랍 아이템 물리, 저장, 획득 판정에는 영향을 주지 않는다.

## 진단 오버레이

F6은 기후/지형 진단 오버레이를 순환한다.

```text
OFF -> Temperature -> Precipitation -> Groundness -> Smoothness -> Weirdness -> PV -> OFF
```

Temperature/Precipitation 오버레이는 전체 `65536 x 65536` 래핑 월드를 덮는 `1024 x 1024` 텍스처다.
각 픽셀은 `64 x 64` 블록 영역을 샘플링한다.

- Temperature는 래핑된 Z를 남북 위도 값으로 사용한다. 월드 가장자리는 춥고 중앙은 덥다.
- Temperature는 넓은 기후대가 유지되도록 중위도 mask를 통해 약한 tileable noise를 더한다.
- Temperature 색상은 낮은 값을 파랑, 높은 값을 빨강으로 매핑한다.
- Precipitation은 지형 높이 노이즈와 같은 4D torus 방식으로 샘플링한 넓은 tileable 2D noise를 사용한다.
- Precipitation 색상은 낮은 값을 회색, 높은 값을 파랑으로 매핑한다.

Groundness/Smoothness/Weirdness/PV 오버레이는 월드 원점 기준 `0..4096` 블록 영역을 `1024 x 1024` 텍스처로 표시한다.
각 픽셀은 `4 x 4` 블록 간격 샘플을 대표한다.

## 하늘 스프라이트

태양과 달 스프라이트는 시간에 따라 변하는 월드 방향에서 투영해 screen-space sprite로 렌더링한다.
현재 투영 half-size는 화면 너비의 `0.04`이며, 높이는 viewport aspect ratio에 맞게 조정한다.
렌더러는 `GameClient`에서 `worldTicks`를 받아 28800틱 하루 주기를 계산한다.
`06H`에는 태양이 동쪽 지평선 근처에 있고, `12H`에는 머리 위에 있으며, `18H`에는 서쪽 지평선 근처에 있다. 달은 반대 방향을 사용한다.
하늘 각도는 하루 주기 동안 감소하므로 `06H` 시작 시점에서 투영된 태양은 지는 것이 아니라 떠오른다.
