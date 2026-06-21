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
높이맵, 초기 청크 블록/유체 채우기, 기후 칼럼 채우기, 3x3 source view 기반 나무 feature resolve는 `src/world/TerrainBuilder.h/.cpp`가 맡는다.
청크 데이터의 skylight 재계산은 `src/world/SkyLightSystem.h/.cpp`가 맡는다.
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

## 바이옴

초기 바이옴 분류는 `temperature`, `precipitation`, `groundness` 3개 입력만 사용한다.
분류 함수는 `src/world/Biome.h/.cpp`에 둔다.

- `temperature`, `precipitation`: `0..1` 값을 5개 band로 나눈다. `0.0 <= value < 0.2`는 `0`, `0.2 <= value < 0.4`는 `1`, `0.4 <= value < 0.6`은 `2`, `0.6 <= value < 0.8`은 `3`, 나머지는 `4`다.
- `groundness`: `0` 미만이면 ocean band `0`, `0` 이상이면 land band `1`이다.

육지 테이블:

```text
        P0           P1          P2       P3              P4
T0      SnowPlain    SnowPlain   Taiga    SnowForest      SnowForest
T1      DryGrass     Grassland   Taiga    Forest          Swamp
T2      DryGrass     Plains      Forest   Forest          Swamp
T3      Desert       DryGrass    Savanna  Forest          Jungle
T4      Desert       Desert      Savanna  TropicalForest  Jungle
```

바다 테이블:

```text
        P0              P1              P2              P3              P4
T0      FrozenOcean     FrozenOcean     ColdOcean       ColdOcean       ColdOcean
T1      ColdOcean       ColdOcean       ColdOcean       TemperateOcean  TemperateOcean
T2      TemperateOcean  TemperateOcean  Ocean           WarmOcean       WarmOcean
T3      WarmOcean       WarmOcean       WarmOcean       TropicalOcean   TropicalOcean
T4      WarmOcean       TropicalOcean   TropicalOcean   TropicalOcean   TropicalOcean
```

디버그 텍스트는 `CLIMATE` 아래에 `BIOME: T[n] P[n] GND[n] - BiomeName` 형식으로 표시한다.

## 초기 스폰

새 월드를 만들 때 플레이어 초기 위치는 월드 생성 전에 결정한다.

- Z 좌표는 온도 중간대에 가까운 `16384`를 사용한다.
- X 좌표는 world seed 기반 난수로 `0..65535` 범위에서 고른다.
- 후보 X/Z column의 표면 블록을 `TerrainBuilder`로 평가하고, 지형 표면 블록이 `grass`이면 그 위 `surfaceY + 1`을 스폰 위치로 저장한다.
- 조건은 현재 `grass` 여부만 본다. 주변 평탄도, 공기 공간, 물가 여부는 검사하지 않는다.
- 최대 후보 횟수 안에 찾지 못하면 fallback 위치를 사용한다.

## 표면 룰

청크 생성은 column별 바이옴을 계산한 뒤 표면/표층 블록을 선택한다.

표면 룰은 공기중 표면과 수중 표면을 분리한다.
수중 표면은 column의 지형 표면이 `seaLevel`보다 낮은 경우다.

```text
Ocean biomes:
  air         grass / dirt
  underwater  sand / sand

Land biomes:
  underwater  sand / sand

Land air:
  Desert      sand  / sandstone
  Swamp       mud   / clay
  Jungle      dirt  / dirt
  Other land  grass / dirt
```

`FrozenOcean`은 해저 블록은 다른 바다처럼 `sand/sand`를 사용한다.
대신 해당 column의 지형 높이가 해수면보다 낮아 물이 생기는 경우, `seaLevel` 위치의 최상단 물 칸만 `ice` 블록으로 대체하고 해당 칸의 fluid는 비운다.
`seaLevel` 아래는 그대로 `water`다.

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
5. 광물 blob, 점토 디스크, 식생, 나무 feature를 생성한다.

