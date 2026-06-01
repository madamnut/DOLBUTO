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
- `fire` 렌더 타입은 바닥 불꽃용 컷아웃 쿼드 묶음으로 만든다.
- `slab` 렌더 타입은 `blockStates`의 attach 상태를 읽어 셀 안의 반칸 cuboid를 별도 메쉬로 만들고, `half_slab`은 attach_grid 상태를 읽어 `0.5 x 0.5 x 1.0` 조각 메쉬를 만든다.
- 청크 메싱은 주변 8청크 정보를 사용해 경계면을 처리한다.
- 조명 전파는 슬랩의 attach 상태를 읽어 붙은 면 방향으로만 블록 감쇄를 적용한다.

## GPU 데이터

지형 메쉬는 최종적으로 `PackedTerrainQuad` 단위로 압축된다.

- CPU 메싱 결과는 임시 `TerrainVertex`/index 형태로 만들어진다.
- 업로드 직전에 quad record로 변환한다.
- terrain vertex shader가 SSBO에서 quad record를 읽어 6개의 가상 vertex를 생성한다.
- packed terrain quad는 packed light 값도 함께 보관하고, shader는 skylight nibble을 꺼내 지형/유체 색에 곱한다.
- terrain은 index buffer 없이 `vkCmdDraw`를 사용한다.
- player는 별도 vertex/index 경로를 유지한다.
- 플레이어 스킨, 1인칭 손, 손에 든 아이템, 드랍 아이템, 블록 파괴 파티클도 지형과 같은 packed light/`skyBrightness`/block light curve를 사용한다. 플레이어 관련 viewmodel은 플레이어 위치의 light를 사용하고, 드랍 아이템과 파티클은 각 렌더 위치의 light를 샘플링한다.

