# 0.0.0.1 요약

## 범위
- `DOLBUTO 0.0.0.1` 버전으로 전환하고, 버전별 개발 기록 체계를 이어갔다.
- 순환 월드, 유체, 기후, 월드 슬롯, RmlUi 기반 UI, 아이템/인벤토리, 드랍 아이템, 오디오, 빌드 배포 구조를 크게 확장했다.
- 실험성 높은 물 셰이더와 청크 피크 진단은 테스트 후 단순한 현재 구조로 정리했다.

## 월드와 좌표
- 월드 X/Z를 `65536 x 65536` 래핑 월드로 다루도록 저장 키와 생성 입력을 정리했다.
- 런타임/렌더 청크 좌표는 실제 좌표를 유지하고, 저장/로드와 지형 생성 랜덤 입력은 래핑 기준을 사용한다.
- 디버그 좌표 표기는 `래핑좌표 [실제좌표]` 형식으로 바꿨다.
- `LOOK` 표기를 `VIEW`로 바꾸고, 선택 블록은 `LOOKAT: 블록명[id] (x, y, z)` 형식으로 표시하도록 했다.
- 블록 상호작용과 LOOKAT 레이캐스트 거리는 8블록으로 늘렸고, LOOKAT은 유체를 무시한다.

## 유체와 물
- `assets/data/fluids.json`을 추가하고 `none`, `water`, `lava`, `methane`, `hydrogen` 유체 ID를 정의했다.
- 유체 셀은 상위 9비트 ID와 하위 7비트 amount를 담는 `uint16_t` 패킹 값을 사용한다.
- 청크 데이터와 저장 페이로드에 블록 배열과 분리된 유체 배열을 추가했다.
- 해수면은 `config/world.json`의 `terrain.seaLevel`에서 읽고, 초기 생성 시 해수면 이하 빈 공간을 amount 100의 water로 채운다.
- 유체 렌더링은 블록과 분리된 유체 메쉬/파이프라인을 사용한다.
- 물 높이는 amount 10단위 기준 10단계로 나누고, 최상단 물은 최대 `0.8`블록 높이로 렌더링한다.
- 위에 물이 있는 유체 셀은 전체 1블록 높이로 렌더링한다.
- Fresnel, 노멀맵, SSR, Hi-Z SSR, 색 흡수 같은 물 셰이더 실험을 진행했지만, 현재는 단순 텍스처 샘플링과 고정 alpha 중심의 단순 물 메쉬로 정리했다.

## 월드 생성과 기후
- 월드 생성 순서를 기본 지형/물 생성, 표면 적용, 식생/소품 생성 순서로 분리했다.
- 표면 블록은 최상단 위가 공기이면 `grass/dirt`, 물이면 `sand/sand`로 적용한다.
- `temperature`와 `precipitation` 전역 기후 노이즈를 추가했다.
- temperature는 남북 방향인 Z축 기준으로 중앙이 높고 양끝이 낮은 구조에 약한 타일링 노이즈를 더한다.
- precipitation은 넓은 타일링 노이즈로 생성한다.
- F6으로 temperature, precipitation 오버레이를 전환해 전체 래핑 월드를 1024x1024 텍스처로 확인할 수 있다.
- 청크 컬럼별 temperature/precipitation byte를 저장 페이로드에 포함했다.

## 렌더링
- 큰 래핑 월드 좌표에서 float 정밀도 흔들림을 줄이기 위해 씬 렌더링을 카메라 상대 좌표로 전환했다.
- 지형 메쉬 저장소 이름을 `rockSubchunks`에서 `solidSubchunks`로 바꿨다.
- 유체 셀이 없는 유체 서브청크는 `fluidSubchunkCounts`로 건너뛴다.
- 로드된 청크 derived cache 재빌드를 한 번의 pass로 합쳐 `emptySubchunks`와 `fluidSubchunkCounts`를 함께 재구성한다.
- 저장된 청크 snapshot이 비동기로 복원된 뒤 `Full/Meshed` 상태이면 주변 3x3 메쉬 조건을 다시 검사해 월드 재진입 직후 렌더 메쉬가 누락되지 않도록 했다.
- 블록 파괴 파티클은 전용 particle pipeline으로 렌더링하고, 기존 블록 텍스처 array의 sub-tile을 사용한다.
- 파티클은 카메라 정면 billboard로 보이되 pitch 각도에서 납작해지지 않도록 view basis를 사용한다.
- 해와 달 sprite 크기를 절반으로 줄이고, 월드 시간에 따라 하늘 방향이 바뀌도록 했다.

