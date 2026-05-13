# 아이템 데이터

이 문서는 현재 아이템 데이터와 블록 드랍 형식을 정의한다.
런타임 아이템 로드, 블록 드랍 생성, 키 입력 기반 드랍 아이템 획득, 런타임 플레이어 인벤토리 삽입은 구현되어 있다.
인벤토리 저장은 `player.dat`에 구현되어 있다.
손에 든 아이템 렌더링은 아직 구현하지 않았다.

## 정의 파일

아이템 정의는 하나의 JSON 파일을 사용한다.

```text
assets/data/items.json
```

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
  "actions": {}
}
```

필드 의미:

- `id`: 부호 없는 숫자 아이템 ID
- `key`: 안정적인 `snake_case` 아이템 키
- `name`: 플레이어에게 표시되는 이름
- `stackSize`: 최대 스택 개수
- `slotTexture`: 확장자를 제외한 인벤토리/핫바 슬롯 텍스처 이름
- `droppedRender.type`: 아이템이 월드에 떨어졌을 때 사용하는 렌더 타입
- `droppedRender.texture`: 월드 드랍 아이템 렌더 상태에서 사용하는 텍스처 이름
- `heldRender.type`: 플레이어가 아이템을 들었을 때 사용하는 렌더 타입
- `heldRender.texture`: 플레이어가 든 아이템 렌더 상태에서 사용하는 텍스처 이름
- `tags`: 이후 시스템을 위한 아이템 분류 태그
- `actions`: 이후 시스템을 위한 아이템 액션 파라미터

`id = 0`은 `none`용으로 예약한다.
실제 아이템은 `id = 1`부터 시작하며, 구체적으로 빈 구간을 남길 이유가 없으면 순차적으로 배정한다.
현재 모든 아이템의 초기 렌더 타입은 `extruded_sprite`다.
드랍된 `extruded_sprite` 아이템은 현재 전용 아이템 파이프라인을 통해 얇은 수평 월드 공간 3D 스프라이트 파생 메쉬로 렌더링한다.
손에 든 아이템 렌더링은 아직 구현하지 않았지만, 해당 렌더 데이터는 아이템 JSON과 런타임 데이터에서 별도로 유지한다.
현재 메쉬는 윗면/아랫면 스프라이트 면과 스프라이트 알파 경계에서 생성한 옆면을 사용한다.
현재 드랍 스프라이트의 바닥 면적은 `0.68 x 0.68` 블록이다.
드랍 아이템 런타임 위치는 아이템의 중앙 하단 접점이다.
드랍 생성은 파괴된 블록 중심 주변에서 시작한다. 블록 좌표는 블록의 중앙 하단점을 의미하므로 블록 중심은 `x, y + 0.5, z`다.
드랍 아이템 생성 오프셋, 초기 속도, 공중 회전, 스핀은 런타임 랜덤 값을 사용하므로 던져지는 방향은 결정적이지 않다.
드랍 아이템 물리는 초당 20틱으로 처리하고, 렌더링은 이전 물리 위치와 현재 물리 위치 사이를 보간한다.
낙하 중에는 플레이어 지상 이동과 같은 중력값 및 수직 속도 공식을 사용한다: `velocityY -= gravity * dt`.
수평 감속, 바닥 충돌, 대략적인 옆면 충돌, X/Y/Z 렌더 회전은 아이템 전용으로 유지한다.
착지 후 드랍 아이템은 이동을 멈추고, 평평하게 놓이도록 X/Z 회전을 초기화하며, 랜덤 Y 회전은 유지하고, 지형 표면에 떠오름 없이 놓인다.
나중에 지지하던 블록이 제거되면, 땅에 있던 아이템은 다음 아이템 물리 틱에서 다시 공중 상태가 되어 낙하한다.
드랍 아이템은 같은 `itemId`이고 획득 중이 아니며 대상 스택에 여유가 있을 때 주변 스택으로 병합될 수 있다.
병합 판정은 X/Y/Z 각 축 차이가 모두 0.75블록 이하인 경우로 처리한다.
병합은 생성 직후와 물리 tick 중 이동한 아이템에 대해 수행한다.
병합을 받은 대상 스택은 접지 상태를 해제하고 위쪽 속도를 최소 `2.0`으로 만들어 살짝 튀어오르게 한다.
병합된 스택은 데이터상 하나의 엔티티지만, 렌더링에서는 count에 따라 1~4개의 겹친 아이템으로 표시한다.
시각 복제본 수는 count `1`, `2~16`, `17~48`, `49~99` 구간에 따라 각각 1, 2, 3, 4개다.
드랍 아이템 렌더링은 아이템별 정적 extruded mesh와 드랍 엔티티별 instance data를 사용한다.
옆면은 불투명 스프라이트 픽셀이 투명 이웃이나 텍스처 경계에 닿는 위치에만 생성한다.
같은 방향의 인접 옆면 경계는 렌더링 전에 span으로 병합하므로, 드랍 아이템은 스프라이트 실루엣을 유지하면서도 경계 픽셀마다 옆면 쿼드를 만들지 않는다.
옆면 UV는 정확한 알파 경계가 아니라 불투명 픽셀 중심을 샘플링해 투명 가장자리 필터링 아티팩트를 피한다.

## 이름 규칙

아이템 키와 텍스처 이름은 다음 규칙을 사용한다.

- `key`: 소문자 `snake_case`
- `texture`: 소문자 `snake_case`
- 텍스처 파일: `{texture}.png`
- `name`: 공백과 대소문자를 사용하는 읽기 쉬운 표시 이름

예시:

```text
key      rock_chunk
name     Rock Chunk
texture  rock_chunk
file     assets/textures/item/rock_chunk.png
```

초기 소스 스프라이트는 소문자 `snake_case` 파일명을 사용한다.

```text
bark_strip.png
bough.png
branch.png
dirt_pile.png
grass_scrap.png
leaf.png
plant_fiber.png
plant_twine.png
plant.png
rock_chunk.png
sand_pile.png
seed.png
```

## 초기 아이템 초안

```json
[
  { "id": 0, "key": "none", "name": "None", "stackSize": 0, "slotTexture": "none", "droppedRender": { "type": "extruded_sprite", "texture": "none" }, "heldRender": { "type": "extruded_sprite", "texture": "none" }, "tags": [], "actions": {} },

  { "id": 1, "key": "rock_chunk", "name": "Rock Chunk", "stackSize": 99, "slotTexture": "rock_chunk", "droppedRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "heldRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "tags": [], "actions": {} },
  { "id": 2, "key": "dirt_pile", "name": "Dirt Pile", "stackSize": 99, "slotTexture": "dirt_pile", "droppedRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "tags": [], "actions": {} },
  { "id": 3, "key": "sand_pile", "name": "Sand Pile", "stackSize": 99, "slotTexture": "sand_pile", "droppedRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "tags": [], "actions": {} },

  { "id": 4, "key": "plant", "name": "Plant", "stackSize": 99, "slotTexture": "plant", "droppedRender": { "type": "extruded_sprite", "texture": "plant" }, "heldRender": { "type": "extruded_sprite", "texture": "plant" }, "tags": [], "actions": {} },
  { "id": 5, "key": "plant_fiber", "name": "Plant Fiber", "stackSize": 99, "slotTexture": "plant_fiber", "droppedRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "tags": [], "actions": {} },
  { "id": 6, "key": "plant_twine", "name": "Plant Twine", "stackSize": 99, "slotTexture": "plant_twine", "droppedRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "tags": [], "actions": {} },
  { "id": 7, "key": "seed", "name": "Seed", "stackSize": 99, "slotTexture": "seed", "droppedRender": { "type": "extruded_sprite", "texture": "seed" }, "heldRender": { "type": "extruded_sprite", "texture": "seed" }, "tags": [], "actions": {} },
  { "id": 8, "key": "grass_scrap", "name": "Grass Scrap", "stackSize": 99, "slotTexture": "grass_scrap", "droppedRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "heldRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "tags": [], "actions": {} },

  { "id": 9, "key": "branch", "name": "Branch", "stackSize": 99, "slotTexture": "branch", "droppedRender": { "type": "extruded_sprite", "texture": "branch" }, "heldRender": { "type": "extruded_sprite", "texture": "branch" }, "tags": [], "actions": {} },
  { "id": 10, "key": "bough", "name": "Bough", "stackSize": 99, "slotTexture": "bough", "droppedRender": { "type": "extruded_sprite", "texture": "bough" }, "heldRender": { "type": "extruded_sprite", "texture": "bough" }, "tags": [], "actions": {} },
  { "id": 11, "key": "bark_strip", "name": "Bark Strip", "stackSize": 99, "slotTexture": "bark_strip", "droppedRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "heldRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "tags": [], "actions": {} },
  { "id": 12, "key": "leaf", "name": "Leaf", "stackSize": 99, "slotTexture": "leaf", "droppedRender": { "type": "extruded_sprite", "texture": "leaf" }, "heldRender": { "type": "extruded_sprite", "texture": "leaf" }, "tags": [], "actions": {} }
]
```

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
- 비어 있는 `key`, `name`, `slotTexture`, `droppedRender.texture`, `heldRender.texture`는 오류다.
- `key`, `slotTexture`, `droppedRender.texture`, `heldRender.texture`는 소문자 `snake_case`를 사용해야 한다.
- 실제 아이템의 `stackSize`는 0이 아니어야 한다.
- `droppedRender.type`, `heldRender.type`은 유효한 아이템 렌더 타입이어야 한다.

## 블록 드랍

드랍 테이블은 `assets/data/blocks.json`의 블록 정의 안에 저장한다.
별도의 `drop_tables.json` 파일에는 저장하지 않는다.

블록 드랍 JSON은 작성 가독성을 위해 아이템 `key`를 사용한다.
로더는 `items.json` 로드 후 각 키를 아이템 ID로 해석한다.
런타임 시스템은 해석된 아이템 ID를 사용한다.

```json
{
  "id": 10000,
  "name": "plant",
  "renderType": "cross",
  "drops": [
    { "item": "plant", "min": 1, "max": 1, "chance": 1.0 },
    { "item": "plant_fiber", "min": 1, "max": 2, "chance": 0.35 },
    { "item": "seed", "min": 1, "max": 1, "chance": 0.05 }
  ]
}
```

드랍 항목 필드:

- `item`: 아이템 `key`
- `min`: 최소 드랍 개수
- `max`: 최대 드랍 개수
- `chance`: `0.0`부터 `1.0`까지의 확률

아이템 드랍이 없는 블록도 빈 `drops` 배열을 정의한다.

블록이 파괴되면 각 드랍 항목은 먼저 `chance`를 굴린다.
성공하면 최종 개수는 `min`부터 `max`까지의 균등 정수 랜덤 값이다.
생성된 드랍 아이템은 `type = DroppedItem`인 청크 소유 `WorldEntity` 엔트리다.
일반 8블록 상호작용 범위 안의 드랍 아이템을 바라보며 `F`를 누르면 해당 아이템은 획득 상태로 표시된다.
아이템은 플레이어 높이 절반의 플레이어 콜라이더 중심을 향해 가속한다.
드랍 아이템 bounds가 플레이어 콜라이더에 닿으면, 공간이 있을 때 런타임 플레이어 인벤토리에 삽입된다.
삽입에 실패하면 드랍 아이템은 남은 개수를 유지한 채 월드에 남는다.
드랍 아이템 엔티티는 소유 청크 payload에 `entityId`, 로컬 위치, 속도, 접지 플래그, `itemId`, `count`를 저장한다.
획득 진행 상태와 렌더 전용 회전/스핀은 저장하지 않는다.

## 런타임 인벤토리

현재 런타임 플레이어 인벤토리는 50개 슬롯을 가진다.
슬롯 인덱스 `0`부터 `9`까지는 핫바 슬롯이다.
인벤토리 화면은 50개 슬롯을 10열 5행으로 표시한다.
맨 아래 줄은 슬롯 `0`부터 `9`이고, 그 위의 줄들은 `10`부터 `19`, `20`부터 `29`, `30`부터 `39`, `40`부터 `49`다.

현재 획득 삽입 방식:

- 50개 슬롯 전체에서 같은 아이템 스택에 먼저 병합한다.
- 슬롯 인덱스 `0`부터 `49` 순서로 같은 아이템 스택을 채운다.
- 슬롯 인덱스 `0`부터 `49` 순서로 빈 슬롯을 채운다.
- 50개 런타임 슬롯은 `saves/<world-name>/player.dat`에 저장한다.
- `Q`를 누르면 현재 선택된 핫바 슬롯에서 아이템 1개를 드랍 아이템 엔티티로 버린다.
- `Shift + Q`를 누르면 현재 선택된 핫바 슬롯의 스택 전체를 드랍 아이템 엔티티로 버린다.
- 버린 아이템은 플레이어 전방 위치에서 생성되고, 시선 방향 속도와 약한 위쪽 속도를 받아 앞으로 튀어나간다.
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
    { "item": "rock_chunk", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "dirt": [
    { "item": "dirt_pile", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "sand": [
    { "item": "sand_pile", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "sandstone": [],
  "mud": [],
  "clay": [],
  "trunk": [],
  "grass": [
    { "item": "dirt_pile", "min": 1, "max": 1, "chance": 1.0 },
    { "item": "grass_scrap", "min": 1, "max": 1, "chance": 0.25 },
    { "item": "seed", "min": 1, "max": 1, "chance": 0.05 }
  ],
  "plant": [
    { "item": "plant", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "stone": [
    { "item": "rock_chunk", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "branch": [
    { "item": "branch", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "leaves": [
    { "item": "leaf", "min": 1, "max": 1, "chance": 1.0 }
  ]
}
```

관련 문서: [[block-data]], [[save-load]], [[ui]]
