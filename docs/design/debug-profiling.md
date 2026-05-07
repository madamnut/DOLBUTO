# 디버그와 프로파일링

## 표시 토글

F3으로 디버그 텍스트 표시를 켜고 끈다.
실행 시 기본값은 켜짐이다.

## 좌상단

좌상단은 플레이어와 프레임 기본 정보를 표시한다.

```text
FPS: 0000 [000.000MS]
POS: X 래핑좌표 [실제좌표] / Y ... / Z 래핑좌표 [실제좌표]
VIEW: YAW ... / PITCH ... [NORTH/EAST/SOUTH/WEST]
LOOKAT: 블록명[id숫자] (x: ..., y: ..., z: ...)
```

FPS와 위치/시야 정보는 20Hz 기준으로 갱신한다.
LOOKAT은 로드되어 있는 청크의 블록만 대상으로 하는 레이캐스트 결과를 표시한다.
유체는 LOOKAT 레이캐스트 대상에 포함하지 않는다.
LOOKAT 좌표는 X/Z를 래핑한 월드 좌표만 표시한다.

## 우상단

우상단은 버전과 하드웨어/렌더링 상태를 표시한다.

```text
DOLBUTO 0.0.0.1
CPU/GPU 이름
VULKAN
DRIVER
RESOLUTION
VRAM 관련 정보
DRAWS
FACES
QUADS
```

하드웨어/드라이버 계열 정보는 자주 바뀌지 않으므로 낮은 빈도로 샘플링한다.

## 좌하단

좌하단은 청크 로딩, 저장, 작업 큐 등 현재 병목 확인용 임시 공간이다.
프로파일링 목적에 따라 표시 항목은 바뀔 수 있다.

현재 확인하던 주요 항목:

- 로드/저장 큐 상태
- chunk load hit/miss
- upload/unload budget
- save done/failed

## 우하단 peak profiler

우하단은 순간 스파이크 확인용 peak profiler다.

- 프로그램 시작 후 5초 뒤 샘플링 시작
- 누적 최대값 유지
- R 키로 재측정

대표 항목:

- `PEAK FRAME`
- `PEAK UPDATE`
- `PEAK ENSURE RUNTIME`
- `PEAK WANT RENDER`
- 세부 ensure/want 단계

## 텍스트 렌더링

디버그 텍스트는 흰색 글자와 검정색 4방향 외곽선을 사용한다.
텍스트는 batch 방식으로 처리해 매 프레임 비용을 줄인다.

관련 문서: [[chunk-system]], [[rendering]], [[save-load]]