렌더링 지형 데이터 타입은 `src/renderer/TerrainTypes.h`에 둔다.
현재 포함 타입은 `TerrainVertex`, `PackedTerrainQuad`, `TerrainMesh`, `TerrainBuildData`이다.
`src/renderer/RendererGpuResources.h/.cpp`는 `Texture`, buffer upload region, texture/image 생성, render target 생성, mipmap 생성, one-time command buffer, buffer upload, texture descriptor set 업데이트를 담당한다.
`src/game/ClientContent.h/.cpp`는 block/item 정의와 텍스처 layer 이름을 Renderer/Vulkan 타입 없이 로드한다.
`src/renderer/RendererAssetStore.h/.cpp`는 `ClientContent`를 입력으로 받아 sky/UI/player/terrain/fluid/item texture, item sprite mesh, prop render mesh를 생성하고 해제한다.
`src/renderer/SpriteRenderPath.h/.cpp`는 sprite pipeline의 push constant 구성, descriptor bind, 6-vertex draw primitive를 담당한다.
`src/renderer/RadialMenuRenderPath.h/.cpp`는 같은 sprite pipeline과 1x1 white texture를 재사용해 아이템 상호작용 원형 UI의 중앙 원, 액션 부채꼴 링, 후보 부채꼴 링을 native Vulkan geometry로 그린다.
`src/renderer/ScreenPresentation.h/.cpp`는 sky sprites, scene color target composite, water screen blur, climate overlay, crosshair, fallback menu, debug text 호출을 조율한다.
`src/world/ClimateSystem.h/.cpp`는 climate seed, tileable climate noise sampling, chunk climate population, temperature/precipitation 계산을 담당한다.
`src/renderer/ClimateOverlayTextureBuilder.h/.cpp`는 temperature/precipitation과 terrain noise overlay texture에 업로드할 RGBA pixel 데이터를 생성한다.
`src/renderer/DebugOverlayText.h/.cpp`는 debug 표시 문자열, 해상도/FPS cache, text batch dirty 상태를 소유한다.
`src/renderer/TerrainGeometryBuilder.h/.cpp`는 solid/blend/cross/prop/fire terrain CPU mesh 생성을 담당하며 Vulkan 타입에 의존하지 않는다.
`src/renderer/RendererTerrainMeshBridge.h/.cpp`는 `TerrainGeometryBuilder`와 `TerrainMesher`를 연결해 chunk mesh와 edited subchunk mesh의 CPU 조립을 담당한다.
`src/renderer/TextRenderPath.h/.cpp`는 font atlas texture 생성, host-visible text vertex buffer, glyph layout, outline/fill text batch draw를 담당한다.
`src/renderer/TerrainRenderPath.h/.cpp`는 terrain chunk render data, render chunk 설치/교체/retire 규칙, retired terrain mesh 수명, packed terrain quad 변환, terrain GPU buffer upload, terrain vertex descriptor set 생성, solid/blend/fluid terrain mesh draw 순회와 terrain frustum culling을 담당한다.
`src/renderer/PlayerModelLoader.h/.cpp`는 `Character.glb`의 node/mesh primitive/vertex/index 데이터를 읽어 파트별 플레이어 모델 source data를 만든다.
`src/renderer/PlayerMeshRenderPath.h/.cpp`는 player GLB 모델 로드, player vertex/index buffer 생성, GLB node transform 기반 매 프레임 player vertex 위치 갱신, player indexed draw와 buffer 수명을 담당한다.
`PlayerMeshRenderPath`는 `Head` node에 렌더 프레임의 head yaw/pitch 추가 transform을 적용한다.
`src/renderer/ParticleRenderPath.h/.cpp`는 블록 파괴 파티클 상태, 파괴 오버레이 quad 생성, 불 연기 파티클, 파티클 수명/형상 기반 terrain 충돌 갱신, host-visible particle vertex/index buffer 업로드, particle draw path를 담당한다.
`src/renderer/DroppedItemRenderCollector.h/.cpp`는 드랍 아이템 청크 frustum culling, 거리 culling, `DroppedItemRenderPath::RenderInstance` 목록 생성을 담당한다.
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
`src/renderer/SkyRenderPath.h/.cpp`는 scene render pass의 첫 draw로 fullscreen sky shader를 호출한다. sky shader는 clear color 고정값 대신 `worldTicks`에서 계산한 실제 sun direction, 낮/밤 판정용 day direction, camera basis, FOV를 받아 view direction과 direction dot 값으로 하늘 위쪽/지평선/아래쪽 그라데이션, 일출/일몰 horizon glow, 태양 방향 glare를 계산한다.
`src/renderer/CloudRenderPath.h/.cpp`는 하늘 스프라이트 뒤, 지형 앞에서 별도 fullscreen alpha pass로 월드 공간 `Y=500..700` 범위의 렌더 전용 volumetric cloud slab을 그린다. 구름은 카메라 기준 화면 노이즈가 아니라 `cameraPosition + viewDirection * t` 월드 좌표에서 3D noise 밀도장을 raymarch해 샘플링하므로 플레이어 이동에 따라 월드 상공에 고정된 것처럼 보이며, 태양과 달 스프라이트를 가릴 수 있다. 키패드 `+`는 `cloudCoverage`를 높여 구름을 많게 하고, 키패드 `-`는 낮춰 구름을 적게 한다. 구름 coverage 디버그 값은 화면 텍스트로 표시하지 않는다.
밤하늘은 낮보다 낮은 RGB ramp를 사용하고, 어두운 계조에서 줄무늬가 보이지 않도록 screen-space hash noise 기반의 약한 dither를 적용한다.
하늘색 디버그를 위해 게임 화면에서 `[`를 누르고 있으면 하루 안의 시간이 해가 뜨는 방향으로 되감기고, `]`를 누르고 있으면 해가 지는 방향으로 빨리 진행된다. 이 입력은 `worldTicks`만 조정하므로 sky shader와 sun/moon sprite 위치가 같은 기준으로 움직인다.
`src/renderer/RendererGameplayBridge.h/.cpp`는 block selection/edit/breaking, pickup/drop, inventory snapshot, block lookup/collision helper, gameplay 결과의 mesh/particle/audio 반영을 담당하는 `RendererGameplayBridge`를 담는다.
`src/renderer/RendererTerrainRuntimeBridge.h/.cpp`는 loaded chunk 갱신, terrain load request, terrain job completion, pending unload, retired terrain chunk 처리, edited mesh rebuild, terrain stats 갱신을 담당하는 `RendererTerrainRuntimeBridge` 객체를 담는다.
`src/renderer/RendererSceneLifecycleBridge.h/.cpp`는 scene load/unload hook 조립과 renderer-specific scene lifecycle callback 연결을 담당하는 `RendererSceneLifecycleBridge` 객체를 담는다.
`src/renderer/RendererConfigBridge.h/.cpp`는 content/GPU asset load, world/render config load, height LUT load를 담당하는 `RendererConfigBridge` 객체를 담는다.
`src/renderer/RendererAudioBridge.h/.cpp`는 audio init/shutdown, listener update, music scene selection, gameplay sound trigger를 담당하는 `RendererAudioBridge` 객체를 담는다.
`src/renderer/RendererDiagnosticsBridge.h/.cpp`는 selected block/climate/debug/performance/VRAM text를 담당하는 `RendererDiagnosticsBridge` 객체를 담는다.
`src/renderer/RendererClimateOverlay.cpp`는 climate overlay texture 생성을 담는다.
`src/game/ClientFrame.h`는 `GameClient`가 한 프레임 렌더링에 넘기는 카메라, 플레이어, overlay, debug, screenshot, world tick 입력을 `ClientFrame` DTO로 묶는다.
`ClientFrame`/`RendererFrame`은 현재 FOV를 `fovRadians`로 함께 전달한다.
`ClientFrame`/`RendererFrame`은 아이템 상호작용 원형 UI의 표시 여부, 액션 수, 후보 수, 선택 인덱스를 `RadialMenuRenderFrame`으로 함께 전달한다.
`ClientFrame`/`RendererFrame`은 cloud render path용 `cloudCoverage`도 전달한다.
스카이라이트 전역 밝기는 `worldTicks`에서 시간 기반으로 계산한 `0.0~1.0` 범위의 `skyBrightness`로 렌더 프레임에 전달한다.
`05:00~07:00`에는 최소 밝기 `0.08`에서 최대 밝기 `1.0`으로 부드럽게 밝아지고, `07:00~17:00`에는 최대 밝기를 유지하며, `17:00~21:00`에는 다시 최소 밝기로 어두워진다. `21:00~05:00`에는 최소 밝기를 유지한다.
terrain/player/particle/selection/dropped item projection과 terrain/dropped item frustum culling, sky sprite projection은 이 값을 같은 프레임 기준으로 사용한다.
1인칭 손과 든 아이템 viewmodel은 화면상 크기와 배치가 FOV 설정에 따라 흔들리지 않도록 별도 고정 FOV `60도`를 사용한다.
FOV 설정은 `config/settings.json`의 `video.fovDegrees`에 저장되며 Options 화면에서 `30도 ~ 110도` 사이로 조정한다.
달리기 중에는 실제 수평 이동이 발생할 때만 월드 FOV 목표값을 현재 Options FOV의 `1.15`배로 두고, 별도 최대값 clamp 없이 보간해 적용한다. Ctrl 또는 toggle sprint 상태여도 플레이어가 정지해 있으면 월드 FOV는 걷기 기본값으로 돌아간다.
이 동적 FOV는 viewmodel에는 적용하지 않는다.
`src/game/ClientRuntime.h/.cpp`는 `GameClient`가 호출하는 클라이언트 런타임 진입점이고, `render()`, `scene()`, `gameplay()`, `ui()`, `diagnostics()` access로 역할별 호출 표면을 제공한다.
Renderer/GPU가 필요 없는 collision query, block selection state, inventory snapshot, UI action/input query, selected block/climate text는 `ClientRuntimeState`에서 직접 처리하고, 렌더러 의존 작업은 `ClientRenderRuntime`에 위임한다.
`src/game/ClientRenderRuntime.h/.cpp`는 `Renderer`를 소유하고 scene/gameplay/UI/render 호출을 현재 renderer bridge 객체로 연결하는 전환기 adapter다.
`src/renderer/RendererFrame.h`는 `ClientRenderRuntime`이 Renderer 호출 직전에 변환하는 renderer 내부 경계 DTO다.
드롭 아이템 생성, 드롭 아이템끼리의 물리 충돌, 물리 tick, pickup 판정은 `src/world/DroppedItemSystem.h/.cpp`가 담당한다.
드롭 아이템 entity id, 청크별 추적, spawn/drop/pickup/raycast/update 조율은 `src/world/DroppedItemRuntime.h/.cpp`가 담당한다.
`src/world/TerrainMesher.h/.cpp`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 맡는다.
solid/blend/cross/prop/fire subchunk mesh 생성은 `TerrainGeometryBuilder`가 담당하고, render chunk storage 조작, Vulkan upload, terrain render data 수명, terrain mesh draw loop는 `TerrainRenderPath`가 담당한다.
chunk mesh와 edited subchunk mesh의 CPU 조립은 `RendererTerrainMeshBridge`가 담당하고, `Renderer`는 결과를 `TerrainRenderPath` 설치 API로 전달한다.
player mesh는 terrain chunk mesh와 별도 indexed vertex buffer 경로이며 `PlayerMeshRenderPath`가 소유한다.
플레이어는 `assets/textures/character/Character.glb`를 직접 읽고, 별도 `Character.mesh` 런타임 캐시 파일은 사용하지 않는다.
플레이어 전체 배치는 body yaw를 기준으로 하고, 머리 회전은 `Head` node local transform 뒤에 추가 yaw/pitch transform을 곱해 처리한다.
보행 모션은 `ClientFrame`/`RendererFrame`의 `playerWalkPhase`와 `playerWalkAmount`로 전달되며, `GameClient`가 물리 tick 사이 값을 보간해 넘긴다.
`PlayerMeshRenderPath`는 팔/다리 node에 추가 pitch transform을 적용하고, 팔꿈치/무릎 하위 node는 한 방향 bend와 각도 상한을 사용한다.
1인칭 손은 같은 GLB에서 오른팔 아래팔 node만 추출한 별도 vertex/index buffer를 사용한다.
현재 선택 핫바 아이템은 `ClientFrame`/`RendererFrame`의 `heldItemId`로 전달되고, `RendererDroppedItems.cpp`가 기존 `DroppedItemRenderPath` item pipeline을 재사용해 1인칭 손 앞에 렌더링한다.
1인칭 손과 든 아이템은 지형 depth에 묻히지 않도록 그리기 직전에 scene depth attachment를 clear한 뒤 viewmodel pipeline으로 그린다.
viewmodel pipeline은 depth test/write를 사용해 viewmodel mesh 내부의 앞뒤 관계를 유지한다.
플레이어 스킨과 1인칭 손은 GLB 원본 vertex/index를 정적 GPU mesh로 유지하고, vertex별 `nodeIndex`와 frame별 node transform storage buffer를 통해 shader에서 최종 위치를 계산한다.
CPU는 매 프레임 vertex buffer를 덮어쓰지 않고, 현재 in-flight frame의 transform buffer만 갱신한다.
아이템을 들고 있을 때는 손 mesh를 숨기고 아이템 viewmodel만 표시한다.
든 아이템은 `item_viewmodel.vert`에서 카메라 회전을 적용하지 않는 view-space 좌표로 렌더링해 화면상 같은 면이 유지된다.
`block_model` 든 아이템은 `modelBlock` 또는 fallback `placeBlock` 블록의 텍스처 layer를 사용하는 작은 블록 mesh로 렌더링한다.
대상 블록이 `slab`이거나 아이템의 `modelShape`가 `slab`, `quarter_log`이면 해당 X/Y/Z 크기의 bottom 기준 mesh와 생성 슬롯 아이콘을 사용한다.
`config/viewmodel.json`은 손/아이템 viewmodel의 view-space 위치, 스케일, 회전값을 제공하고, `RendererConfigBridge`가 `ClientRuntimeState::viewmodelConfig`로 로드한다.
`heldItem`은 `extruded_sprite` 든 아이템에 사용하고, `heldBlockModelItem`은 `block_model` 든 아이템에 사용한다.
fluid subchunk mesh 생성은 `TerrainMesher`가 맡고, 불투명 블록 판정은 `Renderer` callback을 사용한다.
edited subchunk rebuild도 solid/blend mesh와 함께 해당 subchunk의 fluid mesh를 다시 만들고 GPU buffer를 교체한다.
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
scene color target 합성, water screen blur, sky sprite, crosshair, climate overlay, fallback menu 배경/버튼/text, debug text draw 호출은 이 계층에 둔다.
실제 sprite descriptor bind와 push constant draw는 `SpriteRenderPath`가 담당한다.
카메라가 물 안에 있으면 swapchain presentation 전에 quarter resolution water blur target 두 벌을 사용해 Kawase blur를 2회 적용한다.
최종 scene color target 합성은 원본 scene color를 먼저 그린 뒤, 물속 영역에만 Kawase blur 결과를 설정된 비율로 덮어 섞는다.
카메라가 수면 근처의 물 안에 있으면 카메라 pitch 기준 화면 수면선을 계산한 결과를 받아 수면선 아래쪽 영역에만 고정 강도의 물속 blur를 적용한다.
카메라가 수면보다 충분히 아래에 있으면 수면선을 화면 위쪽으로 고정해 전체 화면에 물속 효과를 적용한다.
수면선 자체에는 별도 띠, highlight, feather 효과를 주지 않고 물속 효과가 적용되는 화면 영역만 나눈다.
물색 tint는 흐림 위에 약하게만 더한다.
흐림 강도는 `config/render.json`의 `fluid.water.screenBlur` 설정으로 조정한다.
`enabled`는 효과 사용 여부, `spread`는 Kawase blur가 퍼지는 넓이, `intensity`는 원본 화면과 blur 결과를 섞는 비율, `tint`는 물색 tint 강도를 의미한다.
이 효과는 거리 기반 raymarch나 픽셀별 물 통과 거리 계산을 하지 않는 가벼운 화면 공간 표현이다.

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
- prop `.dpm` 로딩 결과는 렌더링뿐 아니라 블록 선택 레이캐스트에도 사용한다.
- prop 선택 아웃라인은 quad wire가 아니라 `.dpm` local bounds에 동일한 offset/rotation을 적용한 작은 박스로 그린다.

