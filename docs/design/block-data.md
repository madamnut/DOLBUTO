# 블록 데이터

## 정의 파일

블록 정의는 다음 파일에서 읽는다.

```text
assets/data/blocks.json
```

정의 파일 파싱은 `src/data/DataLoaders.h/.cpp`의 `data::parseBlockDefinitions`가 맡는다.
렌더러는 파싱된 블록 정의를 받아 블록 ID별 `BlockDefinition`, 텍스처 레이어, 드랍 테이블, 소품 모델 캐시를 구성한다.

## 블록 상태

일반 블록 ID 외에 셀별 소형 상태값은 청크의 `blockStates` 배열에 `uint16_t`로 저장한다.
상태가 필요 없는 블록은 `0`을 사용한다.
블록 정의의 `stateKind`는 해당 블록이 상태값을 어떤 의미로 해석하는지 정한다.

현재 지원하는 상태 범주는 `stateKind: "attach"`와 `stateKind: "attach_grid"`다.
`attach`는 `renderType: "slab"`에서 사용한다.

```text
0 bottom
1 top
2 north
3 south
4 west
5 east
```

`attach_grid`는 `renderType: "half_slab"`에서 사용한다.
상태값은 `face * 9 + (grid - 1)`이며, `face`는 위 `attach`의 6방향 값이고 `grid`는 배치 면 기준 `7 8 9 / 4 5 6 / 1 2 3` 키패드 위치다.
현재 배치에서는 `5`를 저장하지 않고 플레이어가 보는 방향의 `2/4/6/8` 모서리로 바꿔 저장한다.

`dirt_slab`과 `half_stripped_log`는 `renderType: "slab"`, `stateKind: "attach"`를 사용한다.
`dirt_slab`은 `dirt` 텍스처를 재사용하고, `half_stripped_log`는 껍질벗긴 통나무 텍스처를 재사용한다. 설치한 면에 따라 위/아래/동서남북 반칸 AABB로 렌더링, 레이캐스트, 선택 아웃라인, 배치 충돌, 플레이어 이동 충돌, 발밑 지지 판정을 처리한다.
렌더링 텍스처는 bottom 상태의 기준 반칸 cuboid를 먼저 만들고, 배치 상태에 따라 그 모델 전체를 회전/이동한 것처럼 유지한다.
플레이어, 드랍 아이템, 파티클 지형 충돌은 블록의 `collision`, `renderType`, `blockStates`를 읽어 같은 반블럭 AABB 기준으로 처리한다.
슬랩과 half slab의 조명 감쇄는 별도 렌더 타입 예외 없이 블록 정의의 `lightAttenuation` 값을 따른다.
현재 반블럭 계열은 조명 전파에서 일반 투명 통과값인 `lightAttenuation = 1`을 사용한다.

슬랩 설치는 레이캐스트 충돌 지점을 기준으로 클릭한 면을 `7 8 9 / 4 5 6 / 1 2 3` 키패드형 3x3 구역으로 나누어 판정한다.
`5`는 기존처럼 클릭한 면에 붙는 반블럭을 배치한다.
`2`, `4`, `6`, `8`은 해당 면 위의 아래/왼쪽/오른쪽/위 구역 방향으로 세워진 반블럭을 배치한다.
`1`, `3`, `7`, `9` 모서리는 플레이어 시선이 해당 면 위에서 더 평행한 축을 골라 인접한 직선 구역 중 하나로 해석한다.

`dirt_half_slab`과 `quarter_stripped_log`는 `renderType: "half_slab"`, `stateKind: "attach_grid"` 블록이다.
`dirt_half_slab`은 `dirt` 텍스처만 쓰는 일반 `0.5 x 0.5 x 1.0` 조각이다.
배치 면 기준 `1/3/7/9`는 해당 꼭짓점에 세우고, `2/4/6/8`은 해당 모서리에 눕힌다.
`5`는 플레이어가 보는 방향의 모서리로 눕힌다.
`quarter_stripped_log` 텍스처는 기준 slab을 수직으로 반 자른 `0.5 x 0.5 x 1.0` 조각에서 slab의 위/아래였던 면에 `topBottom`, 원래 외곽 옆면에 `side`, 새로 잘린 수직 절단면에 `verticalSection`을 사용하고, 배치 상태는 이 기준 배치를 회전/이동만 한다.
UV는 현재 배치된 AABB에 맞춰 다시 늘리지 않고 기준 조각의 재질 좌표를 유지한다.

