# 플레이어

## 좌표와 카메라

플레이어 좌표는 발밑 중앙이다.
1인칭 눈 위치는 플레이어 좌표에서 `Y + 1.5625`다.
웅크리는 동안 목표 눈 위치는 `1.5625 * sneakHeightScale`이며, 실제 카메라 눈높이는 이 값으로 보간된다.

## 플레이어 모델

플레이어 모델 소스는 `assets/textures/character/Character.glb`를 사용한다.
런타임은 더 이상 별도 `Character.mesh` 캐시 파일을 사용하지 않고, 시작 시 GLB를 직접 읽는다.
`PlayerModelLoader`는 GLB의 node, mesh primitive, vertex/index 데이터를 읽어 `PlayerMeshRenderPath`가 사용할 파트별 source vertex 목록으로 변환한다.
`PlayerMeshRenderPath`는 각 vertex가 속한 GLB node index를 보존하고, 매 프레임 node transform을 적용한 뒤 `1 / 16` 스케일로 플레이어 월드 좌표에 배치한다.

현재 모델 구조는 다음 파트 이름을 기준으로 한다.

```text
ModelRoot
  Head
  BodyRoot
    Body
    Arm_L
      Arm_LU
      Arm_LL
    Arm_R
      Arm_RU
      Arm_RL
    Leg_L
      Leg_LU
      Leg_LL
    Leg_R
      Leg_RU
      Leg_RL
```

이 구조는 이후 보행 모션, 머리 회전, 팔/다리 procedural animation을 node transform 단계에서 적용하기 위한 기준이다.

카메라 yaw와 몸통 yaw는 분리한다.
저장되는 카메라 yaw/pitch는 기존처럼 실제 시선 방향이고, 렌더링용 `bodyYaw`는 카메라 yaw를 부드럽게 따라가는 몸 방향이다.
머리는 카메라 방향을 즉시 따라가되, 몸 기준 상대 yaw는 `-45도 ~ +45도`로 제한한다.
카메라 yaw와 body yaw 차이가 이 한계를 넘으면 머리 상대 yaw만 한계값으로 clamp한다.
머리 pitch는 카메라 pitch를 `-70도 ~ +70도`로 제한해 `Head` node에 적용한다.

몸 방향은 마인크래프트식 조작처럼 실제 이동 방향이 아니라 카메라 yaw를 부드럽게 따라간다.
뒤로 이동하거나 좌우로 이동해도 몸은 바라보는 전방을 유지한다.
좌우 이동 입력이 있을 때는 전방 기준으로 몸 방향만 좌우 `45도`까지 비틀어 옆걸음 느낌을 만든다.
머리 yaw는 렌더링 시점의 몸 방향을 기준으로 `-45도 ~ +45도` 안에서만 회전한다.
보행 모션은 플레이어의 실제 수평 이동 거리에서 `walkPhase`와 `walkAmount`를 계산하고, 이전 물리 tick 값과 현재 tick 값을 렌더 alpha로 보간해 렌더 프레임에 전달한다.
`PlayerMeshRenderPath`는 node transform 계산 단계에서 보행 pose를 추가한다.
`Arm_L`/`Arm_R`와 `Leg_L`/`Leg_R`는 앞뒤 swing을 담당하고, `Arm_LL`/`Arm_RL`과 `Leg_LL`/`Leg_RL`은 팔꿈치/무릎 접힘을 담당한다.
상하위 관절은 모두 GLB node local transform 뒤에 추가 pitch 회전을 곱해 처리한다.
팔꿈치와 무릎 접힘은 한 방향으로만 적용하고 각도 상한을 둔다.

1인칭 화면에서는 오른팔 아래팔 node인 `Arm_RL` 계열에 속한 vertex만 별도 hand mesh로 추출해 사용한다.
`PlayerMeshRenderPath`는 전체 플레이어 mesh와 1인칭 손 mesh를 함께 소유하며, 1인칭 손은 카메라 기준 오른쪽 아래 위치에 매 프레임 다시 배치한다.
선택 핫바 슬롯에 아이템이 있으면 해당 아이템을 드랍 아이템과 같은 extruded sprite 형태로 손 앞에 렌더링한다.
선택 슬롯이 비어 있어도 1인칭 손은 표시한다.
아이템을 들고 있을 때는 1인칭 팔/손을 렌더링하지 않고 아이템만 표시한다.
1인칭 손과 든 아이템은 그리기 직전에 scene depth를 지운 뒤 viewmodel pipeline으로 렌더링해서 지형이나 블록에 묻히지 않는다.
viewmodel pipeline은 depth test/write를 사용하므로 손/아이템 mesh 내부의 앞뒤 관계는 유지된다.
든 아이템은 카메라 회전과 무관한 화면 고정 좌표계에서 그리므로 카메라를 돌려도 같은 면이 보인다.
1인칭 손/아이템 위치와 회전 보정값은 `config/viewmodel.json`에서 읽는다.
이 파일은 손과 든 아이템의 view-space 위치(`x/y/z`), 스케일, 회전(`rotationX/rotationY/rotationZ`)을 정의한다.
1인칭 손 회전은 아이템 viewmodel과 같은 순서인 X, Z, Y 순서로 적용한다.

