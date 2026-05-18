# 월드 생성

## 기본 형태

월드는 높이맵 기반으로 생성된다.
높이는 `groundness`, `smoothness`, `weirdness`에서 접은 `PV`, local `baseNoise`, terrain spline, `height_lut.bin`을 조합해 만든다.

청크 데이터는 `16 x 512 x 16` 전체 높이를 가진다.

## 노이즈

FastNoise2를 사용한다.
토러스 좌표 변환으로 `65536 x 65536` 범위에서 타일링 가능한 높이맵을 만들도록 구성되어 있다.
월드 X/Z는 같은 크기의 순환 월드로 다룬다.

- 월드 블록 크기: `65536 x 65536`
- 월드 청크 크기: `4096 x 4096`
- 지형 노이즈와 전역 랜덤 입력은 X/Z를 `0~65535` 범위로 래핑해서 사용한다.
- 플레이어와 렌더 청크의 실제 좌표는 세션 중 누적될 수 있지만, 생성 결과는 래핑 좌표 기준으로 반복된다.

현재 설정은 `config/world.json`에서 관리한다.
파일 읽기와 설정 값 검증은 `src/config/ConfigLoaders.h/.cpp`의 `config::loadWorldConfig`가 맡는다.
렌더러는 로드된 설정 값을 월드 생성, 청크 로딩, 기후 노이즈 계산에 적용한다.
높이맵, 초기 청크 블록/유체 채우기, 기후 칼럼 채우기, 나무 feature write 생성과 반영은 `src/world/TerrainBuilder.h/.cpp`가 맡는다.
`Renderer`는 설정값을 `world::TerrainBuilderConfig`로 넘기고, 완료된 청크 설치와 메쉬/GPU 업로드 흐름만 유지한다.

```json
"terrain": {
  "seaLevel": 256,
  "groundnessDomainWarp": {
    "enabled": true,
    "amplitude": 0.3,
    "frequency": 1.0,
    "octaveCount": 2,
    "gain": 0.5
  },
  "groundnessNoise": {
    "featureScale": 4000.0,
    "octaveCount": 4,
    "lacunarity": 3.0,
    "gain": 0.5
  },
  "baseNoise": {
    "featureScale": 2000.0,
    "octaveCount": 2,
    "lacunarity": 2.0,
    "gain": 0.5
  },
  "smoothnessNoise": {
    "featureScale": 4000.0,
    "octaveCount": 2,
    "lacunarity": 2.0,
    "gain": 0.5
  },
  "weirdnessDomainWarp": {
    "enabled": true,
    "amplitude": 0.3,
    "frequency": 1.0,
    "octaveCount": 2,
    "gain": 0.5
  },
  "weirdnessNoise": {
    "featureScale": 4000.0,
    "octaveCount": 1,
    "lacunarity": 2.0,
    "gain": 0.5
  }
}
```

`terrain.seaLevel`은 초기 물 생성 기준 해수면이다.
`terrain.groundnessNoise`는 기존 base noise 계열을 이어받아 바다/해안/땅 성향을 만드는 큰 지형 입력이다.
`terrain.baseNoise`는 Groundness Baseline 주변에 더해지는 local relief 입력이며, domain warp를 적용하지 않는다.
`terrain.smoothnessNoise`는 지역별 요철/경사 성향을 정하는 입력이며, domain warp를 적용하지 않는다.
`terrain.weirdnessNoise`는 PV를 만들기 위한 원본 입력이며, 1 octave로 사용한다.
`terrain.weirdnessDomainWarp`는 weirdness 입력 위치를 타일링 FBM으로 변형한다.
terrain 계열 Simplex scale은 config에 노출하지 않고 내부 `1.0`으로 고정한다.

현재 높이 계산은 다음 순서다.

```text
groundness = tileableFbm(groundnessNoise, groundnessDomainWarp)
smoothness = tileableFbm(smoothnessNoise)
weirdness = tileableFbm(weirdnessNoise, weirdnessDomainWarp)
pv = 1 - abs(3 * abs(weirdness) - 2)
baseNoise = tileableFbm(baseNoise)
baseline = sample(groundness_baseline_lut, groundness)
influence = sample(groundness_influence_lut, groundness)
          * sample(smoothness_influence_lut, smoothness)
rawTerrainValue = baseline + baseNoise * influence
normalizedTerrainValue = clamp((rawTerrainValue + 3.5) / 3.5, 0.0, 2.0)
pvWeight = sample(pv_weight_lut, pv)
         * sample(groundness_pv_weight_lut, groundness)
         * sample(smoothness_pv_weight_lut, smoothness)
pvMultiplier = clamp(1.0 - pvWeight, 0.0, 1.0)
terrainValue = normalizedTerrainValue * pvMultiplier
height = sample(height_lut, terrainValue)
```

