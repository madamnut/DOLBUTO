# 아이템 데이터

이 문서는 현재 아이템 데이터와 블록 드랍 형식을 정의한다.
런타임 아이템 로드, 블록 드랍 생성, 키 입력 기반 드랍 아이템 획득, 런타임 플레이어 인벤토리 삽입, 인벤토리 저장은 구현되어 있다.

## 정의 파일

아이템 정의는 하나의 JSON 파일을 사용한다.

```text
assets/data/items.json
```

정의 파일 파싱은 `src/data/DataLoaders.h/.cpp`의 `data::parseItemDefinitions`가 맡는다.
렌더러는 파싱된 아이템 정의를 받아 아이템 ID별 `ItemDefinition`, 아이템 텍스처 레이어, 드랍/인벤토리용 스프라이트 메쉬를 구성한다.

아이템 텍스처는 다음 위치에 저장한다.

```text
assets/textures/item/*.png
```

## 아이템 정체성

아이템은 블록과 별개의 데이터다.
`plant`라는 블록과 `Plant`라는 아이템은 서로 다른 데이터 엔트리다.
블록을 파괴하거나 상호작용하면 아이템 드랍이 생성될 수 있지만, 블록이 자동으로 그 아이템 자체가 되지는 않는다.

각 아이템은 정체성과 관련된 필드 세 개를 가진다.

- `id`: 런타임 시스템, 저장 데이터, 인벤토리, 해석된 드랍 결과에서 사용하는 숫자 기본 키
- `key`: 데이터 작성, 디버그 도구, 콘솔 명령, 조회 헬퍼에서 사용하는 안정적인 문자열 키
- `name`: UI와 로그에 표시되는 이름이며, 나중에 바뀔 수 있다.

`name`은 조회 키로 사용하지 않는다.

## JSON 형식

```json
{
  "id": 1,
  "key": "rock_chunk",
  "name": "Rock Chunk",
  "stackSize": 99,
  "slotTexture": "rock_chunk",
  "droppedRender": {
    "type": "extruded_sprite",
    "texture": "rock_chunk"
  },
  "heldRender": {
    "type": "extruded_sprite",
    "texture": "rock_chunk"
  },
  "tags": [],
  "components": {
    "useActions": ["ignite"],
    "breakActions": ["smash"],
    "breakLevel": 2,
    "durability": {
      "max": 64
    },
    "fuel": {
      "burnTimeTicks": 2400,
      "heatLevel": 2,
      "remainder": {
        "item": "ash",
        "count": 4
      }
    },
    "burnableLight": {
      "maxTicks": 2400,
      "lightEmission": 12,
      "burnoutItem": "ash",
      "burnoutCount": 1,
      "ticksOnlyWhileHeld": true
    },
    "placeable": {
      "block": "rock"
    }
  }
}
```

필드 의미:

- `id`: 부호 없는 숫자 아이템 ID
- `key`: 안정적인 `snake_case` 아이템 키
- `name`: 플레이어에게 표시되는 이름
- `stackSize`: 최대 스택 개수. 현재 내구도 없는 실제 아이템은 `99`, 내구도 있는 아이템은 `1`을 사용한다.
- `slotTexture`: 확장자를 제외한 인벤토리/핫바 슬롯 텍스처 이름. `slotRender`를 생략하거나 `sprite`로 둘 때 사용한다.
- `slotRender.type`: 인벤토리/핫바 슬롯 아이콘 렌더 타입. 현재 `sprite`, `block_model`을 사용한다.
- `slotRender.texture`: `sprite` 슬롯 아이콘에서 `slotTexture`를 대체할 텍스처 이름
- `droppedRender.type`: 아이템이 월드에 떨어졌을 때 사용하는 렌더 타입. 현재 `extruded_sprite`, `block_model`을 사용한다.
- `droppedRender.texture`: `extruded_sprite` 드랍 아이템 렌더 상태에서 사용하는 텍스처 이름
- `heldRender.type`: 플레이어가 아이템을 들었을 때 사용하는 렌더 타입. 현재 `extruded_sprite`, `block_model`을 사용한다.
- `heldRender.texture`: `extruded_sprite` 든 아이템 렌더 상태에서 사용하는 텍스처 이름
- `tags`: 이후 시스템을 위한 아이템 분류 태그
- `components`: 일부 아이템에만 붙는 선택 기능 묶음
- `components.useActions`: 손에 들었을 때 수행 가능한 월드 상호작용 액션 키 목록
- `components.breakActions`: 좌클릭 블록 파괴에 사용하는 액션 키 목록
- `components.breakLevel`: 아이템 자체의 파괴 레벨
- `components.durability.max`: 인스턴스별 내구도 최대값
- `components.fuel`: fire가 연료로 소비할 수 있는 아이템 속성
- `components.fuel.burnTimeTicks`: 불이 연료로 소비했을 때 더해지는 연소 tick 수
- `components.fuel.heatLevel`: 연료의 처리 온도 단계. `burnTimeTicks > 0`이고 생략하면 1로 처리한다.
- `components.fuel.remainder`: 연료 1개가 다 탄 뒤 드랍할 부산물. 현재는 `{ "item": "ash", "count": n }` 형태를 사용한다.
- `components.burnableLight`: 손에 들었을 때만 연소 시간이 흐르는 휴대 조명 상태 정의. 현재 `lit_torch`에 사용한다.
- `components.placeable.block`: 우클릭 설치로 배치할 블록 이름
- `modelBlock`: `block_model` 아이템 렌더링에 사용할 블록 이름. 생략하면 `components.placeable.block`을 사용한다.
- `modelShape`: `block_model` 아이템 렌더링 형상. 현재 `source`, `cube`, `slab`, `half_slab`, `quarter_log`, `crucible`을 사용한다.
- `modelTexture`: `block_model` 아이템이 블록 ID 대신 재질 텍스처만 직접 사용할 때 참조하는 block texture 이름

