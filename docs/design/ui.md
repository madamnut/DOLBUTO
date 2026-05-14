# UI

## 현재 방향

DOLBUTO는 외부 파일 기반 UI 배치에 RmlUi를 사용한다.

RmlUi 소스는 참고용으로 다음 위치에 포함되어 있다.

```text
third_party/RmlUi
```

포함된 RmlUi 버전은 `6.1`이다.

FreeType 소스는 참고용으로 다음 위치에 포함되어 있다.

```text
third_party/freetype-2.14.2
```

포함된 FreeType 버전은 `2.14.2`이다.

## 빌드 통합

현재 게임 빌드는 `third_party/prebuilt` 아래의 사전 빌드 FreeType/RmlUi 공유 라이브러리를 링크한다.
RmlUi와 FreeType 소스 트리는 참고용이며, 일반 게임 빌드에서 다시 컴파일하지 않는다.

현재 사전 빌드 라이브러리 생성 기준은 다음과 같다.

- `BUILD_SHARED_LIBS = ON`
- `RMLUI_SAMPLES = OFF`
- `RMLUI_LUA_BINDINGS = OFF`
- `RMLUI_LOTTIE_PLUGIN = OFF`
- `RMLUI_SVG_PLUGIN = OFF`
- `RMLUI_FONT_ENGINE = freetype`
- `RMLUI_PRECOMPILED_HEADERS = OFF`
- `RMLUI_COMPILER_OPTIONS = OFF`

루트 `CMakeLists.txt`는 `Freetype::Freetype`와 `RmlUi::Core`를 imported target으로 만들고, 게임 타깃은 이 둘을 링크한다.

## 현재 런타임 상태

디버그 텍스트는 렌더러의 네이티브 FreeType 텍스트 경로를 사용한다.
플레이어가 보는 메뉴 UI는 `assets/ui` 아래의 RmlUi 문서를 사용한다.
로비, 월드 선택, 월드 생성, 일시정지 패널은 화면 중앙에 배치한다.

현재 화면 구성은 다음과 같다.

```text
로비:
  DOLBUTO
  START
  EXIT

월드 선택:
  SELECT WORLD
  저장된 월드 목록
  NEW WORLD
  EXIT

월드 생성:
  NEW WORLD
  새 월드 이름
  새 월드 시드
  CREATE
  EXIT

게임:
  하단 중앙 핫바 HUD
  F1 -> 핫바 HUD와 크로스헤어 숨김/표시
  1-9/0 또는 마우스 휠 -> 선택 핫바 슬롯 변경
  E -> 인벤토리
  ESC -> 일시정지

인벤토리:
  중앙 인벤토리 패널
  E / ESC -> 게임

일시정지:
  RESUME
  EXIT
```

로비의 `START`는 월드 선택 화면을 연다.
로비의 `EXIT`는 프로그램을 종료한다.
로비 타이틀 이미지는 `1040 x 280` 픽셀로 렌더링하며, `520px` 로비 스택 위에 음수 왼쪽 여백을 주어 중앙에 맞춘다.
월드 선택 화면은 월드 행 더블클릭으로 기존 월드에 들어가거나 새 월드 생성 화면으로 이동한다.
월드 목록은 높이가 큰 스크롤 목록이며, 보이는 드래그 가능 스크롤바, 마우스 휠 스크롤, 행 hover 피드백, 더블클릭 진입을 지원한다.
월드 선택의 `NEW WORLD`는 새 월드 생성 화면을 연다.
월드 선택의 `EXIT`는 로비로 돌아간다.
월드 생성의 `CREATE`는 새 월드를 만들고 게임에 진입한다.
월드 생성의 `EXIT`는 월드 선택으로 돌아간다.
일시정지의 `RESUME`은 게임으로 돌아간다.
일시정지의 `EXIT`는 게임 씬을 언로드하고 로비로 돌아간다.

