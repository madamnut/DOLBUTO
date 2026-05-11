# 청크 시스템

## 단위

- 청크 크기: `16 x 512 x 16`
- 서브청크 크기: `16 x 16 x 16`
- 청크당 서브청크 수: 32
- 로딩 판단 단위: 청크
- 메싱 데이터 구성 단위: 서브청크

블록 편집 시에는 전체 청크를 다시 만들지 않고 필요한 서브청크 주변만 다시 메싱하는 방향을 유지한다.

## 로딩 중심

렌더 중심은 플레이어가 속한 2x2 청크 그룹을 기준으로 한다.
`loadGridScale`은 16청크 단위의 로딩 스케일이다.

현재 `config/world.json` 기준:

```json
"loadGridScale": 2
```

## 런타임 데이터 범위

렌더링하려는 청크보다 넓은 런타임 청크 풀을 유지한다.
이유는 feature 전파와 메싱 경계 처리에 렌더 범위 밖 청크가 필요하기 때문이다.

현재 파이프라인은 "렌더 범위 전체를 명시 단계로 지정"하기보다, 각 청크 요청에서 필요한 작업을 파생시키는 구조다.

순환 월드에서도 런타임 청크 좌표는 플레이어 주변의 실제 좌표를 유지한다.
저장과 생성 입력만 `65536 x 65536` 블록 주기로 래핑한다.
이 방식은 월드 경계 밖 청크가 화면상 플레이어 옆에 붙어 렌더링되도록 하기 위한 기준이다.

## 생성 상태

현재 청크 생성 상태는 다음 enum으로 관리된다.

```text
Empty
Featuring
Full
Meshed
```

- `Empty`: 런타임 청크가 없거나 아직 데이터가 없다.
- `Featuring`: 자기 지형/표면/자기 feature 생성은 끝났고, 이웃 feature write를 기다리거나 보유한다.
- `Full`: incoming feature가 반영되어 게임플레이용 데이터로 쓸 수 있다.
- `Meshed`: 렌더링 가능한 메쉬가 준비되어 있다.

## 작업 큐

terrain worker는 4개를 사용한다.
작업 큐는 성격별로 나뉘어 있다.

```text
terrainMeshJobs
terrainFinalizeJobs
terrainFeatureJobs
```

우선순위는 mesh, finalize, feature 순서다.

작업 타입:

- `BuildFeaturing`: 지형/표면/자기 feature 생성과 이웃 feature write 생성
- `FinalizeFeatures`: incoming feature를 청크 데이터에 반영
- `BuildChunkMesh`: Full 청크와 주변 8청크를 사용해 메쉬 생성

## 파생 요청

렌더링을 원하면 다음 요청이 파생된다.

```text
wantRender
  -> wantMesh
      -> wantFull
          -> wantFeaturing
```

`wantMesh`는 청크 자체가 Full이어야 하고, 주변 8청크도 메싱 경계용 데이터로 준비되어야 한다.

## 언로드

렌더 범위에서 벗어난 청크는 먼저 렌더 데이터에서 분리된다.
런타임 유지 범위 밖으로 벗어나면 언로드 큐에 들어간다.

언로드 과정:

```text
render detach
save enqueue 판단
runtime erase
retired GPU resource destroy
```

GPU 리소스 폐기는 한 프레임에 몰리지 않도록 budget을 둔다.

관련 문서: [[world-generation]], [[save-load]], [[rendering]]

## Chunk Column Data

Runtime chunk data keeps climate as per-column arrays alongside block and fluid arrays.

- Blocks: `uint16_t` vector for `16 x 512 x 16` block ids.
- Fluids: `uint16_t` vector for `16 x 512 x 16` packed fluid values.
- Temperature: `uint8_t[256]`, indexed by `localZ * 16 + localX`.
- Precipitation: `uint8_t[256]`, indexed by `localZ * 16 + localX`.
- Fluid subchunk counts: `uint16_t[32]`, one count per `16 x 16 x 16` subchunk.

`fluidSubchunkCounts` is derived runtime data and is not stored in region files.
Generated chunks fill it while writing water.
Loaded chunks rebuild `emptySubchunks` and `fluidSubchunkCounts` together in one layer-major pass over `blocks` and `fluids`.
Fluid mesh generation skips subchunks whose count is `0`.

## Async Snapshot Load

Saved chunk snapshot reads are handled by a single chunk-load worker instead of the render thread.
When `ensureRuntimeChunk` finds a missing runtime chunk, it inserts a runtime shell, marks snapshot loading as requested, enqueues a chunk load job, and returns immediately.

The chunk-load worker calls `loadChunkSnapshot`.
The main thread later installs completed snapshot loads during terrain job completion processing.
If a snapshot exists, it is converted to runtime chunk data and the previous render/full/mesh/feature tickets are preserved.
If no snapshot exists, the shell is marked as load-finished and normal generation requests can proceed.

Region file read/write access is serialized by a region IO mutex so the chunk-load worker and save worker do not read and write region payload/header data at the same time.

## Chunk Entity Data

Runtime chunk data owns a `WorldEntity` array.
The current entity type is `DroppedItem`.

- `entityId`: unique runtime/save identity for a world entity
- `type`: entity payload type
- `position`, `previousPosition`, `velocity`: runtime movement state
- `flags`: compact common state such as grounded
- dropped-item payload: `itemId` and `count`

Entities move with their owner chunk.
If an entity crosses a chunk boundary while both chunks are loaded, ownership is transferred to the target chunk.

## Game Scene Unload

Returning from pause to the lobby unloads the game scene instead of keeping loaded world state alive.

The unload path:

```text
stop terrain workers
enqueue all runtime chunks for save
drain save worker
wait for Vulkan device idle
destroy loaded and retired terrain meshes
clear runtime chunks, desired sets, requested job sets, and unload queues
reset terrain load request
```

The renderer itself remains alive because the lobby still uses renderer-owned swapchain, sprite textures, and font resources.
Starting a later game scene starts terrain and save workers again.
