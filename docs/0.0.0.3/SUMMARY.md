# 0.0.0.3 요약

## 전체 방향

- 0.0.0.3은 0.0.0.2에서 정리한 client runtime, world, gameplay, renderer 경계를 유지하면서 실제 생존 샌드박스 루프를 크게 확장한 버전이다.
- 주요 목표는 원시 제작 루프, 블록/아이템 상호작용, 드랍 아이템 물리, 불과 처리 레시피, 플레이어 이동/렌더링, 시각 효과를 게임 플레이 가능한 수준으로 넓히는 것이었다.
- 빌드는 기존 방침대로 사용자가 직접 수행한다.
- 저장 포맷이 여러 차례 확장되었으므로 0.0.0.3 검증은 새 월드 기준으로 진행하는 것을 전제로 한다.

## 아이템과 제작 루프

- 아이템 데이터는 `components` 기반으로 정리했다.
- 우클릭 작업 액션은 `components.useActions`, 좌클릭 파괴 액션은 `components.breakActions`, 설치는 `components.placeable`, 연료는 `components.fuel`, 휴대 연소 광원은 `components.burnableLight`로 분리했다.
- 아이템 상호작용 레시피는 `assets/data/recipes/interactions.json`, 시간 기반 처리 레시피는 `assets/data/recipes/processings.json`으로 분리했다.
- 드랍 아이템 또는 워크벤치 작업 영역에 있는 아이템을 대상으로 원형 상호작용 UI를 띄우고, 액션과 후보 결과를 선택하는 방식으로 제작을 진행한다.
- `handcraft`는 기본 손작업 액션으로 취급하고, `craft`는 워크벤치 블록의 작업 액션으로 `handcraft` 레시피도 포함한다.
- 석기 계열은 `stone_shard`, `stone_flake`, `stone_chopper`, `stone_blade`, `stone_scraper`, `stone_pounder`를 중심으로 정리했다.
- 나무 계열은 `log`, `stripped_log`, `half_stripped_log`, `quarter_stripped_log`, `wooden_plank`, 막대기 계열, `wooden_peg`, `wooden_box`, `primal_workbench`로 확장했다.
- 식물 섬유 계열은 `plant_fiber`, `plant_twine`, `short_plant_twine`, `long_plant_twine`의 조립/절단 흐름을 추가했다.
- 점토 계열은 `clay_pile`, `unfired_clay_brick`, `clay_brick`, `unfired_clay_pot`, `clay_pot`, `grog`, 내화 점토/벽돌/도가니 계열로 확장했다.
- 횃불은 `torch`와 `lit_torch` 두 아이템으로 분리하고, 남은 연소 시간은 `ItemStack.burnTicksRemaining`에 저장한다.

## 드랍 아이템

- 드랍 아이템은 다시 스택 병합을 지원하되, 내구도, 남은 연소 시간, processing 진행도와 종류가 다르면 병합하지 않는다.
- `extruded_sprite` 드랍 아이템은 물리 AABB와 렌더/상호작용 bounds를 분리했다.
- `extruded_sprite`의 물리 AABB는 작게 유지하고, 렌더링과 pickup/raycast 판정은 더 큰 render bounds를 사용한다.
- `block_model` 드랍 아이템과 손에 든 아이템은 블록 텍스처와 모델 형상을 재사용한다.
- 드랍 아이템이 물속에 있으면 물색 tint를 적용한다.
- 파괴 파티클과 연기 파티클도 물속이면 같은 물색 tint를 적용한다.
- 드랍 아이템은 블록 설치 시 새 블록과 겹치면 옆이나 위로 밀려난다.
- 드랍 아이템, 파티클, 플레이어 이동 충돌은 slab/half slab/crucible 같은 세부 블록 충돌 형상을 공유한다.

## 블록과 블록 상태

- `blockStates` 배열을 추가해 블록 ID와 별개인 셀별 상태를 저장한다.
- `renderType: "slab"`과 `stateKind: "attach"`로 위/아래/동서남북에 붙는 반블록을 지원한다.
- `renderType: "half_slab"`과 `stateKind: "attach_grid"`로 3x3 클릭 구역 기반의 더 작은 조각 배치를 지원한다.
- `dirt_slab`, `dirt_half_slab`, `half_stripped_log`, `quarter_stripped_log`를 추가했다.
- `crucible` 렌더 타입을 추가하고, 바닥과 네 벽 AABB로 구성된 위가 열린 도가니 형상을 지원한다.
- 단면 텍스처 개념으로 `verticalSection`, `horizontalSection`을 추가했다.
- 블록 선택/레이캐스트는 cube/cross/prop/fire/slab/half slab/crucible의 실제 hit shape를 사용한다.
- 블록 파괴는 `hardness`, `breakLevel`, `breakAction` 기준으로 도구 레벨과 액션 보정을 받는다.
- Sandbox 모드의 블록 파괴는 도구와 무관하게 즉시 처리하되, 누르고 있는 반복 파괴는 10틱마다 제한한다.
- 부착 블록은 `attachment.face = bottom` 기준으로 지지 블록이 사라지면 tick에서 일반 블록 파괴 흐름으로 제거된다.
- 잎 decay는 `leafDecayable`, `leafDecaySupport`와 6방향 BFS 깊이 4 기준으로 처리한다.