`id = 0`은 `none`용으로 예약한다.
실제 아이템은 `id = 1`부터 시작하며, 구체적으로 빈 구간을 남길 이유가 없으면 순차적으로 배정한다.
`block_model` 렌더 타입은 아이템의 `modelBlock`으로 지정된 블록의 텍스처 레이어를 사용한다.
`modelBlock`을 생략하면 설치 아이템처럼 `components.placeable.block`으로 지정된 블록을 사용한다.
아이템 데이터에는 별도 `block`이나 `viewModel` 필드를 두지 않는다.
`slotRender.type = "block_model"`도 같은 표시용 블록 텍스처를 사용한다.
콘텐츠 로딩 시 해당 블록의 위/옆면 텍스처를 합성해 `assets/textures/item/generated/{item_key}_slot.png` 아이콘을 만들고, UI는 기존 슬롯 이미지 경로처럼 이 생성 텍스처를 참조한다.
`modelShape`가 `source`이거나 생략된 상태에서 표시 대상 블록이 `renderType: "slab"`이면 슬롯/든 아이템/드랍 아이템의 블록 모델도 기본 bottom 반블럭 형태로 만든다.
`modelShape = "slab"`은 표시 대상 블록을 `1.0 x 0.5 x 1.0` 크기로 렌더링한다.
`modelShape = "half_slab"`은 표시 대상 블록을 `0.5 x 0.5 x 1.0` 크기로 렌더링한다.
`modelShape = "quarter_log"`는 반통나무를 수직으로 한 번 더 자른 `0.5 x 0.5 x 1.0` 크기로 렌더링한다.
`modelShape = "crucible"`은 바닥과 네 벽으로 구성된 위가 열린 도가니 형상으로 렌더링한다.
설치된 `dirt_half_slab`과 `quarter_stripped_log` 블록은 `renderType: "half_slab"`과 `stateKind: "attach_grid"`를 사용해 같은 크기의 배치 가능 조각으로 처리한다.
`quarter_log`의 새로 생긴 수직 절단면 한쪽은 `modelBlock` 블록 재질의 `verticalSection` 레이어를 전체 UV로 사용하고, 나머지 바깥 옆면은 기존 side 텍스처의 아래 절반을 사용한다.

현재 `dirt_pile`은 직접 설치하지 않는다.
`primal_workbench`의 `craft` 작업으로 `dirt_pile` 4개를 `packed_dirt` 1개로 만들고, `packed_dirt`를 설치하면 `dirt` 블록이 된다.
`dirt_pile` 2개는 `dirt_slab` 1개로 만들며, `dirt_slab`은 설치 면에 붙는 반블럭 아이템이다.
`dirt_pile` 1개는 `dirt_half_slab` 1개로 만들며, `dirt_half_slab`은 `half_slab` 배치 규칙을 쓰는 흙 조각 아이템이다.

## 연료 아이템

`components.fuel.burnTimeTicks`는 아이템 1개가 불에 소모될 때 fire 블록 엔티티의 남은 연소 시간에 더해지는 값이다.
`components.fuel.heatLevel`은 해당 연료가 낼 수 있는 처리 온도 단계다. `burnTimeTicks > 0`이고 `heatLevel`을 생략하면 런타임에서 1로 처리한다.
현재는 0레벨 연료를 구분하지 않으며, 실제 연료 단계는 1부터 시작한다.
`heatLevel = 1`은 일반 연료, `heatLevel = 2`는 도기 소성이 가능한 고열 연료로 사용한다.
`components.fuel.remainder`는 해당 연료로 추가된 연소 시간이 끝나는 시점에 드랍되는 부산물이다.
재 생성량은 런타임 계산식이 아니라 아이템 데이터에 명시한다.
현재 게임 시간은 초당 20틱 기준이며, 연료로 쓰는 아이템은 최소 100틱 이상을 사용한다.
가공 아이템은 원재료 합보다 조금 낮은 값을 가진다.
불은 같은 셀 영역에 있는 연료 아이템 중 하나를 무작위로 소비한다.
`charcoal`은 하드코딩된 후순위 연료이며, 다른 연료가 없을 때만 무작위 소비 후보에 들어간다.

## 휴대 조명 아이템

