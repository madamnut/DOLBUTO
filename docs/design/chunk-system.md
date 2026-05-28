# 청크 시스템

## 단위

- 청크 크기: `16 x 512 x 16`
- 서브청크 크기: `16 x 16 x 16`
- 청크당 서브청크 수: 32
- 로딩 판단 단위: 청크
- 메싱 데이터 구성 단위: 서브청크

블록 편집 시에는 전체 청크를 다시 만들지 않고 필요한 서브청크 주변만 다시 메싱하는 방향을 유지한다.

청크/월드 런타임 타입은 `src/world/WorldTypes.h`에 둔다.
이 파일은 청크 크기 상수, `ChunkData`, `RuntimeChunk`, `ChunkGenState`, feature write, terrain job, save/load snapshot, `WorldEntity` 계열 타입을 가진다.
`ChunkSourceData`, `ChunkBlockData`, `ChunkLightData`, `ChunkDerivedCache`는 청크 산출물을 source/block/light/derived 역할로 나누는 타입이다. `ChunkData`는 이 역할 타입들을 합성해 기존 호출부의 필드 접근은 유지하면서 실제 소유 경계를 분리한다.
runtime chunk map과 chunk key/좌표 helper, block 조회/수정, dirty marking, derived cache 갱신은 `src/world/WorldRuntime.h/.cpp`가 소유한다.
클라이언트의 active world 상태, load order, desired/requested/pending unload set, `WorldRuntime`, `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`은 `src/game/ClientWorldRuntime.h/.cpp`가 소유하는 전환 단계다.
`Renderer`는 terrain load, save/unload, mesh queue 경로에서 runtime chunk map을 직접 들고 있지 않고 `ClientWorldRuntime`이 소유한 `WorldRuntime`의 API를 통해 조회, 순회, 생성, 삭제한다.
render/mesh/source/local-light/light ticket 설정, snapshot load 요청 필요 여부, requested job set 갱신, `BuildTerrainSource`/`ResolveFeatures`/`ResolveLight`/`BuildChunkMesh` job 생성 조건은 `ClientWorldRuntime`이 담당한다.
render 요청의 target status 범위 설정, chunk-load 완료 후 주변 frontier 재검사, feature/light/mesh queue 재검사는 `src/game/ClientTerrainCoordinator.h/.cpp`가 담당한다.
terrain/chunk-load 완료 큐 drain, 완료 결과의 install/save/ignore 판정, mesh 완료의 install/retry/ignore 판정, pending unload 큐 pop/cancel/finish도 `ClientWorldRuntime`이 담당한다.
terrain/chunk-load 완료 결과를 실제로 순회하면서 runtime 설치, 저장 snapshot enqueue, 다음 단계 job 재검사, mesh retry/install 결과 반환을 조율하는 흐름은 `src/game/ClientTerrainCompletionHandler.h/.cpp`가 담당한다.
terrain scene load request, worker start/stop, completed work drain, pending unload의 world/save 처리는 `src/game/ClientTerrainSceneRuntime.h/.cpp`가 담당한다.
runtime chunk에서 저장 snapshot을 만들고, 완료된 terrain 결과를 저장 snapshot으로 enqueue하고, 저장 snapshot을 runtime chunk로 복원하는 경계도 `ClientWorldRuntime`이 담당한다.
feature는 더 이상 런타임 incoming/outgoing slot 전파를 기본 경로로 사용하지 않고, 3x3 source snapshot을 worker 입력으로 받아 center 청크만 확정한다.
snapshot load 완료, terrain source/feature/light resolved 청크 설치, mesh queued ticket 초기화, Meshed 상태 전환 같은 terrain 완료 결과의 runtime 상태 갱신도 `WorldRuntime`이 담당한다.
`Renderer`는 terrain scene runtime이 반환한 render reserve/retire 대상, dropped-item tracking refresh, completed mesh GPU 설치만 수행한다.
save worker와 region 저장/로드 실행 로직은 `src/save/SaveSystem.h/.cpp`에 둔다.
chunk-load worker와 snapshot load 요청/완료 큐는 `src/world/ChunkLoadSystem.h/.cpp`에 둔다.
terrain worker와 terrain job/완료 큐는 `src/world/TerrainJobSystem.h/.cpp`에 둔다.
`BuildTerrainSource`/`ResolveFeatures`/`ResolveLight` terrain job 처리는 `src/game/ClientTerrainJobProcessor.h/.cpp`가 담당한다. 내부에서 `src/world/TerrainBuilder.h/.cpp`와 `src/world/SkyLightSystem.h/.cpp`를 호출해 초기 청크 데이터, 3x3 feature resolve와 local skylight cache, 4방향 face 기반 skylight resolve를 수행한다.
`BuildChunkMesh`의 chunk mesh orchestration과 편집 subchunk 주변 block sampling은 `src/world/TerrainMesher.h/.cpp`가 맡는다.
solid/cross/prop subchunk mesh 생성 본체는 `TerrainGeometryBuilder`가 맡고, `RendererTerrainMeshBridge`는 render-dependent mesh job callback과 edited subchunk rebuild에서 이를 `TerrainMesher`에 연결한다.
`Renderer`는 `RendererTerrainMeshBridge`가 만든 CPU mesh 결과를 GPU 업로드/설치 경계로 넘긴다.
fluid subchunk mesh 생성은 `TerrainMesher`가 맡고, 불투명 블록 판정은 `Renderer` callback을 사용한다.