`weirdness`는 직접 높이에 넣지 않고 PV를 만드는 원본 입력으로만 사용한다.
PV는 Groundness/Smoothness 조건과 곱해진 weight로 최종 `terrainValue`를 누르는 곱셈 계수를 만든다.

## 지형 노이즈 오버레이

F6 지형 진단 오버레이는 `groundness`, `smoothness`, `weirdness`, `PV`를 표시한다.
기후 오버레이는 `65536 x 65536` 전체 타일 월드를 축소하지만, 지형 노이즈 오버레이는 월드 원점 기준 `0..4096` 블록 영역만 `1024 x 1024` 텍스처로 축소한다.
따라서 지형 노이즈 오버레이의 한 픽셀은 `4 x 4` 블록 간격 샘플을 대표한다.

## 기후 노이즈

기후 디버그용 전역 값은 `temperature`와 `precipitation`이다.

- `temperature`: 래핑된 월드 Z 좌표 기준 위도 값이다. 남북 끝은 가장 낮고 월드 중앙은 가장 높다.
- `temperature`에는 기후대 형태를 유지하기 위해 중위도에서만 강해지는 약한 타일링 노이즈를 더한다.
- `precipitation`: `65536 x 65536` 주기로 타일링되는 넓은 2D 노이즈다.

기후 설정은 `config/world.json`의 `climate`에서 읽는다.

```json
"climate": {
  "temperature": {
    "noiseStrength": 0.12,
    "noiseFeatureScale": 8192.0,
    "noiseOctaveCount": 2,
    "noiseLacunarity": 2.0,
    "noiseGain": 0.5,
    "noiseSimplexScale": 1.0
  },
  "precipitation": {
    "featureScale": 4096.0,
    "octaveCount": 3,
    "lacunarity": 1.0,
    "gain": 0.5,
    "simplexScale": 1.0
  }
}
```

## 높이 LUT

노이즈 값은 LUT를 통해 실제 높이로 변환한다.

- 높이 LUT 입력 범위: `0.0 ~ +2.0`
- LUT 개수: 1024
- LUT 버전: 1
- LUT 높이 값은 `0~512` 범위 안에 있다고 본다.
- `assets/data/world/spline_editor.pyw`는 spline 곡선 편집 도구다. X/Y 축 범위를 직접 지정할 수 있고, `Ctrl+Z`로 점 편집, 드래그, 추가, 삭제, 로드, 리셋, 범위 변경을 되돌릴 수 있다. 점을 우클릭하면 X/Y 직접 입력과 삭제 버튼이 있는 편집 창을 연다.
- SplineEditor의 LUT export는 편집된 spline을 1024개 `float` 샘플로 굽고 `DLSF` 바이너리 포맷으로 저장한다. 이 포맷은 `version`, `sampleCount`, `xMin`, `xMax`, `yMin`, `yMax`, `float[sampleCount]`를 담으며, height/Groundness/Smoothness/PV spline처럼 spline 그래프 값을 그대로 보존해야 하는 곡선에 사용한다.

## Smoothness 곡선

Smoothness는 실제 경사도를 사후 계산하는 값이 아니라, 해당 지역의 고저 변화가 거칠게 나올지 완만하게 나올지를 정하는 지형 파라미터다.

- `smoothness_influence_lut.*`: Smoothness가 낮을수록 큰 값, 높을수록 작은 값을 가진다. `baseNoise`의 진폭을 조절해 거친 산악/험지와 완만한 구릉/평야를 나누는 주 제어축으로 사용한다.

## PV 곡선

PV는 `weirdness`를 `1 - abs(3 * abs(weirdness) - 2)` 공식으로 접은 peaks and valleys 입력이다.
PV 자체 범위는 `-1.0 ~ +1.0`으로 보고, `pv_weight_lut.*`도 이 입력 범위를 사용한다.