## 블록 드랍

블록 정의는 `drops` 배열을 포함한다.
드랍 항목은 아이템 키로 작성하고, 로드 시점에 아이템 ID로 해석한다.
아이템과 드랍 테이블 초안은 [[item-data]]에 기록한다.

## 블록 상호작용

블록 정의는 우클릭 상호작용용 `interactActions` 배열을 가질 수 있다.
이 값이 비어 있으면 블록 우클릭은 아이템 설치 흐름으로 넘어간다.
값이 있으면 해당 블록은 상호작용 대상 가능성이 있는 것으로 보고, `components.placeable`보다 먼저 원형 상호작용 UI를 연다.

현재 `primal_workbench`는 `interactActions: ["craft"]`를 가진다.
워크벤치의 작업 영역은 블록 바로 위 `1 x 1 x 1` 공간이며, 그 안에 들어온 드랍 아이템 스택을 재료로 감지한다.
블록에 `interactActions`가 있으면 기본 우클릭은 블록 액션을 우선하며, `Shift + 우클릭`은 손에 든 아이템의 블록 대상 `components.useActions`를 우선한다.
현재 `bow_drill`의 `ignite`는 대상 블록 윗칸이 비어 있고 대상 블록이 충돌 블록이면 `fire`를 그 윗칸에 설치한다.

구운 몰드 블록은 `renderType: "mold"`를 사용하고, 아래 지지 블록이 있어야 유지되는 bottom attachment 블록이다.
몰드 블록은 `BlockEntityType::Mold` 상태를 가질 수 있으며, `moltenFluidId`, `moltenAmount`, `coolingTicks`를 저장한다.
작은 도가니의 `pour` 동작으로 몰드에 용탕을 붓고, 몰드 요구량을 채우면 200틱 동안 냉각한 뒤 해당 몰드 위에 cast part 아이템 1개를 드랍하고 내부 상태를 비운다.
몰드 자체는 내구도를 가지지 않고 재사용된다.

현재 몰드 요구량:

```text
small_plate_mold, small_preform_mold  10
plate_mold, preform_mold              20
large_plate_mold, large_preform_mold  30
short_rod_mold                         5
rod_mold                              10
long_rod_mold                         15
```

## 블록 파괴

블록 정의는 블록 파괴용 `hardness`, `breakLevel`, `breakAction` 값을 포함한다.
유체 정의는 블록 파괴 값을 사용하지 않는다.

- `hardness < 0`: 파괴 불가
- `hardness = 0`: 즉시 파괴
- `hardness > 0`: 진행도 기반 파괴
- `breakLevel`: 필요한 최소 도구 레벨
- `breakAction`: 권장 좌클릭 파괴 동작. `none`이면 동작 보정을 적용하지 않는다.

손은 내부적으로 `breakLevel = 1`, 파괴 동작 없음으로 취급한다.
든 아이템에 `components.breakActions`와 `components.breakLevel`이 모두 없으면 좌클릭 파괴에서는 손과 동일하게 취급한다.
손이나 든 아이템의 레벨이 블록의 `breakLevel`보다 낮으면 파괴 진행도와 오버레이가 생기지 않는다.
Sandbox 모드에서는 도구 레벨과 동작을 검사하지 않고 파괴 가능한 블록을 즉시 제거한다.
레벨이 충분하면 파괴 파워는 다음 규칙으로 계산한다.

```text
levelMultiplier = 1.5 ^ (toolLevel - block.breakLevel)
actionMultiplier = block.breakAction == "none" 또는 도구의 components.breakActions에 포함되면 1.0, 아니면 0.5
breakPower = 1.0 * levelMultiplier * actionMultiplier
progress += deltaSeconds * breakPower / hardness
```