## 불, 연료, 처리 레시피

- `fire` 블록은 별도 `Fire` block entity를 가진다.
- fire block entity는 `remainingBurnTicks`, `fireMode`, `fireHeatLevel`, 연료 연소 후 부산물 정보를 저장한다.
- 불 모드는 `exposed`, `pyrolysis`, `firing` 세 가지다.
- 불 중심 `5 x 5 x 3` 감시 범위에서 6방향 flood fill로 내부 공간의 leak 방향 수를 계산한다.
- `leakCount == 0`이면 `pyrolysis`, `leakCount == 1`이고 현재 연료 `heatLevel >= 3`이면 `firing`, 그 외는 `exposed`가 된다.
- 연료는 fire 셀 안의 드랍 아이템 중 가장 높은 `heatLevel`을 우선 소비하고, 같은 heatLevel에서는 무작위로 고른다.
- 연료의 `components.fuel.remainder`는 해당 연료로 추가된 연소 시간이 끝났을 때 드랍된다.
- 현재 목질 연료는 주로 `heatLevel = 2`, `coal`과 `charcoal`은 `heatLevel = 3`이다.
- fire 셀은 연료 소비 공간이고, 같은 Y층 `3 x 3` 내부 작업 공간은 processing 대상 공간이다.
- `pyrolysis`와 `firing`은 5틱마다 작업 공간의 드랍 아이템 `processingTicks`를 누적하고, `requiredTicks`에 도달하면 아이템을 변환한다.
- `pyrolysis`는 통나무/껍질벗긴 통나무 조각을 숯으로, `bark_strip`을 `tar`로 처리한다.
- `firing`은 굽기 전 점토 벽돌/점토 항아리/내화 점토 벽돌/내화 도가니를 완성품으로 처리한다.
- `fire`는 파괴되거나 꺼질 때 깨짐 파티클을 생성하지 않는다.

## 지형 생성과 자원

- 광물 블록은 `rock` 텍스처 위에 `assets/textures/block/mask/` 마스크를 합성하는 방식으로 추가했다.
- 광물 feature는 3D 노이즈 전체 평가가 아니라 청크별 배치 시도와 blob 치환 방식으로 생성한다.
- `config/world.json`의 `features.ores`로 광물별 활성화, 높이, 시도 횟수, 크기, 대체 블록을 설정한다.
- 현재 기본 설정에서는 `coal_ore`만 활성화되어 있다.
- 점토는 surface rule이 아니라 `features.clayDisks` 기반 feature로 생성한다.
- `dirt`, `grass`, `rock`, 광물 블록의 드랍 테이블을 현재 제작 루프에 맞게 정리했다.

## 유체와 물속 처리

- 물 흐름 tick 시스템을 추가했다.
- 유체 변화는 변경 좌표와 6방향 이웃을 다음 유체 tick 대상으로 등록하는 이벤트 기반 방식이다.
- 물은 아래 방향을 먼저 흐르고, 남은 양은 수평 방향으로 평형화한다.
- 유체 시뮬레이션은 5 world tick마다 실행하고, 한 번에 처리하는 좌표 수를 제한한다.
- 물속 플레이어 이동을 추가했다.
- 플레이어 AABB가 물에 닿으면 수영 상태로 처리하고, `Space`는 상승, `Shift`는 빠른 하강, 입력이 없으면 천천히 하강한다.
- Sandbox fly 상태에서는 물속에서도 공기 중 fly와 같은 이동 규칙을 사용한다.
- 1인칭 카메라가 물 안에 들어가면 화면에 Kawase blur 기반 물속 흐림과 tint를 적용한다.
- 1인칭 카메라 위치가 물에 닿으면 들고 있는 `lit_torch`가 꺼진다.

## 플레이어와 카메라

- 왼손 슬롯을 추가하고 `R`로 선택 핫바 슬롯과 왼손 슬롯을 교환한다.
- 왼손 슬롯은 HUD에 표시되지만 인벤토리 50칸 패널에는 포함하지 않는다.
- 왼손 아이템은 1인칭 viewmodel에도 표시되며, 오른손 아이템의 좌우 대칭 배치와 텍스처/geometry mirror를 사용한다.
- 3인칭 플레이어 모델을 `Character.glb` 단일 파일 기준으로 정리했다.
- `Attach_L`, `Attach_R` node를 사용해 3인칭 손에도 왼손/오른손 아이템을 표시한다.
- 3인칭 플레이어는 idle/walk/run/crouch/prone clip과 보행 역재생, 자세 전환 0.4초 블렌딩을 사용한다.
- 엎드리기와 수영 기어오르기는 목표 높이를 고정하지 않고 충돌 해소에 필요한 높이만 천천히 상승하도록 개선했다.
- 엎드린 상태에서는 일반 반블록 자동 step-up을 사용하지 않는다.
- 3인칭 카메라는 지형 충돌을 샘플링해 땅을 뚫지 않도록 보정한다.
- 휴대 광원은 손에 든 `burnableLight.lightEmission` 기준으로 카메라 위치 주변에 렌더링 전용 동적 빛을 더한다.

