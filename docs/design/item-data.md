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
  "stackSize": 1,
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
  "useActions": []
}
```

필드 의미:

- `id`: 부호 없는 숫자 아이템 ID
- `key`: 안정적인 `snake_case` 아이템 키
- `name`: 플레이어에게 표시되는 이름
- `stackSize`: 최대 스택 개수. 현재 실제 아이템은 모두 `1`을 사용한다.
- `slotTexture`: 확장자를 제외한 인벤토리/핫바 슬롯 텍스처 이름
- `droppedRender.type`: 아이템이 월드에 떨어졌을 때 사용하는 렌더 타입
- `droppedRender.texture`: 월드 드랍 아이템 렌더 상태에서 사용하는 텍스처 이름
- `heldRender.type`: 플레이어가 아이템을 들었을 때 사용하는 렌더 타입
- `heldRender.texture`: 플레이어가 든 아이템 렌더 상태에서 사용하는 텍스처 이름
- `tags`: 이후 시스템을 위한 아이템 분류 태그
- `useActions`: 손에 들었을 때 수행 가능한 월드 상호작용 액션 키 목록

`id = 0`은 `none`용으로 예약한다.
실제 아이템은 `id = 1`부터 시작하며, 구체적으로 빈 구간을 남길 이유가 없으면 순차적으로 배정한다.
현재 모든 아이템의 초기 렌더 타입은 `extruded_sprite`다.

## 드랍 아이템 물리와 렌더링

드랍된 `extruded_sprite` 아이템은 전용 아이템 파이프라인을 통해 얇은 수평 월드 공간 3D 스프라이트 파생 메쉬로 렌더링한다.
현재 메쉬는 윗면/아랫면 스프라이트 면과 스프라이트 알파 경계에서 생성한 옆면을 사용한다.
현재 드랍 스프라이트와 드랍 물리 AABB는 같은 `0.68 x 0.05 x 0.68` 블록 크기를 사용한다.
드랍 아이템 런타임 위치는 아이템의 중앙 하단 접점이다.

드랍 생성은 파괴된 블록 중심 주변에서 시작한다.
드랍 아이템 생성 오프셋, 초기 속도, 공중 회전, 스핀은 런타임 랜덤 값을 사용하므로 던져지는 방향은 결정적이지 않다.
드랍 아이템 물리는 초당 20틱으로 처리하고, 렌더링은 이전 물리 위치와 현재 물리 위치 사이를 보간한다.
낙하 중에는 플레이어 지상 이동과 같은 중력값 및 수직 속도 공식을 사용한다: `velocityY -= gravity * dt`.
수평 감속, 바닥 충돌, 대략적인 옆면 충돌, X/Y/Z 렌더 회전은 아이템 전용으로 유지한다.
착지 후 드랍 아이템은 이동을 멈추고, 평평하게 놓이도록 X/Z 회전을 초기화하며, 랜덤 Y 회전은 유지한다.
나중에 지지하던 블록이 제거되면, 땅에 있던 아이템은 다음 아이템 물리 틱에서 다시 공중 상태가 되어 낙하한다.

드랍 아이템은 서로 병합되지 않는다.
월드 드랍 아이템 한 엔티티는 아이템 1개를 의미하며, 저장 payload의 `count`는 호환용으로 남기되 런타임에서는 `1`로 정규화한다.
드랍 아이템끼리 겹치면 이전 tick의 바닥 위치와 현재 바닥 위치를 비교해 위에서 아래 아이템의 top plane을 통과한 경우 먼저 아래 아이템 위에 착지시킨다.
아이템 위에 얹힌 아이템은 아래 아이템과 수평으로 충분히 겹쳐 있는 동안 지지된 것으로 보고, 아래 아이템이 사라지거나 위치가 어긋나면 다시 낙하한다.
그 외 명확한 위아래 겹침은 위쪽 아이템을 Y축으로 보정한다.
같은 높이의 수평 겹침은 두 아이템을 서로 밀어내지 않고, 움직이는 쪽이나 더 최근 생성된 쪽만 보정하고 수평 속도를 감쇠한다.
드랍 아이템끼리는 수평 속도를 서로 전달하지 않는다.
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
stone_shard.png
```

## 초기 아이템 초안

