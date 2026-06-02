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
  "requiredTicks": 1200
}
```

- `type`: processing 종류. 현재는 `firing`을 사용한다.
- `input`: 처리 대상 아이템 key.
- `output`: 완료 후 변환될 아이템 key.
- `requiredTicks`: 완료까지 필요한 tick 수. 현재 20 TPS 기준 `1200`은 60초다.

## Firing

`firing`은 아래와 동서남북 5면이 막히고 위쪽이 열린 fire가 `heatLevel >= 2` 연료를 소비했을 때 시작되는 고온 소성 상태에서 진행된다.
처리 주체는 모든 드랍 아이템이 아니라 `firing` 상태의 fire block entity다.

```text
fire mode == firing
AND 대상 드랍 아이템이 fire 셀의 1 x 1 x 1 처리 영역 안에 있음
AND 대상 아이템이 firing recipe input과 일치함
-> 드랍 아이템 엔티티의 processingTicks 증가
```

`processingTicks`가 `requiredTicks`에 도달하면 해당 드랍 아이템 스택은 같은 count를 유지한 채 output 아이템으로 변환되고, 진행도는 0으로 초기화된다.
진행도는 드랍 아이템 엔티티에 저장되며, 같은 아이템이라도 진행도가 다른 드랍 스택은 병합하지 않는다.

현재 레시피:

```text
unfired_clay_brick -> clay_brick                  1200 ticks
unfired_clay_pot -> clay_pot                      1200 ticks
unfired_refractory_clay_brick -> refractory_clay_brick 1200 ticks
```

관련 문서: [[interactions]], [[../block-data]], [[../save-load]]