초기 위치:

```text
X 0
Y 300
Z 0
```

카메라 모드:

```text
1인칭
3인칭 후방
3인칭 전방
```

F5로 순환한다.
3인칭 카메라는 1인칭 눈 위치를 피벗으로 보고, 거리는 5.5다.

## 이동 모드

게임 모드는 `Survival`과 `Sandbox` 두 가지다.
`Survival`은 체력/허기/갈증이 적용되는 생존 모드이며, `Space` 더블탭 fly 전환을 허용하지 않는다.
`Sandbox`는 무적/자유 이동용 모드이며 기존처럼 `Space` 더블탭으로 ground/fly 모드를 서로 전환한다.
Sandbox fly 상태에서 `Shift`로 하강하며, 하강 중 지면에 닿으면 ground 모드로 돌아간다.
새 월드의 기본 선택값은 `Sandbox`다.

게임 모드는 채팅 명령어로 바꿀 수 있다.

```text
/gamemode survival
/gamemode sandbox
```

현재 `config/world.json` 기준:

```json
"player": {
  "flyMoveSpeed": 64.0,
  "groundMoveSpeed": 4.317,
  "jumpSpeed": 8.4,
  "gravity": 32.0,
  "sprintSpeedScale": 1.3,
  "sneakSpeedScale": 0.3,
  "sneakHeightScale": 0.833333,
  "movementDoubleTapWindow": 0.35
}
```

## 물리

물리는 20Hz 고정 틱으로 처리하고 렌더 위치는 보간한다.
이동 tick 규칙은 `PlayerMovementSystem`이 처리하고, `GameClient`는 입력 상태와 충돌 조회 callback을 전달한다.

- fly: WASD 수평 이동, Space 상승, Shift 하강
- fly: Ctrl을 누르는 동안 이동 속도를 `sprintSpeedScale`만큼 올린다.
- ground: WASD 수평 이동, Space 점프
- ground: Ctrl 또는 W 더블탭으로 달리기를 시작한다.
- ground: Shift로 웅크리며, 이동 속도는 `sneakSpeedScale`, 충돌 높이는 `sneakHeightScale`을 적용한다.
- 달리기와 웅크리기 입력은 Options의 SPRINT/SNEAK 모드에서 hold/toggle을 전환한다.
- ground 이동은 yaw만 반영하고 pitch는 이동 방향에 영향을 주지 않는다.
- 상승 중 재점프는 막는다.
- 웅크리기 충돌 높이는 즉시 적용하지만, 1인칭 눈높이는 별도 `eyeHeightScale`을 목표 높이로 보간해 카메라가 순간 이동하지 않게 한다.
- View Bobbing이 켜져 있으면 ground 이동 중 렌더 카메라 위치에만 좌우/상하 보빙을 적용한다.
- 보빙 강도는 실제 수평 이동량에서 계산한 `walkAmount`를 사용하므로 웅크리기처럼 느린 이동은 약해지고 달리기처럼 빠른 이동은 강해진다.
- 보빙은 카메라 방향, 블록 선택/파괴/설치 raycast, 플레이어 실제 위치에는 반영하지 않는다.
- 달리기 FOV 증가는 sprint 입력 상태만 보지 않고 실제 수평 이동량이 있을 때만 올라간다. Ctrl 또는 toggle sprint 상태로 가만히 있으면 FOV는 걷기 기본값으로 보간 복귀한다.

## 충돌

플레이어 충돌 박스는 다음 크기를 기준으로 한다.

```text
0.6 x 1.75 x 0.6
```

웅크리는 동안 충돌 높이는 `1.75 * sneakHeightScale`로 줄어든다.
충돌은 지형 블록의 `collision` 속성과 렌더 타입을 기준으로 판정한다.
로딩되지 않은 청크 방향으로는 접근할 수 없도록 처리한다.
ground 웅크리기 중 수평 이동은 플레이어 바닥 사각형 아래에 충돌 지형이 하나도 남지 않는 위치로는 진행하지 않는다.
이 판정은 충돌 박스의 `0.6 x 0.6` 바닥 면을 기준으로 하며, vertical 이동에는 적용하지 않는다.

## 블록 상호작용

- 좌클릭: 선택 블록 파괴
- 우클릭: 이전 위치에 현재 배치 모드 블록 배치
- M: 배치 모드를 `rock`과 `glowing_rock` 사이에서 전환
- 상호작용 거리: 8
- 레이캐스트 기준은 항상 1인칭 눈 위치와 1인칭 카메라 방향이다.
- 3인칭 상태에서도 블록 상호작용은 1인칭 기준으로 처리한다.

블록 상호작용의 기본 판정은 `BlockInteractionSystem`이 담당한다.
여기에는 블록 좌표 변환, 레이캐스트, 플레이어 충돌 범위 판정, 블록 파괴 진행 상태 갱신 규칙이 포함된다.
클라이언트에서는 `ClientGameplayRuntime`이 block breaking 상태와 블록 상호작용 결과를 조율한다.
`Renderer`는 gameplay 결과를 받아 mesh 재생성, 파괴/설치 효과음, 파티클 생성을 실행한다.

