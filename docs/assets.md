# 에셋 컨텍스트

## 얼음 알파 수정 (2026-09-25, 최신)

사용자 `실시`로 ice.png의32×32 RGB 픽셀을 보존하고 알파190→255로 변경했다. 핫바는 불투명, 월드190/255는 재질에서 따로 지정한다. 이 변경은 아래 초기 픽셀보존 방침의 알파 예외다. [ice-material-2026-09-25.md](ice-material-2026-09-25.md) 참조.

## 얼음·눈 사용 (2026-09-25)

기존 assets/textures/blocks/ice.png 및 snow.png(각32×32)를 월드 아틀라스와 핫바8/9아이콘에연결. 원본파일·픽셀보존,새에셋다운로드없음. atlas는11타일352×32이며얼음9/눈10번타일이다.

## 용암 사용 (2026-09-25)

이미 존재하던 assets/textures/fluid/lava.png(32×32)를 용암 월드 텍스처와 핫바7번 아이콘에 연결했다. 파일/픽셀은 보존하고 외부 에셋을 다운로드하지 않았다. 후속 정정으로 원본 한 장의 무늬만30° 방향으로 스크롤하며 이미지 회전·두 겹 혼합은 없다. 구체적 표현은 docs/lava-2026-09-25.md.

## 핫바 슬롯 원본 교체 (2026-09-23, 현재)

사용자 `핫바슬릇 스프라이트 다시넣어놓음 확인하고 적용실시`로 out/Sandbox/핫바슬릇.png(22×22 RGBA,alpha0..255)를 확인하고 assets/textures/ui/hotbar_slot.png로 이동·교체했다. 새 SHA256은 `cff7ce525d80f4e10ffcf3b08e3dc60675e441c2085dc7cb13c42f3bd555768f`이며 픽셀/파일 내용은 보존했다. 이전24×24에 대한 아래 기록은 이력이다.

원본1px→4dp 기준으로 슬롯88dp, 선택72dp/8dp inset, 아이콘64dp/12dp inset으로 중앙정렬한다. 10칸 전체폭880dp, 화면하단bottom0, nearest필터와기존선택PNG는유지한다.

## 10칸 핫바 에셋 (2026-09-23)

- out/Sandbox/slot.png → assets/textures/ui/hotbar_slot.png(24×24 RGBA), out/Sandbox/Hotbarscope.png → assets/textures/ui/hotbar_selection.png(18×18 RGBA)로 이동했다. 두 PNG 모두 alpha0..255, 픽셀·파일 내용은 변경하지 않았다.
- SHA256: slot `5b8ad29147f1c450c623439cd54e6c190275c69017976c6cd8fe2edfe157074b`, selection `e97dba421e0e50382076192ad441f4ae26e2e137db2d2bb4196bbd6c96b3c282`. 이동 전후 해시 일치.
- 후속 확대 실시로 UI 슬롯96dp/선택72dp로 원본4배, 선택틀은 중앙12dp inset이다. 핫바는 화면 하단 bottom0에 붙인다. 블록 아이콘은64dp/16dp inset이고 흙·잔디옆면·돌·모래의 기존PNG를 쓴다. 발광 아이콘은 기존 순백 사각형이다. 프레임→아이콘→선택틀 순서로 그리며 RmlUi LoadTexture의 기존 nearest sampler를 사용한다. 별도 이미지 편집/리사이즈 파일은 없다.
- 핫바10칸에서 기존 번호·이름 글자를 제거했다.1~5는기존블록,6~0은빈칸이다. 현재 등록이 없는 자갈/진흙/점토/나무 등은 이 작업에서 추가하지 않았다.

## 절차적 천체 (2026-09-23)

태양·달·별에 새로운 이미지 에셋은 사용하지 않는다. 원반과 별은 셰이더 수식, 달 무늬는 value noise다. Complementary 원본 천체/별 개념 및 수치의 출처를 assets/licenses/Complementary.txt에 기록했다. 기존 참조 노이즈 텍스처나 사용자 전달 에셋은 변경하지 않았다.

## Complementary 참조 노이즈 (2026-09-23)

