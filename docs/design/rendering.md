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

## 메모리

지형 최종 버퍼는 `DEVICE_LOCAL` 메모리에 둔다.
업로드는 staging buffer를 통해 수행한다.

이 구조는 idle 상태의 GPU 읽기 성능을 우선한 결정이다.

## 텍스처

- 블록 텍스처는 `assets/data/blocks.json`에 등록된 텍스처만 texture array에 넣는다.
- 기본 블록 텍스처는 `assets/textures/block`에 있다.
- 수동 mip 파일은 `assets/textures/block/mip`에 둔다.
- 없는 mip 파일은 실행 중 생성해서 mip 폴더에 저장한다.
- mip 전환은 shader에서 카메라 거리 기준으로 처리한다.
- `mipDistanceScale = 1.0`일 때 64블록 단위로 mip 단계가 바뀐다.

## Camera-Relative Rendering

World, player, chunk, and save coordinates remain in world space.
Scene rendering uses camera-relative coordinates before projection to reduce float precision jitter at large wrapped X/Z values.

- CPU gameplay coordinates remain `double` where already used.
- Terrain mesh vertices remain stored in world coordinates.
- Terrain, player, particle, dropped-item, and selection vertex shaders subtract `cameraPosition.xyz` from each world-space vertex.
- Scene view matrices use the camera rotation with translation set to zero.
- Terrain frustum culling tests chunk AABBs after subtracting the render camera position.
- Fragment mip distance uses the length of the camera-relative position instead of `distance(camera, worldPosition)`.

## Block Breaking Overlay

Block breaking crack textures are world-rendering overlays, not UI assets.
They are stored under:

```text
assets/textures/block/breaking/destroy_stage_0.png
...
assets/textures/block/breaking/destroy_stage_9.png
```

When the player holds block break, the selected cube block renders one of the ten overlay stages according to current break progress.
The overlay is emitted as block-space quads using the terrain texture array and forces mip level 0.
While breaking, small block-textured particles spawn from the hit face at a fixed interval.

## Prop Rendering

`renderType = "prop"` blocks are rendered inside the normal terrain mesh path.

- Block data chooses a `.dpm` model with `prop.model`.
- Block data chooses one block texture array layer with `prop.texture`.
- `.dpm` stores quad positions, UVs, and normals; it has no magic and no version field.
- On startup, missing or stale `.dpm` files are regenerated from matching `.glb` files.
- During `.glb` conversion, source triangle pairs are merged back into quads.
- During subchunk meshing, prop quads are appended at the block position.
- Prop quads are emitted double-sided to avoid depending on source model winding.
- `randomOffset` prop blocks offset only rendered X/Z mesh position by up to `0.2` blocks from center.
- Packed terrain positions and UVs use 1/256 precision so small rotated prop geometry and model UV islands survive.

## 컬링

- 프러스텀 컬링을 적용한다.
- 지형 메쉬는 청크/서브청크 렌더 데이터 기준으로 draw한다.
- 와이어프레임은 F4로 토글한다.

## 관련 문서

- [[chunk-system]]
- [[block-data]]
- [[debug-profiling]]

## Fluid Rendering

Fluids are rendered as separate subchunk meshes from block terrain.
The current rendered fluid is `water`.

- Texture: `assets/textures/fluid/water.png`
- Runtime config: `config/render.json` -> `fluid.water.alpha`
- Manual fluid mip textures: not used yet
- `amount = 0` or `id = 0` is not rendered
- Top-surface amount height is rounded up by 10-unit steps from `0.08` to `0.8` block
- A water cell with another water cell above it renders as `1.0` block high

Block terrain, block selection, and player mesh are drawn first, then fluid meshes are drawn in the same scene pass.
Internal fluid faces are skipped when an adjacent fluid reaches the same or greater height.
Fluid rendering uses a separate `fluidPipeline_` with alpha blending enabled and depth write disabled.
The normal terrain pipeline remains non-blended for opaque/cutout block rendering.
Fluids keep depth testing enabled so blocks, cutout terrain, selection outlines, and the player can occlude them through the scene depth buffer.
The fluid pipeline uses `fluid.frag`.
`fluid.frag` samples the fluid texture array and applies a fixed alpha value from render config.
Water normal mapping, Fresnel alpha, depth absorption, and SSR are not part of the current renderer.

## Block Break Particles

Block destruction spawns short-lived runtime particles. Particles are not saved.

- Trigger: successful block removal.
- Count: 24 particles per destroyed block.
- Shape: camera-facing billboard quads using the same right/up basis as the terrain view matrix.
- Texture: the destroyed block's representative block texture layer.
- UVs: deterministic random 4x4 sub-tiles within the texture.
- Lifetime: `0.45 ~ 0.75` seconds.
- Size: `0.10 ~ 0.16` blocks.
- Gravity: `22`.
- Collision: simple floor collision against solid terrain cells with weak bounce and strong X/Z friction.
- Pipeline: dedicated particle graphics pipeline using the existing block texture array.
- Depth test is enabled and depth write is disabled.

Scene draw order is blocks, fluids, player, block break particles, then selection outline.

## Climate Overlay

F6 cycles the climate debug overlay.

```text
OFF -> Temperature -> Precipitation -> OFF
```

The overlay is a `1024 x 1024` texture covering the full `65536 x 65536` wrapped world.
Each pixel samples a `64 x 64` block area.

- Temperature uses wrapped Z as a north-south latitude value: world edges are cold and the center is hot.
- Temperature adds weak tileable noise through a mid-latitude mask so the broad climate bands remain intact.
- Temperature color maps low values to blue and high values to red.
- Precipitation uses a wide tileable 2D noise sampled through the same 4D torus approach as terrain height noise.
- Precipitation color maps low values to gray and high values to blue.

## Sky Sprites

Sun and moon sprites are projected from time-driven world directions and rendered as screen-space sprites.
Their current projected half-size is `0.04` screen width, with height adjusted by the viewport aspect ratio.
The renderer receives `worldTicks` from the application and computes a 28800-tick day cycle.
At `06H` the sun is near the eastern horizon, at `12H` it is overhead, at `18H` it is near the western horizon, and the moon uses the opposite direction.
The sky angle decreases through the day cycle so the projected sun rises instead of setting from the `06H` start time.