Sandbox 모드에서는 파괴 가능한 블록이 도구와 무관하게 즉시 파괴된다.
좌클릭을 새로 누를 때는 즉시 1회 파괴하고, 누른 상태를 유지하는 반복 파괴는 10틱마다 처리한다.
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
stone_pile 0.0
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
primal_workbench 4.0
wooden_box 2.5
fire      0.0
dirt_slab 0.8
half_stripped_log 2.0
quarter_stripped_log 1.0
dirt_half_slab 0.4
rock      5.0
coal_ore, copper_ore, iron_ore, tin_ore, zinc_ore, silver_ore, gold_ore 5.0
```

현재 초기 파괴 동작:

```text
rock, sandstone, *_ore          breakLevel 2 / smash
grass, dirt, sand, mud, clay, gravel  breakLevel 1 / dig
dirt_slab, dirt_half_slab       breakLevel 1 / dig
log, stripped_log, half_stripped_log, quarter_stripped_log, primal_workbench breakLevel 2 / chop
wooden_box                      breakLevel 1 / chop
fire                            breakLevel 0 / none
branch prop                     breakLevel 0 / chop
leaves, plant                   breakLevel 0 / cut
ice                             breakLevel 2 / smash
stone pile prop, large stone pile prop    breakLevel 0 / smash
stone anvil prop, stone mortar prop      breakLevel 0 / smash
air, bedrock                    breakLevel 0 / none
```

현재 `rock`은 파괴되면 `large_stone` 1~2개를 확정 드랍하고, `stone` 0~1개와 `small_stone` 0~1개를 추가 드랍한다.
현재 광물 블록은 같은 돌 부산물과 광물별 원재료 1개를 확정 드랍한다. `coal_ore`는 `coal`, 금속 광물은 `raw_copper`, `raw_iron`, `raw_tin`, `raw_zinc`, `raw_silver`, `raw_gold`를 사용한다.
현재 `dirt`는 파괴되면 `dirt_pile` 4개를 확정 드랍한다.
현재 `grass`는 파괴되면 `dirt_pile` 4개와 `grass_scrap` 2~4개를 확정 드랍하며, `seed`는 낮은 확률 드랍을 유지한다.
현재 `log`는 파괴되면 `log` 아이템 1개를 드랍한다.
`log`, `stripped_log`, `half_stripped_log`, `primal_workbench`, `wooden_box`는 `block_model` 아이템으로, 각 아이템의 `modelBlock` 또는 `components.placeable.block` 대상 블록 텍스처를 작은 블록 모델로 렌더링한다.
`quarter_stripped_log`도 `block_model` 설치 아이템이며, `modelBlock = stripped_log`, `modelShape = quarter_log`, `components.placeable.block = quarter_stripped_log`를 사용한다.
`dirt_half_slab`은 `block_model` 설치 아이템이며, `modelBlock = dirt`, `modelShape = half_slab`, `components.placeable.block = dirt_half_slab`을 사용한다.

블록 파괴 오버레이 텍스처는 블록 렌더링 에셋으로 저장한다.

```text
assets/textures/block/breaking/destroy_stage_0.png
...
assets/textures/block/breaking/destroy_stage_9.png
```

블록 정의는 선택적으로 `breakEffects` 객체를 가질 수 있다.
현재 지원하는 값은 `particles`뿐이며, 생략하면 `true`로 처리한다.

```json
{
  "breakEffects": {
    "particles": false
  }
}
```

`breakEffects.particles = false`이면 해당 블록이 플레이어에게 파괴되거나 런타임 규칙으로 제거될 때 블록 깨짐 파티클을 생성하지 않는다.
사운드와 메쉬 갱신은 기존 파괴 흐름대로 처리한다.
현재 `fire`는 깨짐 파티클을 사용하지 않는다.

## 잎 Decay

블록 정의는 잎 decay용 불리언 속성을 가질 수 있다.

```json
{
  "leafDecayable": true,
  "leafDecaySupport": true
}
```

`leafDecayable = true`인 블록은 주변 블록 변화로 block tick을 받았을 때 6방향 BFS로 `leafDecaySupport = true` 블록과 연결되어 있는지 검사한다.
현재 연결 깊이는 `4`이며, 현재 잎에서 면으로 맞닿은 잎을 따라 최대 4칸 안에 support 블록이 있으면 유지한다.
support 블록이 없으면 해당 잎은 일반 블록 파괴와 같은 방식으로 제거된다.
이때 블록 정의의 `drops`를 사용해 드랍을 만들고, 파티클과 편집 메쉬 갱신도 일반 파괴 이벤트 경로를 사용한다.
단, 자연 decay로 사라지는 잎은 대량 발생 시 소리가 겹치지 않도록 블록 파괴 사운드를 재생하지 않는다.
잎 decay는 즉시 재귀 처리하지 않고, 제거된 좌표와 6방향 이웃이 다음 block tick에 등록되면서 퍼진다.

현재 `leaves`는 `leafDecayable = true`이고, `log`와 `stripped_log`는 `leafDecaySupport = true`다.

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
일반 블록 셀에 유체가 있으면 `max(block.lightAttenuation, fluid.lightAttenuation)`을 사용한다.
유체량은 조명 감쇠량에 영향을 주지 않는다.
따라서 유체 흐름 중 물량만 바뀌는 경우에는 조명을 다시 계산하지 않고, 유체가 없던 셀에 생기거나 있던 유체가 사라지는 경우처럼 유체 존재/종류가 바뀔 때만 조명 dirty로 본다.
빛이 한 셀에서 이웃 셀로 전파될 때 현재 셀의 출구 방향 감쇄와 다음 셀의 입구 방향 감쇄 중 큰 값을 사용한다.
조명 전파는 `renderType`별 특수 예외를 두지 않고 블록 데이터의 감쇄값을 사용한다. 해당 셀에 유체가 있으면 블록 감쇄와 유체 감쇄 중 큰 값을 쓴다.

현재 초기값:

```text
air, plant, stone pile prop, large stone pile prop, branch prop  1
slab, half_slab, crucible            1
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