## 로딩 중심

렌더 중심은 플레이어가 속한 2x2 청크 그룹을 기준으로 한다.
`loadGridScale`은 16청크 단위의 로딩 스케일이다.

현재 `config/world.json` 기준:

```json
"loadGridScale": 2
```

## 런타임 데이터 범위

렌더링하려는 청크보다 넓은 런타임 청크 풀을 유지한다.
이유는 feature resolve, skylight resolve, 메싱 경계 처리에 렌더 범위 밖 청크가 필요하기 때문이다.

현재 파이프라인은 "큰 범위를 한 job에서 처리"하지 않고, 각 단계가 필요한 read-only 입력 view를 읽어 center 청크 하나만 산출하는 구조다. feature와 mesh는 3x3 입력을 사용하고, light resolve는 center와 4방향 이웃 face만 사용한다. 요청 단계는 필요한 의존 청크의 target status만 올리고, 실제 job enqueue는 frontier scheduler가 맡는다.

순환 월드에서도 런타임 청크 좌표는 플레이어 주변의 실제 좌표를 유지한다.
저장과 생성 입력만 `65536 x 65536` 블록 주기로 래핑한다.
이 방식은 월드 경계 밖 청크가 화면상 플레이어 옆에 붙어 렌더링되도록 하기 위한 기준이다.

## 생성 상태

현재 청크 생성 상태는 다음 enum으로 관리된다.

```text
Empty
TerrainSourceReady
LocalLightReady
LightResolved
Meshed
```

- `Empty`: 런타임 청크가 없거나 아직 데이터가 없다.
- `TerrainSourceReady`: 높이맵, 기본 지형/물, 표면 룰, 기후 칼럼까지 생성된 source 데이터가 준비됐다. 이 단계에서 column별 지형 높이/표면 Y와 deterministic feature 후보도 캐시한다.
- `LocalLightReady`: center 주변 3x3 `TerrainSourceReady` 청크를 입력으로 feature를 확정하고, center block/fluid 기준 local skylight가 1회 계산되어 `localLight` cache에 저장됐다.
- `LightResolved`: center와 4방향 이웃 `LocalLightReady` 청크의 local light face를 입력으로 skylight 경계 전파가 반영됐다.
- `Meshed`: center 주변 3x3 `LightResolved` 청크를 입력으로 렌더링 가능한 메쉬가 준비되어 있다.

## 작업 큐

terrain worker는 4개를 사용하며 `TerrainJobSystem`이 worker thread와 큐를 소유한다.
작업 큐는 성격별로 나뉘어 있다.

```text
terrainFeatureJobs    // BuildTerrainSource
terrainFinalizeJobs   // ResolveFeatures + local skylight
terrainLightJobs      // ResolveLight
terrainMeshJobs       // BuildChunkMesh
```

우선순위는 mesh, light resolve, feature+local-light resolve, source 생성 순서다.

작업 타입:

- `BuildTerrainSource`: center 청크의 기본 지형/물/표면/기후 source 데이터를 생성한다.
- `ResolveFeatures`: 3x3 `TerrainSourceReady` 입력 view에서 source cache와 feature 후보를 읽고, center 청크에 닿는 feature만 적용한 뒤 center block/fluid 기준 local skylight를 계산해 `LocalLightReady`를 만든다.
- `ResolveLight`: center `localLight` 전체와 동서남북 이웃의 맞닿은 `localLight` face만 읽고, center 내부 boundary propagation으로 resolved light를 산출한다.
- `BuildChunkMesh`: 3x3 `LightResolved` 입력 view를 사용해 center subchunk mesh 생성을 조율한다.

