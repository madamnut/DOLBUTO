# 프로젝트 개요

## 목표

DOLBUTO는 Vulkan/C++ 기반의 샌드박스 복셀 게임이다.
최종 방향은 마인크래프트 류의 블록 월드이며, 현재는 월드 생성, 청크 로딩, 메쉬 렌더링, 블록 편집, 저장/로드의 기초 시스템을 구현한 상태다.

## 구현 원칙

- 한 파일이나 한 함수에 모든 기능을 몰아넣지 않고 역할별로 분리한다.
- 구현은 최대한 단순하게 시작하고, 실제 병목이나 필요가 확인될 때만 확장한다.
- 과한 범용화, 미리 만든 대규모 시스템, 불필요한 외부 의존성은 피한다.
- 빌드는 항상 사용자가 직접 수행한다.

## 주요 구성

- 애플리케이션/입력/플레이어: [[player]]
- 런타임 경로: [[runtime-paths]]
- Vulkan 렌더링: [[rendering]]
- 청크 로딩/작업 큐: [[chunk-system]]
- 지형 생성/피처 생성: [[world-generation]]
- 월드 저장/로드: [[save-load]]
- 블록 정의와 텍스처: [[block-data]]
- 디버그/프로파일링: [[debug-profiling]]
- 배포 설정: [[build-and-distribution]]

## 코드 구조

`main.cpp`는 로그 초기화, GLFW/window 생성, 최상위 예외 처리, `GameClient` 실행을 담당한다.
`src/game/GameClient.h/.cpp`는 클라이언트 런타임 오케스트레이터로서 메인 루프, 입력, 플레이어 상태, 월드 선택/생성, 월드/플레이어 상태 저장, UI action 처리를 담당한다.
현재 `Renderer`는 Vulkan 렌더링뿐 아니라 월드 런타임, 청크 저장/로드, UI, 오디오, 아이템 렌더링까지 넓은 책임을 아직 많이 가진다.
0.0.0.2에서는 `GameClient`를 기준으로 전체 흐름과 렌더링 책임을 나누는 방향으로 정리를 진행한다.

현재 분리된 타입 헤더:

```text
src/world/BlockData.h
src/world/WorldTypes.h
src/items/ItemData.h
src/renderer/TerrainTypes.h
```

`WorldTypes.h`는 청크 크기 상수, 청크 데이터, feature write, terrain job, 저장 snapshot, 런타임 청크, 월드 엔티티 같은 월드 런타임 타입을 담는다.

현재 분리된 데이터 로더:

```text
src/data/DataLoaders.h
src/data/DataLoaders.cpp
src/config/ConfigLoaders.h
src/config/ConfigLoaders.cpp
src/assets/PropModelLoader.h
src/assets/PropModelLoader.cpp
src/gameplay/BlockInteractionSystem.h
src/gameplay/BlockInteractionSystem.cpp
src/gameplay/PlayerInventory.h
src/gameplay/PlayerInventory.cpp
src/audio/AudioSystem.h
src/audio/AudioSystem.cpp
src/ui/UiSystem.h
src/ui/UiSystem.cpp
src/ui/InventoryUi.h
src/ui/InventoryUi.cpp
src/ui/RmlInput.h
src/ui/RmlInput.cpp
src/save/SaveFormat.h
src/save/SaveFormat.cpp
src/save/SaveSystem.h
src/save/SaveSystem.cpp
src/world/ChunkLoadSystem.h
src/world/ChunkLoadSystem.cpp
src/world/DroppedItemSystem.h
src/world/DroppedItemSystem.cpp
src/world/TerrainBuilder.h
src/world/TerrainBuilder.cpp
src/world/TerrainJobSystem.h
src/world/TerrainJobSystem.cpp
src/world/TerrainMesher.h
src/world/TerrainMesher.cpp
src/world/WorldRuntime.h
src/world/WorldRuntime.cpp
```

