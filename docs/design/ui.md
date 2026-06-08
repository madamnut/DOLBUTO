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
월드 생성 화면은 클릭할 때마다 `SURVIVAL`/`SANDBOX`가 전환되는 게임모드 토글 버튼을 가지며, 기본 선택값은 `SANDBOX`다.
월드 생성의 `EXIT`는 월드 선택으로 돌아간다.
일시정지의 `RESUME`은 게임으로 돌아간다.
일시정지의 `EXIT`는 게임 씬을 언로드하고 로비로 돌아간다.
Options 화면은 BGM/SFX 볼륨, FOV, View Bobbing, sprint/sneak hold-toggle 설정을 제공한다.
`View Bobbing`은 `config/settings.json`의 `video.viewBobbing`에 저장하며, ON이면 1인칭/3인칭 ground 이동 렌더 카메라 위치에 보빙을 적용한다.

로비 또는 일시정지 오버레이가 활성화되면 플레이어 이동, 카메라 회전, 블록 선택, 블록 편집, 크로스헤어를 비활성화한다.
로비와 월드 선택은 청크 로딩을 요청하거나 처리하지 않는다.
로비와 월드 선택에서는 게임 씬을 렌더링하지 않으며, 하늘, 지형, 유체, 플레이어, 선택 표시, 기후 오버레이, 크로스헤어, 디버그 텍스트를 건너뛴다.
일시정지는 플레이어 입력을 막되 청크 로딩은 계속 유지한다.
일시정지는 게임 씬을 일시정지 오버레이 뒤에 계속 렌더링하지만 좌상단 디버그 텍스트는 숨긴다.
인벤토리는 플레이어 입력만 막고 청크 로딩과 게임 시뮬레이션은 유지한다.
인벤토리는 게임 씬, 디버그 텍스트, 핫바를 반투명 검정 오버레이 뒤에 계속 렌더링하며 마우스 커서를 해제한다.
인게임 HUD는 일반 게임 화면과 인벤토리 화면에서 표시되는 RmlUi 문서이며, 하단 중앙 핫바와 핫바 왼쪽 왼손 슬롯을 포함한다.
게임 화면에서 드랍 아이템 상호작용 후보가 열리면 HUD 문서 안의 원형 후보 UI를 표시한다.
원형 후보 UI는 화면 중앙의 미선택 원, 중간 액션 링, 바깥 후보 아이템 링으로 구성된다.
원형 후보 UI가 처음 열릴 때는 액션이 하나뿐이어도 자동 선택하지 않고 중앙 `Cancel` 상태로 시작한다.
부채꼴 배경과 선택 하이라이트는 RmlUi가 아니라 native Vulkan 경로인 `RadialMenuRenderPath`가 그린다.
RmlUi는 액션 심볼, 후보 아이템 아이콘, 중앙 선택 라벨만 담당한다.
액션/후보 아이콘은 각 부채꼴 링 두께의 정중앙 반지름에 배치한다.
대상 스택 개수가 부족해서 1회 실행할 수 없는 후보는 해당 후보가 차지하는 바깥 링 구간을 빨간색으로 표시한다.
중앙 라벨은 선택된 후보가 있으면 후보 아이템 이름을, 후보가 없고 액션만 선택되어 있으면 액션 이름을, 선택이 없으면 `Cancel`을 표시한다.
블록 액션과 손에 든 아이템 액션이 결합된 상호작용은 같은 액션 구간 안에 여러 액션 심볼을 함께 배치하고, 중앙 라벨은 `Craft + Cut`처럼 결합된 액션 이름을 표시한다.
액션 심볼은 `assets/textures/symbol/actions/<action>.png` 경로를 사용한다.
선택 판정은 `GameClient`가 화면 중앙과 마우스 위치의 거리/각도로 계산한다.
F1은 일반 게임 화면 HUD 표시 여부를 토글한다.
숨김 상태에서는 핫바 RmlUi 문서와 네이티브 크로스헤어 스프라이트를 렌더링하지 않는다.
디버그 텍스트, 메뉴, 일시정지 UI, 인벤토리 UI는 F1로 숨기지 않는다.
원형 후보 UI가 열린 동안에는 F1으로 HUD가 숨겨져 있어도 HUD 문서를 렌더링한다.