`BuildTerrainSource`는 이 과정에서 column별 `terrainHeight`, `terrainSurfaceY`, deterministic tree 후보를 함께 저장한다.
이 source checkpoint는 이후 `ResolveFeatures`가 3x3 입력 view를 읽을 때 높이맵을 다시 샘플링하지 않고 사용할 수 있는 재사용 지점이다.
저장 파일에서 복원된 청크는 후보 목록을 저장하지 않으므로, `WorldRuntime::rebuildDerivedCaches`가 block scan으로 높이/표면 cache만 복원하고 feature 후보는 resolve 단계의 fallback 경로에서 다시 판단한다.

기본 모양 규칙:

- 맨 아래는 bedrock
- 나머지 내부는 rock

bedrock 높이는 전역 난수를 사용해 1~4 범위로 만든다.

## 식생

전역 0~255 난수를 식생 판단에 사용한다.

- plant: `0 ~ 147`
- large stone pile prop: `148 ~ 151`
- stone pile prop: `152 ~ 159`
- branch prop: `160 ~ 167`
- tree: `168 ~ 170`

grass 위에는 같은 0~255 식생 난수 구간에 따라 plant, large stone pile prop, stone pile prop, branch prop, tree 중 하나가 배타적으로 생성된다.
tree는 grass 위에 log와 leaves를 배치한다.

## 나무와 feature resolve

나무와 광물 blob은 청크 경계를 넘을 수 있으므로 feature 단계는 center 청크 단독으로 판단하지 않는다.

- `ResolveFeatures` job은 center 주변 3x3 `TerrainSourceReady` 청크를 입력으로 받는다.
- fresh source 청크는 `TerrainSourceReady`에서 캐시한 deterministic tree 후보를 사용한다.
- 후보 cache가 없는 로드 청크는 복원된 `terrainHeight`를 사용해 deterministic tree 후보를 fallback 평가한다.
- log/leaves가 center 청크 좌표에 닿는 경우에만 center 결과 청크에 쓴다.
- leaves는 air 또는 plant만 덮어쓴다.
- log는 leaves/plant보다 우선한다.
- worker는 주변 청크를 수정하지 않고 center 청크 복사본만 반환한다.

일반 광물과 점토 디스크는 저장 후보 목록을 만들지 않고, `ResolveFeatures`가 3x3 source chunk 좌표에서 결정적 난수로 배치 시도를 다시 계산한다.
광물 feature 목록은 `config/world.json`의 `features.ores`에서 읽는다.

```json
"features": {
  "ores": [
    {
      "name": "coal_ore",
      "enabled": true,
      "block": "coal_ore",
      "replace": "rock",
      "minY": 200,
      "maxY": 512,
      "attemptsPerChunk": 60,
      "size": 25
    }
  ]
}
```

현재 설정 파일에는 `coal_ore`, `copper_ore`, `tin_ore`, `gold_ore`, `iron_ore`가 등록되어 있다.
각 시도는 source chunk 안에서 anchor 좌표를 하나 뽑고, 짧은 선분을 따라 여러 타원 샘플을 겹치는 blob 형태로 만든다.
center chunk에 닿은 후보 칸 중 현재 블록이 `replace`와 같은 칸만 `block`으로 치환한다.
지형 표면 위 공기, 물, 표층 블록, bedrock에 걸친 시도는 자연스럽게 불발되거나 일부만 생성된다.
점토 디스크는 `features.clayDisks`에서 읽는다.

```json
"clayDisks": {
  "enabled": true,
  "block": "clay",
  "replace": ["dirt", "mud", "sand", "clay"],
  "chancePerChunk": 0.20,
  "radiusMin": 4,
  "radiusMax": 6,
  "halfHeight": 1
}
```

점토 디스크는 source chunk마다 확률 판정으로 최대 1회 시도한다.
시도 좌표는 source chunk 내부의 X/Z만 랜덤으로 고르고, Y는 해당 column의 수중 바닥 `terrainSurfaceY`를 사용한다.
바닥 바로 위 칸에 물 유체가 있어야 하며, `replace` 목록에 포함된 블록만 `block`으로 바꾼다.
디스크 반지름은 `radiusMin..radiusMax`, 세로 범위는 중심 바닥 Y 기준 `-halfHeight..+halfHeight`다.
3x3 source view에서 각 source chunk의 후보를 재현하고 center chunk에 닿은 칸만 쓰므로, 청크 경계에 걸친 디스크도 생성 순서와 무관하게 이어진다.
광물 blob을 먼저 적용하고, 이후 점토 디스크, 나무 feature를 적용한다.