`torch`와 `lit_torch`는 별도 아이템으로 관리한다.
꺼진 `torch`는 `components.useActions`의 `light` 액션을 가지고, fire 블록에 적용하면 손에 든 아이템이 `lit_torch`로 교체된다.
켜진 `lit_torch`는 `components.useActions`의 `ignite`, `extinguish` 액션을 가진다.
`ignite`를 일반 블록에 적용하면 기존 점화 규칙처럼 대상 블록 위에 fire를 만들고, `extinguish`를 블록에 적용하면 손에 든 아이템이 `torch`로 교체된다.
`lit_torch`는 `components.burnableLight.maxTicks`만큼 잔여 연소 시간을 가진다.
이 값은 `ItemStack.burnTicksRemaining`으로 저장되며, 선택된 오른손 핫바 슬롯이나 왼손 슬롯에 들려 있을 때만 1틱씩 감소한다.
잔여 시간이 0이 되면 `components.burnableLight.burnoutItem`과 `burnoutCount`에 따라 현재 스택이 교체된다.
현재 `lit_torch`는 2400틱 동안 타고, 다 타면 `ash` 1개가 된다.
`components.burnableLight.lightEmission`은 휴대 광원 렌더링에 사용할 데이터 값이다.
선택 핫바 슬롯 또는 왼손 슬롯에 `portableLightEmission > 0`인 아이템이 있으면, 렌더 프레임은 가장 큰 emission 값을 카메라 위치 기준 다이나믹 라이트로 전달한다.
이 휴대 광원은 월드 조명 데이터, 청크 조명, 저장 데이터에는 반영하지 않는 렌더링 전용 효과다.

현재 연료 값:

```text
item                                               fuel.burnTimeTicks  fuel.heatLevel  fuel.remainder
plant, plant_fiber, grass_scrap, leaf, bark_strip  100            1          ash x1
short_plant_twine                                  100            1          ash x1
plant_twine                                        160            1          ash x1
long_plant_twine                                   256            1          ash x1
branch                                             300            1          ash x1
short_wooden_stick                                 120            1          ash x1
wooden_stick                                       240            1          ash x1
long_wooden_stick                                  800            1          ash x2
bough                                              1000           1          ash x2
log                                                2000           1          ash x4
stripped_log                                       1800           1          ash x3
half_stripped_log                                  900            1          ash x2
quarter_stripped_log                               450            1          ash x1
wooden_plank                                       225            1          ash x1
wooden_peg                                         100            1          ash x1
charcoal, coal                                     2400           2          ash x4
```

## Fire Processing

fire 셀에 있는 아이템은 연료로 소비될 수 있고, fire 중심 같은 Y층 `3 x 3` 작업 공간 안에서 fire 셀을 제외한 BFS 내부 셀의 아이템은 [[recipe/processings]] 대상이 될 수 있다.
`pyrolysis`는 leak이 없는 밀폐 작업 공간에서 진행되며, 현재 `log`, `stripped_log`, `half_stripped_log`, `quarter_stripped_log`를 `charcoal`로 변환한다.
`bark_strip`은 같은 `pyrolysis` 처리에서 `tar` 1개로 변환된다.
`firing`은 leak이 정확히 1개인 작업 공간에서 `heatLevel >= 2` 연료를 소비했을 때 진행되며, 현재 굽기 전 점토 아이템을 구운 결과물로 변환한다.
둘 다 처리 시간은 600틱이다.
`grog`는 구운 점토를 잘게 부순 내화 보강재이며, `clay_brick`을 `smash`하면 4개, `clay_pot`을 `smash`하면 8개를 얻는다.

## 드랍 아이템 물리와 렌더링

드랍된 `extruded_sprite` 아이템은 전용 아이템 파이프라인을 통해 얇은 수평 월드 공간 3D 스프라이트 파생 메쉬로 렌더링한다.
현재 메쉬는 윗면/아랫면 스프라이트 면과 스프라이트 알파 경계에서 생성한 옆면을 사용한다.
현재 드랍 스프라이트와 기본 드랍 물리 AABB는 같은 `0.4 x 0.05 x 0.4` 블록 크기를 사용한다.
드랍된 `block_model` 아이템은 `modelBlock` 또는 fallback `components.placeable.block` 블록의 6면 텍스처를 사용하는 작은 블록 mesh로 렌더링하며, 기본 렌더 크기와 기본 물리 AABB는 모두 `0.2 x 0.2 x 0.2`다.
표시 대상이 `slab`이거나 `modelShape`가 `slab`, `quarter_log`이면 해당 X/Y/Z 크기의 블록 모델을 사용하되 드랍 물리 AABB는 기존 `block_model` 기본값을 유지한다.
드랍 아이템 런타임 위치는 아이템의 중앙 하단 접점이다.

