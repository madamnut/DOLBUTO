# 블록 데이터

## 정의 파일

블록 정의는 다음 파일에서 읽는다.

```text
assets/data/blocks.json
```

정의 파일 파싱은 `src/data/DataLoaders.h/.cpp`의 `data::parseBlockDefinitions`가 맡는다.
렌더러는 파싱된 블록 정의를 받아 블록 ID별 `BlockDefinition`, 텍스처 레이어, 드랍 테이블, 소품 모델 캐시를 구성한다.

## 블록 드랍

블록 정의는 `drops` 배열을 포함한다.
드랍 항목은 아이템 키로 작성하고, 로드 시점에 아이템 ID로 해석한다.
아이템과 드랍 테이블 초안은 [[item-data]]에 기록한다.

## 블록 파괴

블록 정의는 블록 파괴용 `hardness`, `breakLevel`, `breakAction` 값을 포함한다.
유체 정의는 블록 파괴 값을 사용하지 않는다.

- `hardness < 0`: 파괴 불가
- `hardness = 0`: 즉시 파괴
- `hardness > 0`: 진행도 기반 파괴
- `breakLevel`: 필요한 최소 도구 레벨
- `breakAction`: 권장 좌클릭 파괴 동작. `none`이면 동작 보정을 적용하지 않는다.

손은 내부적으로 `breakLevel = 1`, 파괴 동작 없음으로 취급한다.
든 아이템에 `breakActions`와 `breakLevel`이 모두 없으면 좌클릭 파괴에서는 손과 동일하게 취급한다.
손이나 든 아이템의 레벨이 블록의 `breakLevel`보다 낮으면 파괴 진행도와 오버레이가 생기지 않는다.
레벨이 충분하면 파괴 파워는 다음 규칙으로 계산한다.

```text
levelMultiplier = 1.5 ^ (toolLevel - block.breakLevel)
actionMultiplier = block.breakAction == "none" 또는 도구의 breakActions에 포함되면 1.0, 아니면 0.5
breakPower = 1.0 * levelMultiplier * actionMultiplier
progress += deltaSeconds * breakPower / hardness
```

Sandbox 모드에서는 파괴 가능한 블록이 도구와 무관하게 최대 `0.5`초 안에 파괴된다.
`hardness < 0`인 파괴 불가 블록은 Sandbox 모드에서도 파괴되지 않는다.
Sandbox 모드의 블록 파괴는 도구 내구도를 소비하지 않는다.

내구도가 있는 아이템으로 `breakLevel >= 1` 블록을 실제로 파괴하면 내구도를 소비한다.
권장 `breakAction`이 맞거나 블록 동작이 `none`이면 1, 레벨은 충분하지만 동작이 다르면 3을 소비한다.
`breakLevel = 0` 블록, 손, 내구도 없는 아이템은 내구도를 소비하지 않는다.

현재 초기값:

```text
air      -1.0
bedrock  -1.0
plant     0.0
stone     0.0
branch    0.0
leaves    0.5
mud       0.7
clay      0.8
sand      1.0
dirt      1.3
gravel    1.4
grass     1.5
ice       2.0
sandstone 4.0
log       4.0
stripped_log 4.0
rock      5.0
```

현재 초기 파괴 동작:

```text
rock, sandstone                 breakLevel 2 / smash
grass, dirt, sand, mud, clay, gravel  breakLevel 1 / dig
log, stripped_log               breakLevel 2 / chop
branch prop                     breakLevel 0 / chop
leaves, plant                   breakLevel 0 / cut
ice                             breakLevel 2 / smash
stone prop                      breakLevel 0 / smash
air, bedrock                    breakLevel 0 / none
```

현재 `log`는 파괴되면 `log` 아이템 1개를 드랍한다.
`log`, `stripped_log`는 `block_model` 아이템으로, 각 아이템의 `placeBlock` 대상 블록 텍스처를 작은 블록 모델로 렌더링한다.

블록 파괴 오버레이 텍스처는 블록 렌더링 에셋으로 저장한다.

```text
assets/textures/block/breaking/destroy_stage_0.png
...
assets/textures/block/breaking/destroy_stage_9.png
```

## LightAttenuation

블록과 유체 정의는 조명 전파 감쇠값 `lightAttenuation`을 가진다.
값 범위는 `0~15`이며 로더에서 이 범위로 클램프한다.

```text
0  감쇠 없음
1  일반 투명 통과
2  물, 나뭇잎, 얼음처럼 더 흐려지는 통과
15 차단
```

청크 skylight 계산은 블록 ID를 직접 분기하지 않고 `BlockDefinition`/`FluidDefinition`에서 만든 attenuation table을 조회한다.
셀에 유체가 있으면 `max(block.lightAttenuation, fluid.lightAttenuation)`을 사용한다.
유체량은 조명 감쇠량에 영향을 주지 않는다.
따라서 유체 흐름 중 물량만 바뀌는 경우에는 조명을 다시 계산하지 않고, 유체가 없던 셀에 생기거나 있던 유체가 사라지는 경우처럼 유체 존재/종류가 바뀔 때만 조명 dirty로 본다.