## 플레이어 스탯

현재 플레이어 스탯은 `PlayerStats`가 HP, 허기, 갈증과 각 최대값을 가진다.
각 스탯의 기본값과 최대값은 모두 `100`이며, 현재 단계에서는 사망 처리, 허기/갈증 감소 규칙, 저장 포맷에는 연결하지 않는다.
HUD 좌하단의 세로 게이지 3개는 왼쪽부터 HP, 허기, 갈증을 표시한다.
세 게이지 모두 `assets/textures/ui/player/Gauge.png` 프레임 위에 각 스탯 색상의 RmlUi 사각형 바를 각 스탯 비율만큼 bottom-up으로 표시한다.
HP는 붉은색, 허기는 황갈색, 갈증은 파란색을 사용한다.

채팅 명령어로 스탯을 테스트할 수 있다.

```text
/stat
/stat hp
/stat hunger
/stat thirst
/stat hp add -10
/stat hunger set 50
/stat thirst add 20
```

## 입력

- `W/S`: 앞뒤 이동
- `A/D`: 좌우 이동
- `Space`: fly 상승, ground 점프
- `Space` 더블탭: Sandbox에서만 ground/fly 전환
- `W` 더블탭: 달리기 시작
- `Ctrl`: ground 달리기, fly 가속
- `Shift`: fly 모드 하강
- `Shift`: ground 웅크리기
- `F`: 드랍 아이템 획득
- `Q`: 선택 핫바 아이템 1개 버리기
- `Shift+Q`: 선택 핫바 아이템 스택 전체 버리기
- `E`: 인벤토리 열기/닫기
- `F1`: 핫바 HUD와 크로스헤어 표시 전환
- `F2`: 스크린샷
- `F3`: 디버그 텍스트
- `F4`: 지형 와이어프레임
- `F5`: 시점 전환
- `F6`: 기후 오버레이 전환
- `F11`: 전체화면 전환
- `Esc`: 게임에서는 일시정지, 인벤토리/일시정지에서는 게임 복귀
- `Enter`: 채팅/명령어 입력창 열기

관련 문서: [[block-data]], [[debug-profiling]]

## 인벤토리 런타임

플레이어 인벤토리 규칙은 `PlayerInventory`가 담당하고, 클라이언트 런타임 소유권은 `ClientGameplayRuntime`이 가진다.
현재 슬롯 수는 50개이며, 앞 10개 슬롯은 핫바로 사용한다.

`PlayerInventory`는 다음 규칙을 담당한다.

- 슬롯 snapshot 적용과 저장용 snapshot 생성
- 아이템 stack 삽입과 남은 개수 계산
- 좌클릭/우클릭/Shift-click 인벤토리 조작
- 인벤토리 커서 stack 반환
- 핫바 숫자키 교환
- 선택 핫바 슬롯에서 아이템 제거

`ClientUiBridge`는 인벤토리 UI 표시, 마우스 좌표 기반 슬롯 hit test, RmlUi 입력 기반 슬롯 조작, 아이템 정의의 표시 데이터 변환을 담당한다.
실제 슬롯 변경은 `ClientGameplayRuntime`에 위임하고, `Renderer`는 RmlUi/Vulkan backend와 viewport 전달만 유지한다.

## 플레이어 저장 데이터

플레이어 상태는 `saves/world/player.dat`에서 로드하고 같은 파일에 저장한다.

- `x`, `y`, `z`는 플레이어 발밑 위치를 사용한다.
- `x`, `z`는 래핑된 월드 좌표로 저장한다.
- `yaw`, `pitch`는 카메라 시점을 복원한다.
- `moveMode`는 `0 = fly`, `1 = ground`로 저장한다.
- `gameMode`는 `0 = survival`, `1 = sandbox`로 저장한다.
- `verticalVelocity`는 지상 이동의 점프/낙하 관성을 복원한다.
- HP, 허기, 갈증과 각 최대값을 저장한다.
- 50개 런타임 인벤토리 슬롯은 이동 상태 뒤에 `uint16 itemId`, `uint16 count` 쌍으로 저장한다.
- 임시 인벤토리 커서 스택은 저장하지 않는다.
- 보간 상태는 저장하지 않는다. 로드 시 이전 위치는 로드된 위치로 설정한다.

관련 문서: [[save-load]]

## 블록 설치 충돌

설치할 충돌 블록이 현재 플레이어 콜라이더와 겹칠 예정이면 블록 설치를 무시한다.
충돌이 없는 블록은 이 규칙으로 막지 않는다.

## 디버그 시간

좌상단 디버그 텍스트는 월드 시간을 다음 형식으로 포함한다.

```text
TIME: 0D 06H 00M
```

표시는 `saves/<world-name>/world.dat`에 저장된 월드 `totalTicks` 값에서 계산한다.