조명 이식 승인 범위로 `ref/ComplementaryUnbound/shaders/lib/textures/noise.png`, `cloud-water.png`를 각각 `assets/textures/effects/complementary_noise.png`, `complementary_cloud_water.png`로 복사했다. 원본은 보존한다. SceneEffects가 선형/repeat sampler로 읽으며 구름 그림자, 수중 빛기둥과 물 그림자 무늬에 사용한다. 후속 물 전체 이식으로 물결 RG 노멀/alpha 시차, 물 색 노이즈, 거품, 굴절에도 같은 두 원본 텍스처를 재사용한다. 후속 구름 이식은 noise.png의 R/B 채널을 원본 Noise3D/2D 구름 밀도에도 재사용한다. 새 이미지 에셋은 없다. 출처는 `assets/licenses/Complementary.txt`; 이 파일은 원본 저작물에 새 라이선스를 부여하지 않는다.

AI 작업용 문서다. 이 목록은 전달된 파일의 정리 현황이며, 게임 내 블록 등록이나 렌더링 구현을 확정하는 문서는 아니다.

## 초기 블록 텍스처

2026-09-07 사용자가 프로젝트 루트에 전달한 PNG 5개를 `assets/textures/blocks/`로 이동했다. 파일명은 용도가 명확하여 유지했다. 이동 전후 SHA-256 비교로 모든 파일의 내용이 보존되었음을 확인했다.

용도는 파일명과 이미지 확인에 따른 분류다. 모두 `32 × 32` 픽셀이며 시작 화면의 텍스처 선택 UI와 첫 월드의 흙/잔디/돌 면 재질에 연결했다. 첫 월드 빌드와 캡처에서 재질 표시를 확인했다.

| 경로 | 용도 |
|---|---|
| `assets/textures/blocks/dirt.png` | 흙 |
| `assets/textures/blocks/grass_bottom.png` | 잔디 블록 밑면 |
| `assets/textures/blocks/grass_side.png` | 잔디 블록 옆면 |
| `assets/textures/blocks/grass_top.png` | 잔디 블록 윗면 |
| `assets/textures/blocks/rock.png` | 바위/돌 |

- `dirt.png`와 `grass_bottom.png`는 SHA-256이 동일한 파일이다. 전달된 용도 구분을 유지하며 둘 다 보관했다.
- 텍스처 리사이즈, 픽셀 수정, 포맷 변환은 수행하지 않았다.
- UI의 블록 텍스처 미리보기는 nearest 필터로 확대한다. 글꼴은 linear 필터를 사용한다. 마지막 빌드의 월드 아틀라스는 160×32다. 새 소스에서는 물 타일을 추가하여 192×32로 조립한다. 타일 순서는 dirt/grass_top/grass_side/grass_bottom/rock/water이며 원본 파일을 보존한다. nearest와 타일 내 texel clamp를 사용하며, 밉맵은 아직 없다.

## 유체 텍스처

- 사용자 `실시` 승인으로 루트 `water.png`를 `assets/textures/fluid/water.png`로 이동했다. `fluid` 분류는 사용자 지정이며 파일명과 픽셀은 유지했다.
- 32×32, 8비트 RGBA PNG다. 이미지에서 청록색 물 텍스처를 확인했고 이동 전후 SHA-256 일치를 확인했다: `605037bee8e56b5b3f0992cfe5f7b988bfcb9628d25270a6fa01247b6f3642de`.
- 정리 당시 기존 파일 참조는 없었다. 후속 `실시`로 월드 아틀라스의 6번째 타일에 연결하고 정적인 물 생성·렌더링을 추가했다(빌드 대기). UI 재질 선택 슬롯에는 물을 추가하지 않았다. 해수면은 Y=192이며 물 흐름은 미구현이다. 강 생성 제거 후에도 바다용 물 에셋과 렌더링은 유지한다.

## UI 및 글꼴

