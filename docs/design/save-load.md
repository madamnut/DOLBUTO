# 저장과 로드

## 저장 위치

월드 저장 파일은 다음 경로를 사용한다.

```text
saves/<world-name>/regions/
```

Debug와 Release의 실제 기준 경로는 [[runtime-paths]]를 따른다.

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
- 청크 packed light 데이터
- incoming feature slot 값
- incoming feature mask 값

블록 데이터와 유체 데이터는 각각 별도 RLE run으로 저장한다.
유체 값은 `uint16_t` 패킹 값이다.
packed light 값은 `uint8_t` RLE run으로 저장하며, 상위 4비트는 skylight, 하위 4비트는 block light이다.
현재 구현은 skylight만 계산하고 block light는 `0`으로 둔다.
저장되는 최종 청크 상태는 `LightResolved`이며, 중간 결과는 `TerrainSourceReady`, `LocalLightReady` 상태로 저장될 수 있다.
현재 feature 기본 경로는 incoming feature write 전파가 아니라 3x3 source view 기반 center resolve다. 저장 포맷의 incoming feature slot 필드는 전환기 호환 필드로 남아 있지만 새 생성 파이프라인은 여기에 의존하지 않는다.

저장하지 않는 것:

- render ticket
- mesh ticket
- source/feature/light ticket
- job priority
- GPU mesh
- outgoing feature publish ticket

## 압축

청크 페이로드는 raw payload를 만든 뒤 저장 payload로 압축해 region 섹터에 기록한다.
현재 코드에는 LZ4 블록 인코딩/디코딩 경로가 있다.

청크 엔트리의 `storedSize`는 압축 후 크기이고, `rawSize`는 압축 전 크기다.

저장 payload 직렬화/역직렬화와 LZ4 block encode/decode는 `src/save/SaveFormat.h/.cpp`가 담당한다.

## 저장 시점

저장은 `src/save/SaveSystem.h/.cpp`의 save worker가 담당한다.
`SaveSystem`은 저장 큐, pending snapshot, clean revision cache, region header cache, region file IO, 저장/로드 카운터를 소유한다.
클라이언트에서는 `ClientWorldRuntime`이 `SaveSystem` 인스턴스와 active world directory를 소유하고, runtime chunk snapshot 생성, 완료된 terrain 결과의 snapshot enqueue, 전체 runtime chunk save enqueue를 담당한다.
`Renderer`는 전환 단계에서 저장 worker 시작/정지와 저장 완료 callback 연결만 수행한다.
`WorldRuntime`은 runtime chunk dirty serial/dirty flag 갱신을 담당한다.
저장 완료 callback에서 런타임 청크의 saved backing/dirty 상태를 갱신하는 연결은 아직 `Renderer`가 보유한다.
snapshot load 완료 drain, desired 여부 판정, 기존 render/mesh/source/feature/local-light/light ticket 보존과 loaded runtime chunk 설치는 `ClientWorldRuntime`과 `WorldRuntime`의 상태 설치 API를 통해 처리한다.
`ChunkLoadSystem`은 비동기 snapshot load 요청을 받아 `SaveSystem`의 load 함수를 호출하고 완료 결과를 prepare queue로 넘긴다.
`ChunkPrepareSystem`은 별도 worker에서 저장 snapshot을 `RuntimeChunk`로 복원하고 derived cache를 재구축한다.
메인 스레드는 준비된 `PreparedChunkLoad`를 설치하고, entity id 정규화와 저장 clean revision 갱신처럼 runtime 상태에 닿는 작업만 수행한다.

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

저장 데이터가 있으면 snapshot을 prepare worker에서 런타임 청크로 복원한다.
저장 데이터가 없으면 월드 생성 파이프라인으로 새 청크를 만든다.

## 종료 처리

종료 시에는 worker를 멈추고 완료 큐를 회수한 뒤, 런타임 청크와 필요한 완료 결과를 저장 큐에 넣는다.
마지막으로 save worker flush가 끝나야 종료가 안전하다.

관련 문서: [[chunk-system]], [[runtime-paths]]

## 기후 페이로드 데이터

청크 payload는 block/fluid/light RLE 데이터 바로 뒤, incoming feature write 앞에 기후 데이터를 저장한다.

- `temperature`: 청크 컬럼당 `uint8_t` 하나, 총 256 raw byte.
- `precipitation`: 청크 컬럼당 `uint8_t` 하나, 총 256 raw byte.
- 값은 `0~255`로 인코딩하고 `0.0~1.0`으로 디코딩한다.
- 유체는 런타임과 저장 데이터에서 별도의 packed `uint16_t` 배열로 유지한다.
- light는 런타임과 저장 데이터에서 별도의 packed `uint8_t` 배열로 유지한다.
- 현재 저장 포맷은 light payload가 없는 legacy 청크를 지원하지 않는다. 기존 세이브는 삭제하고 새로 생성하는 것을 전제로 한다.

