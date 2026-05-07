# 월드 생성

## 기본 형태

월드는 높이맵 기반으로 생성된다.
높이는 `height_lut.bin`의 값을 그대로 사용한다.

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

```json
"terrain": {
  "seaLevel": 256,
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

`terrain.seaLevel`은 초기 물 생성 기준 해수면이다.

## 높이 LUT

노이즈 값은 LUT를 통해 실제 높이로 변환한다.

- 노이즈 입력 범위: `-2.0 ~ +2.0`
- LUT 개수: 1024
- LUT 버전: 1
- LUT 높이 값은 `0~512` 범위 안에 있다고 본다.

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
      높이맵 + 기본 지형/물 + 표면 + 자기 feature + outgoing feature 생성
  -> FinalizeFeatures
      incoming feature 반영
  -> Full
  -> BuildChunkMesh
  -> Meshed
```

관련 문서: [[chunk-system]], [[block-data]], [[save-load]]