핫바와 인벤토리 스프라이트는 같은 4배 픽셀 스케일을 사용해 슬롯 크기를 맞춘다.
핫바 선택 스코프는 핫바 왼쪽과 아래쪽 가장자리에서 원본 기준 3픽셀만큼 오프셋된다.
핫바 슬롯은 왼쪽부터 `1`부터 `9`, 그 다음 `0`으로 선택한다.
현재 선택 스코프는 4배 스케일에서 슬롯 사이를 원본 기준 17픽셀 단위로 이동한다.
왼손 슬롯은 핫바 왼쪽에 별도 HUD 슬롯으로 표시하며, `assets/textures/ui/player/slot.png`를 단일 슬롯 배경으로 사용한다.
왼손 슬롯 배경은 `24 x 24` 원본 슬롯 스프라이트를 4배 스케일한 `96 x 96` 크기로 렌더링한다.
왼손 슬롯의 아이템 아이콘, 개수 텍스트, 내구도 바는 배경 안쪽 `16px, 16px` 위치에서 핫바 슬롯과 같은 `64 x 64` 표시 규칙을 사용한다.

런타임 인벤토리는 50개의 아이템 슬롯을 가진다.
슬롯 `0`부터 `9`까지는 핫바이며 HUD와 인벤토리 화면의 맨 아래 줄에 모두 표시된다.
왼손 슬롯은 별도 저장 슬롯이며 인벤토리 화면의 50칸 패널에는 표시하지 않는다.
인벤토리 화면은 10열 5행을 사용한다.
맨 아래 줄은 슬롯 `0`부터 `9`이고, 그 위 행부터 `10`~`19`, `20`~`29`, `30`~`39`, `40`~`49` 순서다.
인벤토리 슬롯 아이콘은 원본 픽셀 좌표를 4배 스케일로 환산해 배치한다.
인벤토리 스프라이트는 원본 기준 외곽 패딩 4픽셀, 아이템 슬롯 `16 x 16`픽셀, 슬롯 간격 1픽셀, 핫바 줄과 위 줄 사이 간격 6픽셀을 사용한다.
아이템 개수 텍스트는 24px 글꼴 크기를 사용하고, 각 `64 x 64` 슬롯 안에서 상단 오프셋 40px, 텍스트 박스 높이 20px, 오른쪽 정렬 텍스트 박스 폭 48px, 슬롯 오른쪽에서 2px 안쪽 위치에 배치한다.
내구도가 있는 아이템은 현재 내구도가 최대보다 낮고 0보다 클 때 슬롯 하단에 내구도 바를 표시한다.
내구도 바 배경은 검정색이며, 채움은 왼쪽에서 시작해 남은 내구도 비율만큼 표시한다.
내구도 1은 배경만 보이는 상태로 처리하고, 최대 내구도 상태에서는 바를 숨긴다.
채움 색은 낮을수록 빨강, 중간은 노랑, 높을수록 연두에 가깝게 보간한다.
핫바 아이템 슬롯은 아이템 아이콘 뒤에 항상 반투명 검정 배경을 렌더링한다.
인벤토리 아이템 슬롯은 기본적으로 투명하며, hover 상태에서만 반투명 검정 배경을 렌더링한다.
아이템 아이콘과 개수 텍스트는 hover 배경보다 위에 렌더링한다.
디버그 슬롯 오버레이는 활성화 시 회색 사각형과 빨간색 `0`~`49` 슬롯 인덱스를 렌더링하지만, 기본값은 비활성화이며 비활성화 중에는 RmlUi hit test 대상에서도 제외된다.

인벤토리 조작은 임시 커서 슬롯을 사용한다.
커서 슬롯은 저장되는 50개 인벤토리 슬롯에 포함되지 않으며, 인벤토리 화면을 닫으면 런타임 인벤토리로 반환된다.
인벤토리 왼쪽 클릭은 슬롯 아이템 집기, 커서 아이템 놓기, 다른 아이템과 교환을 수행한다.
인벤토리 오른쪽 클릭은 커서가 비어 있을 때 슬롯 아이템을 집거나, 빈 슬롯에 커서 아이템 1개를 놓는다.
Shift-클릭은 클릭한 아이템을 반대 인벤토리 영역으로 옮긴다.
핫바 슬롯은 메인 인벤토리로, 메인 인벤토리 슬롯은 핫바로 이동한다.
인벤토리가 열린 동안 숫자 키 `1`~`9`와 `0`은 커서 슬롯이 비어 있을 때 hover 중인 슬롯과 대응 핫바 슬롯을 교환한다.
게임 화면 또는 인벤토리 화면에서 `R`은 현재 선택된 핫바 슬롯과 왼손 슬롯을 교환한다.