드랍 생성은 파괴된 블록 중심 주변에서 시작한다.
드랍 아이템 생성 오프셋, 초기 속도, 공중 회전, 스핀은 런타임 랜덤 값을 사용하므로 던져지는 방향은 결정적이지 않다.
드랍 아이템 물리는 초당 20틱으로 처리하고, 렌더링은 이전 물리 위치와 현재 물리 위치 사이를 보간한다.
낙하 중에는 플레이어 지상 이동과 같은 중력값 및 수직 속도 공식을 사용한다: `velocityY -= gravity * dt`.
수평 감속, 바닥 충돌, 옆면 충돌, X/Y/Z 렌더 회전은 아이템 전용으로 유지한다.
지형 충돌은 드랍 아이템의 실제 AABB와 블록의 `collision`, `renderType`, `blockStates`를 비교해 처리하므로 반블럭은 붙은 면의 반칸 범위만 충돌한다.
착지 후 드랍 아이템은 이동을 멈추고, 평평하게 놓이도록 X/Z 회전을 초기화하며, 랜덤 Y 회전은 유지한다.
나중에 지지하던 블록이 제거되면, 땅에 있던 아이템은 다음 아이템 물리 틱에서 다시 공중 상태가 되어 낙하한다.

드랍 아이템은 같은 `itemId`이고 획득 중이 아니며 대상 스택에 여유가 있을 때 주변 스택으로 병합될 수 있다.
병합 판정은 X/Y/Z 각 축 차이가 모두 0.75블록 이하인 경우로 처리한다.
병합은 물리 tick 끝에서 수행하며, 병합을 받은 대상 스택은 접지 상태를 해제하고 위쪽 속도를 최소 `2.0`으로 만들어 살짝 튀어오르게 한다.
월드 드랍 아이템 한 엔티티는 하나의 아이템 스택을 의미하며, 저장 payload의 `count`는 로드 시 아이템의 `stackSize` 이하로 정규화한다.
드랍 아이템끼리 겹치면 이전 tick의 바닥 위치와 현재 바닥 위치를 비교해 위에서 아래 아이템의 top plane을 통과한 경우 먼저 아래 아이템 위에 착지시킨다.
아이템 위에 얹힌 아이템은 아래 아이템과 수평으로 충분히 겹쳐 있는 동안 지지된 것으로 보고, 아래 아이템이 사라지거나 위치가 어긋나면 다시 낙하한다.
그 외 명확한 위아래 겹침은 위쪽 아이템을 Y축으로 보정한다.
같은 높이의 수평 겹침은 두 아이템을 서로 밀어내지 않고, 움직이는 쪽이나 더 최근 생성된 쪽만 보정하고 수평 속도를 감쇠한다.
드랍 아이템끼리는 수평 속도를 서로 전달하지 않는다.
드랍 아이템 렌더링은 아이템별 정적 extruded mesh와 드랍 엔티티별 instance data를 사용한다.
병합된 스택은 데이터상 하나의 엔티티지만, 렌더링에서는 count에 따라 1~4개의 겹친 아이템으로 표시한다.
시각 복제본 수는 count `1`, `2~16`, `17~48`, `49~99` 구간에 따라 각각 1, 2, 3, 4개다.
스택 드랍 아이템의 물리 AABB 높이도 같은 복제본 수를 사용한다.
즉 렌더링에서 2~4단으로 쌓여 보이는 스택은 충돌 두께도 기본 높이의 2~4배가 된다.
`block_model` 드랍 아이템도 같은 복제본 구간과 오프셋 규칙을 사용한다.
옆면은 불투명 스프라이트 픽셀이 투명 이웃이나 텍스처 경계에 닿는 위치에만 생성한다.
같은 방향의 인접 옆면 경계는 렌더링 전에 span으로 병합하므로, 드랍 아이템은 스프라이트 실루엣을 유지하면서도 경계 픽셀마다 옆면 쿼드를 만들지 않는다.
옆면 UV는 정확한 알파 경계가 아니라 불투명 픽셀 중심을 샘플링해 투명 가장자리 필터링 아티팩트를 피한다.

## 이름 규칙

아이템 키와 텍스처 이름은 다음 규칙을 사용한다.

- `key`: 소문자 `snake_case`
- `texture`: 소문자 `snake_case`
- 텍스처 파일: `{texture}.png`
- `name`: 공백과 대소문자를 사용하는 읽기 쉬운 표시 이름

초기 소스 스프라이트는 소문자 `snake_case` 파일명을 사용한다.

```text
bark_strip.png
bough.png
branch.png
charcoal.png
ash.png
clay_brick.png
clay_pile.png
clay_pot.png
coal.png
dirt_pile.png
grog.png
grass_scrap.png
leaf.png
long_wooden_stick.png
long_plant_twine.png
plant_fiber.png
plant_twine.png
plant.png
raw_copper.png
raw_gold.png
raw_iron.png
refractory_clay_brick.png
refractory_clay_pile.png
raw_silver.png
raw_tin.png
raw_zinc.png
rock_chunk.png
sand_pile.png
seed.png
short_plant_twine.png
short_wooden_stick.png
stone_pounder.png
stone_shard.png
tar.png
unfired_clay_brick.png
unfired_clay_pot.png
unfired_refractory_clay_brick.png
wood_shavings.png
wooden_peg.png
wooden_plank.png
wooden_stick.png
```