현재 `fire`는 `lightEmission = 15`를 사용한다.

## 블록 엔티티

일반 블록 ID만으로 표현하기 어려운 셀별 런타임 상태는 청크의 block entity 목록에 저장한다.
block entity는 청크 로컬 X/Z, 월드 Y, 타입, 타입별 상태 값을 가진다.
현재 타입은 `Fire`와 `Crucible`이다.

`fire` 블록은 설치되거나 로드될 때 같은 좌표에 fire block entity를 가진다.
초기 남은 연소 시간은 `200`틱이며, 기본 열 단계는 `1`이다.
fire block entity는 노출 불 `exposed`, 열분해 불 `pyrolysis`, 도기 소성 불 `firing` 모드와 현재 타는 연료의 `fireHeatLevel`을 가진다.
블록 변화가 발생하면 변화 좌표 자신은 `SelfBlockChanged`, 동서남북/상하 6방향 이웃은 `BlockNeighborChanged` 이유로 block tick에 등록된다.
추가로 블록 변화 좌표가 fire의 구조 감시 범위 안에 들어가면 해당 fire도 `BlockNeighborChanged` 이유로 block tick에 등록된다.
fire는 주변 블록 변화 이벤트와 연료 소비 시점에 현재 열 단계와 구조 폐색 단계에 맞는 모드를 다시 검사한다.
fire 작업 공간은 fire 중심 같은 Y층의 `3 x 3` 영역이다. 구조 감시 범위는 fire 중심 `5 x 5 x 3` AABB이며, 위 작업 공간은 아직 사용하지 않는다.
구조 판정은 fire 셀에서 6방향 flood fill로 도달 가능한 비고체 셀을 내부 공간으로 본다.
열린 경계 칸을 만나면 감시 AABB의 동서남북/상하 6개 껍데기 방향 중 해당 방향만 leak으로 기록하고, 그 경계 칸 너머로는 더 탐색하지 않는다.
따라서 배기구 밖의 열린 외부 공간이 다른 방향으로 이어져 있어도 내부에서 처음 빠져나간 방향 하나만 `leakCount`에 반영된다.
작업 공간 안에 벽이 있어도 된다. 벽 뒤쪽처럼 fire에서 6방향으로 도달할 수 없는 셀은 processing 대상에 포함하지 않으며, 벽 바깥 빈 공간도 fire 내부 공간과 연결되지 않으면 leak으로 세지 않는다.
폐색 단계는 `leakCount == 0`이면 `sealed`, `leakCount == 1`이면 `vented`, 그 외는 `exposed`다.
`sealed` 구조는 `pyrolysis` 모드가 된다.
`vented` 구조는 현재 타는 연료의 `heatLevel >= 3`일 때만 `firing` 모드가 된다.
그 외 조합은 `exposed` 모드가 된다.
남은 연소 시간 감소는 별도 `FireBurn` tick으로 진행하며, fire가 계속 존재하면 자기 자신을 다시 `FireBurn`으로 등록한다.
연소 시간이 0이 되면 fire 셀의 `1 x 1 x 1` 영역 안에 있는 드랍 아이템 중 가장 높은 `heatLevel`을 가진 연료 아이템 1개를 소비한다. 같은 `heatLevel` 후보가 여러 개면 그 안에서 무작위로 고른다. fire 셀 안에 있는 아이템은 작업 공간에 포함되더라도 연료 소비 대상이며, processing 대상에서는 제외한다.
연료를 소비하면 해당 아이템의 `components.fuel.burnTimeTicks`만큼 남은 연소 시간이 늘어나고, fire block entity의 `fireHeatLevel`이 해당 연료의 `heatLevel`로 바뀐다.
연료에 `components.fuel.remainder`가 있으면 fire block entity가 해당 부산물 아이템과 개수를 기억하고, 그 연료로 추가된 연소 시간이 끝나는 시점에 드랍 아이템으로 뱉는다.
연료 소비 시점과 구조 변경 이벤트 시점 모두 현재 `fireHeatLevel`과 폐색 단계로 모드를 다시 결정한다.
`pyrolysis`와 `firing` fire는 5틱마다 fire 셀을 제외한 BFS 내부 작업 셀의 드랍 아이템을 확인하고, [[recipe/processings]]의 현재 모드 레시피 대상이면 해당 드랍 아이템의 `processingTicks`를 5틱씩 증가시킨다.
요구 tick에 도달한 드랍 아이템 스택이 1개이면 해당 엔티티를 결과 아이템과 `outputCount`로 교체한다. 스택이 2개 이상이면 원본 count를 1 줄이고 같은 위치에 결과 드랍 엔티티를 새로 만든다.
소비할 연료가 없으면 fire 블록은 `air`로 바뀌고 일반 블록 파괴와 같은 갱신 경로로 메쉬, 파티클, 사운드 이벤트를 발생시킨다.
fire block entity만 남고 실제 블록이 fire가 아니면 stale 상태로 보고 제거한다.