## 슬랩 렌더링

`renderType = "slab"` 블록은 일반 지형 메쉬 경로 안에서 반칸 cuboid로 렌더링한다.
`renderType = "half_slab"` 블록은 같은 경로에서 `0.5 x 0.5 x 1.0` cuboid로 렌더링한다.

- 현재 상태 범주는 `stateKind: "attach"`이며 bottom/top/north/south/west/east를 지원한다.
- `half_slab`은 `stateKind: "attach_grid"`를 사용한다. 상태값은 `face * 9 + (grid - 1)`이고, grid는 배치 면의 `7 8 9 / 4 5 6 / 1 2 3` 위치다.
- 슬랩 메쉬는 큐브형 블록의 greedy meshing 대상이 아니며, 셀별 6면을 필요한 만큼 방출한다.
- 슬랩의 외곽 면이 셀 경계에 닿고 이웃 블록이 해당 면을 가릴 수 있을 때만 그 면을 생략한다.
- 슬랩 텍스처는 bottom 상태의 기준 반칸 cuboid 6면을 먼저 정의하고, top/north/south/west/east 상태는 그 기준 모델을 셀 안에서 회전/이동한 결과로 렌더링한다.
- 따라서 세워진 슬랩도 기준 상태의 위/아래/옆면 관계와 UV 영역을 유지한다. bottom 기준 옆면은 텍스처의 아래 절반을 사용한다.
- `half_slab` 배치에서 grid `1/3/7/9`는 꼭짓점에 세운 조각이고, `2/4/6/8`은 모서리에 눕힌 조각이다. grid `5`는 설치 시 플레이어가 보는 방향의 `2/4/6/8`로 변환한다.
- `half_slab` 텍스처도 기준 slab을 수직으로 반 자른 `0.5 x 0.5 x 1.0` 조각 모델을 먼저 정의한다. slab의 위/아래였던 면은 `topBottom`, 원래 외곽 옆면은 `side`, 새로 잘린 수직 절단면은 `verticalSection`을 사용하고, attach_grid 상태는 이 기준 배치를 회전/이동만 한다.
- `half_slab` UV는 현재 배치된 AABB에 맞춰 다시 0~1로 펴지 않는다. 배치 좌표와 재질 좌표를 분리해, 조각을 다른 위치나 방향으로 놓아도 slab을 자른 기준의 위/아래/외곽/절단면 관계가 유지된다.
- 조명은 셀 전체 차단이 아니라 붙은 면 방향 차단으로 계산한다. bottom 슬랩은 아래 방향만 막고, top/side 슬랩도 각각 붙은 면 방향 하나만 막는다.
- 선택 레이캐스트와 선택 아웃라인은 같은 슬랩 AABB를 사용한다.
- 아이템 슬롯/든 아이템/드랍 아이템의 `block_model`은 기본 bottom 슬랩 모양을 사용하고, 옆면도 아래 절반 UV를 사용한다.

