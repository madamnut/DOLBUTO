# 클라이언트 구조

## 방향

DOLBUTO 클라이언트는 `Renderer` 중심 구조에서 런타임 시스템 중심 구조로 전환한다.
구조 기준은 파일 크기가 아니라 역할, 소유권, 의존 방향이다.

## 계층

현재 클라이언트 계층은 아래 방향을 기준으로 둔다.

```text
main.cpp
  -> GameClient
      -> client runtime
          -> world / gameplay / save
      -> renderer facade
          -> renderer internal systems
      -> ui / audio bridge
```

의존 방향은 위에서 아래로만 흐른다.
`world`, `gameplay`, `save` 계층은 `Renderer`, Vulkan, GLFW, RmlUi, OpenAL 타입에 의존하지 않는다.

## main.cpp

`main.cpp`는 프로세스 진입점이다.
로그 초기화, GLFW 초기화, window 생성/파괴, window icon 설정, 최상위 예외 처리를 담당한다.
게임 루프나 월드 규칙, 렌더링 리소스 생성 규칙을 직접 갖지 않는다.

## GameClient

`GameClient`는 composition root이자 최상위 오케스트레이터다.
메인 루프, 입력 callback 연결, 화면 상태 전환, 플레이어 상태 저장/로드, world metadata 저장/로드, subsystem 호출 순서를 조율한다.

`GameClient`는 개별 gameplay 규칙을 직접 소유하지 않는 방향으로 유지한다.
규칙이 커지면 `world`, `gameplay`, `client runtime` 계층으로 옮긴다.

## Client Runtime

클라이언트 런타임 계층은 월드/게임플레이 상태의 클라이언트 실행 흐름을 담당한다.
현재 `ClientWorldRuntime`은 active world 상태, load order, terrain desired/requested/unload set, `WorldRuntime`, `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`을 소유한다.

이 계층은 Renderer/Vulkan 타입을 받지 않는다.
렌더러가 필요한 일은 mesh upload, render data detach, UI 표시, audio event처럼 명시적인 경계 호출로 넘긴다.

## World / Gameplay / Save

`world`, `gameplay`, `save` 계층은 향후 GameServer 분리를 고려한 규칙 계층이다.
이 계층에는 chunk 상태, terrain 생성, save/load, item/entity, inventory, block interaction 같은 규칙을 둔다.

이 계층의 타입은 렌더러가 읽을 수 있지만, 이 계층이 렌더러를 호출하거나 Vulkan 리소스를 저장하지 않는다.

## Renderer

`Renderer`는 외부에서 볼 때 렌더링 facade다.
공개 API는 프레임 렌더링, scene load/unload bridge, UI input bridge, 렌더 기반 질의처럼 클라이언트가 실제로 호출해야 하는 것만 남긴다.

`Renderer` 내부는 다음 책임으로 계속 분리한다.

- Vulkan device/swapchain/render pass/pipeline
- GPU buffer/image/descriptor/upload
- terrain render path
- UI render path
- text/sprite/overlay render path
- particle render path
- dropped item render path

단순히 파일만 나누는 것은 최종 목표가 아니다.
다만 큰 함수를 책임 객체로 옮기기 전, 같은 책임의 구현을 한 파일에 모아 검토 가능하게 만드는 중간 단계는 허용한다.

## 경계 타입

계층 사이에는 구체적인 runtime 내부 타입보다 목적이 드러나는 DTO를 우선한다.
현재 프레임 렌더링 입력은 `RendererFrame`으로 묶고, 월드 목록 표시 데이터는 `game::WorldListItem`으로 둔다.

앞으로 terrain mesh upload, render chunk detach, UI command, audio event 같은 경계도 목적별 DTO로 정리한다.

## 패턴

- `GameClient`: composition root, facade 성격의 오케스트레이터
- `Renderer`: rendering facade
- `ClientWorldRuntime`: client world runtime coordinator
- `WorldRuntime`: world state owner
- `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`: worker/system owner

인터페이스와 추상 base class는 실제 테스트 경계, 서버 분리 경계, platform 교체 경계가 필요할 때만 추가한다.
ECS는 현재 필수 구조로 도입하지 않는다.

## 현재 전환 상태

`Renderer`는 아직 저장 snapshot 생성, snapshot 복원, terrain job callback, 일부 gameplay event 실행 시점, UI/audio bridge를 유지한다.
0.0.0.2에서는 이 책임들을 renderer 밖의 runtime 시스템으로 점진적으로 옮긴다.