점토/가공 재료 계열 일반 아이템은 `extruded_sprite` 렌더 타입과 `stackSize = 99`를 사용한다.
도가니 아이템은 `block_model`, `modelShape = "crucible"`, `modelTexture`를 사용해 재질만 바꾼 도가니 형상으로 표시한다.

```text
clay_pile                              id 43
unfired_clay_brick                     id 44
clay_brick                             id 45
unfired_clay_pot                       id 46
clay_pot                               id 47
grog                                   id 48
refractory_clay_pile                   id 49
unfired_refractory_clay_brick          id 50
refractory_clay_brick                  id 51
ash                                    id 52
tar                                    id 53
wood_shavings                          id 54
unfired_refractory_clay_crucible       id 55
refractory_clay_crucible               id 56
dirt_half_slab                         id 57
```

## 현재 아이템 목록

현재 아이템 목록의 정본은 `assets/data/items.json`이다.
이 문서는 스키마와 주요 규칙을 설명하고, 개별 아이템의 전체 JSON 나열은 데이터 파일을 기준으로 확인한다.
아이템 기능 필드는 최상위에 두지 않고 `components` 아래에만 둔다.
## 아이템 상호작용 후보

월드 상호작용 후보 초안은 다음 파일에 둔다.

```text
assets/data/recipes/interactions.json
```

이 파일은 손에 든 아이템의 `components.useActions`와 땅에 떨어진 대상 아이템을 기준으로 후보 아이템 목록을 제공한다.
현재 초안에서는 `held item` 조건을 별도로 쓰지 않는다.
손 아이템이 해당 `action`을 가지고 있고, 땅에 떨어진 아이템 key가 `target`과 일치하면 `candidates` 목록을 UI 후보로 표시한다.
`handcraft`는 기본 손 액션으로 취급하며, 어떤 아이템을 들고 있어도 해당 아이템의 `components.useActions` 앞에 중복 없이 포함된다.
`targetCount`는 상호작용 1회에 소비할 대상 드랍 아이템 개수이며, 생략하면 `1`이다.
`ingredients`는 작업대 영역에서 함께 소비할 추가 재료 목록이며, 항목은 `{ "item": "<key>", "count": <n> }` 형식으로 쓴다.
추가 재료가 있는 레시피는 단일 드랍 아이템 직접 상호작용이 아니라 `primal_workbench` 같은 블록 작업 영역에서 처리한다.
`candidates`의 항목은 단일 아이템 key 문자열이거나, 여러 출력 아이템을 묶은 객체일 수 있다.
블록 대상 레시피는 `targetBlock`을 사용한다. `"*"`이면 공기가 아닌 모든 블록을 대상으로 보며, 후보의 `{ "block": "<block>", "placement": "above_target" }`는 대상 블록의 윗칸에 블록을 설치하는 결과를 뜻한다.

```json
[
  {
    "action": "chip",
    "target": "stone_shard",
    "candidates": ["stone_chopper"]
  },
  {
    "action": "smash",
    "target": "stone_shard",
    "min": 1,
    "max": 2,
    "candidates": ["stone_flake"]
  },
  {
    "action": "grind",
    "target": "stone_shard",
    "candidates": ["stone_pounder"]
  },
  {
    "action": "chip",
    "target": "stone_flake",
    "candidates": ["stone_blade", "stone_scraper"]
  },
  {
    "action": "scrape",
    "target": "plant",
    "candidates": ["plant_fiber"]
  },
  {
    "action": "handcraft",
    "target": "plant_fiber",
    "targetCount": 2,
    "candidates": ["plant_twine"]
  },
  {
    "action": "handcraft",
    "target": "plant_twine",
    "targetCount": 2,
    "candidates": ["long_plant_twine"]
  },
  {
    "action": "handcraft",
    "target": "short_plant_twine",
    "targetCount": 2,
    "candidates": ["plant_twine"]
  },
  {
    "action": "cut",
    "target": "plant_twine",
    "min": 2,
    "max": 2,
    "candidates": ["short_plant_twine"]
  },
  {
    "action": "cut",
    "target": "long_plant_twine",
    "min": 2,
    "max": 2,
    "candidates": ["plant_twine"]
  },
  {
    "action": "scrape",
    "target": "log",
    "candidates": [
      {
        "items": ["stripped_log", "bark_strip"]
      }
    ]
  },
  {
    "action": "split",
    "target": "stripped_log",
    "candidates": [
      { "item": "half_stripped_log", "min": 2, "max": 2 }
    ]
  },
  {
    "action": "split",
    "target": "half_stripped_log",
    "candidates": [
      { "item": "quarter_stripped_log", "min": 2, "max": 2 },
      { "item": "wooden_plank", "min": 4, "max": 4 }
    ]
  },
  {
    "action": "carve",
    "target": "stripped_log",
    "candidates": ["primal_workbench"]
  },
  {
    "action": "carve",
    "target": "quarter_stripped_log",
    "min": 2,
    "max": 2,
    "candidates": ["long_wooden_stick"]
  },
  {
    "action": "carve",
    "target": "bough",
    "candidates": ["long_wooden_stick"]
  },
  {
    "action": "carve",
    "target": "branch",
    "candidates": ["wooden_stick"]
  },
  {
    "action": "split",
    "target": "wooden_plank",
    "min": 4,
    "max": 4,
    "candidates": ["long_wooden_stick"]
  },
  {
    "action": "cut",
    "target": "long_wooden_stick",
    "min": 2,
    "max": 2,
    "candidates": ["wooden_stick"]
  },
  {
    "action": "cut",
    "target": "wooden_stick",
    "min": 2,
    "max": 2,
    "candidates": ["short_wooden_stick"]
  },
  {
    "action": "carve",
    "target": "short_wooden_stick",
    "candidates": ["wooden_peg"]
  },
  {
    "action": "pound",
    "target": "wooden_plank",
    "targetCount": 2,
    "ingredients": [
      { "item": "wooden_peg", "count": 2 }
    ],
    "candidates": ["wooden_box"]
  },
  {
    "action": "craft",
    "target": "wooden_stick",
    "targetCount": 2,
    "ingredients": [
      { "item": "plant_twine", "count": 1 }
    ],
    "candidates": ["bow_drill"]
  },
  {
    "action": "ignite",
    "targetBlock": "*",
    "candidates": [
      { "block": "fire", "placement": "above_target" }
    ]
  }
]
```