## 불 렌더링

`renderType = "fire"` 블록은 일반 지형 메시 경로 안에서 컷아웃 쿼드 묶음으로 렌더링한다.

- 텍스처는 block texture array의 `fire/fire_00` 레이어를 사용한다.
- 중앙에는 plant와 같은 크기의 X자 쿼드 2장을 블록 중앙에 둔다.
- 바닥 네 변에는 블록 한 면 크기의 쿼드 4장을 두고, 위쪽 edge를 중심 방향으로 30도 기울인다.
- 같은 바닥 네 변에서 안쪽으로 0.1블록 당긴 위치에 완전 수직 90도 쿼드 4장을 추가로 둔다.
- 모든 쿼드는 양면으로 방출한다.
- `ClientContent`는 `fire/fire_00`부터 `fire/fire_13`까지 14프레임을 block texture array에 연속 등록한다.
- 지형 메쉬에는 `fire/fire_00` layer만 저장하고, terrain fragment shader가 프레임 시간으로 현재 fire layer를 선택한다.
- 모든 불은 같은 프레임 값을 사용하므로 초당 12프레임의 동기화된 단순 애니메이션으로 표시된다.
- 불 블록은 활성 fire emitter로 등록되어 `assets/textures/particle/smoke/smoke_0.png`부터 `smoke_7.png`까지의 연기 파티클을 주기적으로 생성한다.
- fire emitter는 블록 설치/제거와 청크 로드/언로드 시점에 갱신하며, 매 프레임 전체 월드를 스캔하지 않는다.
- fire block entity가 `pyrolysis` mode이면 해당 emitter의 연기 생성 간격을 1/3로 줄여 일반 불보다 3배 많은 연기를 만든다.
- 연기 파티클은 위로 천천히 상승하고 X/Z 방향으로 약하게 흔들리며, 생성 시점에 `0.8~1.0`블록 크기로 정해진 값을 유지한다.
- 연기 파티클은 중심점 기준으로 지형 충돌 형상과 충돌하면 해당 축 이동을 막고 튕기지는 않는다.
- 연기 애니메이션 프레임은 파티클 수명 비율로 `smoke_0`에서 `smoke_7`까지 1회 진행하고, alpha는 생성 직후 fade-in 후 수명 끝으로 갈수록 fade-out한다.