`refractory_clay_crucible` 블록은 설치되거나 로드될 때 같은 좌표에 crucible block entity를 가진다.
crucible block entity는 내부 용탕 종류 `moltenFluidId`와 용탕량 `moltenAmount`를 저장한다.
도가니 자체는 아래 블록이 fire인지 직접 처리하지 않고, 아래 fire block entity가 현재 타고 있으면 그 `fireHeatLevel`을 열 입력으로 읽는다.
열 입력이 있고 내부 용량이 남아 있으면 도가니 내부 AABB 안의 드랍 아이템 중 [[recipe/processings]]의 `smelt` 레시피 대상 하나를 진행한다.
비어 있는 도가니는 처리 가능한 광물 중 필요한 열 단계가 가장 낮은 그룹을 고르고, 같은 단계 후보가 여러 개면 무작위로 하나를 진행한다.
이미 `smelt` 진행 중인 아이템은 우선 계속 처리한다.
하나의 도가니는 한 종류의 용탕만 담을 수 있으므로, 이미 용탕이 들어 있으면 같은 `outputFluid` 레시피만 진행한다.
현재 도가니 용량은 `100`이고, 금속 원재료 1개는 완료 시 용탕 `10`을 더한다.
crucible block entity만 남고 실제 블록이 도가니가 아니면 stale 상태로 보고 제거한다.

## 랜덤 오프셋