```json
[
  { "id": 0, "key": "none", "name": "None", "stackSize": 0, "slotTexture": "none", "droppedRender": { "type": "extruded_sprite", "texture": "none" }, "heldRender": { "type": "extruded_sprite", "texture": "none" }, "tags": [], "useActions": [] },

  { "id": 1, "key": "rock_chunk", "name": "Rock Chunk", "stackSize": 1, "slotTexture": "rock_chunk", "droppedRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "heldRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "tags": [], "useActions": [] },
  { "id": 2, "key": "dirt_pile", "name": "Dirt Pile", "stackSize": 1, "slotTexture": "dirt_pile", "droppedRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "tags": [], "useActions": [] },
  { "id": 3, "key": "sand_pile", "name": "Sand Pile", "stackSize": 1, "slotTexture": "sand_pile", "droppedRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "tags": [], "useActions": [] },

  { "id": 4, "key": "plant", "name": "Plant", "stackSize": 1, "slotTexture": "plant", "droppedRender": { "type": "extruded_sprite", "texture": "plant" }, "heldRender": { "type": "extruded_sprite", "texture": "plant" }, "tags": [], "useActions": [] },
  { "id": 5, "key": "plant_fiber", "name": "Plant Fiber", "stackSize": 1, "slotTexture": "plant_fiber", "droppedRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "tags": [], "useActions": [] },
  { "id": 6, "key": "plant_twine", "name": "Plant Twine", "stackSize": 1, "slotTexture": "plant_twine", "droppedRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "tags": [], "useActions": [] },
  { "id": 7, "key": "seed", "name": "Seed", "stackSize": 1, "slotTexture": "seed", "droppedRender": { "type": "extruded_sprite", "texture": "seed" }, "heldRender": { "type": "extruded_sprite", "texture": "seed" }, "tags": [], "useActions": [] },
  { "id": 8, "key": "grass_scrap", "name": "Grass Scrap", "stackSize": 1, "slotTexture": "grass_scrap", "droppedRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "heldRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "tags": [], "useActions": [] },

  { "id": 9, "key": "branch", "name": "Branch", "stackSize": 1, "slotTexture": "branch", "droppedRender": { "type": "extruded_sprite", "texture": "branch" }, "heldRender": { "type": "extruded_sprite", "texture": "branch" }, "tags": [], "useActions": [] },
  { "id": 10, "key": "bough", "name": "Bough", "stackSize": 1, "slotTexture": "bough", "droppedRender": { "type": "extruded_sprite", "texture": "bough" }, "heldRender": { "type": "extruded_sprite", "texture": "bough" }, "tags": [], "useActions": [] },
  { "id": 11, "key": "bark_strip", "name": "Bark Strip", "stackSize": 1, "slotTexture": "bark_strip", "droppedRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "heldRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "tags": [], "useActions": [] },
  { "id": 12, "key": "leaf", "name": "Leaf", "stackSize": 1, "slotTexture": "leaf", "droppedRender": { "type": "extruded_sprite", "texture": "leaf" }, "heldRender": { "type": "extruded_sprite", "texture": "leaf" }, "tags": [], "useActions": [] },
  { "id": 13, "key": "stone_shard", "name": "Stone Shard", "stackSize": 1, "slotTexture": "stone_shard", "droppedRender": { "type": "extruded_sprite", "texture": "stone_shard" }, "heldRender": { "type": "extruded_sprite", "texture": "stone_shard" }, "tags": [], "useActions": ["chip", "smash"] },
  { "id": 14, "key": "stone_flake", "name": "Stone Flake", "stackSize": 1, "slotTexture": "stone_flake", "droppedRender": { "type": "extruded_sprite", "texture": "stone_flake" }, "heldRender": { "type": "extruded_sprite", "texture": "stone_flake" }, "tags": [], "useActions": ["chip"] },
  { "id": 15, "key": "stone_chopper", "name": "Stone Chopper", "stackSize": 1, "slotTexture": "stone_chopper", "droppedRender": { "type": "extruded_sprite", "texture": "stone_chopper" }, "heldRender": { "type": "extruded_sprite", "texture": "stone_chopper" }, "tags": [], "useActions": ["smash"] },
  { "id": 16, "key": "stone_blade", "name": "Stone Blade", "stackSize": 1, "slotTexture": "stone_blade", "droppedRender": { "type": "extruded_sprite", "texture": "stone_blade" }, "heldRender": { "type": "extruded_sprite", "texture": "stone_blade" }, "tags": [], "useActions": ["cut"] },
  { "id": 17, "key": "stone_scraper", "name": "Stone Scraper", "stackSize": 1, "slotTexture": "stone_scraper", "droppedRender": { "type": "extruded_sprite", "texture": "stone_scraper" }, "heldRender": { "type": "extruded_sprite", "texture": "stone_scraper" }, "tags": [], "useActions": ["scrape", "pierce"] }
]
```

