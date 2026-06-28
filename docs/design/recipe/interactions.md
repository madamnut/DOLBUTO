# interactions

`interactions.json`은 플레이어가 직접 액션을 선택해서 즉시 실행하는 레시피를 정의한다.
손으로 수행하는 `handcraft`, 작업대의 `craft`, 도구의 `cut`, `carve`, `split`, `scrape` 같은 액션이 이 범주에 들어간다.

## 정의 파일

```text
assets/data/recipes/interactions.json
```

## 실행 기준

- 드랍 아이템 직접 상호작용은 바라보는 드랍 아이템의 `target`과 손에 든 아이템의 `components.useActions`를 기준으로 후보를 찾는다.
- `handcraft`는 기본 손 액션으로 취급하므로 어떤 아이템을 들고 있어도 사용할 수 있다.
- `primal_workbench`의 `craft`는 작업 영역 안의 드랍 아이템을 재료로 감지하며, `handcraft` 레시피도 포함해서 표시한다.
- `ingredients`가 있는 레시피는 작업대 같은 블록 작업 영역에서 처리한다.

## 현재 점토 흐름

```text
clay_pile x1 + handcraft/craft
-> unfired_clay_brick

dirt_pile x1 + handcraft/craft
-> dirt_half_slab

clay_pile x2 + handcraft/craft
-> unfired_clay_pot

clay_pile x1 + grog x1 + sand_pile x1 + craft
-> refractory_clay_pile

refractory_clay_pile x1 + handcraft/craft
-> unfired_refractory_clay_brick

refractory_clay_pile x4 + handcraft/craft
-> unfired_refractory_clay_crucible

refractory_clay_pile x2 + handcraft/craft
-> unfired_refractory_clay_small_crucible
-> unfired_mold_base

unfired_mold_base x1 + carve
-> unfired_small_plate_mold
-> unfired_plate_mold
-> unfired_large_plate_mold
-> unfired_small_preform_mold
-> unfired_preform_mold
-> unfired_large_preform_mold
-> unfired_short_rod_mold
-> unfired_rod_mold
-> unfired_long_rod_mold

clay_brick x1 + smash
-> grog x4

clay_pot x1 + smash
-> grog x8

torch + light on fire block
-> lit_torch held in hand

lit_torch + extinguish on block
-> torch held in hand
```

관련 문서: [[processings]], [[../item-interactions]], [[../item-data]]

## 신형 입력 기반 레시피

`craft`는 기존 `target + ingredients` 양식 외에 `inputs` 양식을 지원한다.
`inputs`는 제작에 필요한 입력을 동등하게 나열하며, 각 항목은 특정 `key` 또는 아이템 데이터의 `components` 조건을 가진다.
`as`는 해당 입력에 실제로 매칭된 스택을 결과 생성이나 `constraints`에서 참조할 이름이다.

```json
{
  "action": "craft",
  "inputs": [
    {
      "components": {
        "assemblyPart": {
          "part": "head"
        }
      },
      "count": 1,
      "as": "head"
    },
    {
      "components": {
        "assemblyPart": {
          "part": "binding"
        }
      },
      "count": 1,
      "as": "binding"
    },
    {
      "components": {
        "assemblyPart": {
          "part": "handle"
        }
      },
      "count": 1,
      "as": "handle"
    }
  ],
  "constraints": [
    {
      "op": "==",
      "left": "binding.components.assemblyPart.size",
      "right": "handle.components.assemblyPart.size"
    },
    {
      "op": "in",
      "left": "binding.components.assemblyPart.size",
      "right": "head.components.assemblyPart.allowedSizes"
    }
  ],
  "candidates": [
    [
      {
        "key": "head_binding_handle",
        "count": 1,
        "derive": {
          "type": "head_binding_handle",
          "head": "head",
          "binding": "binding",
          "handle": "handle"
        }
      }
    ]
  ]
}
```

`constraints`는 현재 `==`와 `in`을 지원한다.
`candidates`는 2차원 배열이며, 바깥 배열은 UI에서 선택 가능한 후보, 안쪽 배열은 해당 후보를 선택했을 때 동시에 나오는 출력 목록이다.

`derive.type = "head_binding_handle"`은 조립 도구 전용 결과 생성 규칙이다.
결과 도구 이름은 헤드 재료 표시명과 `head.type + size` 결과 도구명을 조합한다.
액션과 break level은 헤드에서 복사하고, 내구도는 헤드 내구도에 바인딩/핸들 재료 보정값을 곱한 뒤 헤드의 현재 내구도 비율을 승계한다.
렌더 텍스처는 `handle -> head -> binding` 순서로 합성한 `generated/composites/{head}__{binding}__{handle}__{size}` 텍스처를 사용한다.
