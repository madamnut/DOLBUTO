# processings

`processings.json`은 환경이나 장치 상태에 의해 시간 경과로 진행되는 레시피를 정의한다.
플레이어가 액션을 선택해 즉시 실행하는 `interactions.json`과 분리한다.

## 정의 파일

```text
assets/data/recipes/processings.json
```

## JSON 형식

```json
{
  "type": "firing",
  "input": "unfired_clay_brick",
  "output": "clay_brick",
  "outputCount": 1,
  "requiredTicks": 600
}
```

- `type`: processing 종류. 현재는 `firing`, `pyrolysis`를 사용한다.
- `input`: 처리 대상 아이템 key.
- `output`: 완료 후 변환될 아이템 key.
- `outputCount`: 완료 후 나오는 결과 개수. 생략하면 `1`이다.
- `requiredTicks`: 완료까지 필요한 tick 수. 현재 20 TPS 기준 `600`은 30초다.

## Firing

`firing`은 fire 중심 같은 Y층 `3 x 3` 작업 공간의 BFS leak이 정확히 1개이고, fire가 `components.fuel.heatLevel >= 2` 연료를 소비했을 때 시작되는 고온 소성 상태에서 진행된다.
처리 주체는 모든 드랍 아이템이 아니라 `firing` 상태의 fire block entity다.

```text
fire mode == firing
AND 대상 드랍 아이템이 fire 셀을 제외한 BFS 내부 작업 셀에 있음
AND 대상 아이템이 firing recipe input과 일치함
-> 드랍 아이템 엔티티의 processingTicks 증가
```

`pyrolysis`는 같은 작업 공간의 BFS leak이 0개인 완전 밀폐 fire에서 진행된다.
fire 셀 안의 아이템은 연료 후보이며 processing 대상에서 제외한다.

`processingTicks`가 `requiredTicks`에 도달하면 count가 1인 드랍 아이템 엔티티는 output item과 `outputCount`로 교체된다.
count가 2 이상이면 원본 스택 count를 1 줄이고, 같은 위치에 output item과 `outputCount`를 가진 새 드랍 엔티티를 만든다.
진행도와 processing 종류는 드랍 아이템 엔티티에 저장되며, 같은 아이템이라도 진행도 또는 processing 종류가 다른 드랍 스택은 병합하지 않는다.

현재 레시피:

```text
unfired_clay_brick -> clay_brick                       x1, 600 ticks
unfired_clay_pot -> clay_pot                           x1, 600 ticks
unfired_refractory_clay_brick -> refractory_clay_brick x1, 600 ticks
unfired_refractory_clay_crucible -> refractory_clay_crucible x1, 600 ticks
log -> charcoal                                        x4, 600 ticks
stripped_log -> charcoal                               x4, 600 ticks
half_stripped_log -> charcoal                          x3, 600 ticks
quarter_stripped_log -> charcoal                       x2, 600 ticks
bark_strip -> tar                                      x1, 600 ticks
```

관련 문서: [[interactions]], [[../block-data]], [[../save-load]]