## 컬링

- 프러스텀 컬링을 적용한다.
- 지형 메쉬는 청크/서브청크 렌더 데이터 기준으로 draw한다.
- 와이어프레임은 F4로 토글한다.
- 지형/유체 조명은 메쉬에 패킹된 skylight와 block light를 분리해 읽는다. skylight는 프레임 전역 `skyBrightness`를 곱하고, block light는 시간대 영향을 받지 않는 절대 밝기로 둔 뒤 `max(skyLight * skyBrightness, blockLight)`를 연속 light curve `x*x*(0.667482 + 0.332518*x)`로 매핑해 적용한다.

## 관련 문서

- [[chunk-system]]
- [[block-data]]
- [[debug-profiling]]

## 유체 렌더링

유체는 블록 지형과 분리된 subchunk mesh로 렌더링한다.
현재 렌더링되는 유체는 `water`이다.

비유체 지형 mesh는 `solidSubchunks`와 `blendSubchunks`로 나뉜다.
`solidSubchunks`는 `opaque`/`cutout` 블록을 담고, `blendSubchunks`는 `alphaMode = "blend"` 블록을 담는다.
cube 블록, `cross` 블록, `prop` 블록 모두 블록 정의의 alpha mode에 따라 solid 또는 blend mesh로 배정된다.

