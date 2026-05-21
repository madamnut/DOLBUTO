# 디버그와 프로파일링

## 표시 토글

F3으로 디버그 텍스트 표시를 켜고 끈다.
실행 시 기본값은 켜짐이다.
F6으로 기후/지형 진단 오버레이를 순환한다.

```text
OFF -> Temperature -> Precipitation -> Groundness -> Smoothness -> Weirdness -> PV -> OFF
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
DOLBUTO 0.0.0.2
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

좌하단은 프레임 단계별 피크 시간을 millisecond 단위로 표시한다.
값은 실행 중 누적 최댓값이며, 게임 또는 인벤토리 화면에서 `R`을 누르면 초기화한다.

```text
PERF MAX [R]
TL_FINISH
TL_SNAPSHOT
TL_INSTALL
TL_RESUME
TL_TOTAL
TL_COUNT
TD_POP
TD_HANDLE
TD_TERR_CNT
TD_POP_CNT
```

`TL_FINISH`는 chunk-load 완료 shell을 찾고 load state를 캡처하는 구간이다.
`TL_SNAPSHOT`은 prepare worker가 만든 runtime chunk를 메인 스레드에서 마무리하는 구간이다. 현재 메인 스레드에는 entity 정규화와 clean revision 갱신만 남고, 저장 snapshot 복원과 derived cache 재구축은 `ChunkPrepareSystem`에서 처리한다.
`TL_INSTALL`은 복원된 runtime chunk를 `WorldRuntime`에 설치하는 구간이다.
`TL_RESUME`은 로드 완료 이후 주변 frontier scheduling을 재개하는 구간이다.
`TL_TOTAL`은 chunk-load 완료 처리 전체 구간이다.
`TL_COUNT`는 프레임별 chunk-load 완료 처리 개수의 누적 최댓값이다.
`TD_POP`은 worker 완료 큐와 chunk-load 완료 큐에서 완료 결과를 꺼내는 구간이다.
`TD_HANDLE`은 꺼낸 완료 결과를 runtime 상태에 반영하고 다음 단계 job을 깨우는 completion handler 전체 구간이다.
`TD_TERR_CNT`, `TD_POP_CNT`는 각각 terrain 완료와 전체 pop 개수의 누적 최댓값이다.

## 우하단 피크 프로파일러

현재 우하단 피크 프로파일러 텍스트는 표시하지 않는다.
피크 재측정용 `R` 입력은 좌하단 피크 프로파일러가 사용한다.

## 텍스트 렌더링

디버그 텍스트는 흰색 글자와 검정색 4방향 외곽선을 사용한다.
텍스트는 batch 방식으로 처리해 매 프레임 비용을 줄인다.

관련 문서: [[chunk-system]], [[rendering]], [[save-load]]

## 기후 디버그 텍스트

좌상단 디버그 텍스트는 플레이어가 있는 칼럼에서 샘플링한 기후 값을 다음 형식으로 표시한다.

```text
CLIMATE: T[0.000] P[0.000]
TERRAIN: GND[0.000] SMTH[0.000] W[0.000] PV[0.000] BASE[0.000] INF[0.000] VAL[0.000] H[0]
```

`T`는 temperature, `P`는 precipitation이며 둘 다 `0.0~1.0`으로 디코딩한 값이다.
`GND`는 플레이어가 있는 칼럼의 groundness noise 값이며, terrain Groundness Baseline/Influence spline에 들어가기 전의 원본 값이다.
`SMTH`는 같은 칼럼의 smoothness noise 원본 값이다.
`W`는 weirdness noise 원본 값이고, `PV`는 `1 - abs(3 * abs(W) - 2)`로 접은 peaks and valleys 값이다.
`BASE`는 Groundness/Smoothness/PV Baseline spline 합산 결과, `INF`는 Groundness/Smoothness/PV Influence spline 곱 결과, `VAL`은 height LUT 입력값, `H`는 청크 heightmap 생성 경로의 최종 높이다.
F6은 전체 화면 진단 오버레이를 꺼짐, temperature, precipitation, groundness, smoothness, weirdness, PV 순서로 계속 전환한다.
temperature/precipitation은 `65536 x 65536` 전체 타일 월드를 `1024 x 1024` 텍스처로 축소 샘플링한다.
groundness/smoothness/weirdness/PV는 월드 원점 기준 `0..4096` 블록 영역을 `1024 x 1024` 텍스처로 축소 샘플링한다.
groundness/smoothness 색상은 `-1..1` 구간을 기준으로 잡고 범위 밖 값은 양 끝 색으로 고정한다.
weirdness/PV는 `-1..1` 구간을 반전 흑백으로 표시해 높은 값일수록 검정, 낮은 값일수록 흰색이 된다.

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

하단 디버그 텍스트는 현재 좌하단 피크 프로파일러로 사용한다.