- `assets/ui/main.rml`, `main.rcss`: RmlUi 시작 화면. 새 소스는 중앙 세로 버튼(싱글플레이/옵션/게임 종료)과 렌더 거리 옵션 패널로 교체했다. 텍스처 소개 화면을 제거했지만 블록 PNG는 월드/HUD에서 계속 사용한다. 메뉴 교체는 미빌드다. 게임 메뉴는 RmlUi, 개발 상태 패널은 Dear ImGui다.
- `assets/ui/world.rml`, `world.rcss`: 자유 비행 조작 안내, 로딩 상태와 렌더 거리 설정을 표시하는 투명 HUD. 흙/잔디/돌 선택 슬롯과 파괴·놓기 안내를 dev/release에 빌드하고 월드 캡처에서 배치를 확인했다. 슬롯은 기존 dirt/grass_side/rock PNG를 재사용한다.
- 후속 소스의 world HUD는 Tab 설정창을 제거하고 Esc 옵션 오버레이(화면 전체 #00000099 배경, 중앙 렌더 거리/게임으로 돌아가기/시작 화면으로)를 추가했다. 기존 HUD는 배경 아래에 남고 십자선은 옵션 동안 숨긴다. 이 레이아웃은 미빌드/미확인이다.
- `assets/fonts/NotoSansKR.ttf`: Noto Sans KR 가변 글꼴. 한글 UI 표시용이며 원본 `OFL.txt`를 함께 보관한다.
- 글꼴 출처: `https://github.com/google/fonts`, 커밋 `b38c5c93af322c45f633e17ac440ec1e6c94d489`, 원본 경로 `ofl/notosanskr/NotoSansKR[wght].ttf`.
- 글꼴 SHA-256: `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252`.

## F3 전용 굵은 글꼴

- 원본 NotoSansKR.ttf의 fvar wght 기본값은100(범위100..900)이다. F3에서 얇은 획과 두꺼운 외곽선이 겹친 사용자 캡처를 확인했다. 원본은 유지하고 weight600 static instance인 assets/fonts/NotoSansKR-SemiBold.ttf를 추가했다. F3는 이 파일을25.5px로 사용하고 기존 F8/RmlUi 글꼴은 그대로다.
- 생성: fonttools4.59.0의 instantiateVariableFont(wght=600, inplace=True, updateFontNames=True). 재현 도구는 tools/bake_debug_font.py이며 게임 빌드에 자동 실행하지 않는다. FontTools는 에셋 변환에만 .cache/font-tools로 설치했으며 런타임/게임 빌드 의존성이 아니다. 원본과 동일한 OFL.txt를 동봉한다.
- 생성 파일 SHA-256: `3ba54ec377922b5b5bce89e0d7f0e1b220a986f126cadd221ffcd03edf94efaf`. usWeightClass600과fvar 제거를 확인했다.

## 수중 모래 텍스처 (2026-09-14, 미빌드)

- 사용자가 out/Sandbox/assets/textures/blocks/에 추가한 sand.png를 이미지/PNG 헤더로 확인했다. 32×32, 8비트 RGB다. 수중 지층 실시 승인으로 원본 assets/textures/blocks/sand.png에 복사하고 실행 폴더의 전달 파일도 보존했다. 복사 전후 바이트 일치 확인, SHA-256: c2cfdcd96ec1e540be6ec20f8b05019a748cdf32f97956cfd8a64949b07298ec. 픽셀/이름/포맷은 변경하지 않았다.
- 월드 아틀라스는 dirt/grass_top/grass_side/grass_bottom/rock/water/sand 순서의7타일(224×32)로 확장했다. 모래 면 재질은6, 물은기존5다. fragment UV는 textureSize로 실제 아틀라스 크기를 사용한다. 새 렌더링은 미빌드/미실행이다.
- 추가 전달된 clay/gravel/leaves/log_side/log_topbottom/mud/plant는 실행 폴더에서 확인만 했으며 이번 승인에서는 등록하거나 원본 폴더로 복사하지 않았다. 기존1/2/3 선택 슬롯도 유지한다.

## 발광 블록과 중앙 핫바 (2026-09-14, 미빌드)

- 아틀라스 마지막에 코드로 만든32×32 순백RGBA255 타일을 추가해8타일256×32다. dirt/grass_top/grass_side/grass_bottom/rock/water/sand/white 순서이며 재질7은발광블록이다. 외부흰텍스처파일은필요하지않다.
- world.rml은 중앙하단핫바5칸으로확장했다. sand아이콘은기존모래PNG, 발광아이콘은Rml흰사각형이다. 선택테두리/숫자1~5를표시하고휠선택을연결했다. world-status태그/스타일/갱신코드는제거했다. 실제화면은미빌드/미검증이다.