현재 초기값:

```text
air, plant, stone prop, branch prop  1
leaves, ice                          2
그 외 불투명 블록                    15
none fluid                           0
water, lava                          2
methane, hydrogen                    1
```

## LightEmission

블록 정의는 블록 라이트 원천값 `lightEmission`을 가질 수 있다.
값 범위는 `0~15`이며 로더에서 이 범위로 클램프한다.

`lightEmission`은 skyLight가 아니라 blockLight 채널에 들어가는 값이다.
blockLight는 시간대별 하늘 밝기의 영향을 받지 않고, 렌더링에서는 `max(skyLight * skyBrightness, blockLight)`로 skyLight와 합성한다.

현재 블록 데이터에는 발광 블록이 없다.

## 랜덤 오프셋

`randomOffset`는 `cross`, `prop` 렌더 타입에 쓰는 블록 데이터 불리언 플래그다.
값이 true이면 렌더링되는 메쉬만 X/Z 방향으로 `-0.2 ~ +0.2` 블록만큼 이동한다.
오프셋은 래핑된 월드 좌표와 기존 배치 salt에서 결정적으로 계산한다.
저장된 블록 데이터, 충돌, 생성, 블록 정체성은 바뀌지 않는다.
현재 사용 블록은 `plant`, `stone`, `branch`다.

## 부착 블록

블록 정의는 선택적으로 `attachment` 객체를 가질 수 있다.

```json
{
  "attachment": {
    "face": "bottom"
  }
}
```

현재 지원하는 값은 `face = "bottom"`뿐이다.
이 값은 해당 블록의 아래쪽 면이 유효한 지지 블록에 붙어 있어야 한다는 뜻이다.
현재 유효한 지지 블록은 `collision = true`인 블록이다.

블록이 설치/파괴되거나 유체 제거를 동반한 블록 변경이 발생하면 변경 좌표와 6방향 이웃을 다음 블록 tick 대상으로 등록한다.
블록 tick에서 부착 블록의 지지가 사라진 것이 확인되면 해당 블록은 일반 블록 파괴와 같은 방식으로 제거된다.
이때 드랍, 파티클, 사운드, 편집 메쉬 갱신을 모두 수행한다.
연쇄 파괴는 즉시 재귀 처리하지 않고, 제거된 좌표와 그 6방향 이웃이 다음 블록 tick에 다시 등록되는 방식으로 이어진다.

현재 `plant`, `stone` prop, `branch` prop은 `bottom` 부착 블록이다.

## 방향성 랜덤 회전

`directional`이 `false`이면 지형 메싱에서 래핑된 월드 좌표 기반의 결정적 4방향 랜덤 회전을 적용한다.
큐브 블록은 윗면 UV를 회전한다.
`cross` 블록은 교차 평면을 블록 중심 기준으로 회전한다.
`prop` 블록은 로드된 모델 쿼드를 블록 중심 기준으로 회전한다.
`directional`이 `true`이면 이러한 랜덤 회전은 비활성화된다.

등록된 블록 엔트리만 texture array에 포함된다.

## 선택 레이캐스트

블록 선택과 좌클릭 파괴는 DDA로 지나가는 블록 셀을 찾은 뒤, 렌더 타입별 hit shape를 검사한다.

```text
cube  : 기존 1 x 1 x 1 블록 셀
cross : 렌더링에 쓰는 X자 quad 2장
prop  : `.dpm` prop quad mesh
```

`cross`와 `prop`은 렌더링과 동일한 `randomOffset` 및 4방향 랜덤 회전을 적용해서 hit 판정을 한다.
알파 픽셀 단위 테스트는 하지 않는다.

선택 아웃라인도 렌더 타입별로 다르게 그린다.

```text
cube  : 기존 블록 박스
cross : X자 quad 2장의 edge
prop  : prop mesh의 local AABB에 렌더 변환을 적용한 bounds 박스
```

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
8     log
9     leaves
10    gravel
11    ice
12    stripped_log
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
- `alphaBlend`: blend 렌더링에서 텍스처 전체에 곱하는 alpha 값
- `mipDistanceScale`: mip 거리 배율
- `textures`: 면별 텍스처 매핑

`alphaMode = "blend"` 블록은 일반 solid 지형 mesh가 아니라 별도 blend subchunk mesh로 분리된다.
blend 블록은 terrain texture array를 그대로 사용하며, `alphaBlend` 값을 packed terrain material에 담아 fragment shader에서 최종 alpha에 곱한다.
현재 `ice`가 이 경로를 사용한다.
`ice`는 주변 블록 face를 지우지 않도록 `faceOcclusion = "none"`을 사용하고, 같은 ice끼리 붙은 내부면만 `sameBlockFaceCulling = true`로 제거한다.

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

유체 정의는 현재 `id`, `name`, `lightAttenuation`을 가진다.

