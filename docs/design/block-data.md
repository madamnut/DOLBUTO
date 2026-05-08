# 블록 데이터

## 정의 파일

블록 정의는 다음 파일에서 읽는다.

```text
assets/data/blocks.json
```

등록된 블록 엔트리만 texture array에 포함된다.

## 현재 블록 ID

```text
0     air
1     rock
2     grass
3     dirt
4     sand
5     sandstone
6     mud
7     clay
8     trunk
9     leaves
10000 plant
65535 bedrock
```

## 주요 속성

- `renderType`: `none`, `cube`, `cross`
- `directional`: 방향성을 가지는지 여부
- `collision`: 플레이어 충돌 여부
- `ao`: 메싱 AO 적용 여부
- `faceOcclusion`: `none`, `opaque`, `cutout`
- `sameBlockFaceCulling`: 같은 블록끼리 면을 가릴지 여부
- `alphaMode`: `opaque`, `cutout`, `blend`
- `alphaCutoff`: cutout 기준값
- `mipDistanceScale`: mip 거리 배율
- `textures`: 면별 텍스처 매핑

## 렌더 타입

`none`:

- 렌더링하지 않는다.
- air가 사용한다.

`cube`:

- 일반 6면 블록이다.
- 그리디 메싱 대상이다.
- 현재 mip 처리 기준이 되는 기본 블록 타입이다.

`cross`:

- X자 스프라이트 형태다.
- plant가 사용한다.
- 양면으로 보이도록 메쉬를 만든다.

## 텍스처 매핑

지원하는 텍스처 키:

- `all`
- `top`
- `bottom`
- `side`
- `topBottom`

텍스처 파일은 `assets/textures/block/*.png`에서 찾는다.
수동 mip 파일은 `assets/textures/block/mip/*_mipN.png`를 우선 사용한다.
없는 mip은 실행 중 생성된다.

## 블록 저장 타입

런타임 청크의 블록 데이터는 `uint16_t` 블록 ID 배열이다.
청크 전체 크기는 `16 x 512 x 16`이므로 블록 수는 131072개다.

## 유체 데이터

유체 정의는 다음 파일에서 읽는 것을 기준으로 한다.

```text
assets/data/fluids.json
```

유체 정의는 현재 `id`, `name`만 가진다.
유체별 물성 파라미터는 아직 정의하지 않는다.

```json
{
  "id": 1,
  "name": "water"
}
```

현재 유체 ID:

```text
0   none
1   water
2   lava
300 methane
301 hydrogen
```

유체 ID 범위는 다음 기준을 사용한다.

- `0`: 유체 없음
- `1~299`: 액체
- `300~511`: 기체

런타임 유체 셀 데이터는 `uint16_t`로 표현한다.
상위 9비트는 유체 ID, 하위 7비트는 유체량이다.

- `id = 0`, `amount = 0`: 유체 없음
- `id = 1~511`: 유체 종류
- `amount = 1~100`: 유체량
- `amount = 100`: 가득 찬 상태
- `amount = 101~127`: 예약값

청크 런타임 데이터는 블록 배열과 유체 배열을 분리해서 가진다.

```text
blocks: uint16_t block id 배열
fluids: uint16_t packed fluid 배열
```

초기 월드 생성은 해수면 `Y = 256` 이하의 빈 공간에 `water`를 `amount = 100`으로 채운다.

관련 문서: [[rendering]], [[world-generation]], [[save-load]]

## Fluid Rendering Notes

`fluids.json` includes `id = 0`, `name = "none"` as an explicit reserved entry.
It is not a real render/simulation fluid.
Packed fluid data with `id = 0` and `amount > 0` is invalid.

`water` rendering uses `assets/textures/fluid/water.png`.
Water is rendered as a simple textured fluid mesh with `fluid.water.alpha` from `config/render.json`.
Water normal mapping, Fresnel alpha, depth absorption, and SSR are not part of the current renderer.
Fluids do not use manual mip textures yet.
Rendered fluid top-surface height is quantized by amount in 10-unit steps.
A water cell with another water cell above it renders as a full-height `1.0` block.

```text
1~10   -> 0.08 block
11~20  -> 0.16 block
...
91~100 -> 0.80 block
```

## Prop Block Draft

`renderType = "prop"` is reserved for small ground props stored in the normal `blocks` array.
It does not add a separate chunk data layer.

Current draft IDs:

```text
20000 stone
20001 branch
```

Prop blocks use `prop.model` to choose the model and `prop.texture` to choose one block texture name.
Source models are stored in `assets/textures/block/model/{model}.glb`.
Runtime/cache models are stored in `assets/textures/block/model/{model}.dpm`.
Textures are stored in `assets/textures/block/{texture}.png`.

At startup, the renderer checks prop models used by block data.
If `{model}.dpm` is missing, invalid by file size, or older than `{model}.glb`, it tries to convert the `.glb` file into `.dpm`.
If both files are missing or conversion fails, the runtime writes a warning to the log.

`dpm` is a compact binary prop mesh format with no magic and no version field:

```text
uint32 quadCount
repeat quadCount:
  float position[4][3]
  float uv[4][2]
  float normal[3]
```

Runtime prop rendering loads `.dpm` into a block-ID mesh cache.
During subchunk meshing, each prop block appends the cached quads into the normal terrain mesh.
GLB triangle pairs are merged back into quads during conversion.
Prop quads are emitted double-sided so source model face winding does not decide visibility.

```json
{
  "id": 20000,
  "name": "stone",
  "renderType": "prop",
  "collision": false,
  "faceOcclusion": "none",
  "alphaMode": "opaque",
  "prop": {
    "model": "stone",
    "texture": "rock"
  }
}
```

```json
{
  "id": 20001,
  "name": "branch",
  "renderType": "prop",
  "collision": false,
  "faceOcclusion": "none",
  "alphaMode": "opaque",
  "prop": {
    "model": "branch",
    "texture": "trunk"
  }
}
```