현재 후보 선택 규칙은 단순 테스트 규칙이다.
우클릭을 유지하면 후보 UI에 가능한 액션 영역과 후보 아이템 영역이 표시된다.
마우스 위치의 중심 기준 거리와 각도에 따라 액션 또는 후보 아이템을 선택하고, 우클릭을 떼면 선택한 액션/후보 조합을 대상 드랍 아이템에 적용한다.
액션 영역은 화면 위쪽을 시작각으로 삼아 시계방향으로 나뉘고, 후보 아이템 영역은 선택된 액션 구역 안에서 그 액션의 시작각부터 시계방향으로 나뉜다.
후보 아이템은 아이콘만 표시하고, 선택 중인 액션 또는 후보 이름은 중앙 라벨로 표시한다.
후보가 여러 출력 아이템을 가지면 후보 영역 안에 출력 아이콘들을 함께 표시한다.
대상 스택 개수가 레시피의 `targetCount`보다 적어 1회 실행할 수 없는 후보는 해당 후보가 차지하는 바깥 링 구간을 빨간색으로 표시한다.
우클릭 해제 시 `Ctrl`이 눌려 있지 않으면 선택 후보를 1회만 처리한다.
우클릭 해제 시 `Ctrl`이 눌려 있으면 대상 스택, 레시피의 `targetCount`, 손 아이템의 남은 내구도가 허용하는 만큼 반복 처리한다.
손에 든 아이템이 내구도를 가지지 않으면 `Ctrl` 반복 처리 때 대상 스택과 `targetCount`만으로 처리 횟수를 제한한다.
대상 스택이 전부 처리되면 기존 드랍 엔티티를 선택 후보의 첫 번째 출력 아이템 스택으로 직접 바꾼다.
대상 스택이 일부 남으면 기존 드랍 엔티티는 남은 원본 count를 유지하고, 결과물은 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
대상 드랍 아이템과 결과 아이템이 모두 내구도를 가지면 대상의 현재 내구도 비율을 결과 아이템에 적용하고, 결과 내구도는 올림 처리한다.
레시피 또는 후보 출력의 `min`/`max`가 2 이상을 허용하면 나머지 결과물은 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
출력 개수가 결과 아이템의 `stackSize`를 넘으면 여러 드랍 스택으로 나눠 생성한다.
후보의 두 번째 이후 출력 아이템도 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
변환된 후보 아이템은 접지 상태를 해제하고 위쪽 속도와 회전을 줘서 한 번 튀어오르게 한다.
손에 든 아이템은 소모하지 않지만, 내구도를 가지면 실제 처리 횟수만큼 내구도를 소비한다.

## 런타임 조회

로더는 다음 인덱스를 만든다.

```cpp
std::vector<ItemData> itemsById;
std::unordered_map<std::string, uint16_t> itemIdByKey;
```

로더 검증:

- 중복 `id`는 오류다.
- 중복 `key`는 오류다.
- `id = 0`, `key = "none"`이 없으면 오류다.
- 비어 있는 `key`, `name`은 오류다.
- `sprite` 슬롯 렌더는 비어 있지 않은 `slotTexture` 또는 `slotRender.texture`를 사용한다.
- `extruded_sprite` 렌더 타입은 비어 있지 않은 `droppedRender.texture`, `heldRender.texture`를 사용한다.
- `key`, `slotTexture`, `slotRender.texture`, `droppedRender.texture`, `heldRender.texture`는 소문자 `snake_case`를 사용해야 한다. 생성 슬롯 텍스처는 `generated/{item_key}_slot` 경로를 사용한다.
- 내구도 없는 실제 아이템의 `stackSize`는 `99`, 내구도 있는 아이템의 `stackSize`는 `1`이어야 한다.
- `droppedRender.type`, `heldRender.type`은 유효한 아이템 렌더 타입이어야 한다.
- `block_model` 렌더 타입은 `modelBlock`으로 렌더 대상 블록을 찾고, 생략 시 `components.placeable.block`을 사용한다.
- `slotRender.type = "block_model"`은 같은 표시 대상 블록으로 슬롯 아이콘 대상 블록을 찾는다.
- `modelTexture`가 있으면 `modelBlock/components.placeable.block` 대신 해당 block texture를 모든 면의 재질로 사용한다.
- 기능 필드 `useActions`, `breakActions`, `breakLevel`, `durability`, `fuel`, `burnableLight`, `placeable`은 모두 `components` 아래에만 둔다.
- `components.fuel.burnTimeTicks`는 생략 가능하며 음수 입력은 `0`으로 정규화한다.

