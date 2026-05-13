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

## 청크 칼럼 데이터

런타임 청크 데이터는 블록 배열과 유체 배열 옆에 기후 값을 칼럼별 배열로 보관한다.

- Blocks: `16 x 512 x 16` 블록 ID를 담는 `uint16_t` vector.
- Fluids: `16 x 512 x 16` packed fluid 값을 담는 `uint16_t` vector.
- Temperature: `uint8_t[256]`, `localZ * 16 + localX`로 인덱싱한다.
- Precipitation: `uint8_t[256]`, `localZ * 16 + localX`로 인덱싱한다.
- Fluid subchunk counts: `uint16_t[32]`, `16 x 16 x 16` subchunk마다 하나의 count를 가진다.

`fluidSubchunkCounts`는 파생 런타임 데이터이며 region 파일에는 저장하지 않는다.
생성된 청크는 물을 기록하면서 이 값을 채운다.
로드된 청크는 `blocks`와 `fluids`를 layer-major 순서로 한 번 순회하면서 `emptySubchunks`와 `fluidSubchunkCounts`를 함께 재구축한다.
유체 mesh 생성은 count가 `0`인 subchunk를 건너뛴다.

## 비동기 스냅샷 로드

저장된 청크 snapshot 읽기는 render thread가 아니라 단일 chunk-load worker가 처리한다.
`ensureRuntimeChunk`가 없는 런타임 청크를 발견하면 runtime shell을 삽입하고, snapshot loading requested 상태를 표시하고, chunk load job을 큐에 넣은 뒤 즉시 반환한다.

chunk-load worker는 `loadChunkSnapshot`을 호출한다.
main thread는 이후 terrain job 완료 처리 중 완료된 snapshot load를 설치한다.
snapshot이 있으면 runtime chunk data로 변환하고 이전 render/full/mesh/feature ticket을 보존한다.
snapshot이 없으면 shell을 load-finished로 표시하고 일반 생성 요청이 진행될 수 있게 한다.
snapshot에서 복원된 청크가 `Full` 이상이면 해당 청크와 주변 8청크의 메쉬 조건을 다시 검사한다.
이는 저장된 청크가 비동기로 늦게 설치되면서 주변 청크의 `BuildChunkMesh` 조건을 새로 만족하는 경우를 처리하기 위함이다.

chunk-load worker와 save worker가 region payload/header 데이터를 동시에 읽고 쓰지 않도록 region file read/write 접근은 region IO mutex로 직렬화한다.

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

## 게임 씬 언로드

일시정지에서 로비로 돌아가면 로드된 월드 상태를 유지하지 않고 게임 씬을 언로드한다.

언로드 경로는 다음과 같다.

```text
terrain worker 중지
모든 runtime chunk를 저장 큐에 추가
save worker 비우기
Vulkan device idle 대기
로드된 terrain mesh와 retired terrain mesh 파괴
runtime chunk, desired set, requested job set, unload queue 비우기
terrain load request 초기화
```

로비가 렌더러 소유 swapchain, sprite texture, font resource를 계속 사용하므로 렌더러 자체는 살아 있다.
나중에 게임 씬을 다시 시작하면 terrain worker와 save worker를 다시 시작한다.