## 소품과 모델
- `stone` ID `20000`, `branch` ID `20001` 소품 블록을 추가했다.
- 소품 블록은 일반 블록 ID 공간에 남기고, 블록 데이터에서 `prop.model`, `prop.texture`를 지정한다.
- `.glb` 소품 모델을 시작 시 `.dpm` quad 데이터로 변환해 런타임 캐시로 사용한다.
- `.dpm`은 magic/version 없이 quad count와 quad vertex data를 저장한다.
- GLB 변환은 삼각형 쌍을 다시 쿼드로 병합하고, position/UV 정밀도를 1/256 블록 단위로 맞췄다.
- plant, stone, branch, tree 배치 범위를 하나의 랜덤 바이트 안에서 배타적으로 나눴다.
- `randomOffset` 블록 플래그를 추가하고, cross/prop 블록에 결정적 X/Z 렌더 오프셋 `-0.2~+0.2`블록을 적용했다.
- `directional = false`인 top face, cross, prop 렌더링에 결정적 4방향 랜덤 회전을 적용했다.

## UI와 로비
- RmlUi 6.1과 FreeType 2.14.2를 통합했다.
- 네이티브 디버그 폰트 rasterizer를 `stb_truetype`에서 FreeType으로 전환했다.
- 로비, 월드 선택, 월드 생성, pause UI를 RmlUi 문서로 구현했다.
- RmlUi Vulkan UI pipeline, compiled geometry, texture loading, generated texture, scissor region, input routing을 구현했다.
- 로비는 rock 타일링 배경과 `Title.png` 타이틀 이미지를 사용한다.
- pause는 게임 화면 위에 반투명 검정 오버레이로 표시되고, 청크 로딩은 계속 진행된다.
- 로비와 게임 씬 수명을 분리했다.
- 로비에서는 terrain/save worker와 초기 청크 로드를 시작하지 않고, 게임 씬 로드 시에만 월드 런타임을 시작한다.
- pause `EXIT`는 플레이어와 청크를 저장하고, terrain worker와 런타임 상태를 정리한 뒤 로비로 돌아간다.
- 월드 목록은 `saves` 아래 월드 폴더를 스캔해 표시하고, 더블클릭으로 진입한다.
- 새 월드 생성 UI는 이름과 seed 입력을 받는다.
- 월드 목록에는 생성 시각과 최근 플레이 시각을 표시한다.
- RmlUi 입력에는 mouse wheel, key modifier, 클립보드 copy/paste를 연결했다.

## 플레이어 상태와 시간
- `saves/<world-name>/world.dat`에 월드 total ticks, seed, 생성 시각, 최근 플레이 시각을 저장한다.
- 인게임 시간은 24시간 60분 체계이며, 인게임 1분은 20틱이다.
- 게임 시작 시간은 `0D 06H 00M` 기준이다.
- 좌상단 디버그 텍스트에 `TIME`과 `SEED`를 추가했다.
- `saves/<world-name>/player.dat`에 플레이어 위치/상태와 50슬롯 인벤토리를 저장한다.
- 플레이어 상태 파일은 버전 필드 없는 고정 바이너리 레이아웃으로 취급한다.
- 블록 설치 시 설치될 블록이 플레이어 콜라이더와 겹치면 설치 명령을 무시한다.