인벤토리 아이템 툴팁은 `assets/textures/ui/Tooltip.png`를 RmlUi `ninepatch` decorator로 사용한다.
툴팁 스프라이트 시트는 `16 x 16` 외곽 스프라이트와 6px 테두리를 사용하므로 내부 stretch 사각형은 `6px 6px 4px 4px`이다.
툴팁은 인벤토리가 열려 있고, 커서 아이템이 비어 있으며, 마우스가 아이템 슬롯 위에 있을 때만 표시된다.
툴팁 제목은 25px 글꼴과 28px 라인 높이를 사용한다.
툴팁 세부 라인은 20px 글꼴과 24px 라인 높이를 사용한다.
툴팁은 아이템 이름과 `id`, `key`, 개수, 우클릭 액션, 좌클릭 파괴 액션, 파괴 레벨, 내구도, 연소 시간, 연료 열 레벨, 스택 크기, 슬롯 텍스처, 드랍 렌더, 드랍 텍스처, 손 렌더, 손 텍스처 디버그 필드를 표시한다.
툴팁 배경 이미지는 RCSS `image-color`로 알파를 곱해 원본 PNG가 불투명해도 9패치 배경이 반투명하게 보이도록 한다.
툴팁 크기는 `width`와 `height`를 직접 지정하지 않고 RmlUi의 absolute auto layout으로 정한다.
툴팁 제목과 세부 라인은 줄바꿈하지 않으므로 content box는 실제 텍스트 줄 폭을 기준으로 잡힌다.
툴팁 위치는 마우스 위치에서 16px 떨어진 곳에서 시작하고, 현재 프레임버퍼 바깥으로 나가지 않도록 기본 좌표 계산 단계에서 가로/세로 방향을 뒤집거나 클램프한다.

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