`randomOffset`는 `cross`, `prop` 렌더 타입에 쓰는 블록 데이터 불리언 플래그다.
값이 true이면 렌더링되는 메쉬만 X/Z 방향으로 `-0.2 ~ +0.2` 블록만큼 이동한다.
오프셋은 래핑된 월드 좌표와 기존 배치 salt에서 결정적으로 계산한다.
저장된 블록 데이터, 충돌, 생성, 블록 정체성은 바뀌지 않는다.
현재 사용 블록은 `plant`, `stone_pile`, `large_stone_pile`, `branch`다.

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

현재 `plant`, `stone_pile` prop, `large_stone_pile` prop, `branch` prop, `fire`는 `bottom` 부착 블록이다.

## 방향성 랜덤 회전

구운 몰드 9종도 `bottom` 부착 블록이며, 설치 시점과 block tick에서 아래 지지 블록이 `collision = true`인지 확인한다.

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
fire  : 바닥 중심 기준 0.8 x 0.1 x 0.8 AABB
```

`cross`와 `prop`은 렌더링과 동일한 `randomOffset` 및 4방향 랜덤 회전을 적용해서 hit 판정을 한다.
알파 픽셀 단위 테스트는 하지 않는다.

선택 아웃라인도 렌더 타입별로 다르게 그린다.

```text
cube  : 기존 블록 박스
cross : X자 quad 2장의 edge
prop  : prop mesh의 local AABB에 렌더 변환을 적용한 bounds 박스
fire  : 바닥 중심 기준 0.8 x 0.1 x 0.8 bounds 박스
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
13    primal_workbench
14    wooden_box
15    fire
16    coal_ore
17    copper_ore
18    iron_ore
19    tin_ore
20    zinc_ore
21    silver_ore
22    gold_ore
23    dirt_slab
24    half_stripped_log
25    quarter_stripped_log
26    refractory_clay_crucible
27    dirt_half_slab
28    small_plate_mold
29    plate_mold
30    large_plate_mold
31    small_preform_mold
32    preform_mold
33    large_preform_mold
34    short_rod_mold
35    rod_mold
36    long_rod_mold
10000 plant
20000 stone_pile
20001 branch
20002 large_stone_pile
20003 stone_anvil
20004 stone_mortar
65535 bedrock
```

## 주요 속성

- `renderType`: `none`, `cube`, `cross`, `prop`, `fire`, `slab`, `half_slab`, `crucible`, `mold`
- `directional`: 방향성을 가지는지 여부
- `collision`: 플레이어 충돌 여부
- `ao`: 메싱 AO 적용 여부
- `faceOcclusion`: `none`, `opaque`, `cutout`
- `sameBlockFaceCulling`: 같은 블록끼리 면을 가릴지 여부
- `alphaMode`: `opaque`, `cutout`, `blend`
- `alphaCutoff`: cutout 기준값
- `alphaBlend`: blend 렌더링에서 텍스처 전체에 곱하는 alpha 값
- `breakEffects.particles`: 블록 파괴/제거 시 깨짐 파티클을 생성할지 여부
- `leafDecayable`: 주변 변화 tick에서 잎 decay 검사를 받을지 여부
- `leafDecaySupport`: 잎 decay 연결을 유지하는 support 블록인지 여부
- `mipDistanceScale`: mip 거리 배율
- `textures`: 면별 텍스처 매핑

`crucible`은 바닥 `1.0 x 0.2 x 1.0`과 네 벽으로 구성된 위가 열린 렌더 타입이다.
내부 빈 공간은 대략 `0.6 x 0.8 x 0.6`이며, 충돌/레이캐스트/선택 아웃라인은 전체 큐브가 아니라 바닥과 네 벽 AABB를 사용한다.
도가니는 장치성 블록이고 내부가 비어 있으므로 조명 전파에서는 일반 투명 통과값인 `lightAttenuation = 1`을 사용한다.

`mold`는 바닥에 놓는 낮은 몰드 렌더 타입이다.
한 셀 중앙의 `0.625 x 0.125 x 0.625` AABB를 선택/충돌 기준으로 사용한다.
구운 몰드 9종은 `collision = true`이며, 내부 홈 모양은 충돌에서 무시하고 배치된 몰드의 외곽 판 형태만 낮은 AABB로 처리한다.
렌더링은 cuboid가 아니라 아이템 몰드와 같은 alpha 기반 extruded sprite mesh를 사용한다.
`mold_bottom`을 아래 `0.0625` 높이 레이어로, 각 몰드의 `*_mold_top`을 위 `0.0625` 높이 레이어로 변환해 terrain mesh에 넣는다.
몰드 텍스처는 `32 x 32` 이미지 전체 UV를 사용하며, 실제 보이는 평면과 옆면은 중앙 `20 x 20` 영역의 alpha 실루엣에서 나온다.
구운 몰드 9종은 `attachment.face = "bottom"`을 사용하며, 설치 시점과 block tick에서 아래 지지 블록이 `collision = true`인지 확인한다.

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

`fire`:

- 바닥에 붙는 컷아웃 스프라이트 불이다.
- 중앙 X자 2장과 바닥 네 변에서 중심으로 30도 기울어진 쿼드 4장을 함께 방출한다.
- 중앙 X자는 plant와 같은 1블록 크기이며 random offset 없이 블록 중앙에 정렬한다.
- 네 변 쿼드는 각각 블록 한 면 크기이며, 아래쪽 edge는 바닥 네 변에 맞추고 위쪽 edge는 중심 방향으로 기울인다.
- 같은 네 변에서 안쪽으로 0.1블록 당긴 위치에 90도 수직 쿼드 4장을 추가로 방출해 옆면 실루엣을 보강한다.
- 텍스처는 `assets/textures/block/fire/fire_00.png`부터 `fire_13.png`까지 14프레임을 사용한다.
- 모든 `fire` 블록은 초당 12프레임의 같은 시간 기반 프레임을 사용해 동기화된 애니메이션으로 표시한다.
- 충돌은 없고, 선택/파괴 hit shape는 바닥 중심 기준 `0.8 x 0.1 x 0.8`이다.
- `lightAttenuation = 0`, `lightEmission = 15`로 정의한다.
- 남은 연소 시간은 블록 ID가 아니라 fire block entity에 저장한다.

## 텍스처 매핑

지원하는 텍스처 키:

- `all`
- `top`
- `bottom`
- `side`
- `topBottom`
- `verticalSection`
- `horizontalSection`

텍스처 파일은 `assets/textures/block/{texture}.png`에서 찾는다.
텍스처 이름에 `/`가 포함되면 `assets/textures/block/fire/fire_00.png`처럼 하위 폴더를 가리킬 수 있다.
수동 mip 파일은 `assets/textures/block/mip/*_mipN.png`를 우선 사용한다.
없는 mip은 실행 중 생성된다.

텍스처 값은 기존처럼 문자열을 사용할 수 있고, 마스크 합성이 필요한 경우 객체를 사용할 수 있다.

```json
{
  "textures": {
    "all": {
      "base": "rock",
      "mask": "mask/mask_iron_ore"
    }
  }
}
```

객체 텍스처는 로드 시점에 `assets/textures/block/{base}.png` 위에 `assets/textures/block/{mask}.png`를 알파 기준으로 합성한다.
결과 PNG는 `assets/textures/block/generated/` 아래에 생성하고, 기존 블록 texture array에는 생성된 텍스처 이름을 등록한다.
현재 광물 블록은 이 방식으로 `rock` 텍스처 위에 `assets/textures/block/mask/`의 광물 마스크를 올린다.

단면 텍스처는 블록을 가공한 아이템 모델에서 사용하는 재질 레이어다.
기본값은 `verticalSection = left`, `horizontalSection = up`이며, 별도로 지정하지 않으면 기존 6면 중 해당 면의 레이어를 사용한다.
단면 키는 문자열만 지원한다.
값이 예약된 면 이름이면 기존 6면 중 해당 면의 레이어를 참조하고, 그 외 문자열이면 `assets/textures/block/{value}.png` 텍스처를 직접 사용한다.
면 이름은 `up/top`, `down/bottom`, `left/west`, `right/east`, `front/south`, `back/north`를 지원한다.

```json
{
  "textures": {
    "topBottom": "stripped_log_topbottom",
    "side": "stripped_log_side",
    "verticalSection": "stripped_log_section_vertical"
  }
}
```

현재 `stripped_log`는 `verticalSection`에 `stripped_log_section_vertical`을 사용하고, `horizontalSection`은 기본값인 위쪽 면을 사용한다.

## 블록 저장 타입

런타임 청크의 블록 데이터는 `uint16_t` 블록 ID 배열이다.
청크 전체 크기는 `16 x 512 x 16`이므로 블록 수는 131072개다.

## 유체 데이터

유체 정의는 다음 파일에서 읽는 것을 기준으로 한다.

```text
assets/data/fluids.json
```

유체 정의는 현재 `id`, `name`, `lightAttenuation`, 선택적 `texture`를 가진다.

```json
{
  "id": 1,
  "name": "water",
  "texture": "water",
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
1000 molten_tin
1001 molten_zinc
1002 molten_silver
1003 molten_gold
1004 molten_copper
1005 molten_iron
```

유체 ID 범위는 다음 기준을 사용한다.

- `0`: 유체 없음
- `1~299`: 액체
- `300~511`: 기체
- `1000~1099`: 장치 내부 상태로만 쓰는 금속 용탕

런타임 유체 셀 데이터는 `uint16_t`로 표현한다.
상위 9비트는 유체 ID, 하위 7비트는 유체량이다.

- `id = 0`, `amount = 0`: 유체 없음
- `id = 1~511`: 유체 종류
- `amount = 1~100`: 유체량
- `amount = 100`: 가득 찬 상태
- `amount = 101~127`: 예약값

청크 유체 셀에 패킹되는 유체 ID는 상위 9비트 범위에 들어가는 흐르는 유체만 대상으로 한다.
`molten_*` 용탕은 도가니 block entity의 내부 상태로만 저장하며, 청크 유체 셀이나 유체 시뮬레이션에는 넣지 않는다.

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
20000 stone_pile
20001 branch
20002 large_stone_pile
```

소품 블록은 `prop.model`로 모델을 선택하고, `prop.texture`로 블록 텍스처 이름 하나를 선택한다.
소스 모델은 `assets/textures/block/model/{model}.glb`에 저장한다.
런타임/캐시 모델은 `assets/textures/block/model/{model}.dpm`에 저장한다.
텍스처는 `assets/textures/block/{texture}.png`에 저장한다.
소품 모델 UV는 일반 블록과 같은 텍셀 밀도를 기준으로 맞춘다.
현재 블록 텍스처는 `32 x 32`이므로 모델의 `16` Blockbench unit, 즉 `1` 블록 길이가 UV `1.0`에 대응한다.
예를 들어 `0.375` 블록 폭의 stone_pile 면은 UV 폭 `0.375`에 맞추고, large_stone_pile을 구성하는 `0.5` 블록 폭 면은 UV 폭 `0.5`에 맞춘다.

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
  "name": "stone_pile",
  "renderType": "prop",
  "collision": false,
  "faceOcclusion": "none",
  "alphaMode": "opaque",
  "prop": {
    "model": "stone_pile",
    "texture": "rock"
  },
  "drops": [
    { "item": "small_stone", "min": 1, "max": 2, "chance": 1.0 },
    { "item": "stone", "min": 1, "max": 2, "chance": 1.0 }
  ]
}
```



`stone_anvil`과 `stone_mortar`는 플레이어가 설치하는 prop 블록이다. 둘 다 `collision = true`, `randomOffset = false`, `attachment.face = "bottom"`을 사용하고, 파괴 시 같은 이름의 아이템을 1개 드랍한다. prop 블록 충돌은 full block AABB가 아니라 해당 prop `.dpm` mesh의 local bounds를 렌더 변환과 같은 랜덤 회전/오프셋 규칙으로 world AABB화해서 판정한다. 아이템 슬롯 아이콘은 `block_model` 경로에서 해당 prop `.dpm` mesh를 고정 아이소메트릭 카메라로 투영해 생성한다.
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
