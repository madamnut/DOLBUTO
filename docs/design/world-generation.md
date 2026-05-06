# 월드 생성

## 기본 형태

월드는 높이맵 기반으로 생성된다.
현재 높이는 대략 120에서 140 범위에 놓이도록 설계되어 있다.

청크 데이터는 `16 x 512 x 16` 전체 높이를 가진다.

## 노이즈

FastNoise2를 사용한다.
토러스 좌표 변환으로 `65536 x 65536` 범위에서 타일링 가능한 높이맵을 만들도록 구성되어 있다.

현재 설정은 `config/world.json`에서 관리한다.

```json
"terrain": {
  "domainWarp": {
    "enabled": true,
    "amplitude": 0.5,
    "frequency": 1.0,
    "octaveCount": 2,
    "gain": 0.5
  },
  "baseNoise": {
    "featureScale": 2000.0,
    "octaveCount": 4,
    "lacunarity": 3.0,
    "gain": 0.3,
    "simplexScale": 1.0
  }
}
```

## 높이 LUT

노이즈 값은 LUT를 통해 실제 높이로 변환한다.

- 노이즈 입력 범위: `-2.0 ~ +2.0`
- LUT 개수: 1024
- LUT 버전: 1

## 지형 블록 구성

현재 기본 지형은 다음 규칙을 따른다.

- 맨 아래는 bedrock
- 표면 최상단은 grass
- 그 아래 4칸은 dirt
- 나머지 내부는 rock

bedrock 높이는 전역 난수를 사용해 1~4 범위로 만든다.

## 식생

전역 0~255 난수를 식생 판단에 사용한다.

- plant: `0 ~ 167`
- tree: `168 ~ 170`

grass 위에는 70% 확률에 해당하는 기준으로 plant가 생성된다.
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
      지형 + 표면 + 자기 feature + outgoing feature 생성
  -> FinalizeFeatures
      incoming feature 반영
  -> Full
  -> BuildChunkMesh
  -> Meshed
```

관련 문서: [[chunk-system]], [[block-data]], [[save-load]]