## 렌더링과 시각 효과

- 물속 화면 흐림은 presentation 단계의 Kawase blur로 처리한다.
- 구름은 월드 상공 `Y=500..700` 범위의 렌더 전용 volumetric cloud slab으로 구현했다.
- 하늘은 밤하늘 ramp를 더 어둡게 조정하고, 태양/달 스프라이트와 sky glare를 분리했다.
- bloom은 밝기 threshold 기반에서 alpha mask 기반 bloom source target 방식으로 변경했다.
- 불, 태양, 달처럼 bloom 대상인 픽셀만 bloom source target에 기록한다.
- 구름, blend 지형, 유체는 bloom source도 같은 비율로 가려 뒤쪽 bloom이 부자연스럽게 남지 않게 했다.
- 불은 14프레임 컷아웃 스프라이트를 초당 12프레임으로 동기화 재생한다.
- 불 연기는 normal/darker/lighter texture set을 사용하며, `pyrolysis`는 더 많은 darker smoke, `firing`은 lighter smoke를 사용한다.
- 파티클 vertex buffer는 persistent mapping 상태로 유지하고, quad index buffer는 생성 시점에 한 번만 채운다.
- block tick으로 여러 블록이 바뀌는 경우 edited mesh rebuild를 좌표 목록으로 배치 처리해 leaf decay 렉을 줄였다.

## UI와 문서

- RmlUi 로비에 노란 스플래시 텍스트를 추가했다.
- 스플래시 텍스트 목록은 `assets/ui/splashes.json`으로 분리했다.
- 인벤토리 슬롯 게이지는 내구도 전용이 아니라 `components.slotGauge.source` 기반 공용 게이지로 정리했다.
- 툴팁에는 액션, 파괴 레벨, 내구도, 연소 시간, 연료 열 레벨, 렌더 데이터 같은 디버그 정보를 표시한다.
- README는 GitHub 첫 화면용 한국어 소개, itch.io/YouTube 링크, 조작법 중심으로 정리했다.
- `screenshots/`는 Git 추적 대상에서 제외했다.
- `docs/design`의 item, block, recipe, rendering, player, UI, save/load 문서를 현재 구현 기준으로 갱신했다.

## 저장 포맷 변경

- 청크 payload에 `blockStates`, 드랍 아이템 `durability`, `burnTicksRemaining`, `processingTicks`, `processingType`, fire block entity의 `fireMode`, `fireHeatLevel`, 연료 부산물 상태를 추가했다.
- 플레이어 저장에는 50개 인벤토리 슬롯 뒤 왼손 슬롯을 추가했다.
- 플레이어 인벤토리 슬롯은 `itemId`, `count`, `durability`, `burnTicksRemaining`을 저장한다.
- 일부 기존 플레이어 파일과 드랍 아이템 payload는 읽을 수 있게 보강했지만, 0.0.0.3 내부에서도 저장 포맷이 많이 바뀌었으므로 안정 검증은 새 월드 기준으로 진행한다.
- `fire` block entity의 구버전 pit kiln 예약 결과물 방식은 제거하고, 드랍 아이템 엔티티 processing 방식으로 전환했다.

## 남은 검증과 리스크

- 실제 빌드와 실행 검증은 사용자가 직접 수행한다.
- Release 출력 폴더가 `assets`, `config`, `shaders`, 런타임 DLL, 새 UI JSON을 모두 포함하는지 확인해야 한다.
- 새 월드에서 저장 후 로비 복귀, 재진입, 플레이어/드랍 아이템/fire/processing 상태 복원이 정상인지 확인해야 한다.
- 0.0.0.3은 저장 호환성보다 현재 구조 확장을 우선했으므로 기존 세이브를 릴리스 검증 기준으로 삼지 않는다.
- `pyrolysis`와 `firing` 구조 판정은 현재 5 x 5 x 3 감시 범위와 같은 Y층 3 x 3 작업 공간만 사용한다. 위쪽 작업 공간은 아직 사용하지 않는다.
- 도구/제작/점토/불 루프는 초기 콘텐츠이므로 밸런스와 이름, 드랍 수량은 다음 버전에서 조정될 수 있다.
- 물, 구름, bloom, 동적 광원, 파티클 물속 tint는 화면 확인이 필요하다.
- 3인칭 손 아이템, 왼손 viewmodel, 자세 전환, 엎드리기/수영 기어오르기는 카메라와 충돌 상태에 따라 추가 튜닝이 필요할 수 있다.