- 텍스처: `assets/textures/fluid/water.png`
- 런타임 설정: `config/render.json` -> `fluid.water.alpha`
- 물속 화면 흐림 설정: `config/render.json` -> `fluid.water.screenBlur`
- 수동 유체 mip 텍스처는 아직 사용하지 않는다.
- `amount = 0` 또는 `id = 0`은 렌더링하지 않는다.
- 윗면 amount 높이는 10단위 올림으로 `0.08`~`0.8`블록에 매핑한다.
- 위에 다른 물 셀이 있는 물 셀은 `1.0`블록 높이로 렌더링한다.

블록 지형은 solid, blend 순서로 그린다.
그 다음 같은 scene pass에서 유체 mesh를 그린다.
인접 유체가 같거나 더 높은 높이에 도달하면 내부 유체 face는 생략한다.
유체 mesh 생성은 `fluidSubchunkCounts` 값이 `0`인 subchunk를 건너뛴다.
blend 블록과 유체 렌더링은 alpha blending을 켜고 depth write를 끈 별도 pipeline을 사용한다.
일반 terrain pipeline은 opaque/cutout 블록 렌더링을 위해 non-blend 상태로 유지한다.
유체는 depth test를 유지하므로 블록, cutout 지형, 선택 외곽선, 플레이어가 scene depth buffer를 통해 유체를 가릴 수 있다.
blend 블록도 depth test를 유지하고 depth write를 끈다.
유체 pipeline은 `fluid.frag`를 사용한다.
`fluid.frag`는 fluid texture array를 샘플링하고 render config의 고정 alpha 값을 적용한다.
`config/render.json` 파일 읽기와 값 검증은 `src/config/ConfigLoaders.h/.cpp`의 `config::loadRenderConfig`가 맡는다.
물 normal mapping, Fresnel alpha, depth absorption, SSR은 현재 렌더러에 포함되어 있지 않다.

## 포스트 프로세스와 블룸