## 아이템과 인벤토리
- `assets/data/items.json`을 추가하고 아이템 `id`, `key`, `name`, `maxStack`, `slotTexture`, `droppedRender`, `heldRender` 구조를 도입했다.
- 블록 드랍 테이블은 `blocks.json` 안의 `drops` 배열로 정의하고, 아이템 `key`를 참조한다.
- 아이템 렌더 타입은 드랍/소지 상태 모두 `extruded_sprite`를 기준으로 정리했다.
- 50슬롯 런타임 인벤토리를 구현했다. `0~9`는 핫바, 나머지는 메인 인벤토리다.
- RmlUi 핫바와 `E`로 여는 인벤토리 UI를 추가했다.
- 숫자키 `1~9`, `0`, 마우스 휠로 핫바 선택을 전환한다.
- 인벤토리 UI는 pause가 아니며, 열려 있는 동안 월드 tick과 플레이어 physics는 계속 진행한다.
- 기본 좌/우클릭 슬롯 조작, Shift-click 이동, number-key swap, 임시 커서 스택 반환을 구현했다.
- 아이템 툴팁은 `Tooltip.png` 9patch 배경을 사용하고, 화면 밖으로 나가지 않도록 clamp한다.
- 드랍 아이템 획득은 기존 스택을 먼저 채운 뒤 빈 슬롯을 채우며, 슬롯 인덱스 0부터 49 순서로 진행한다.

## 드랍 아이템과 엔티티
- 블록 파괴 시 드랍 테이블 기준으로 드랍 아이템 엔티티를 생성한다.
- 드랍 아이템은 청크 소유 `WorldEntity`로 저장하며, entity id, position, velocity, grounded flag, item id, count를 저장한다.
- pickup state와 render spin은 런타임 전용으로 저장하지 않는다.
- entity 변경은 terrain revision과 분리된 dirty serial로 저장 여부를 추적한다.
- 드랍 아이템 렌더링은 얇은 3D `extruded_sprite` 메쉬를 사용한다.
- 드랍 아이템 옆면은 스프라이트 알파 경계 span을 병합해 만든다.
- 드랍 아이템 물리는 중앙 하단점을 충돌 기준으로 사용하고, 20 TPS 고정 업데이트와 렌더 보간을 적용한다.
- 착지한 아이템 아래 블록이 제거되면 다시 낙하한다.
- `F`로 바라보는 드랍 아이템을 획득 대상으로 만들고, 플레이어 중심으로 가속하며 날아오게 했다.
- `Q`는 현재 핫바 슬롯 아이템 1개를 버리고, `Shift + Q`는 스택 전체를 버린다.
- 드랍 아이템 렌더링은 청크 AABB 프러스텀 컬링과 개별 48블록 거리 컬링을 적용한다.
- 드랍 아이템 보유 청크 캐시를 유지해 전체 런타임 청크 순회를 피한다.
- 같은 item id의 드랍 아이템은 생성 직후와 물리 tick 중 주변 스택으로 병합된다.
- 병합 판정은 X/Y/Z 각 축 차이 0.75블록 이하이며, 병합된 대상은 살짝 튀어오른다.
- 병합된 스택은 count 구간에 따라 1~4개 시각 복제본으로 겹쳐 보이게 렌더링한다.
- 드랍 아이템 렌더링은 정적 item mesh와 persistent mapped instance buffer, item id 기준 batch draw로 최적화했다.

## 블록 파괴
- 블록 데이터에 `hardness`를 추가했다.
- `hardness`는 손 `breakPower = 1.0` 기준 대략적인 파괴 시간으로 취급한다.
- `air`와 `bedrock`은 파괴 불가능 또는 특수값으로 두고, plant/stone/branch는 즉시 파괴 대상으로 설정했다.
- 좌클릭 유지 시 진행도에 따라 블록 파괴 금 오버레이 `destroy_stage_0..9.png`를 표시한다.
- 채굴 중에는 face particle이 나오고, 최종 파괴 시 블록 파괴 파티클과 드랍 아이템이 생성된다.

