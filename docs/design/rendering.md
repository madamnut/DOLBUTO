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
- Normal texture: `assets/textures/fluid/water_normal.jpg`
- The current normal texture keeps the existing filename and uses a 256x256 PNG water-normal source.
- Fresnel alpha: base `0.7`, edge `0.95`, power `1.0`
- Runtime config: `config/render.json` -> `fluid.water.baseAlpha`, `fluid.water.edgeAlpha`, `fluid.water.fresnelPower`, `normalScale`, `normalTiling`, `normalSpeed`, `ssr`
- Manual fluid mip textures: not used yet
- `amount = 0` or `id = 0` is not rendered
- Amount height is rounded up by 10-unit steps from `0.1` to `1.0` block

Block terrain is drawn first, then fluid meshes are drawn.
Internal fluid faces are skipped when an adjacent fluid reaches the same or greater height.
Fluid rendering uses a separate `fluidPipeline_` with alpha blending enabled and depth write disabled.
The normal terrain pipeline remains non-blended for opaque/cutout block rendering.
The fluid pipeline uses `fluid.frag`.
`terrain.vert` provides the reconstructed quad normal so fluid alpha becomes stronger at grazing view angles.
`water_normal.jpg` is loaded as `VK_FORMAT_R8G8B8A8_UNORM` through `stbi_load`.
`fluid.frag` samples the texture RG channels as signed water-normal offsets four times: medium, small, broad, and very broad wave scales.
The combined offset uses fixed medium/small/big wave weights, is reduced at grazing Fresnel angles, and perturbs the Fresnel/reflection normal; it does not deform water geometry.

## Water SSR

Water SSR uses the offscreen scene color and scene depth from the scene pass.

- Scene rendering is split into an offscreen scene pass and a swapchain composite/fluid pass.
- The offscreen scene pass renders sky sprites, block terrain, block selection, and player mesh into scene color/depth targets.
- The swapchain pass first composites the scene color target, then renders fluid meshes.
- `fluid.frag` samples the offscreen scene color and scene depth directly.
- `fluid.frag` samples scene color and scene depth in the offscreen texture coordinate space.
- Fluid occlusion against terrain is handled in the shader by linearizing and comparing the water fragment depth against the sampled scene depth.
- SSR ray samples project world-space ray points through the existing MVP and compare linearized ray depth against linearized scene depth.
- The SSR march uses variable step growth and refinement when it overshoots a possible hit.
- Misses fall back to `ssr.fallbackColor` and `ssr.fallbackStrength`.

`ssr.thickness` is interpreted in linear depth units.
The Hi-Z compute path is not used.