로비 또는 일시정지 오버레이가 활성화되면 플레이어 이동, 카메라 회전, 블록 선택, 블록 편집, 크로스헤어를 비활성화한다.
로비와 월드 선택은 청크 로딩을 요청하거나 처리하지 않는다.
로비와 월드 선택에서는 게임 씬을 렌더링하지 않으며, 하늘, 지형, 유체, 플레이어, 선택 표시, 기후 오버레이, 크로스헤어, 디버그 텍스트를 건너뛴다.
일시정지는 플레이어 입력을 막되 청크 로딩은 계속 유지한다.
일시정지는 게임 씬을 일시정지 오버레이 뒤에 계속 렌더링하지만 좌상단 디버그 텍스트는 숨긴다.
인벤토리는 플레이어 입력만 막고 청크 로딩과 게임 시뮬레이션은 유지한다.
인벤토리는 게임 씬, 디버그 텍스트, 핫바를 반투명 검정 오버레이 뒤에 계속 렌더링하며 마우스 커서를 해제한다.
인게임 HUD는 일반 게임 화면과 인벤토리 화면에서 표시되는 RmlUi 문서이며, 현재는 하단 중앙 핫바 껍데기만 포함한다.
F1은 일반 게임 화면 HUD 표시 여부를 토글한다.
숨김 상태에서는 핫바 RmlUi 문서와 네이티브 크로스헤어 스프라이트를 렌더링하지 않는다.
디버그 텍스트, 메뉴, 일시정지 UI, 인벤토리 UI는 F1로 숨기지 않는다.

핫바와 인벤토리 스프라이트는 같은 4배 픽셀 스케일을 사용해 슬롯 크기를 맞춘다.
핫바 선택 스코프는 핫바 왼쪽과 아래쪽 가장자리에서 원본 기준 3픽셀만큼 오프셋된다.
핫바 슬롯은 왼쪽부터 `1`부터 `9`, 그 다음 `0`으로 선택한다.
현재 선택 스코프는 4배 스케일에서 슬롯 사이를 원본 기준 17픽셀 단위로 이동한다.

런타임 인벤토리는 50개의 아이템 슬롯을 가진다.
슬롯 `0`부터 `9`까지는 핫바이며 HUD와 인벤토리 화면의 맨 아래 줄에 모두 표시된다.
인벤토리 화면은 10열 5행을 사용한다.
맨 아래 줄은 슬롯 `0`부터 `9`이고, 그 위 행부터 `10`~`19`, `20`~`29`, `30`~`39`, `40`~`49` 순서다.
인벤토리 슬롯 아이콘은 원본 픽셀 좌표를 4배 스케일로 환산해 배치한다.
인벤토리 스프라이트는 원본 기준 외곽 패딩 4픽셀, 아이템 슬롯 `16 x 16`픽셀, 슬롯 간격 1픽셀, 핫바 줄과 위 줄 사이 간격 6픽셀을 사용한다.
아이템 스택 개수 텍스트는 24px 글꼴 크기를 사용하고, 각 `64 x 64` 슬롯 안에서 상단 오프셋 40px, 텍스트 박스 높이 20px, 오른쪽 정렬 텍스트 박스 폭 48px, 슬롯 오른쪽에서 2px 안쪽 위치에 배치한다.
핫바 아이템 슬롯은 아이템 아이콘 뒤에 항상 반투명 검정 배경을 렌더링한다.
인벤토리 아이템 슬롯은 기본적으로 투명하며, hover 상태에서만 반투명 검정 배경을 렌더링한다.
아이템 아이콘과 스택 개수는 hover 배경보다 위에 렌더링한다.
디버그 슬롯 오버레이는 활성화 시 회색 사각형과 빨간색 `0`~`49` 슬롯 인덱스를 렌더링하지만, 기본값은 비활성화이며 비활성화 중에는 RmlUi hit test 대상에서도 제외된다.

인벤토리 조작은 임시 커서 슬롯을 사용한다.
커서 슬롯은 저장되는 50개 인벤토리 슬롯에 포함되지 않으며, 인벤토리 화면을 닫으면 런타임 인벤토리로 반환된다.
인벤토리 왼쪽 클릭은 전체 스택 집기, 커서 전체 스택 놓기, 같은 아이템 스택 제한까지 병합, 다른 스택과 교환을 수행한다.
인벤토리 오른쪽 클릭은 커서가 비어 있을 때 절반 스택을 집거나, 빈 슬롯에 커서 아이템 1개를 놓거나, 같은 아이템의 비어 있지 않은 스택에 1개를 추가한다.
Shift-클릭은 클릭한 스택을 반대 인벤토리 영역으로 옮긴다.
핫바 슬롯은 메인 인벤토리로, 메인 인벤토리 슬롯은 핫바로 이동한다.
인벤토리가 열린 동안 숫자 키 `1`~`9`와 `0`은 커서 슬롯이 비어 있을 때 hover 중인 슬롯과 대응 핫바 슬롯을 교환한다.

