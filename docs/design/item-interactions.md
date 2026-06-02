# item-interactions

이 문서는 월드에 떨어진 아이템을 우클릭 액션으로 작업하는 아이템 상호작용 시스템을 정리한다.

## 입력 흐름

- 우클릭은 먼저 바라보는 월드 드랍 아이템이 `interactions.json`의 `target`과 일치하는지 확인한다.
- target 가능성이 있으면 `useActions`가 `placeActions`보다 우선하며, 손 아이템으로 가능한 후보가 없어도 설치 흐름으로 넘어가지 않는다.
- 우클릭을 유지하는 동안 원형 UI가 열리고 마우스 위치로 액션 또는 후보를 선택한다.
- 원형 UI가 처음 열릴 때는 액션 개수와 관계없이 선택 없음 상태이며, 중앙 `Cancel` 영역을 가리킨다.
- 가장 안쪽 원은 `Cancel`, 바깥 1링은 액션 영역, 그 바깥 1링은 선택 액션의 후보 영역이다.
- 액션 영역은 화면 위쪽을 시작각으로 시계방향 배치한다.
- 후보 영역은 선택 액션의 시작각부터 시계방향으로 배치하며, 후보가 여러 개이면 해당 액션 각도 구간의 중앙에 맞춰 나눈다.
- 후보는 아이콘만 표시하고, 중앙 라벨은 현재 선택한 액션 또는 후보 이름을 표시한다.
- 후보 하나가 여러 출력 아이템을 가지면 후보 아이콘 영역 안에 출력 아이콘들을 함께 표시한다.
- 우클릭을 떼면 선택 후보를 실행한다. `Cancel`, 빈 영역, `Esc`는 취소한다.
- UI가 열린 동안에도 `F2` 스크린샷 입력은 처리한다.

## 데이터

아이템이 수행 가능한 우클릭 작업은 아이템의 `useActions`에 둔다.
실제 조합은 `assets/data/recipes/interactions.json`에서 정의한다.
좌클릭 블록 파괴 동작은 `breakActions`로 분리한다.

```json
{
  "action": "chip",
  "target": "stone_flake",
  "candidates": ["stone_blade", "stone_scraper"]
}
```

문자열 후보는 기본적으로 결과 아이템 1개를 만든다.
레시피의 `min`과 `max`를 지정하면 선택한 후보 아이템의 생성 개수 범위를 정한다.

```json
{
  "action": "smash",
  "target": "stone_shard",
  "min": 1,
  "max": 2,
  "candidates": ["stone_flake"]
}
```

후보 하나가 여러 종류의 출력 아이템을 만들 수 있다.
이 경우 `candidates` 항목을 객체로 쓰고 `items`에 출력 아이템 목록을 둔다.

```json
{
  "action": "scrape",
  "target": "log",
  "candidates": [
    {
      "items": ["stripped_log", "bark_strip"]
    }
  ]
}
```

출력 아이템별로 개수를 다르게 주려면 `items` 안의 항목을 객체로 쓴다.

```json
{
  "action": "split",
  "target": "wooden_plank",
  "candidates": [
    {
      "items": [
        { "item": "long_wooden_stick", "min": 4, "max": 4 }
      ]
    }
  ]
}
```

현재 구현은 `items`와 같은 의미로 `outputs` 배열 이름도 허용한다.
레시피의 `targetCount`를 지정하면 실행 1회에 대상 드랍 아이템을 해당 개수만큼 소비한다.
생략하면 대상 아이템 1개를 소비한다.

## 실행 규칙

우클릭 해제 시 `Ctrl`이 눌려 있지 않으면 선택 후보를 1회만 처리한다.
우클릭 해제 시 `Ctrl`이 눌려 있으면 같은 후보를 가능한 만큼 반복 처리한다.
반복 처리 횟수는 대상 스택 개수, 레시피의 `targetCount`, 손 아이템의 남은 내구도 중 가능한 값으로 제한한다.
손에 든 아이템이 내구도를 가지지 않으면 반복 처리 때 대상 스택과 `targetCount`만으로 처리 횟수를 제한한다.
`handcraft`는 기본 손 액션으로 취급하며, 어떤 아이템을 들고 있어도 해당 아이템의 `useActions` 앞에 중복 없이 포함된다.
대상 스택 개수가 레시피의 `targetCount`보다 적어 1회 실행도 불가능한 후보는 UI에 계속 표시하되 해당 후보가 차지하는 바깥 링 구간을 빨간색으로 표시한다.
대상 스택이 전부 처리되면 첫 번째 출력 아이템 스택은 대상 드랍 엔티티를 직접 대체한다.
대상 스택이 일부만 처리되면 대상 드랍 엔티티의 원본 count를 남기고, 모든 출력 아이템은 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
출력 개수가 아이템의 `stackSize`를 넘으면 `stackSize` 이하의 여러 드랍 스택으로 나눠 생성한다.
두 번째 이후 출력 아이템은 모두 대상 위치 근처에 별도 드랍 아이템으로 생성한다.
변환된 결과 아이템과 추가 생성 아이템은 위쪽 속도와 회전을 받아 한 번 튀어오른다.
대상 아이템과 출력 아이템이 모두 내구도를 가지면 대상의 현재 내구도 비율을 출력 아이템에 적용하고, 소수점은 올림 처리한다.
레시피 실행에 성공하면 손에 든 아이템의 내구도를 실제 처리 횟수만큼 소비한다.
실행 실패 또는 취소 시 내구도는 소비하지 않는다.