## 오디오
- `assets/audio/sfx`, `assets/audio/music`, `assets/audio/ambience` 디렉터리를 추가했다.
- OpenAL Soft 기반 오디오 재생 경로를 추가했다.
- SFX는 PCM 16-bit mono/stereo WAV를 시작 시 OpenAL buffer로 로드한다.
- 3D 위치 사운드용 stereo WAV는 로드 시 mono로 다운믹스한다.
- `Break.wav`는 블록 파괴 성공 시, `Place.wav`는 블록 설치 성공 시 3D 사운드로 재생한다.
- `Button_Click.wav`는 RmlUi 버튼 click 시 2D 사운드로 재생한다.
- `Pop.wav`는 드랍 아이템이 실제로 인벤토리에 들어간 시점에 2D 사운드로 재생한다.
- OpenAL listener는 매 프레임 카메라 위치와 방향으로 갱신한다.
- 배경음악은 `assets/audio/music` 하나의 목록을 로비와 인게임에서 공유한다.
- 로비 계열 화면 진입 시 음악을 즉시 시작하고, 인게임은 10초~60초 랜덤 대기 후 시작한다.
- 씬 전환 시 이전 음악 상태는 저장하지 않고 리셋한다.
- OGG 음악은 OpenAL 확장이 아니라 `stb_vorbis`를 사용한다.
- 현재 OGG 음악은 시작 시 전체 디코딩하지 않고 2초 분량 OpenAL buffer 3개를 큐로 돌리는 스트리밍 방식이다.
- WAV 음악은 예외적으로 재생 직전에 단일 buffer로 로드한다.

## 저장과 빌드
- 청크 저장 페이로드에 블록 RLE, 유체 RLE, 기후 byte, 청크 엔티티 데이터를 포함하도록 확장했다.
- 저장 호환성은 초기 개발 중이므로 강하게 보장하지 않는 방향을 유지했다.
- GLFW, FreeType, RmlUi, OpenAL Soft는 `third_party/prebuilt` 기반 사전 빌드 라이브러리 사용 방향으로 정리했다.
- 빌드 구성은 `Debug`와 `Release`만 사용하도록 정리했다.
- 별도 `DOLBUTOPortable` 타깃을 제거하고, Release 출력 폴더 자체를 포터블 실행 단위로 본다.
- 기존 `dist` 폴더는 더 이상 현재 빌드 흐름의 필수 산출물이 아니다.

## 디버그와 프로파일링
- 좌상단 디버그 텍스트에 `CLIMATE`, `TIME`, `SEED`를 추가했다.
- 우상단 디버그 텍스트는 버전, 하드웨어, Vulkan/driver, 해상도, VRAM, draw/faces/quads 중심으로 유지한다.
- 한동안 청크 로딩 병목 확인을 위해 좌하단 청크 피크 진단과 `R` 재측정 입력을 추가했다.
- 이후 좌하단/하단 디버그 텍스트, `R` 초기화 입력, 내부 누적 피크 계측 로직을 제거했다.
- 현재 하단 디버그 텍스트는 표시하지 않는다.
- F1은 일반 게임 화면의 핫바와 crosshair 표시를 토글한다.

## 주요 결정
- 빌드는 계속 사용자가 직접 수행한다.
- 저장 호환성보다 현재 개발 속도와 구조 정리를 우선한다.
- 유체는 블록과 같은 셀에 공존할 수 있으므로 블록 배열과 유체 배열을 분리한다.
- 고급 물 셰이더 작업은 핵심 게임 시스템이 더 자리 잡은 뒤 다시 단계적으로 진행한다.
- 로비, 게임 씬, pause, inventory는 입력/시뮬레이션/렌더링 생명주기를 명확히 분리한다.
- 드랍 아이템은 범용 entity system의 첫 용례로 두되, mobs나 복잡한 ECS는 아직 미룬다.
- 인벤토리 동적 아이템 상태, 내구도, 모듈형 아이템 인스턴스 데이터는 이후 확장 과제로 남긴다.
- 오디오는 OpenAL Soft를 기본으로 사용하고, 음악은 OGG 스트리밍 중심으로 운용한다.

## 남은 문제
- 실제 빌드와 실행 검증은 사용자가 수행해야 한다.
- 물 렌더링은 현재 단순화 상태이며, 최종 물 셰이더는 게임 요소가 더 자리 잡은 뒤 다시 설계해야 한다.
- 아이템 인스턴스의 동적 상태, 내구도, 커스텀 이름, 손에 든 아이템 렌더링은 아직 본격 구현 전이다.
- mobs나 더 넓은 범용 entity/component system은 아직 설계 단계에 가깝다.
- 드랍 아이템과 청크 저장/로드는 기능이 커졌으므로 장시간 플레이와 월드 전환 상황에서 추가 검증이 필요하다.
- 사전 빌드 서드파티 라이브러리는 옵션, 아키텍처, 런타임이 바뀔 때 재생성 절차를 다시 확인해야 한다.
