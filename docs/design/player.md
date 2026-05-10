# 플레이어

## 좌표와 카메라

플레이어 좌표는 발밑 중앙이다.
1인칭 눈 위치는 플레이어 좌표에서 `Y + 1.5625`다.

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

V 키로 fly/ground 모드를 전환한다.
기본 모드는 fly다.

현재 `config/world.json` 기준:

```json
"player": {
  "flyMoveSpeed": 64.0,
  "groundMoveSpeed": 4.317,
  "jumpSpeed": 8.4,
  "gravity": 32.0
}
```

## 물리

물리는 20Hz 고정 틱으로 처리하고 렌더 위치는 보간한다.

- fly: WASD 수평 이동, Space 상승, Shift 하강
- ground: WASD 수평 이동, Space 점프
- ground 이동은 yaw만 반영하고 pitch는 이동 방향에 영향을 주지 않는다.
- 상승 중 재점프는 막는다.

## 충돌

플레이어 충돌 박스는 다음 크기를 기준으로 한다.

```text
0.6 x 1.75 x 0.6
```

충돌은 지형 블록의 `collision` 속성과 렌더 타입을 기준으로 판정한다.
로딩되지 않은 청크 방향으로는 접근할 수 없도록 처리한다.

## 블록 상호작용

- 좌클릭: 선택 블록 파괴
- 우클릭: 이전 위치에 rock 배치
- 상호작용 거리: 5
- 레이캐스트 기준은 항상 1인칭 눈 위치와 1인칭 카메라 방향이다.
- 3인칭 상태에서도 블록 상호작용은 1인칭 기준으로 처리한다.

## 입력

- `W/S`: 앞뒤 이동
- `A/D`: 좌우 이동
- `Space`: 상승 또는 점프
- `Shift`: fly 모드 하강
- `F`: dropped item pickup
- `V`: fly/ground 전환
- `F2`: 스크린샷
- `F3`: 디버그 텍스트
- `F4`: 지형 와이어프레임
- `F5`: 시점 전환
- `F11`: 전체화면 전환
- `Esc`: 종료

관련 문서: [[block-data]], [[debug-profiling]]

## Player Save Data

Player state is loaded from and saved to `saves/world/player.dat`.

- `x`, `y`, `z` use the player foot position.
- `x` and `z` are stored as wrapped world coordinates.
- `yaw` and `pitch` restore the camera view.
- `moveMode` stores `0 = fly`, `1 = ground`.
- `verticalVelocity` restores jump/fall momentum for ground movement.
- The 50 runtime inventory slots are saved after movement state as `uint16 itemId` and `uint16 count` pairs.
- The transient inventory cursor stack is not saved.
- Interpolation state is not saved; on load, previous position is set to the loaded position.

Related document: [[save-load]]

## Block Placement Collision

Block placement is ignored when the placed collision block would overlap the current player collider.
Non-collision blocks are not blocked by this rule.

## Debug Time

The upper-left debug text includes world time as:

```text
TIME: 0D 06H 00M
```

The display is derived from the world `totalTicks` value stored in `saves/<world-name>/world.dat`.