## 상태 목표와 frontier scheduling

렌더링을 원하면 청크별 `targetGenState`를 먼저 칠한 뒤, scheduler가 현재 승급 가능한 frontier만 job queue에 넣는다.
렌더 대상 1청크 기준 목표 상태 범위는 다음과 같다.

```text
requestRenderCascade
  -> center: target Meshed
  -> radius 1 square: target LightResolved
  -> radius 2 square without corners: target LocalLightReady
  -> radius 3 square without corners: target TerrainSourceReady
  -> scheduleAround(radius 3)
```

`scheduleAround`는 render cascade 직후 초기 frontier를 넓게 깨울 때만 청크의 `currentStatus`, `targetGenState`, 이웃 상태를 보고 가능한 단계만 enqueue한다.
완료 이벤트는 단계별 dependent shape만 다시 검사한다. `TerrainSourceReady` 완료는 주변 3x3 `LocalLightReady` 후보, `LocalLightReady` 완료는 center + 4방향 `LightResolved` 후보, `LightResolved` 완료는 주변 3x3 mesh 후보만 검사한다.
snapshot load 완료는 복원된 상태에 따라 자기 source job 또는 위 dependent shape들을 순서대로 깨운다.
각 단계 job은 의존 범위를 한꺼번에 생성하지 않는다. feature와 mesh의 3x3, light resolve의 center + 4방향 입력은 모두 read-only 입력 범위이고, 결과는 항상 center 청크 하나다.
`requestLightCascade`는 center `LightResolved`, center + 4방향 `LocalLightReady`, radius 2 square without corners `TerrainSourceReady`만 목표로 잡는다.
source-only 청크는 저장 로드가 먼저 걸릴 수 있으므로 `sourceTicket`으로 로드 완료 후 `BuildTerrainSource`를 재개한다.

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

## 청크 칼럼 데이터

런타임 청크 데이터는 블록 배열, 유체 배열, packed light 배열 옆에 기후 값을 칼럼별 배열로 보관한다.

- Blocks: `16 x 512 x 16` 블록 ID를 담는 `uint16_t` vector.
- Fluids: `16 x 512 x 16` packed fluid 값을 담는 `uint16_t` vector.
- Local light: `16 x 512 x 16` packed local skylight 값을 담는 `uint8_t` vector.
- Light: `16 x 512 x 16` packed resolved light 값을 담는 `uint8_t` vector.
- Temperature: `uint8_t[256]`, `localZ * 16 + localX`로 인덱싱한다.
- Precipitation: `uint8_t[256]`, `localZ * 16 + localX`로 인덱싱한다.
- Terrain source cache: `terrainHeight`, `terrainSurfaceY`, `terrainFeatureCandidates`는 `TerrainSourceReady`에서 만든 checkpoint 데이터다. fresh terrain source는 나무 후보를 보존하고, 저장된 청크를 로드한 경우에는 block scan으로 높이/표면만 복원한 뒤 feature 후보는 fallback 평가를 사용한다.
- Fluid subchunk counts: `uint16_t[32]`, `16 x 16 x 16` subchunk마다 하나의 count를 가진다.

`fluidSubchunkCounts`는 파생 런타임 데이터이며 region 파일에는 저장하지 않는다.
생성된 청크는 물을 기록하면서 이 값을 채운다.
로드된 청크는 `blocks`와 `fluids`를 layer-major 순서로 한 번 순회하면서 `emptySubchunks`와 `fluidSubchunkCounts`를 함께 재구축한다.
packed light의 상위 4비트는 skylight, 하위 4비트는 block light이다.
`ResolveFeatures`는 청크별 local light를 한 번만 계산하고, `ResolveLight`는 center 청크와 4방향 이웃 face만 읽어 center 내부 경계 전파를 수행한다. block light는 블록 정의의 `lightEmission`을 원천값으로 사용하고 skyLight와 같은 packed light 배열에 저장한다.
skyLight/blockLight 전파는 청크별 `lightAttenuation` cache를 먼저 만든 뒤 `block index + light` 큐와 `index +/- 1/16/256` 이웃 offset을 사용한다.
블록 편집 경로는 생성 파이프라인 전체를 다시 태우지 않는다. `setBlockAtWorld`는 block 배열을 수정한 뒤 해당 청크의 `localLight` cache를 갱신하고, edited mesh rebuild 직전에 `WorldRuntime::resolveEditedSkyLightAtWorld`가 편집된 subchunk에서 시작해 6방향 인접 subchunk의 boundary light를 seed로 읽어 해당 subchunk의 resolved light를 다시 계산한다. 계산된 subchunk face 값이 바뀌면 맞닿은 subchunk를 dirty queue에 넣어 변화가 멈출 때까지 전파하고, 바뀐 subchunk만 edited mesh rebuild 대상에 추가한다.
로드된 청크의 light 배열이 없거나 크기가 맞지 않으면 현재 저장 포맷에서는 legacy save로 보고 사용하지 않는다.
유체 mesh 생성은 count가 `0`인 subchunk를 건너뛴다.

