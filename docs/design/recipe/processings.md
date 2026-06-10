# processings

processing recipe는 환경, 블록, 장치 상태에 의해 시간 경과로 진행되는 레시피를 정의한다.
플레이어가 액션을 선택해 즉시 실행하는 [[interactions]]와 분리한다.

## 정의 파일

processing recipe는 process key별 JSON 파일로 나눈다.
파일명의 확장자를 뺀 값이 process key가 된다.

```text
assets/data/recipes/processings/
  firing.json
  pyrolysis.json
  smelt.json
```

예를 들어 `pyrolysis.json`에 들어간 레시피는 로드 시 `type = "pyrolysis"`로 등록된다.
각 레시피 객체 안에 `type` 필드를 반복해서 적을 필요는 없다.

## JSON 형식

아이템을 다른 아이템으로 바꾸는 processing은 다음 형식을 사용한다.

```json
{
  "input": "log",
  "output": "charcoal",
  "outputCount": 4,
  "requiredTicks": 600
}
```

용탕처럼 아이템 대신 유체 상태를 증가시키는 processing은 다음 형식을 사용한다.

```json
{
  "input": "raw_copper",
  "outputFluid": "molten_copper",
  "outputAmount": 10,
  "requiredHeatLevel": 3,
  "requiredTicks": 200
}
```

- `input`: 처리 대상 아이템 key.
- `output`: 완료 후 바뀌는 아이템 key.
- `outputCount`: 완료 후 나오는 결과 아이템 개수. 생략하면 `1`이다.
- `outputFluid`: 완료 후 장치 내부에 더할 유체 key.
- `outputAmount`: 완료 후 더할 유체량.
- `requiredHeatLevel`: 처리에 필요한 최소 열 단계.
- `requiredTicks`: 완료까지 필요한 tick 수. 현재 20 TPS 기준 `200`은 10초, `600`은 30초다.

`output`과 `outputFluid` 중 하나는 있어야 한다.

## Firing

`firing`은 fire 중심 같은 Y층 `3 x 3` 작업 공간에서 fire 연결 내부 공간의 leak 방향이 정확히 1개이고, fire가 `components.fuel.heatLevel >= 3` 연료를 소비했을 때 진행되는 고온 소성 상태다.
처리 주체는 모든 열원 아이템이 아니라 `firing` 상태의 fire block entity다.

```text
fire mode == firing
AND 대상 드랍 아이템이 fire 자신을 제외한 BFS 내부 작업 공간에 있음
AND 대상 아이템이 firing recipe input과 일치함
-> 드랍 아이템 엔티티의 processingTicks 증가
```

현재 `firing.json` 레시피:

```text
unfired_clay_brick -> clay_brick                       x1, 600 ticks
unfired_clay_pot -> clay_pot                           x1, 600 ticks
unfired_refractory_clay_brick -> refractory_clay_brick x1, 600 ticks
unfired_refractory_clay_crucible -> refractory_clay_crucible x1, 600 ticks
```

## Pyrolysis

`pyrolysis`는 같은 작업 공간의 BFS leak이 0개인 완전 밀폐 fire에서 진행된다.
fire 셀 안의 아이템은 연료 후보이므로 processing 대상에서 제외한다.

현재 `pyrolysis.json` 레시피:

```text
log -> charcoal                  x4, 600 ticks
stripped_log -> charcoal         x4, 600 ticks
half_stripped_log -> charcoal    x3, 600 ticks
quarter_stripped_log -> charcoal x2, 600 ticks
bark_strip -> tar                x1, 600 ticks
```

## Smelt

`smelt`는 도가니 내부에 들어온 금속 원재료 아이템을 용탕으로 바꾸는 processing이다.
불은 위 블록에 열만 전달하고, 도가니가 자신의 내부 상태와 내부 드랍 아이템을 기준으로 처리한다.

도가니는 내부 용량 `100`을 가진다.
광물 원재료 1개는 완료 시 같은 금속 용탕 `10`을 더한다.
하나의 도가니에는 한 종류의 용탕만 담을 수 있다.
비어 있는 도가니는 처리 가능한 광물 중 필요한 열 단계가 가장 낮은 그룹을 고르고, 같은 단계 후보가 여러 개면 무작위로 하나를 진행한다.
이미 `smelt` 진행 중인 아이템이 있으면 그 진행을 우선 계속한다.

현재 `smelt.json` 레시피:

```text
raw_tin    -> molten_tin    +10, heat 2, 200 ticks
raw_zinc   -> molten_zinc   +10, heat 2, 200 ticks
raw_silver -> molten_silver +10, heat 3, 200 ticks
raw_gold   -> molten_gold   +10, heat 3, 200 ticks
raw_copper -> molten_copper +10, heat 3, 200 ticks
raw_iron   -> molten_iron   +10, heat 4, 200 ticks
```

## 진행 상태

진행 중인 processing 종류는 드랍 아이템 엔티티에 저장된다.
같은 아이템이라도 진행 중인 processing 종류가 다른 드랍 스택은 병합하지 않는다.

현재 `processingType` 값:

```text
0 none
1 pyrolysis
2 firing
3 smelt
```

`processingTicks`가 `requiredTicks`에 도달하면 count가 1인 드랍 아이템 엔티티는 결과로 교체되거나 제거된다.
count가 2 이상이면 원본 스택 count를 1 줄이고, item-to-item processing은 결과 드랍 엔티티를 새로 만들며, smelt processing은 도가니의 `moltenAmount`만 증가시킨다.

관련 문서: [[interactions]], [[../block-data]], [[../save-load]]