## 블록 드랍

드랍 테이블은 `assets/data/blocks.json`의 블록 정의 안에 저장한다.
별도의 `drop_tables.json` 파일에는 저장하지 않는다.

블록 드랍 JSON은 작성 가독성을 위해 아이템 `key`를 사용한다.
로더는 `items.json` 로드 후 각 키를 아이템 ID로 해석한다.
런타임 시스템은 해석된 아이템 ID를 사용한다.

블록이 파괴되면 각 드랍 항목은 먼저 `chance`를 굴린다.
성공하면 최종 개수는 `min`부터 `max`까지의 균등 정수 랜덤 값이다.
최종 개수가 2개 이상이면 같은 스택 엔티티가 아니라 개별 드랍 아이템 엔티티를 여러 개 생성한다.
생성된 드랍 아이템은 `type = DroppedItem`인 청크 소유 `WorldEntity` 엔트리다.
일반 8블록 상호작용 범위 안의 드랍 아이템을 바라보며 `F`를 누르면 해당 아이템은 획득 상태로 표시된다.
아이템은 플레이어 높이 절반의 플레이어 콜라이더 중심을 향해 가속한다.
드랍 아이템 bounds가 플레이어 콜라이더에 닿으면, 공간이 있을 때 런타임 플레이어 인벤토리에 삽입된다.
삽입에 실패하면 드랍 아이템은 월드에 남는다.
드랍 아이템 엔티티는 소유 청크 payload에 `entityId`, 로컬 위치, 속도, 접지 플래그, `itemId`, `count`, `durability`, `burnTicksRemaining`, `processingTicks`, `processingType`을 저장한다.
`processingTicks`와 `processingType`은 [[recipe/processings]]의 시간 기반 처리 진행도이며, 같은 아이템이라도 내구도, 잔여 연소 시간, 진행도, 처리 종류가 다른 드랍 스택은 병합하지 않는다.
획득 진행 상태와 렌더 전용 회전/스핀은 저장하지 않는다.

## 런타임 인벤토리

현재 런타임 플레이어 인벤토리는 50개 슬롯을 가진다.
슬롯 인덱스 `0`부터 `9`까지는 핫바 슬롯이다.
왼손 슬롯은 50개 슬롯과 분리된 단일 슬롯이며, 인벤토리 화면에는 표시하지 않고 HUD 핫바 왼쪽에만 표시한다.
인벤토리 화면은 50개 슬롯을 10열 5행으로 표시한다.
맨 아래 줄은 슬롯 `0`부터 `9`이고, 그 위의 줄들은 `10`부터 `19`, `20`부터 `29`, `30`부터 `39`, `40`부터 `49`다.

현재 획득 삽입 방식:

- 인벤토리 한 슬롯은 내구도 없는 아이템을 최대 99개까지 담고, 내구도 있는 아이템은 1개만 담는다.
- 슬롯 인덱스 `0`부터 `49` 순서로 빈 슬롯을 채운다.
- 50개 런타임 슬롯과 왼손 슬롯은 `saves/<world-name>/player.dat`에 저장한다.
- `R`을 누르면 현재 선택된 핫바 슬롯과 왼손 슬롯을 교환한다.
- `Q`를 누르면 `ClientGameplayRuntime`이 현재 선택된 핫바 슬롯에서 아이템 1개를 드랍 아이템 엔티티로 버린다.
- `Ctrl + Q`를 누르면 현재 선택된 핫바 슬롯의 전체 스택을 드랍 아이템 엔티티로 버린다.
- 버린 아이템은 카메라 위치에서 시선 방향으로 0.5블록 앞에 생성되고, 시선 방향 속도와 약한 위쪽 속도를 받아 앞으로 튀어나간다.
- 빈 핫바 슬롯이거나 드랍 엔티티를 생성할 수 없는 경우에는 인벤토리를 차감하지 않는다.

인벤토리 UI 조작은 임시 커서 `ItemStack`을 사용한다.
커서 스택은 저장하지 않으며, 인벤토리 화면이 닫힐 때 런타임 인벤토리로 되돌린다.
인벤토리 툴팁은 `ItemStack`과 `ItemDefinition`의 현재 런타임 아이템 데이터를 표시한다. 인스턴스별 override는 아직 구현하지 않았다.

슬롯 위치 디버그 오버레이 코드는 유지하지만, 기본값으로 비활성화한다.

## 초기 드랍 초안

정확한 밸런스 값은 초안이다.

