# 블록 데이터

## 정의 파일

블록 정의는 다음 파일에서 읽는다.

```text
assets/data/blocks.json
```

등록된 블록 엔트리만 texture array에 포함된다.

## 현재 블록 ID

```text
0     air
1     rock
2     grass
3     dirt
4     sand
5     sandstone
6     mud
7     clay
8     trunk
9     leaves
10000 plant
65535 bedrock
```

## 주요 속성

- `renderType`: `none`, `cube`, `cross`
- `directional`: 방향성을 가지는지 여부
- `collision`: 플레이어 충돌 여부
- `ao`: 메싱 AO 적용 여부
- `faceOcclusion`: `none`, `opaque`, `cutout`
- `sameBlockFaceCulling`: 같은 블록끼리 면을 가릴지 여부
- `alphaMode`: `opaque`, `cutout`, `blend`
- `alphaCutoff`: cutout 기준값
- `mipDistanceScale`: mip 거리 배율
- `textures`: 면별 텍스처 매핑

## 렌더 타입

`none`:

- 렌더링하지 않는다.
- air가 사용한다.

`cube`:

- 일반 6면 블록이다.
- 그리디 메싱 대상이다.
- 현재 mip 처리 기준이 되는 기본 블록 타입이다.

`cross`:

- X자 스프라이트 형태다.
- plant가 사용한다.
- 양면으로 보이도록 메쉬를 만든다.

## 텍스처 매핑

지원하는 텍스처 키:

- `all`
- `top`
- `bottom`
- `side`
- `topBottom`

텍스처 파일은 `assets/textures/block/*.png`에서 찾는다.
수동 mip 파일은 `assets/textures/block/mip/*_mipN.png`를 우선 사용한다.
없는 mip은 실행 중 생성된다.

## 블록 저장 타입

런타임 청크의 블록 데이터는 `uint16_t` 블록 ID 배열이다.
청크 전체 크기는 `16 x 512 x 16`이므로 블록 수는 131072개다.

유체 데이터 구조는 아직 본격적으로 사용하지 않는다.

관련 문서: [[rendering]], [[world-generation]], [[save-load]]