인벤토리 아이템 툴팁은 `assets/textures/ui/Tooltip.png`를 RmlUi `ninepatch` decorator로 사용한다.
툴팁 스프라이트 시트는 `16 x 16` 외곽 스프라이트와 6px 테두리를 사용하므로 내부 stretch 사각형은 `6px 6px 4px 4px`이다.
툴팁은 인벤토리가 열려 있고, 커서 스택이 비어 있으며, 마우스가 아이템 슬롯 위에 있을 때만 표시된다.
툴팁은 아이템 이름과 `id`, `key`, 개수, 스택 크기, 슬롯 텍스처, 드랍 렌더, 드랍 텍스처, 손 렌더, 손 텍스처 디버그 필드를 표시한다.
툴팁 배경 이미지는 RCSS `image-color`로 알파를 곱해 원본 PNG가 불투명해도 9패치 배경이 반투명하게 보이도록 한다.
툴팁 너비는 표시되는 텍스트 줄 중 가장 긴 줄에서 계산하며, 최소 180px, 최대 520px로 제한한다.
툴팁 위치는 마우스 위치에서 16px 떨어진 곳에서 시작하고, 현재 프레임버퍼 바깥으로 나가지 않도록 가로/세로 방향을 뒤집거나 클램프한다.

로비에서 게임으로 나온 첫 프레임은 플레이어가 기본 중심 청크 그룹에 그대로 있더라도 초기 지형 로드를 강제로 수행한다.
로비와 게임 씬의 생명주기는 분리되어 있다.
로비가 표시되는 동안에도 렌더러는 살아 있으므로 swapchain, 메뉴 텍스처, 폰트 atlas를 계속 사용할 수 있다.
게임 씬은 terrain/save worker, 로드된 런타임 청크, 지형 메시, 지형 작업 큐, terrain worker 진행 상태를 소유한다.
게임 시작은 게임 씬 worker를 시작하고 지형 로드 요청 플래그를 초기화한다.
일시정지에서 로비로 돌아가면 플레이어 상태를 저장하고, terrain worker를 멈추고, 모든 런타임 청크 저장을 큐에 넣고, save worker를 비우고, Vulkan device idle을 기다린 뒤, 로드된 지형 렌더 데이터를 파괴하고, 런타임 청크 상태를 비우고, 지형 로드 요청 플래그를 초기화한다.
로비에서 다시 게임을 시작하면 첫 게임 프레임에 지형 로딩을 다시 시작한다.

RmlUi 메뉴 문서는 다음과 같다.

```text
assets/ui/lobby.rml
assets/ui/world_select.rml
assets/ui/world_create.rml
assets/ui/hud.rml
assets/ui/inventory.rml
assets/ui/pause.rml
assets/ui/style.rcss
```

플레이어 UI 스프라이트는 다음 위치에 둔다.

```text
assets/textures/ui/player
```

월드 선택 문서는 저장된 월드 목록과 하단 작업 버튼을 가진다.
저장된 월드 행에는 생성 시각과 최근 플레이 시각을 표시한다.
월드 생성 문서는 이름과 시드 입력창을 가진다.
월드 목록 항목은 `saves` 아래 월드별 저장 디렉터리를 스캔해 채운다.

## 런타임 통합

RmlUi 런타임 수명주기와 문서 소유권은 `src/ui/UiSystem.h`와 `src/ui/UiSystem.cpp`에 둔다.
인벤토리/핫바/툴팁의 표현 문자열과 좌표 계산은 `src/ui/InventoryUi.h`와 `src/ui/InventoryUi.cpp`에 둔다.
GLFW 입력 값을 RmlUi 입력 값으로 변환하는 코드는 `src/ui/RmlInput.h`와 `src/ui/RmlInput.cpp`에 둔다.
Vulkan 렌더링 자원과 화면별 RML 내용 갱신은 기존 Vulkan device, render pass, descriptor set layout, sampler, texture upload 경로, 런타임 asset 경로 helper를 재사용해야 하므로 아직 `Renderer`에 남아 있다.

`UiSystem`이 소유하는 항목은 다음과 같다.

- RmlUi 초기화와 종료
- RmlUi context 생명주기
- RmlUi 문서 로드와 종료
- 메뉴 문서 show/hide 상태
- 버튼 click과 월드 행 dblclick 이벤트 수신
- UI action 큐
- GLFW 기반 RmlUi system interface
- RmlUi mouse/key/text/wheel 입력 전달
- 핫바 scope class, 인벤토리/툴팁/월드 목록 element 갱신
- 월드 목록 RML 생성