## 현재 제작 흐름

```text
stone_shard + chip
-> stone_chopper

stone_shard + smash
-> stone_flake x1~2

stone_shard + grind
-> stone_pounder

stone_flake + chip
-> stone_blade / stone_scraper 후보

plant + scrape
-> plant_fiber

plant_fiber x2 + handcraft
-> plant_twine

plant_twine x2 + handcraft
-> long_plant_twine

short_plant_twine x2 + handcraft
-> plant_twine

plant_twine + cut
-> short_plant_twine x2

long_plant_twine + cut
-> plant_twine x2

log + scrape
-> stripped_log + bark_strip

stripped_log + split
-> wooden_plank x8

stripped_log + carve
-> primal_workbench

bough + carve
-> long_wooden_stick

branch + carve
-> wooden_stick

wooden_plank + split
-> long_wooden_stick x4

long_wooden_stick + cut
-> wooden_stick x2

wooden_stick + cut
-> short_wooden_stick x2

short_wooden_stick + carve
-> wooden_peg

primal_workbench area:
wooden_plank x2 + wooden_peg x2 + craft + pound
-> wooden_box

primal_workbench area:
wooden_stick x2 + plant_twine + craft
-> bow_drill

bow_drill + ignite on block
-> fire above target block
```

## 블록 작업대 상호작용

`interactActions`가 있는 블록을 우클릭하면 블록 액션이 기본 상호작용으로 열린다.
현재 `primal_workbench`는 `craft` 액션을 제공한다.
작업대의 재료 감지 영역은 블록 바로 위 `1 x 1 x 1` 공간이며, 이 영역과 AABB가 겹치는 드랍 아이템 스택을 재료 후보로 본다.
시야 레이캐스트가 `interactActions`가 있는 블록을 잡으면, 그 위의 드랍 아이템도 작업 재료로 다룰 수 있도록 블록 상호작용을 드랍 아이템 직접 상호작용보다 먼저 연다.

`craft`는 `handcraft` 레시피를 포함한다.
따라서 `plant_fiber` 2개를 작업대 위에 올려두고 `primal_workbench`를 우클릭하면 `plant_twine` 후보가 표시된다.
재료 수량이 부족한 후보는 기존 단일 아이템 상호작용과 동일하게 비활성 후보로 표시하고 실행하지 않는다.

플레이어가 든 아이템의 `useActions`도 블록 액션에 더해질 수 있다.
예를 들어 작업대가 `craft`를 제공하고 손에 든 도구가 `cut`을 제공하면, 해당 레시피 후보는 `craft + cut` 액션 구간으로 표시한다.
이 경우 액션 구간에는 두 액션 심볼을 함께 배치한다.
단, 블록 자체에 `interactActions`가 있으면 기본 우클릭은 블록 액션을 우선한다.
손에 든 아이템의 블록 대상 액션을 우선하려면 `Shift + 우클릭`을 사용한다.
현재 `bow_drill`의 `ignite`는 블록 대상 액션이며, 후보를 실행하면 대상 블록 바로 윗칸에 `fire` 블록을 설치한다.

현재 우클릭 작업 액션:

```text
stone_shard: chip, smash, grind
stone_flake: chip
stone_chopper: smash, split
stone_blade: cut, carve
stone_scraper: scrape, pierce
stone_pounder: pound, smash
bow_drill: ignite
base hand action: handcraft
```

현재 좌클릭 파괴 액션:

```text
stone_shard: smash
stone_chopper: chop, dig
stone_blade: cut
stone_pounder: smash
stone_flake, stone_scraper: 없음
```

관련 문서: [[ui]], [[player]], [[item-data]]
