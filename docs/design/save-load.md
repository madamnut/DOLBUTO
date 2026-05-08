# 저장과 로드

## 저장 위치

월드 저장 파일은 다음 경로를 사용한다.

```text
saves/world/regions/
```

Debug와 Release/Portable의 실제 기준 경로는 [[runtime-paths]]를 따른다.

## region 파일

저장은 `.region` 파일 단위로 한다.

- region 크기: `16 x 16` 청크
- 섹터 크기: 4096바이트
- 헤더 크기: 4096바이트
- 청크 엔트리 수: 256
- 청크 엔트리 크기: 16바이트

각 청크 엔트리는 다음 정보를 가진다.

```text
offsetSector
sectorCount
storedSize
rawSize
```

region 인덱스는 파일명으로 구분한다.
순환 월드 기준으로 저장 청크 좌표는 `0~4095` 범위로 래핑한다.
따라서 region 좌표 범위는 축별 `0~255`다.
런타임 청크 좌표가 음수이거나 `4096` 이상이어도 저장/로드는 래핑된 청크 좌표의 region 파일을 사용한다.

## 청크 페이로드

청크 페이로드에는 저장 가능한 런타임 데이터만 들어간다.

- 생성 상태 `genState`
- 청크 revision
- 청크 블록 데이터
- 청크 유체 데이터
- incoming feature slot
- incoming feature mask

블록 데이터와 유체 데이터는 각각 별도 RLE run으로 저장한다.
유체 값은 `uint16_t` 패킹 값이다.

저장하지 않는 것:

- render ticket
- mesh ticket
- full/featuring ticket
- job priority
- GPU mesh
- outgoing feature publish ticket

## 압축

청크 페이로드는 raw payload를 만든 뒤 저장 payload로 압축해 region 섹터에 기록한다.
현재 코드에는 LZ4 블록 인코딩/디코딩 경로가 있다.

청크 엔트리의 `storedSize`는 압축 후 크기이고, `rawSize`는 압축 전 크기다.

## 저장 시점

저장은 save worker가 담당한다.

저장 대상:

- 언로드되는 청크
- 작업 결과가 돌아왔지만 이미 언로드 상태인 청크
- 종료 시점에 런타임에 남아있는 청크

저장 스킵:

- 저장 파일에서 불러온 뒤 편집/변경되지 않은 clean 청크

새로 생성된 청크는 저장 파일에 backing이 없으므로 저장 대상이다.

## 로드

청크 요청 시 먼저 region 저장 파일에서 snapshot을 찾는다.

- pending save snapshot hit
- region header cache hit
- save miss

저장 데이터가 있으면 snapshot을 런타임 청크로 복원한다.
저장 데이터가 없으면 월드 생성 파이프라인으로 새 청크를 만든다.

## 종료 처리

종료 시에는 worker를 멈추고 완료 큐를 회수한 뒤, 런타임 청크와 필요한 완료 결과를 저장 큐에 넣는다.
마지막으로 save worker flush가 끝나야 종료가 안전하다.

관련 문서: [[chunk-system]], [[runtime-paths]]

## Climate Payload Data

Chunk payloads store climate immediately after block/fluid RLE data and before incoming feature writes.

- `temperature`: 256 raw bytes, one `uint8_t` per chunk column.
- `precipitation`: 256 raw bytes, one `uint8_t` per chunk column.
- Values are encoded `0~255` and decoded as `0.0~1.0`.
- Fluids remain a separate packed `uint16_t` array in runtime and save data.

## Player State

Player state is stored separately from region chunk data.

```text
saves/world/player.dat
```

The file is a fixed binary layout with no version field.

```text
double x
double y
double z
float yaw
float pitch
uint8 moveMode        // 0 = fly, 1 = ground
double verticalVelocity
```

Total size is 41 bytes. X/Z are saved as wrapped world coordinates.