```json
{
  "id": 1,
  "name": "water",
  "lightAttenuation": 2
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

## 유체 틱

유체 시뮬레이션은 월드 전체를 매 tick 순회하지 않고, 셀 상태가 바뀐 좌표 주변만 다음 유체 tick 대상으로 등록한다.
셀 상태 변경은 블록 설치/파괴와 유체량 변경을 포함한다.
변경이 일어난 좌표와 동서남북/상하 6방향 이웃을 다음 유체 tick set에 넣으며, 같은 좌표는 set으로 중복 제거한다.

블록 설치로 대상 칸이 `air`가 아니게 되면 해당 칸의 유체는 제거된다.
이 경우에도 별도 유체 제거 이벤트가 아니라, 블록 설치로 셀 상태가 바뀐 결과로 주변 유체 tick이 등록된다.

현재 시뮬레이션 대상 유체는 `water`뿐이다.
물은 액체로 취급하며 아래 방향을 먼저 시도하고, 그 다음 수평 방향을 처리한다.

- 아래 칸이 빈 블록이고 비어 있거나 물이면, 현재 칸에서 아래 칸으로 최대 `100`까지 이동한다.
- 아래로 이동한 뒤 남은 물이 있으면 현재 칸과 동서남북 4칸 중 물을 담을 수 있는 칸을 대상으로 수평 평형화를 시도한다.
- 수평 평형화는 대상 칸들의 총 물량을 동일하게 나누며, 칸별 물량 차이가 `1` 이하이면 이미 평형으로 본다.
- 수평 대상은 `air` 블록인 칸만 포함한다. 로드되지 않았거나 고체 블록인 칸은 유체를 받을 수 없다.
- 유체량이 바뀐 칸은 다시 다음 tick 대상으로 주변 6방향과 함께 등록된다.

유체 시뮬레이션은 5 world tick마다 한 번 실행한다.
현재 렌더 브리지에서는 실행 1회당 최대 `256`개 좌표를 처리하고, 남은 좌표는 다음 유체 tick으로 넘긴다.

관련 문서: [[rendering]], [[world-generation]], [[save-load]]

## 유체 렌더링 참고

`fluids.json`은 `id = 0`, `name = "none"`을 명시적인 예약 엔트리로 포함한다.
이 값은 실제 렌더링/시뮬레이션 유체가 아니다.
패킹된 유체 데이터에서 `id = 0`이고 `amount > 0`인 값은 유효하지 않다.

`water` 렌더링은 `assets/textures/fluid/water.png`를 사용한다.
물은 `config/render.json`의 `fluid.water.alpha`를 적용한 단순 텍스처 유체 메쉬로 렌더링한다.
물 노멀 매핑, Fresnel alpha, depth absorption, SSR은 현재 렌더러에 포함하지 않는다.
유체는 아직 수동 mip 텍스처를 사용하지 않는다.
렌더링되는 유체 윗면 높이는 유체량을 10 단위 단계로 양자화한다.
위에 다른 물 셀이 있는 물 셀은 높이 `1.0`의 가득 찬 블록으로 렌더링한다.

```text
1~10   -> 0.08 block
11~20  -> 0.16 block
...
91~100 -> 0.80 block
```

## 소품 블록 초안

`renderType = "prop"`은 일반 `blocks` 배열에 저장되는 작은 지면 소품용으로 예약한다.
별도의 청크 데이터 레이어는 추가하지 않는다.

현재 초안 ID:

```text
20000 stone
20001 branch
```

소품 블록은 `prop.model`로 모델을 선택하고, `prop.texture`로 블록 텍스처 이름 하나를 선택한다.
소스 모델은 `assets/textures/block/model/{model}.glb`에 저장한다.
런타임/캐시 모델은 `assets/textures/block/model/{model}.dpm`에 저장한다.
텍스처는 `assets/textures/block/{texture}.png`에 저장한다.

시작 시 렌더러는 블록 데이터가 사용하는 소품 모델 이름을 수집하고 `src/assets/PropModelLoader.h/.cpp`에 변환/로드를 위임한다.
`{model}.dpm`이 없거나, 파일 크기 기준으로 유효하지 않거나, `{model}.glb`보다 오래된 경우 `.glb` 파일을 `.dpm`으로 변환하려고 시도한다.
두 파일이 모두 없거나 변환에 실패하면 런타임 로그에 경고를 기록한다.

`dpm`은 magic과 버전 필드가 없는 간단한 바이너리 소품 메쉬 형식이다.

```text
uint32 quadCount
repeat quadCount:
  float position[4][3]
  float uv[4][2]
  float normal[3]
```

런타임 소품 렌더링은 `PropModelLoader`가 `.dpm`에서 읽은 렌더링용 quad 배열을 블록 ID 메쉬 캐시에 저장한다.
서브청크 메싱 중 각 소품 블록은 캐시된 쿼드를 일반 지형 메쉬에 추가한다.
GLB 삼각형 쌍은 변환 중 다시 쿼드로 병합한다.
소품 쿼드는 양면으로 방출하므로 소스 모델의 face winding이 가시성을 결정하지 않는다.

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
  },
  "drops": [
    { "item": "stone_shard", "min": 1, "max": 1, "chance": 1.0 }
  ]
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
    "texture": "log_side"
  }
}
```