## 비동기 스냅샷 로드

저장된 청크 snapshot 읽기는 render thread가 아니라 단일 chunk-load worker가 처리한다.
`ensureRuntimeChunk`가 없는 런타임 청크를 발견하면 runtime shell을 삽입하고, snapshot loading requested 상태를 표시하고, chunk load job을 큐에 넣은 뒤 즉시 반환한다.

chunk-load worker는 `SaveSystem`의 snapshot load 함수를 호출한다.
main thread는 이후 terrain job 완료 처리 중 완료된 snapshot load를 설치한다.
snapshot이 있으면 runtime chunk data로 변환하고 이전 render/full/mesh/feature ticket을 보존한다.
snapshot이 없으면 shell을 load-finished로 표시하고 일반 생성 요청이 진행될 수 있게 한다.
snapshot에서 복원된 청크가 설치되면 완료 청크 주변 frontier를 다시 검사한다.
이는 저장된 청크가 비동기로 늦게 설치되면서 주변 청크의 feature/light/mesh 조건을 새로 만족하는 경우를 처리하기 위함이다.

chunk-load worker와 save worker가 region payload/header 데이터를 동시에 읽고 쓰지 않도록 `SaveSystem` 내부 region IO mutex로 region file read/write 접근을 직렬화한다.

## 청크 엔티티 데이터

런타임 청크 데이터는 `WorldEntity` 배열을 소유한다.
현재 entity type은 `DroppedItem`이다.

- `entityId`: 월드 엔티티의 고유 runtime/save identity
- `type`: 엔티티 payload type
- `position`, `previousPosition`, `velocity`: 런타임 이동 상태
- `flags`: grounded 같은 공통 상태를 담는 compact 값
- dropped-item payload: `itemId`와 `count`

엔티티는 소유 청크와 함께 이동한다.
두 청크가 모두 로드된 상태에서 엔티티가 청크 경계를 넘으면 소유권을 대상 청크로 이전한다.
드롭 아이템 엔티티의 생성, 드롭 아이템끼리의 물리 충돌, 물리 tick, pickup 판정, 청크 소유권 helper는 `DroppedItemSystem`이 담당한다.
월드 드롭 아이템 엔티티 하나는 아이템 스택 하나를 의미하며, 저장 payload의 `count`는 로드 시 해당 아이템의 `stackSize` 이하로 정규화한다.
드롭 아이템 entity id, 청크별 추적, spawn/drop/pickup/raycast/update 조율은 `DroppedItemRuntime`이 담당하며, `WorldRuntime`의 조회/순회 API를 통해 런타임 청크를 읽는다.
렌더러의 드롭 아이템 draw 경로는 `DroppedItemRuntime`이 제공하는 추적 상태를 렌더 후보 수집에 전달하고 GPU draw만 조율한다.

## 게임 씬 언로드

일시정지에서 로비로 돌아가면 로드된 월드 상태를 유지하지 않고 게임 씬을 언로드한다.
전체 game scene load/unload 순서는 `ClientSceneLifecycle`이 조율하고, terrain worker/save/chunk-load와 pending unload 처리는 `ClientTerrainSceneRuntime`이 담당한다.

언로드 경로는 다음과 같다.

```text
terrain worker 중지
모든 runtime chunk를 저장 큐에 추가
save worker 비우기
Vulkan device idle 대기
로드된 terrain mesh와 retired terrain mesh 파괴
ClientWorldRuntime의 runtime chunk, desired set, requested job set, unload queue 비우기
terrain load request 초기화
```

로비가 렌더러 소유 swapchain, sprite texture, font resource를 계속 사용하므로 렌더러 자체는 살아 있다.
나중에 게임 씬을 다시 시작하면 terrain job system, chunk-load worker, save worker를 다시 시작한다.