월드 씬은 swapchain 색상 포맷이 아니라 별도의 scene color target에 먼저 렌더링한다.
가능한 GPU에서는 scene color target과 포스트 프로세스 임시 target에 `VK_FORMAT_R16G16B16A16_SFLOAT`를 사용하고, 선형 필터링까지 지원하지 않으면 swapchain 색상 포맷으로 되돌린다.

블룸은 `config/render.json`의 `bloom` 섹션으로 제어한다.

- `enabled`: 블룸 패스 사용 여부.
- `threshold`: 이 밝기 이상의 픽셀만 블룸 후보로 추출한다.
- `intensity`: 최종 화면에 더하는 블룸 세기.
- `radius`: downsample/upsample sample offset 배율.

렌더 순서는 scene pass 이후 `bloom_downsample.frag`로 밝은 픽셀을 1/4 해상도 target에 추출하고, 1/8, 1/16, 1/32 해상도로 순차 downsample한다.
그 다음 `bloom_upsample.frag`로 작은 mip부터 큰 mip로 additive upsample해 여러 반경의 번짐을 합친다.
최종 presentation pass에서는 scene color를 먼저 그리고, additive sprite pipeline으로 블룸 텍스처를 더한 뒤 물속 화면 블러와 기후 오버레이를 그린다.
`fire` terrain fragment는 애니메이션 프레임 샘플 이후 색을 2배로 올려 HDR scene target에서 threshold를 넘을 수 있게 한다.

## 블록 파괴 파티클

블록 파괴와 불 연기는 수명이 짧은 런타임 파티클을 생성한다. 파티클은 저장하지 않는다.
파티클 상태와 GPU buffer 수명, draw command 방출은 `ParticleRenderPath`가 소유하고, `Renderer`는 블록 정의 확인, texture layer 선택, 현재 파괴 상태와 terrain collision callback만 전달한다.

- 트리거: 블록 제거 성공.
- 개수: 파괴된 블록당 24개.
- 형태: terrain view matrix와 같은 right/up basis를 사용하는 카메라 정면 billboard quad.
- 텍스처: 파괴된 블록의 대표 block texture layer.
- UV: 텍스처 내부의 결정론적 랜덤 4x4 sub-tile.
- 수명: `0.45 ~ 0.75`초.
- 크기: `0.10 ~ 0.16`블록.
- 중력: `22`.
- 충돌: 파티클 AABB와 지형 충돌 형상을 비교하는 바닥 충돌, 약한 bounce, 강한 X/Z friction.
- Pipeline: 기존 block texture array를 사용하는 전용 particle graphics pipeline.
- 불 연기 파티클은 별도 smoke particle texture array를 같은 particle pipeline에 바인딩해 그린다.
- Depth test는 켜고 depth write는 끈다.
- 파티클 vertex는 파티클 위치에서 `WorldRuntime::lightAtWorld`를 샘플링한 packed light를 담고, 프레임 전역 `skyBrightness`와 같은 light curve를 통해 밝아지거나 어두워진다.

씬 그리기 순서는 solid 블록, blend 블록, 유체, 3인칭 플레이어, 블록 파괴 파티클, 드랍 아이템, 선택 외곽선, 1인칭 viewmodel 순서다.
1인칭 viewmodel은 마지막에 그리기 직전 scene depth를 clear하고, viewmodel끼리는 depth test/write를 사용한다.

## 드랍 아이템 렌더링

드랍 아이템은 전용 item pipeline으로 렌더링한다.
아이템 스프라이트에서 만든 로컬 extruded mesh와 `block_model`용 작은 큐브 mesh는 시작 시 정적 vertex/index buffer에 한 번 업로드한다.
로컬 extruded mesh 생성은 `ItemSpriteMeshBuilder`가 담당하고, `block_model` mesh는 `modelBlock` 또는 fallback `placeBlock` 블록의 6면 텍스처 layer를 사용해 구성한다.
결과 타입은 모두 `DroppedItemRenderPath::ItemSpriteMesh`이다.
프레임마다 CPU가 아이템 쿼드 정점을 다시 펼치지 않고, 드랍 아이템 위치/회전/텍스처 layer만 담은 instance buffer를 갱신한다.
instance buffer는 persistent mapping 상태로 유지해 매 프레임 `vkMapMemory`/`vkUnmapMemory`를 반복하지 않는다.
item instance buffer는 frame-in-flight별 영역을 나누고, 각 프레임 영역 안에서 월드 드랍 아이템 영역과 1인칭 viewmodel 아이템 영역을 분리한다.
따라서 같은 프레임에서 viewmodel 아이템을 그려도 이미 기록된 월드 드랍 아이템 draw command의 instance data를 덮어쓰지 않는다.
정적 mesh GPU buffer, instance buffer, instance 업로드, 정렬, item id별 batch draw는 `DroppedItemRenderPath`가 소유한다.
`DroppedItemRuntime`은 드랍 아이템 runtime 상태와 tick을 갱신하고, `DroppedItemRenderCollector`는 월드 엔티티를 순회해 렌더 후보 `RenderInstance` 목록을 만들며, `RendererDroppedItems.cpp`는 카메라/pipeline/texture 정보를 `DroppedItemRenderPath`에 전달한다.
렌더 후보 수를 줄이기 위해 다음 컬링을 적용한다.

