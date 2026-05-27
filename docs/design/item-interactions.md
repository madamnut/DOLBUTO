# item-interactions

이 문서는 손에 든 아이템으로 드랍 아이템에 수행하는 우클릭 상호작용의 현재 구현 기준을 정리한다.

## 입력 흐름

- 우클릭은 바라보는 드랍 아이템과 손에 든 아이템의 `useActions`를 기준으로 상호작용 후보를 찾는다.
- 후보가 있으면 HUD 후보 UI를 열고, 후보가 없으면 기존 블록 배치 흐름으로 넘어간다.
- 후보 UI가 열린 동안 우클릭을 유지하면 카메라 회전과 플레이어 이동을 멈추고 마우스 위치로 선택을 갱신한다.
- 가장 안쪽 원은 `Cancel` 영역이고, 바깥 1링은 액션 영역, 그 바깥 1링은 선택 액션의 후보 아이템 영역이다.
- 액션 구역은 화면 위쪽을 시작각으로 삼아 시계방향으로 각도를 나누며, 후보 아이템은 선택된 액션 구역 안에서 그 액션의 시작각부터 시계방향으로 배정한다.
- 원형 UI의 부채꼴 배경과 선택 하이라이트는 native Vulkan 렌더 패스가 그린다.
- RmlUi는 액션 심볼, 후보 아이템 아이콘, 중앙 라벨만 올린다.
- 액션 심볼과 후보 아이템 아이콘은 각 링 두께의 정중앙에 배치한다.
- 후보 아이템은 아이콘만 표시하고, 중앙 라벨은 현재 선택된 액션 이름 또는 후보 아이템 이름을 표시한다.
- 우클릭을 떼면 선택한 후보를 실행한다.
- 후보 없이 `Cancel` 영역이나 빈 영역에서 우클릭을 떼거나 `Esc`를 누르면 취소한다.
- 후보 UI가 열린 동안에도 `F2` 스크린샷 입력은 처리한다.

## 데이터

아이템의 `useActions`는 그 아이템을 손에 들었을 때 수행 가능한 우클릭 작업 목록이다.
실제 조합은 `assets/data/interactions.json`의 레시피가 결정한다.
좌클릭 블록 파괴용 동작은 `breakActions`로 분리한다.

```json
{
  "action": "chip",
  "target": "stone_flake",
  "candidates": ["stone_blade", "stone_scraper"]
}
```

레시피는 기본적으로 결과 아이템 1개를 만든다.
`min`과 `max`를 지정하면 결과 개수 범위를 정할 수 있다.
현재는 선택한 후보 아이템 하나를 대상으로 `min`부터 `max` 사이의 개수를 생성한다.

```json
{
  "action": "smash",
  "target": "stone_shard",
  "min": 1,
  "max": 2,
  "candidates": ["stone_flake"]
}
```

## 실행 규칙

선택된 레시피는 대상 드랍 아이템 엔티티를 첫 결과 아이템으로 직접 바꾼다.
결과 개수가 2개 이상이면 나머지 결과물은 대상 위치 근처에 별도 드랍 아이템 엔티티로 생성한다.
생성된 결과 아이템은 위쪽 속도와 회전을 받아 한 번 튀어오른다.
손에 든 아이템이 내구도를 가지면 레시피 실행 성공 시 내구도 1을 소비한다.
레시피 실행이 실패하거나 취소되면 내구도를 소비하지 않는다.

## 현재 석기 흐름

```text
stone_shard + chip
-> stone_chopper

stone_shard + smash
-> stone_flake x1~2

stone_flake + chip
-> stone_blade / stone_scraper 후보

plant + scrape
-> plant_fiber
```

현재 우클릭 액션은 다음 기준을 사용한다.

```text
stone_shard: chip, smash
stone_flake: chip
stone_chopper: smash
stone_blade: cut
stone_scraper: scrape, pierce
```

좌클릭 파괴 액션은 다음 기준을 사용한다.

```text
stone_shard: smash
stone_chopper: chop, dig
stone_blade: cut
stone_flake, stone_scraper: 없음
```

관련 문서: [[ui]], [[player]], [[item-data]]
