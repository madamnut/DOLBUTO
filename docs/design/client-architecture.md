# 클라이언트 구조

## 방향

DOLBUTO 클라이언트는 `Renderer` 중심 구조에서 런타임 시스템 중심 구조로 전환한다.
구조 기준은 파일 크기가 아니라 역할, 소유권, 의존 방향이다.

## 계층

현재 클라이언트 계층은 아래 방향을 기준으로 둔다.

```text
main.cpp
  -> GameClient
      -> ClientRuntimeFacade
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
`GameClient`는 `Renderer`를 직접 소유하지 않고 `ClientRuntimeFacade`를 통해 scene/gameplay/UI/render 경계를 호출한다.

`GameClient`는 개별 gameplay 규칙을 직접 소유하지 않는 방향으로 유지한다.
규칙이 커지면 `world`, `gameplay`, `client runtime` 계층으로 옮긴다.
game scene 전환의 세부 순서도 `Renderer`가 아니라 `ClientSceneLifecycle` 같은 client runtime 계층에서 조율한다.

## Client Runtime

현재 구현 기준으로 저장 snapshot 생성/복원과 feature slot 전파는 `ClientWorldRuntime`이 담당한다.
`GameClient`가 사용하는 클라이언트 런타임 API는 `ClientRuntimeFacade`가 묶는다.
game scene load/unload 순서, active world 설정, gameplay reset, terrain scene start/stop, save flush는 `ClientSceneLifecycle`이 담당한다.
terrain scene load request, worker lifecycle, completed work drain, pending unload의 world/save 처리는 `ClientTerrainSceneRuntime`이 담당한다.
terrain request cascade와 feature finalize/mesh retry job queue 조율은 `ClientTerrainCoordinator`가 담당한다.
terrain/chunk-load 완료 결과의 save/install/ignore/retry 처리 흐름은 `ClientTerrainCompletionHandler`가 담당한다.
`BuildFeaturing`/`FinalizeFeatures` terrain job 처리는 `ClientTerrainJobProcessor`가 담당하고, render-dependent mesh job CPU 조립은 `RendererTerrainMeshBridge`가 담당한다.
`Renderer`는 일부 gameplay event 실행 시점, UI/audio bridge, GPU mesh install 경계를 유지한다.

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
공개 API는 프레임 렌더링과 swapchain resize 같은 렌더러 직접 경계로 제한한다.
scene/gameplay/UI 조작 API는 `ClientRuntimeFacade`에서만 접근하는 전환 경계로 둔다.

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

현재 GPU buffer/image/texture 생성과 upload helper는 `VulkanResourceManager`가 담당한다.
`Renderer`는 Vulkan device, queue, command pool, descriptor pool/layout, sampler handle을 주입하고 frame/scene 흐름에서 필요한 리소스 생성을 호출한다.
현재 terrain render chunk storage, render chunk 설치/교체/retire 규칙, retired mesh cleanup, packed quad 변환, terrain GPU upload, terrain mesh draw loop와 frustum culling은 `TerrainRenderPath`가 담당한다.
`Renderer`는 terrain job completion과 edit mesh 결과를 의미 기반 API로 `TerrainRenderPath`에 전달하고, draw pass에서 pipeline/push constant/texture descriptor 준비 뒤 terrain draw를 위임하는 facade로 남긴다.

## 경계 타입

계층 사이에는 구체적인 runtime 내부 타입보다 목적이 드러나는 DTO를 우선한다.
현재 프레임 렌더링 입력은 `RendererFrame`으로 묶고, 월드 목록 표시 데이터는 `game::WorldListItem`으로 둔다.

앞으로 terrain mesh upload, render chunk detach, UI command, audio event 같은 경계도 목적별 DTO로 정리한다.

## 패턴

- `GameClient`: composition root, facade 성격의 오케스트레이터
- `ClientRuntimeFacade`: GameClient-facing client runtime facade
- `Renderer`: rendering facade
- `ClientSceneLifecycle`: game scene lifecycle coordinator
- `ClientTerrainSceneRuntime`: terrain scene lifecycle coordinator
- `ClientTerrainCoordinator`: terrain request/job queue coordinator
- `ClientTerrainCompletionHandler`: terrain completion flow handler
- `ClientWorldRuntime`: client world runtime coordinator
- `WorldRuntime`: world state owner
- `SaveSystem`, `ChunkLoadSystem`, `TerrainJobSystem`: worker/system owner

인터페이스와 추상 base class는 실제 테스트 경계, 서버 분리 경계, platform 교체 경계가 필요할 때만 추가한다.
ECS는 현재 필수 구조로 도입하지 않는다.

## 현재 전환 상태

`Renderer`는 아직 terrain job callback, 일부 gameplay event 실행 시점, UI/audio bridge, GPU mesh install 경계를 유지한다.
0.0.0.2에서는 이 책임들을 renderer 밖의 runtime 시스템으로 점진적으로 옮긴다.