`DataLoaders`는 `assets/data/blocks.json`과 `assets/data/items.json`의 정의 파싱만 담당한다.
`ConfigLoaders`는 `config/world.json`과 `config/render.json`의 설정 파일 읽기와 값 검증/클램프를 담당한다.
`PropModelLoader`는 소품 모델 `.glb` 파싱, `.dpm` 변환/검증, 렌더링용 소품 quad 로드를 담당한다.
`BlockInteractionSystem`은 블록 좌표 변환, 플레이어 충돌 범위 판정, 블록 레이캐스트, 블록 파괴 진행 상태를 담당한다.
`PlayerInventory`는 플레이어 인벤토리 슬롯, 임시 커서 스택, 스택 병합, 클릭/Shift-click/핫바 교환 규칙을 담당한다.
`AudioSystem`은 OpenAL device/context, 효과음 buffer/source pool, 음악 source와 OGG/WAV 재생 상태를 소유한다.
`UiSystem`은 RmlUi 초기화/종료, context, 문서, 버튼 이벤트 수신, 문서 표시 상태, 문서 element 갱신을 소유한다.
`InventoryUi`는 인벤토리/핫바 슬롯 좌표, 아이템/툴팁 RML 문자열, 툴팁 위치 계산을 담당하는 표시 helper다.
`RmlInput`은 GLFW 키/수정키 상태를 RmlUi 입력 값으로 변환한다.
`SaveFormat`은 region 청크 payload 직렬화/역직렬화, LZ4 block encode/decode, 저장 좌표 래핑 helper를 담당한다.
`SaveSystem`은 save worker, 저장 큐, pending snapshot, clean revision cache, region header cache, region file IO, 저장/로드 카운터를 소유한다.
`ChunkLoadSystem`은 chunk-load worker, snapshot load 요청 큐, 완료 큐, 중복 요청 추적을 소유한다.
`DroppedItemSystem`은 드롭 아이템 엔티티 생성, 청크 소유권 helper, 병합, 물리 tick, 플레이어 pickup 판정을 담당한다.
`TerrainBuilder`는 높이맵, 초기 청크 블록/유체/기후 데이터, tree feature write 생성과 feature write 반영을 담당한다.
`TerrainJobSystem`은 terrain worker thread, terrain job 큐, terrain 완료 큐를 소유한다.
`TerrainMesher`는 chunk mesh와 편집 subchunk mesh의 CPU orchestration을 담당한다.
`WorldRuntime`은 runtime chunk map, chunk key/좌표 helper, runtime block 조회/수정, dirty marking, chunk derived cache 갱신, terrain 완료 결과의 runtime chunk 상태 설치를 소유한다.
`Renderer` 구현은 `src/renderer/Renderer.cpp`, `src/renderer/RendererUi.cpp`, `src/renderer/RendererDroppedItems.cpp`로 나뉘어 있다.
`RendererDroppedItems.cpp`는 dropped item의 아이템 스프라이트 mesh, GPU instance 업로드, draw path, Renderer callback 연결을 유지하며, runtime chunk 조회/순회는 `WorldRuntime`을 경유한다.
텍스처 배열 생성, Vulkan 리소스 생성은 아직 `Renderer`가 담당한다.
`Renderer`는 오디오 초기화/종료, listener 갱신, 음악 씬 분류, 효과음 발생 시점만 `AudioSystem`에 전달한다.
`Renderer`는 RmlUi의 Vulkan `RenderInterface`, UI geometry 업로드, 인벤토리 입력을 `PlayerInventory`에 전달하고 아이템 정의를 표시용 데이터로 변환하는 역할을 아직 담당한다.
`Renderer`는 블록 상호작용 입력을 `BlockInteractionSystem`에 전달하고 실제 블록 쓰기, mesh 재생성, particle/sound 실행을 담당한다.
`Renderer`는 runtime chunk를 직접 보관하지 않고 `WorldRuntime`의 조회/순회/생성 API를 통해 접근하며, 저장할 runtime chunk snapshot 생성과 snapshot에서 runtime chunk를 복원하는 역할을 유지한다.
`Renderer`는 terrain job callback에서 `TerrainBuilder`와 `TerrainMesher`를 호출하며, terrain 완료 결과의 runtime 상태 설치는 `WorldRuntime`에 위임하고 GPU mesh buffer 설치를 담당한다.
fluid subchunk mesh 생성은 `TerrainMesher`가 맡고, 불투명 블록 판정은 `Renderer` callback을 사용한다.

## 좌표계

- `X+`: 동쪽
- `Z+`: 북쪽
- 블록 좌표의 기준은 블록의 아래 중앙이다.
- 플레이어 좌표 `POS`는 발밑 중앙이다.
- 1인칭 눈 위치는 플레이어 `Y + 1.5625`다.

## 현재 주의점

- 현재 버전은 `0.0.0.2`이다.
- `docs/design`은 확정된 설계만 남기는 공간이고, 실험 과정은 `docs/0.0.0.2` 날짜 로그에 기록한다.
- Release 경로와 Debug 경로가 다르므로 경로 관련 작업은 [[runtime-paths]] 기준을 따른다.