## 청크 엔티티 페이로드 데이터

청크 payload는 incoming feature write 뒤, 청크 revision 앞에 청크 소유 월드 엔티티도 저장한다.

```text
uint16 entityCount
repeat entityCount:
  uint16 type          // 1 = DroppedItem
  uint64 entityId
  float localX
  float y
  float localZ
  float velocityX
  float velocityY
  float velocityZ
  uint8 flags          // bit 0 = grounded
  if type == DroppedItem:
    uint16 itemId
    uint16 count
uint64 revision
```

엔티티 위치는 청크 로컬 X/Z와 월드 Y로 저장한다.
드랍 아이템의 회전, 스핀, 나이, 획득 진행 상태는 런타임 전용이며 저장하지 않는다.
엔티티만 바뀐 경우에는 terrain revision을 올리지 않고 런타임 dirty serial을 사용한다. terrain revision은 mesh validity에도 사용되기 때문이다.

## 플레이어 상태

플레이어 상태는 region 청크 데이터와 분리해서 저장한다.

```text
saves/<world-name>/player.dat
```

파일은 버전 필드가 없는 고정 바이너리 레이아웃이다.

```text
double x
double y
double z
float yaw
float pitch
uint8 moveMode        // 0 = fly, 1 = ground
uint8 gameMode        // 0 = survival, 1 = sandbox
double verticalVelocity
uint16 hp
uint16 maxHp
uint16 hunger
uint16 maxHunger
uint16 thirst
uint16 maxThirst
repeat 50:
  uint16 itemId
  uint16 count
```

전체 크기는 254바이트다. X/Z는 래핑된 월드 좌표로 저장한다.
인벤토리 슬롯 `0~49`는 이동 상태 뒤에 저장한다.
임시 인벤토리 커서 스택은 저장하지 않는다.
현재 포맷은 레거시 `player.dat` 호환을 제공하지 않으며, 파일 크기가 부족하면 기본 플레이어 상태를 사용한다.

## 게임 씬 저장 경계

Pause `EXIT`는 로비로 돌아가기 전에 게임 씬에 같은 저장 경계를 적용한다.
먼저 플레이어 상태를 저장하고, terrain worker를 멈추고, 모든 런타임 청크를 저장 큐에 넣은 뒤, 로드된 terrain data를 파괴하기 전에 save worker를 비운다.
로비 씬이 활성화된 동안 save worker는 정지 상태를 유지하며, 다음 게임 씬에서 다시 시작한다.

## 월드 상태

월드 단위 metadata는 플레이어와 region 데이터와 분리해서 저장한다.

```text
saves/<world-name>/world.dat
```

파일은 버전 필드가 없는 고정 바이너리 레이아웃이다.

```text
uint64 totalTicks
uint64 seed
uint64 createdUnixSeconds
uint64 lastPlayedUnixSeconds
```

게임 시간은 인게임 1분당 20틱을 사용한다.
하루는 `24 * 60 * 20 = 28800`틱이다.
`world.dat`가 없으면 월드는 `7200`틱에서 시작하며 `0D 06H 00M`으로 표시된다.
seed는 좌상단 디버그 텍스트에 `SEED: <value>`로 표시한다.
생성 시각과 최근 플레이 시각은 Unix seconds로 저장하고 UI 표시용으로만 포맷한다.
월드 상태 저장은 `lastPlayedUnixSeconds`를 갱신한다. `createdUnixSeconds`는 월드 생성 시점에 고정된다.

## 월드 슬롯

로비는 저장된 월드 목록과 새 월드 생성 폼이 있는 월드 선택 UI를 가진다.
월드 생성에는 월드 이름, seed, 초기 게임모드가 필요하다.
월드는 `saves` 바로 아래의 폴더로 저장한다.
월드 폴더 이름은 월드 이름이다.
월드 선택 UI는 월드 목록 화면 진입 시 `saves`를 스캔하고 `world.dat`를 포함한 디렉터리를 표시한다.
각 월드 행은 월드 생성 시각과 최근 플레이 시각을 표시한다.
월드를 생성하면 `saves/<world-name>/regions`를 만들고, `world.dat`와 선택한 게임모드가 반영된 기본 `player.dat`를 쓴 뒤 게임 씬에 진입한다.