- 런타임은 드랍 아이템을 가진 청크별 카운트와 전체 드랍 아이템 수를 캐시한다.
- 드랍 아이템 렌더링은 `runtimeChunks` 전체가 아니라 드랍 아이템을 가진 청크 목록만 순회한다.
- 청크 AABB가 카메라 프러스텀 밖이면 해당 청크의 드랍 아이템은 모두 건너뛴다.
- 청크가 프러스텀 안에 있어도, 개별 드랍 아이템 위치가 카메라에서 48블록보다 멀면 렌더링하지 않는다.
- 거리 판정은 보간된 드랍 아이템 위치와 카메라 위치 사이의 3D 거리 제곱으로 처리한다.
- 드랍 아이템 엔티티 하나는 `count`에 따라 1~4개의 렌더 instance로 표시할 수 있다.
- 시각 복제본 수는 count `1`, `2~16`, `17~48`, `49~99` 구간에 따라 각각 1, 2, 3, 4개다.
- `extruded_sprite`와 `block_model` 드랍 아이템 모두 같은 복제본 배치 규칙을 사용한다.
- 같은 아이템 mesh를 사용하는 instance는 item id 기준으로 정렬한 뒤 batch draw한다.
- `extruded_sprite`는 item texture array를, `block_model`은 terrain texture array를 바인딩해 같은 item pipeline으로 나누어 그린다.
- instance buffer에는 위치/회전/텍스처 layer, 렌더 크기와 함께 정규화된 sky/block light가 들어간다. 드랍 아이템은 보간된 월드 위치의 light를 사용하고, 손에 든 viewmodel 아이템은 플레이어 위치의 light를 사용한다.

이 컬링은 렌더링 후보만 줄이며, 드랍 아이템 물리, 저장, 획득 판정에는 영향을 주지 않는다.

## 슬롯 아이콘 렌더링

인벤토리/핫바 슬롯 아이콘은 RmlUi `<img>`로 표시한다.
`sprite` 슬롯 아이콘은 `assets/textures/item/{slotTexture}.png`를 직접 사용한다.
`block_model` 슬롯 아이콘은 콘텐츠 로딩 중 `assets::writeBlockItemIcon`이 `modelBlock` 또는 fallback `placeBlock` 블록 텍스처를 합성해 `assets/textures/item/generated/{item_key}_slot.png` 파일로 만든다.
UI는 생성된 텍스처도 일반 슬롯 이미지와 같은 경로로 읽는다.
아이템 슬롯 아이콘도 `modelShape`의 X/Y/Z 크기를 반영하므로 `quarter_log`는 반블럭보다 낮은 조각이 아니라 수직으로 한 번 더 자른 긴 1/4 통나무 조각으로 보인다.
`quarter_log`의 한쪽 수직 절단면은 `modelBlock` 블록 재질의 `verticalSection` 레이어를 전체 UV로 사용한다.
현재 `quarter_stripped_log`는 `stripped_log`의 `verticalSection = stripped_log_section_vertical` 설정을 절단면 텍스처로 사용한다.

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
달 스프라이트는 scene pass에서 약간 따뜻한 흰노란 tint를 곱해 dual-filter bloom에 은은하게 잡히도록 한다.
렌더러는 `GameClient`에서 `worldTicks`를 받아 28800틱 하루 주기를 계산한다.
`06H`에는 태양이 동쪽 지평선 근처에 있고, `12H`에는 머리 위에 있으며, `18H`에는 서쪽 지평선 근처에 있다. 달은 반대 방향을 사용한다.
하늘 각도는 하루 주기 동안 감소하므로 `06H` 시작 시점에서 투영된 태양은 지는 것이 아니라 떠오른다.
