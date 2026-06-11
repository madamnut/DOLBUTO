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