## 생성 파이프라인

현재 파이프라인:

```text
Empty
  -> BuildTerrainSource
      높이맵 + 기본 지형/물 + 표면 + 기후 source 생성
  -> TerrainSourceReady
  -> ResolveFeatures
      3x3 TerrainSourceReady 입력 view에서 center feature 확정 + local skylight cache 생성
  -> LocalLightReady
  -> ResolveLight
      center localLight와 4방향 이웃 localLight face로 center resolved skylight 확정
  -> LightResolved
  -> BuildChunkMesh
      3x3 LightResolved 입력 view에서 center mesh 생성
  -> Meshed
```

`BuildTerrainSource`, `ResolveFeatures`, `ResolveLight`의 CPU 처리는 `ClientTerrainJobProcessor`가 수행한다.
feature resolve는 주변 3x3 청크를 입력으로 읽고, light resolve는 center와 동서남북 `localLight` face를 입력으로 읽지만 결과는 center 청크 하나만 반환한다.
`ResolveFeatures`는 feature가 확정된 center 청크의 local skylight를 같은 worker 단계에서 1회 계산해 `localLight` cache에 저장한다.
`ResolveLight`는 3x3 `ChunkData` 전체를 복사하거나 local light를 반복 계산하지 않고, center `localLight`와 동서남북 이웃의 맞닿은 `localLight` face만 읽어 center 내부 boundary propagation으로 resolved light를 산출한다.
skylight 전파 루프는 청크별 attenuation cache와 block-index queue를 공유해 local light 계산과 resolved light 계산이 같은 propagation 경로를 사용한다.
`BuildChunkMesh`는 `TerrainMesher`가 처리하며, light가 확정된 3x3 입력을 사용해 경계면과 AO/light 값을 안정적으로 샘플링한다.
런타임 블록 편집은 별도 subchunk light resolve 경로를 사용한다. 편집된 subchunk는 자기 block/fluid attenuation과 6방향 인접 subchunk boundary light를 seed로 resolved light를 다시 만들고, face 값 변화가 있는 경우에만 인접 subchunk로 dirty 전파한다. 이 경로는 청크 생성 job을 다시 넣지 않고 바뀐 subchunk mesh만 즉시 재생성한다.
렌더 요청은 하위 단계를 즉시 재귀 호출하지 않고 청크별 `targetGenState`를 먼저 설정한다.
이후 `scheduleAround`가 현재 상태와 이웃 조건을 검사해 승급 가능한 frontier만 terrain job queue에 넣는다.
mesh 요청의 target 범위는 실제 의존성 모양을 따른다. mesh center는 3x3 `LightResolved`를 요구하고, light resolve가 center + 4방향 `LocalLightReady`만 읽기 때문에 `LocalLightReady` 목표는 radius 2 square without corners로 제한된다. 그 feature 입력 source는 radius 3 square without corners까지만 필요하다.
완료 이후 재검사는 단계별 dependent shape로 제한한다. source 완료는 local-light 후보 3x3, local-light 완료는 light 후보 center + 4방향, light 완료는 mesh 후보 3x3만 깨운다.
terrain worker 큐와 완료 큐 소유권은 `TerrainJobSystem`에 있고, `Renderer`는 render-dependent mesh job과 GPU 설치 경계만 담당한다.

저장 파일에서 불러온 청크는 생성 파이프라인을 새로 타지 않는다.
`ChunkLoadSystem`은 snapshot IO만 수행하고, `ChunkPrepareSystem`이 별도 worker에서 snapshot blocks/fluids/light/source 데이터를 새 `RuntimeChunk`로 복원한 뒤 derived cache를 재구축한다.
메인 스레드는 준비된 청크를 기존 shell의 ticket/load state와 병합해 설치하고, 필요한 주변 frontier만 다시 검사한다.

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