이전 `pv_baseline_lut.*`, `pv_influence_lut.*` 파일과 로딩 경로는 제거했다. PV를 높이에 반영할 때는 PV/Groundness/Smoothness weight LUT를 별도로 정의한다.
- `pv_weight_lut.*`: PV valley 구간 자체의 영향도다.
- `groundness_pv_weight_lut.*`: 낮은 양수 Groundness 지형대에서 PV 영향을 살린다.
- `smoothness_pv_weight_lut.*`: 양수 Smoothness 지형대에서 PV 영향을 살린다.

## 해수면

해수면은 `Y = 256`이다.
현재 값은 `config/world.json`의 `terrain.seaLevel`에서 읽는다.
초기 월드 생성은 해수면 이하의 빈 공간에 `water`를 유체량 100으로 채운다.
유체 흐름과 렌더링은 별도 단계에서 처리한다.

## 지형 블록 구성

현재 기본 지형은 다음 순서로 만든다.

1. 높이맵을 만든다.
2. 기본 블록 모양은 `air`, `rock`, `bedrock`으로 만들고, 해수면 이하 빈 공간은 유체 `water`로 채운다.
3. 각 칼럼의 지형 최상단 위가 `air`이면 표면은 `grass`, 아래 4칸은 `dirt`로 바꾼다.
4. 각 칼럼의 지형 최상단 위가 `water`이면 표면과 아래 4칸은 `sand`로 바꾼다.
5. 식생과 나무 feature를 생성한다.

기본 모양 규칙:

- 맨 아래는 bedrock
- 나머지 내부는 rock

bedrock 높이는 전역 난수를 사용해 1~4 범위로 만든다.

## 식생

전역 0~255 난수를 식생 판단에 사용한다.

- plant: `0 ~ 151`
- stone prop: `152 ~ 159`
- branch prop: `160 ~ 167`
- tree: `168 ~ 170`

grass 위에는 같은 0~255 식생 난수 구간에 따라 plant, stone prop, branch prop, tree 중 하나가 배타적으로 생성된다.
tree는 grass 위에 trunk와 leaves를 배치한다.

## 나무와 feature write

나무는 청크 경계를 넘을 수 있으므로 feature write를 사용한다.

- trunk는 현재 기준으로 자기 청크 내부에만 배치된다.
- leaves는 이웃 청크로 넘어갈 수 있다.
- 이웃으로 넘어가는 leaves는 대상 청크의 incoming feature slot으로 전달된다.
- leaves는 air 또는 plant만 덮어쓴다.
- trunk는 leaves/plant보다 우선한다.

## 생성 파이프라인

현재 단순화된 파이프라인:

```text
Empty
  -> BuildFeaturing
      높이맵 + 기본 지형/물 + 표면 + 자기 feature + outgoing feature 생성
  -> FinalizeFeatures
      incoming feature 반영
  -> Full
  -> BuildChunkMesh
  -> Meshed
```

`BuildFeaturing`과 `FinalizeFeatures`의 CPU 지형 생성/feature 반영 처리는 `TerrainBuilder`가 수행한다.
terrain worker 큐와 완료 큐 소유권은 `TerrainJobSystem`에 있고, `Renderer`는 job callback에서 빌더를 호출해 결과를 받는다.

관련 문서: [[chunk-system]], [[block-data]], [[save-load]]

## 청크 채우기 최적화

생성된 청크 데이터는 layer-major 순서로 채운다. 런타임 인덱스가 다음과 같기 때문이다.

```cpp
index = (y * ChunkSizeZ + localZ) * ChunkSizeX + localX
```

청크 생성은 표면과 식생 배치를 위해 heightmap에서 `terrainTopY[256]`을 캐시한다.
또한 bedrock pass와 subsurface pass가 같은 column hash를 반복하지 않도록 `bedrockHeights[256]`도 캐시한다.
최소 높이 아래의 공통 solid 범위는 전체 layer 단위로 채운다.
그 위에서는 높이가 변하는 layer만 column별로 분기한다.
물 기록은 `fluidSubchunkCounts[y / 16]`를 갱신하며, bedrock은 기본 `rock / air / water` 형태를 만든 뒤 적용한다.

## 기후 청크 데이터

기후는 런타임 청크 데이터에 X/Z column별로 저장한다.

- `temperature[256]`: `0~255`로 인코딩된 `uint8_t`, 디코딩 시 `0.0~1.0`.
- `precipitation[256]`: `0~255`로 인코딩된 `uint8_t`, 디코딩 시 `0.0~1.0`.
- Column index는 `localZ * 16 + localX`이다.
- 생성된 청크는 F6 오버레이와 같은 tileable climate noise로 이 배열을 채운다.
