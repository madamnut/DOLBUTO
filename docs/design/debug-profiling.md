# 디버그와 프로파일링

## 표시 토글

F3으로 디버그 텍스트 표시를 켜고 끈다.
실행 시 기본값은 켜짐이다.
F6으로 기후 오버레이를 순환한다.

```text
OFF -> Temperature -> Precipitation -> OFF
```

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
- 청크 load hit/miss
- upload/unload 예산
- save done/failed 상태

## 우하단 피크 프로파일러

우하단은 순간 스파이크 확인용 피크 프로파일러다.

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

## 기후 디버그 텍스트

좌상단 디버그 텍스트는 플레이어가 있는 칼럼에서 샘플링한 기후 값을 다음 형식으로 표시한다.

```text
CLIMATE: T[0.000] P[0.000]
```

`T`는 temperature, `P`는 precipitation이며 둘 다 `0.0~1.0`으로 디코딩한 값이다.
F6은 전체 화면 기후 오버레이를 꺼짐, temperature, precipitation 순서로 계속 전환한다.

## 런타임 로그

게임은 `logs` 아래에 텍스트 로그를 생성한다.

```text
logs/Latest.txt
logs/DOLBUTO_YYYYMMDD_HHMMSS_mmm.txt
```

실행 중 두 파일에는 같은 로그 줄이 기록된다.
`Latest.txt`는 다음 실행 때 교체된다.
로그 줄에는 millisecond 단위 로컬 시간과 `INFO`, `WARN`, `ERROR`, `DEBUG` 같은 레벨이 포함된다.

## 하단 디버그 텍스트

우하단 피크 프로파일러 텍스트는 현재 표시하지 않는다.
좌하단 청크 로딩 진단은 재설정 가능한 누적 피크 latency 값을 millisecond 단위로 표시한다.

`R`을 누르면 표시 중인 모든 청크 피크 값이 `0.000`으로 초기화된다.
초기화 뒤 각 값은 그 이후 측정된 최대 latency를 표시한다.

표시 그룹은 다음과 같다.

```text
CHUNK PEAK FRAME: TOTAL[] UPDATE[] JOB[] UPLOAD[] UNLOAD[]
CHUNK PEAK REQUEST: GRID[] ENSURE[] WANT[] DETACH[] SCAN[]
CHUNK PEAK ENSURE: KEY[] MARK[] FIND[] LOAD[] CREATE[] TOUCH[]
CHUNK PEAK WANT: ENSURE[] INSERT[] READY[] DEPEND[]
CHUNK PEAK MESHREQ: READY[] DEPEND[]
CHUNK PEAK ENSURE COUNT: CALL[] HIT[] MISS[] SAVED[] EMPTY[]
CHUNK PEAK REQUEST COUNT: WANT[] MESH[] FULL[] FEATURE[]
```

count 줄은 마지막 `R` 초기화 이후 프레임당 호출 수의 최대값을 표시한다.
`HIT`는 런타임 청크가 이미 있었다는 뜻이다.
`MISS`는 런타임 청크 entry를 새로 만들어야 했다는 뜻이다.
`SAVED`는 해당 miss가 저장된 청크 snapshot에서 복원되었다는 뜻이다.
`EMPTY`는 해당 miss가 생성을 위한 빈 runtime shell을 만들었다는 뜻이다.
`TOTAL`은 전체 CPU frame 피크이며, 인게임 frame-time spike에 가장 가까운 표시값이다.