`RmlInput`이 소유하는 항목은 다음과 같다.

- GLFW key를 RmlUi key identifier로 변환
- GLFW modifier bit를 RmlUi modifier bit로 변환
- 현재 GLFW modifier 상태를 RmlUi modifier bit로 계산

`InventoryUi`가 소유하는 항목은 다음과 같다.

- 인벤토리와 핫바 슬롯 좌표 계산
- 인벤토리 hit test용 슬롯 판정
- 디버그 슬롯, 아이템 슬롯, 커서 아이템 RML 생성
- 아이템 툴팁 RML 생성
- 툴팁 크기와 화면 안쪽 위치 계산

`Renderer`가 계속 소유하는 항목은 다음과 같다.

- Vulkan `Rml::RenderInterface` 구현
- UI geometry 업로드 버퍼
- 전용 RmlUi graphics pipeline
- 기존 texture upload 경로를 통한 UI texture 로딩
- 런타임 인벤토리 상태와 커서 슬롯 상태
- 아이템 정의를 `InventoryUi` 표시 데이터로 변환
- 인벤토리 슬롯 클릭과 숫자키 핫바 교환 같은 조작 처리

GameClient는 게임 화면이 아닐 때 GLFW 마우스, 텍스트, 기본 키 입력을 RmlUi context로 전달한다.
인벤토리는 비게임 입력 화면으로 취급하므로 마우스 이동과 클릭은 플레이어 카메라나 블록 상호작용 대신 RmlUi로 전달된다.
키보드 입력은 GLFW modifier 상태를 RmlUi로 전달해 텍스트 입력창이 Shift+Arrow, Ctrl+C/V 같은 선택 및 편집 단축키를 처리할 수 있게 한다.
`UiSystem`은 경과 시간과 클립보드 접근을 위해 GLFW 기반의 작은 RmlUi system interface를 제공한다.
`UiSystem`은 GameClient에서 들어온 mouse/key/text/wheel 입력을 RmlUi context로 전달한다.
RmlUi 클릭/더블클릭 이벤트는 `UiSystem`이 action으로 보관하고, GameClient가 메뉴 action으로 소비한다.
핫바, 인벤토리, 툴팁, 월드 목록은 `UiSystem` 메서드를 통해 문서 element에 반영한다.
기존 네이티브 메뉴 hit test는 RmlUi를 사용할 수 없을 때의 fallback으로만 유지하며, 정상 RmlUi 메뉴 클릭 뒤에는 실행되면 안 된다.
현재 메뉴 문서는 주요 layout block에 명시적 absolute positioning을 사용한다.
이는 현재 RmlUi 통합에서 자동 margin이 있는 browser-style stacked layout보다 예측 가능하기 때문이다.
RmlUi 문서의 상대 이미지 경로는 `assets/ui` 기준으로 해석하므로 `../textures/ui/Title.png` 같은 참조가 일반 asset tree를 통해 로드된다.
RmlUi가 이미 결합되었지만 유효하지 않은 절대 텍스처 경로를 제공하면, 렌더러는 `/textures/...` suffix를 `assetDirectory()/textures` 아래로 다시 매핑한다.
로비와 월드 메뉴 배경은 RmlUi가 소유하며, 네이티브 메뉴 스프라이트 fallback 대신 repeat mode 이미지 decorator를 사용한다.
일시정지 메뉴는 반투명 검정 RmlUi 오버레이 뒤에 게임 씬을 계속 보이게 한다.

`third_party/RmlUi/Backends` 아래의 RmlUi backend 소스는 참고 코드다.
런타임 코드는 sample backend target에 직접 의존하지 말고 필요한 아이디어를 엔진 쪽으로 복사하거나 맞게 변형해야 한다.
제공되는 Vulkan backend는 자체 Vulkan instance와 swapchain을 소유하므로 현재 렌더러에 직접 연결하면 안 된다.

## 참고

네이티브 메뉴 오버레이는 RmlUi 초기화 실패 시 fallback 경로로 남겨둔다.
월드별 저장 디렉터리는 RmlUi 월드 목록과 새 월드 생성 흐름에서 선택된다.

## 관련 문서

- [[rendering]]
- [[runtime-paths]]
- [[build-and-distribution]]