## 아이템 상호작용 후보

월드 상호작용 후보 초안은 다음 파일에 둔다.

```text
assets/data/interactions.json
```

이 파일은 손에 든 아이템의 `useActions`와 땅에 떨어진 대상 아이템을 기준으로 후보 아이템 목록을 제공한다.
현재 초안에서는 `held item` 조건을 별도로 쓰지 않는다.
손 아이템이 해당 `action`을 가지고 있고, 땅에 떨어진 아이템 key가 `target`과 일치하면 `candidates` 목록을 UI 후보로 표시한다.

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
    "action": "chip",
    "target": "stone_flake",
    "candidates": ["stone_blade", "stone_scraper"]
  },
  {
    "action": "scrape",
    "target": "plant",
    "candidates": ["plant_fiber"]
  }
]
```

현재 후보 선택 규칙은 단순 테스트 규칙이다.
우클릭을 유지하면 후보 UI에 가능한 액션 영역과 후보 아이템 영역이 표시된다.
마우스 위치의 중심 기준 거리와 각도에 따라 액션 또는 후보 아이템을 선택하고, 우클릭을 떼면 선택한 액션/후보 조합을 대상 드랍 아이템에 적용한다.
액션 영역은 화면 위쪽을 시작각으로 삼아 시계방향으로 나뉘고, 후보 아이템 영역은 선택된 액션 구역 안에서 그 액션의 시작각부터 시계방향으로 나뉜다.
후보 아이템은 아이콘만 표시하고, 선택 중인 액션 또는 후보 이름은 중앙 라벨로 표시한다.
기존 드랍 엔티티를 첫 번째 후보 아이템으로 직접 바꾼다.
레시피의 `min`/`max`가 2 이상을 허용하면 나머지 결과물은 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
변환된 후보 아이템은 접지 상태를 해제하고 위쪽 속도와 회전을 줘서 한 번 튀어오르게 한다.
손에 든 아이템은 소모하지 않는다.

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
- 실제 아이템의 `stackSize`는 `1`이어야 한다.
- `droppedRender.type`, `heldRender.type`은 유효한 아이템 렌더 타입이어야 한다.

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
드랍 아이템 엔티티는 소유 청크 payload에 `entityId`, 로컬 위치, 속도, 접지 플래그, `itemId`, `count`를 저장한다.
획득 진행 상태와 렌더 전용 회전/스핀은 저장하지 않는다.

## 런타임 인벤토리

현재 런타임 플레이어 인벤토리는 50개 슬롯을 가진다.
슬롯 인덱스 `0`부터 `9`까지는 핫바 슬롯이다.
인벤토리 화면은 50개 슬롯을 10열 5행으로 표시한다.
맨 아래 줄은 슬롯 `0`부터 `9`이고, 그 위의 줄들은 `10`부터 `19`, `20`부터 `29`, `30`부터 `39`, `40`부터 `49`다.

현재 획득 삽입 방식:

- 인벤토리 한 슬롯은 아이템 1개만 담는다.
- 슬롯 인덱스 `0`부터 `49` 순서로 빈 슬롯을 채운다.
- 50개 런타임 슬롯은 `saves/<world-name>/player.dat`에 저장한다.
- `Q`를 누르면 `ClientGameplayRuntime`이 현재 선택된 핫바 슬롯에서 아이템 1개를 드랍 아이템 엔티티로 버린다.
- `Shift + Q`도 현재 선택된 핫바 슬롯의 아이템 1개를 드랍 아이템 엔티티로 버린다.
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

## 현재 아이템 액션/내구도 필드

아이템의 우클릭 드랍 아이템 상호작용 액션은 `useActions`에 저장한다.
좌클릭 블록 파괴 액션은 `breakActions`에 저장한다.
`breakLevel`은 아이템 자체의 파괴 레벨이며, 블록의 `breakLevel`보다 낮으면 해당 블록을 파괴하지 못한다.
`maxDurability`가 0보다 크면 인스턴스별 `ItemStack.durability`를 사용하며, 새로 생성되는 아이템은 최대 내구도로 초기화한다.

현재 석기 아이템 기준:

```text
stone_shard: useActions chip/smash, breakActions smash, breakLevel 1, maxDurability 64
stone_flake: useActions chip
stone_chopper: useActions smash, breakActions chop/dig, breakLevel 1, maxDurability 64
stone_blade: useActions cut, breakActions cut, breakLevel 1, maxDurability 64
stone_scraper: useActions scrape/pierce
```

관련 문서: [[block-data]], [[save-load]], [[ui]]
