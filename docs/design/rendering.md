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
`src/renderer/RendererUi.cpp`는 RmlUi `RenderInterface` 구현과 UI 입력/인벤토리 표시 갱신을 담는다.
`src/renderer/RendererDroppedItems.cpp`는 dropped item 아이템 스프라이트 mesh 생성, GPU instance 업로드, draw path를 담는다.
`src/renderer/RendererFrame.h`는 `GameClient`가 한 프레임 렌더링에 넘기는 카메라, 플레이어, overlay, debug, screenshot, world tick 입력을 `RendererFrame` DTO로 묶는다.
드롭 아이템 생성/병합/물리 tick/pickup 판정은 `src/world/DroppedItemSystem.h/.cpp`가 담당한다.
`src/world/TerrainMesher.h/.cpp`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 맡는다.
solid subchunk의 greedy meshing 본체와 Vulkan 업로드는 아직 `Renderer`가 담당한다.
fluid subchunk mesh 생성은 `TerrainMesher`가 맡고, 불투명 블록 판정은 `Renderer` callback을 사용한다.
초기 청크 지형 생성과 feature 반영은 `src/world/TerrainBuilder.h/.cpp`로 분리되어 있다.

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
프레임마다 CPU가 아이템 쿼드 정점을 다시 펼치지 않고, 드랍 아이템 위치/회전/텍스처 layer만 담은 instance buffer를 갱신한다.
instance buffer는 persistent mapping 상태로 유지해 매 프레임 `vkMapMemory`/`vkUnmapMemory`를 반복하지 않는다.
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

## 기후 오버레이

F6은 기후 디버그 오버레이를 순환한다.

```text
OFF -> Temperature -> Precipitation -> OFF
```

오버레이는 전체 `65536 x 65536` 래핑 월드를 덮는 `1024 x 1024` 텍스처다.
각 픽셀은 `64 x 64` 블록 영역을 샘플링한다.

- Temperature는 래핑된 Z를 남북 위도 값으로 사용한다. 월드 가장자리는 춥고 중앙은 덥다.
- Temperature는 넓은 기후대가 유지되도록 중위도 mask를 통해 약한 tileable noise를 더한다.
- Temperature 색상은 낮은 값을 파랑, 높은 값을 빨강으로 매핑한다.
- Precipitation은 지형 높이 노이즈와 같은 4D torus 방식으로 샘플링한 넓은 tileable 2D noise를 사용한다.
- Precipitation 색상은 낮은 값을 회색, 높은 값을 파랑으로 매핑한다.

## 하늘 스프라이트

태양과 달 스프라이트는 시간에 따라 변하는 월드 방향에서 투영해 screen-space sprite로 렌더링한다.
현재 투영 half-size는 화면 너비의 `0.04`이며, 높이는 viewport aspect ratio에 맞게 조정한다.
렌더러는 `GameClient`에서 `worldTicks`를 받아 28800틱 하루 주기를 계산한다.
`06H`에는 태양이 동쪽 지평선 근처에 있고, `12H`에는 머리 위에 있으며, `18H`에는 서쪽 지평선 근처에 있다. 달은 반대 방향을 사용한다.
하늘 각도는 하루 주기 동안 감소하므로 `06H` 시작 시점에서 투영된 태양은 지는 것이 아니라 떠오른다.