assets/ui/options.rml
assets/ui/style.rcss
```

플레이어 UI 스프라이트는 다음 위치에 둔다.

```text
assets/textures/ui/player
```

월드 선택 문서는 저장된 월드 목록과 하단 작업 버튼을 가진다.
저장된 월드 행에는 생성 시각과 최근 플레이 시각을 표시한다.
월드 생성 문서는 이름, 시드 입력창과 게임모드 토글 버튼을 가진다.
월드 목록 항목은 `saves` 아래 월드별 저장 디렉터리를 스캔해 채운다.

## 런타임 통합

RmlUi 런타임 수명주기와 문서 소유권은 `src/ui/UiSystem.h`와 `src/ui/UiSystem.cpp`에 둔다.
인벤토리/핫바/툴팁의 표현 문자열과 기본 좌표 계산은 `src/ui/InventoryUi.h`와 `src/ui/InventoryUi.cpp`에 둔다.
툴팁 element의 최종 박스 크기는 `src/ui/UiSystem.cpp`가 `width`/`height`를 강제하지 않고 RmlUi 레이아웃에 맡긴다.
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
- 툴팁 기본 위치 계산

`Renderer`가 계속 소유하는 항목은 다음과 같다.

- Vulkan `Rml::RenderInterface` 구현
- UI geometry 업로드 버퍼
- 전용 RmlUi graphics pipeline
- 기존 texture upload 경로를 통한 UI texture 로딩
- 런타임 인벤토리 상태와 커서 슬롯 상태
- 아이템 정의를 `InventoryUi` 표시 데이터로 변환
- 인벤토리 슬롯 클릭과 숫자키 핫바 교환 같은 조작 처리

GameClient는 게임 화면이 아닐 때 GLFW 마우스, 텍스트, 기본 키 입력을 RmlUi context로 전달한다.
게임 화면에서도 HUD 채팅 입력이 열려 있으면 GLFW 마우스, 텍스트, 기본 키 입력을 RmlUi context로 전달하고 gameplay 입력은 소비한다.
채팅 입력은 Enter로 열고, Enter 제출 또는 Escape 취소로 닫는다. `/` 키는 입력창을 여는 별도 단축키로 쓰지 않는다.
slash-prefixed text는 `src/game/CommandSystem.h/.cpp`의 로컬 명령어 처리 경로로 전달한다.
현재 지원 명령어는 `/help`, `/pos`, `/seed`, `/tp <x> <y> <z>`, `/time set <ticks>`, `/time add <ticks>`이다.
`/tp` 좌표는 숫자 절대 좌표와 Minecraft-style `~`, `~10`, `~-5` 상대 좌표를 지원한다.
제출 뒤 채팅 내역은 HUD 좌측 하단에 text-only 투명 overlay로 남고, 입력창을 다시 열었을 때만 배경 박스와 입력 input을 표시한다.
로비와 pause 메뉴의 `OPTIONS` 버튼은 같은 `assets/ui/options.rml` 문서를 연다.
Options 화면은 `BGM`과 `SFX` 볼륨을 range slider로 조정하고, Back 시 Options를 연 원래 화면으로 돌아간다.
로비에서 연 Options는 로비와 같은 배경을 사용하고, pause에서 연 Options는 게임 화면 위의 반투명 overlay로 표시한다.
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

## 로비 스플래시 텍스트

로비 화면은 타이틀 이미지 위에 노란색 스플래시 텍스트를 표시한다.
스플래시 텍스트는 `assets/ui/lobby.rml`의 `lobby-splash` 요소에 표시되며, `UiSystem` 초기화 시 아래 목록 중 하나를 무작위로 선택한다.

```text
DOLBUTO!!
{현재 버전}!!
DEV!!
```

현재 스플래시 텍스트 목록은 `assets/ui/splashes.json`의 최상위 JSON 문자열 배열에서 읽는다.
버전 문구가 들어간 항목은 버전 전환 작업에서 함께 갱신한다.
파일이 없거나 유효한 항목이 없으면 `UiSystem`의 기본 fallback 목록을 사용한다.

스플래시 텍스트는 `assets/ui/style.rcss`의 `lobby-splash` 스타일로 위치와 노란색을 정의한다.
위치는 타이틀 표시 박스의 우하단 꼭짓점에서 좌상방향으로 100px 떨어진 지점을 스플래시 박스 중심으로 잡는다.
텍스트 테두리는 쓰지 않으며, `splash-pulse` keyframes로 반시계 방향 40도 회전 상태에서 커졌다가 돌아오는 scale 애니메이션을 반복한다.
RmlUi `transform`은 `RendererRmlUiBackend`의 `SetTransform` 상태를 기준으로 UI vertex를 변환해 반영한다.

## 현재 코드 경계

`ClientUiBridge`는 `UiSystem`과 `ClientGameplayRuntime` 사이의 클라이언트 UI 어댑터다.
hotbar/inventory RML 조립, cursor item 갱신, tooltip 표시, inventory slot hit test, 숫자키 hotbar 교환, mouse click 기반 inventory 조작, radial 액션/후보 RML 조립, world list 표시 DTO 변환을 담당한다.
`ClientUiBridge`는 Vulkan 타입에 의존하지 않고, `Renderer`는 viewport 크기와 입력 값을 전달한다.

`Renderer`가 UI에서 계속 담당하는 부분은 Vulkan `Rml::RenderInterface` 구현, UI geometry buffer 업로드, RmlUi graphics pipeline, RmlUi texture load/release, scissor state 처리다.

## Options 조작 설정

Options 화면은 `BGM`, `SFX` 볼륨 슬라이더와 `SPRINT`, `SNEAK` 입력 모드 전환 버튼을 가진다.
`SPRINT`와 `SNEAK`은 각각 `HOLD`와 `TOGGLE` 사이를 클릭으로 전환한다.
설정값은 `config/settings.json`의 `controls.toggleSprint`, `controls.toggleSneak`에 저장한다.
로비에서 Options에 들어가면 로비 배경을 유지하고, pause에서 들어가면 게임 화면 위의 반투명 overlay로 표시한다.

## FOV 옵션

Options 화면은 `FOV` 슬라이더를 포함하며 `30`부터 `110`까지 1도 단위로 조정한다.
FOV 설정값은 `config/settings.json`의 `video.fovDegrees`에 저장한다.

## HUD 생존 스탯 게이지

HUD 좌하단에는 플레이어 HP, 허기, 갈증 게이지를 표시한다.
`Gauge.png`는 공통 프레임으로 사용하고, 스탯 바는 스탯별 색을 가진 RmlUi 사각형 요소의 높이를 현재 스탯 비율에 맞춰 직접 갱신한다.
HP는 붉은색, 허기는 황갈색, 갈증은 파란색으로 표시한다.
현재 스탯 상태는 `GameClient`의 `PlayerStats`가 소유하고, `UiSystem::setPlayerStats`는 RmlUi 요소 갱신만 담당한다.
채팅 명령어 `/stat`, `/stat <hp|hunger|thirst>`, `/stat <hp|hunger|thirst> add <value>`, `/stat <hp|hunger|thirst> set <value>`로 스탯 게이지를 테스트한다.
채팅 패널은 스탯 게이지와 같은 좌측 padding을 사용하되, 게이지 위쪽에 배치해 서로 가리지 않게 한다.