```json
{
  "rock": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 }
  ],
  "coal_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "coal", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "copper_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_copper", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "iron_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_iron", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "tin_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_tin", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "zinc_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_zinc", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "silver_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_silver", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "gold_ore": [
    { "item": "rock_chunk", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "raw_gold", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "dirt": [
    { "item": "dirt_pile", "min": 4, "max": 4, "chance": 1.0 }
  ],
  "sand": [
    { "item": "sand_pile", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "sandstone": [],
  "mud": [],
  "clay": [
    { "item": "clay_pile", "min": 4, "max": 4, "chance": 1.0 }
  ],
  "log": [
    { "item": "log", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "stripped_log": [
    { "item": "stripped_log", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "grass": [
    { "item": "dirt_pile", "min": 4, "max": 4, "chance": 1.0 },
    { "item": "grass_scrap", "min": 2, "max": 4, "chance": 1.0 },
    { "item": "seed", "min": 1, "max": 1, "chance": 0.05 }
  ],
  "plant": [
    { "item": "plant", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "stone": [
    { "item": "stone_shard", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "branch": [
    { "item": "branch", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "leaves": [
    { "item": "leaf", "min": 1, "max": 1, "chance": 1.0 }
  ]
}
```

## 현재 아이템 컴포넌트 필드

아이템의 우클릭 드랍 아이템 상호작용 액션은 `components.useActions`에 저장한다.
좌클릭 블록 파괴 액션은 `components.breakActions`에 저장한다.
우클릭 블록/오브젝트 설치 가능 여부는 `components.placeable` 존재로 판단하고, 실제 설치할 블록은 `components.placeable.block`으로 지정한다.
우클릭 시 바라보는 드랍 아이템이 `assets/data/recipes/interactions.json`의 `target`으로 등장하면 `components.useActions`가 `components.placeable`보다 우선한다.
이때 손 아이템으로 실행 가능한 후보가 없어도 설치는 시도하지 않는다.
블록에 `interactActions`가 있으면 블록 액션이 기본 우클릭 상호작용으로 우선한다. `Shift + 우클릭`은 손에 든 아이템의 블록 대상 `components.useActions`를 우선해, 예를 들어 `bow_drill`의 `ignite`로 대상 블록 윗칸에 `fire`를 만들 수 있다.
`components.breakLevel`은 아이템 자체의 파괴 레벨이며, 블록의 `breakLevel`보다 낮으면 해당 블록을 파괴하지 못한다.
`components.breakActions`와 `components.breakLevel`이 모두 비어 있는 아이템은 좌클릭 파괴에서 손과 동일하게 취급한다.
`components.durability.max`가 0보다 크면 인스턴스별 `ItemStack.durability`를 사용하며, 새로 생성되는 아이템은 최대 내구도로 초기화한다.
드랍 아이템 상호작용으로 내구도 있는 대상 아이템이 결과 아이템으로 변환되면, 대상 아이템의 현재 내구도 비율을 결과 아이템의 최대 내구도에 적용하고 소수점은 올림한다.

현재 설치 아이템 기준:

```text
rock_chunk: components.placeable.block rock
packed_dirt: components.placeable.block dirt
dirt_slab: components.placeable.block dirt_slab
dirt_half_slab: components.placeable.block dirt_half_slab, modelBlock dirt, modelShape half_slab
sand_pile: components.placeable.block sand
plant: components.placeable.block plant
branch: components.placeable.block branch
log: components.placeable.block log
stripped_log: components.placeable.block stripped_log
half_stripped_log: components.placeable.block half_stripped_log
quarter_stripped_log: components.placeable.block quarter_stripped_log
primal_workbench: components.placeable.block primal_workbench
wooden_box: components.placeable.block wooden_box
refractory_clay_crucible: components.placeable.block refractory_clay_crucible
```

`half_stripped_log`, `quarter_stripped_log`는 `modelBlock: "stripped_log"`와 `modelShape`를 사용해 표시 형태를 정하고, 각각 `slab`, `half_slab` 배치 블록으로 설치된다.
`dirt_half_slab`은 `modelBlock: "dirt"`, `modelShape: "half_slab"`을 사용해 흙 재질의 작은 조각으로 표시하고 `half_slab` 배치 블록으로 설치된다.

현재 석기 아이템 기준:

```text
stone_shard: components.useActions chip/smash/grind, components.breakActions smash, components.breakLevel 2, components.durability.max 64
stone_flake: components.useActions chip, components.durability.max 64
stone_chopper: components.useActions smash/split, components.breakActions chop/dig, components.breakLevel 2, components.durability.max 64
stone_blade: components.useActions cut/carve, components.breakActions cut, components.breakLevel 2, components.durability.max 64
stone_scraper: components.useActions scrape/pierce, components.durability.max 64
stone_pounder: components.useActions pound/smash, components.breakActions smash, components.breakLevel 2, components.durability.max 64
bow_drill: components.useActions ignite, components.durability.max 4
torch: components.useActions light
lit_torch: components.useActions ignite/extinguish, components.burnableLight
```

관련 문서: [[block-data]], [[save-load]], [[ui]]
