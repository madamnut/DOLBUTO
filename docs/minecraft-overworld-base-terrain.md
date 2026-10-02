# Minecraft Java 26.2 오버월드 초기 지형: 노이즈에서 돌·물·공기까지

기준: 2026-10-02, 로컬 `ref/sources/MCP-Reborn`의 Minecraft Java26.2 소스와 동일 버전 client.jar 기본 데이터. MCP-Reborn revision `727d72ffc66bcdf1a8c16ee92b120db2eaa46e26`, MCP `20260616.103818`, official mappings26.2.

문서 관계: [전체 월드 생성 설명](minecraft-world-generation.md) → **이 문서: NOISE까지의 초기 지형** → [오버월드 표면 처리](minecraft-overworld-surface.md). 전체 문서를 먼저 읽지 않아도 이 문서 안에서 스플라인·밀도·재질 결정을 끝까지 추적할 수 있다.

빠른 이동: [범위·구조도](#1-범위와-완료-지점) · [스플라인 평가](#5-peaks--valleys와-중첩-스플라인의-평가법) · [전체 스플라인 구성](#6-offsetfactorjaggedness의-전체-구성) · [밀도](#8-스플라인에서-실제-밀도로) · [동굴](#9-동굴-밀도-함수-전체-연결) · [Aquifer](#11-aquifer-지하수용암경계의-고체) · [블록 기록](#14-블록-배열에-실제로-기록하는-순서) · [완전 수치 트리](#부록-a-오버월드-지형-스플라인-9개-완전-전개)

## 1. 범위와 완료 지점

목표는 **땅이 어디 있고, 어디가 빈 공간이며, 어디에 물을 채울지**를 정하는 과정이다. 오버월드의 시드와 노이즈 초기화, C/E/W·PV, 모든 중첩 스플라인, 3D 밀도, 노이즈 동굴, 보간·캐시, Aquifer, 기본 블록 채우기를 다룬다. 일반 오버월드를 중심으로 설명하고 Large Biomes·Amplified의 차이와 스플라인 전체도 포함한다. 네더·엔드의 지형 그래프는 포함하지 않는다.

완료 지점은 `ChunkStatusTasks.generateNoise` → `NoiseBasedChunkGenerator.fillFromNoise/doFill`이 반환하는 기본 블록 배열이다. 잔디·흙·모래·눈·기반암·심층암 표면 규칙, 이후 CARVERS의 별도 굴착, 일반 광석 feature·식생·구조물 블록 배치는 다음 작업이다.

**바닐라 NOISE 결과가 오직 stone/water/air 세 종류만이라는 뜻은 아니다.** 같은 단계의 Aquifer에는 용암이 있고, OreVeinifier에는 대형 철·구리 광맥과 충전재가 있다. 이들은 기본 지형의 형상 흐름과 구분해 설명한다. 반대로 deepslate의 일반 지층 전환과 bedrock은 표면 단계로 넘긴다. 대형 광맥이 만드는 deepslate_iron_ore와 일반 deepslate 층은 서로 다른 생성 경로다.

### 1.1 전제와 단계 경계

| 입력·선행 자료 | 필요한 이유 |
| --- | --- |
| 세계 시드와 noise_settings | 노이즈·재질·범위·활성 옵션을 확정 |
| density_function/noise 레지스트리 | 밀도 연산 그래프와 옥타브 파라미터를 연결 |
| BIOMES 상태 | 실제 파이프라인에서는 NOISE보다 먼저 생성. 전체 바이옴 선택 표는 이 문서 범위 밖 |
| 구조물 시작·참조 자료 | Beardifier가 지형 밀도에 기여할 수 있음. 구조물 블록은 아직 배치하지 않음 |
| Blender / 업그레이드 상태 | 옛 청크 접합·특정 retrogen의 보정 경로. 새 월드에서는 대체로 항등/비활성 경로 |

```mermaid
flowchart TD
  SEED[시드와 오버월드 설정] --> RANDOM[RandomState와 이름별 노이즈]
  RANDOM --> SIGNAL[C / E / W와 공유 Shift]
  SIGNAL --> PV[PV: W를 접어 R 생성]
  SIGNAL --> SPLINE[Offset / Factor / Jaggedness]
  PV --> SPLINE
  SPLINE --> DEPTH[Y gradient / Factor / jagged noise]
  BASE[Base 3D noise] --> CHEESE[Sloped cheese]
  DEPTH --> CHEESE
  CHEESE --> CAVE[입구 / cheese / spaghetti / pillar]
  CAVE --> POST[slide / blend / 보간 / squeeze]
  POST --> FINAL[noodle과 min: final density]
  FINAL --> STRUCT[Beardifier 더하기]
  STRUCT --> AQUIFER[Aquifer: 유체 또는 공기 / 고체 후보]
  AQUIFER --> MAT[대형 광맥 규칙 / default stone]
  MAT --> OUT[NOISE 완료: 블록과 WG 높이맵 및 유체 후처리 표식]
  OUT --> SURFACE[다음 문서: SURFACE]
```

다이어그램은 데이터 연결이다. 모든 함수를 모든 블록에서 항상 다시 계산한다는 뜻은 아니다. 부모 스플라인의 두 하위 값을 보간하는 구조와 청크 cell 보간은 서로 다른 보간이다.

### 1.2 표기와 코드 위치

- C=Continentalness, E=Erosion, W=접기 전 Weirdness, R=PV(W).
- S_O/S_F/S_J는 순수 스플라인 출력, O/F/J는 전역 offset 및 blending wrapper를 포함한 함수 출력이다.
- `lerp(t,a,b)=a+t(b-a)`. 수치 앞의 `≈`는 설명용 근사이며 부록의 정확한 값을 대체하지 않는다.
- `null` 재질은 공기가 아니라 다음 규칙에 결정을 넘긴다는 뜻이다.
- `noise(id,xzScale,yScale)`의 좌표 배율과 옥타브 주파수를 함께 봐야 한다. yScale=0도 내부 랜덤 Y 오프셋을 0으로 만드는 뜻은 아니다.
- 구체적 JSON 필드와 숫자는 부록 A–D에 있다. 원본 JAR 식별과 재생성 방법은 부록 E다.

| 구현 | 원본 |
| --- | --- |
| 스플라인 생성·평가 | [TerrainProvider](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/data/worldgen/TerrainProvider.java), [CubicSpline](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/util/CubicSpline.java) |
| 난수·노이즈 | [RandomState](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/RandomState.java), [WorldgenRandom](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/WorldgenRandom.java), [NoiseData](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/data/worldgen/NoiseData.java) |
| 밀도 연결 | [NoiseRouterData](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/NoiseRouterData.java), [DensityFunctions](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/DensityFunctions.java) |
| 표본·캐시·채우기 | [NoiseChunk](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/NoiseChunk.java), [NoiseBasedChunkGenerator](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/NoiseBasedChunkGenerator.java) |
| 물·광맥 | [Aquifer](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/Aquifer.java), [OreVeinifier](../ref/sources/MCP-Reborn/src/main/java/net/minecraft/world/level/levelgen/OreVeinifier.java) |

## 2. 좌표·설정·청크 단위

| 단위 | 의미 |
| --- | --- |
| 블록 좌표 | 밀도·표면·배치가 최종적으로 참조하는 `(x,y,z)` |
| 청크 좌표 | XZ 한 변 16블록. 음수 좌표도 바닥 나눗셈/산술 시프트 의미를 유지 |
| 섹션 | 16×16×16 블록의 저장 단위 |
| quart 좌표 | 한 단위 4블록인 기후·바이옴 표본 좌표 |
| noise cell | 값비싼 밀도 계산을 보간할 공간. 오버월드는 XZ 4블록, Y 8블록 |

`NoiseSettings`의 `size_horizontal`, `size_vertical`은 블록 수가 아니다. 각각 4를 곱한 것이 cell 폭·높이다. 오버월드 설정 `(min_y=-64, height=384, size_horizontal=1, size_vertical=2)`는 블록 Y `-64..319`, 4×8×4 cell을 뜻한다. 위쪽 경계 `320`은 높이 범위의 끝과 보간 표본 좌표에 쓰이므로 실제 최고 블록 `319`와 구분한다.

| 기본 설정 | min_y / height | cell XZ / Y | 기본 고체 / 유체 | sea_level |
| --- | --- | --- | --- | ---: |
| overworld / large_biomes / amplified | -64 / 384 | 4 / 8 | stone / water | 63 |

이 표는 **noise generator 범위**다. 차원의 건축 가능 높이나 다른 프리셋과 동일한 개념으로 취급하지 않는다. 생성기는 `clampToHeightAccessor`로 실제 생성 대상의 높이 범위와도 교차시킨다. `sea_level=63`인 기본 FluidStatus는 `y<63`에 물을 반환하므로 물 블록의 최상단 Y와 수면 평면을 구분해야 한다.

`WorldOptions`의 시드·구조물 생성 설정, `WorldDimensions`의 차원별 generator, 레지스트리의 `noise_settings`, `density_function`, `noise`, `biome`, `configured_feature`, `placed_feature`, `structure`, `structure_set`, `template_pool` 등이 함께 월드의 생성 규칙을 이룬다. 같은 시드라도 이들 설정이나 게임 버전이 다르면 같은 월드를 보장하지 않는다.

## 3. 시드에서 난수와 노이즈를 만드는 과정

### 3.1 하나의 전역 난수 커서를 공유하지 않는다

`RandomState`는 설정의 난수 알고리즘으로 세계 시드에서 positional random factory를 만든다. 기본 오버월드는 새 난수 방식을 사용한다. 이 문서는 다른 차원의 legacy 초기화는 다루지 않는다.

난수 분리의 예:

```text
world seed
  -> settings.getRandomSource().newInstance(seed).forkPositional()
      -> hash("aquifer").forkPositional(): 대수층 중심
      -> hash("ore").forkPositional(): 광맥 재질
      -> hash("terrain"): 새 방식의 BlendedNoise
      -> noise resource ID: 각 NormalNoise
          -> octave 이름: 개별 ImprovedNoise
```

Xoroshiro 계열의 positional factory는 좌표 해시와 저장된 seed를 결합해 그 위치의 난수 생성기를 만든다. 이 방식은 다른 청크가 먼저 생성되어도 해당 좌표에서 재현 가능한 값을 만들기 좋다. 다만 **모든 결과가 처리 순서와 무관하다**는 뜻은 아니다. feature 목록의 순서·시드 분배와 인접 블록 상태, 데이터팩 변경은 결과를 바꿀 수 있다.

### 3.2 초기 지형에서의 이름별·좌표별 난수 분리

이름 기반 분기는 어떤 종류의 표본인지, 좌표 기반 분기는 어디의 표본인지를 나눈다. Aquifer 중심과 광맥의 광석 확률이 같은 전역 난수 커서를 공유하는 구조가 아니다. XoroshiroPositionalRandomFactory.at은 `Mth.getSeed(x,y,z)`와 저장된 seedLo를 XOR하고 seedHi와 함께 새 난수 생성기를 만든다. fromHashOf는 이름에서 얻은 128비트 값과 저장된 seed들을 결합한다.

포팅 시 Java long의 overflow, 좌표의 부호, 동일 이름의 해시 방식까지 유지해야 좌표별 동일 표본을 얻을 수 있다. 장식·일반 광석·구조물 후보 배치의 별도 난수 배분은 NOISE 이후/외부 단계이므로 전체 설명서에서 다룬다.

## 4. 노이즈의 실제 구성

### 4.1 ImprovedNoise → PerlinNoise → NormalNoise

`ImprovedNoise`는 난수로 만든 256개 permutation과 축별 오프셋을 가지고, 격자 꼭짓점의 gradient dot product를 부드럽게 보간한다. `PerlinNoise`는 이 노이즈를 여러 옥타브로 합친다. `NormalNoise`는 서로 다른 두 PerlinNoise 합을 약간 다른 좌표 배율로 더해 보정한다.

```text
PerlinNoise:
  frequency_i = 2^(firstOctave+i)
  valueFactor_i = [2^(n-1)/(2^n-1)] / 2^i
  result = sum(amplitude_i * ImprovedNoise_i(wrap(p*frequency_i)) * valueFactor_i)

NormalNoise:
  secondCoordinates = p * 1.0181268882175227
  output = (firstPerlin(p) + secondPerlin(secondCoordinates)) * valueFactor
  valueFactor = (1/6) / [0.1 * (1 + 1/(nonzeroOctaveSpan+1))]
```

여기서 `nonzeroOctaveSpan`은 0이 아닌 진폭의 마지막 인덱스와 첫 인덱스의 차이다. 단순히 전체 옥타브 개수로 대체하면 다를 수 있다. 진폭 합으로 나눈 일반적인 정규화 FBM과도 다르다. `PerlinNoise.wrap`은 큰 좌표의 수치 문제를 줄이는 약 33,554,432 단위 축약이며 DOLBUTO의 게임 월드 순환 규칙과 동일한 기능이 아니다.

### 4.2 중요한 기본 노이즈

| 용도 | firstOctave | 진폭 |
| --- | ---: | --- |
| temperature | -10 | 1.5, 0, 1, 0, 0, 0 |
| vegetation / humidity | -8 | 1, 1, 0, 0, 0, 0 |
| continentalness | -9 | 1, 1, 2, 2, 2, 1, 1, 1, 1 |
| erosion | -9 | 1, 1, 0, 1, 1 |
| ridge / weirdness | -7 | 1, 2, 1, 0, 0, 0 |
| offset / shared shift | -3 | 1, 1, 1, 0 |
| jagged | -16 | 1을 16개 |

전체 목록은 부록 D에 있다. 실제 블록 크기당 파장은 firstOctave 하나만으로 결정되지 않는다. 함수가 입력 좌표에 곱하는 `xz_scale`, `y_scale`, shift까지 합쳐야 한다.

Large Biomes는 temperature·vegetation·continentalness·erosion의 firstOctave를 각각 2 낮춘 별도 노이즈를 사용한다. 기본 주파수는 1/4이 되지만 shift와 ridge 등 모든 입력을 일괄 4배 확대하는 구현은 아니다.

### 4.3 공유 Shift와 축 이름의 함정

기본 shift 함수는 다음과 같은 좌표 관계다. `N_shift`는 레지스트리의 `offset` NormalNoise다.

```text
shiftX(x,z) = 4 * N_shift(x/4, 0, z/4)
shiftZ(x,z) = 4 * N_shift(z/4, x/4, 0)
signal(x,z) = N_signal(x*0.25 + shiftX, 0, z*0.25 + shiftZ)
```

온도·습도·C·E·W가 이 변형된 좌표를 공유한다. 서로 완전히 독립적인 위치 왜곡을 만드는 것과 다르다. 또한 클래스·변수 이름이 혼동을 일으킨다.

| 코드/등록 이름 | 실제 의미 |
| --- | --- |
| `overworld/ridges`, `NoiseRouter.ridges` | 접기 전 Weirdness W |
| `overworld/ridges_folded` | `PV(W)`인 R |
| TerrainProvider의 인자 `weirdness` | W |
| TerrainProvider의 인자 `ridges` | R |
| Climate의 weirdness | W, R이 아님 |

## 5. Peaks & Valleys와 중첩 스플라인의 평가법

### 5.1 Weirdness를 접는 함수

실행 밀도 함수의 이상적인 표기는 다음과 같다.

```text
R = -3 * (abs(abs(W) - 2/3) - 1/3)
```

| W | R (이상적인 값) | 해석 |
| ---: | ---: | --- |
| 0 | -1 | 계곡 중심 쪽 |
| ±1/3 | 0 | 중간 띠 |
| ±2/3 | 1 | 능선·정상 띠 |
| ±1 | 0 | 다시 중간 띠 |

R은 W의 부호를 잃는다. 그래서 같은 R이라도 W 양·음에 따라 Factor와 Jaggedness에서 다른 결과가 나오게 별도의 W 스플라인이 남아 있다. 본문의 분수는 설명용이고, `TerrainProvider.peaksAndValleys(float)`로 제어점 위치를 만들 때 쓰는 float 상수와 runtime density graph의 double 상수는 정밀도가 다르다. 예를 들어 W=0.4와 W=0.56666666에서 얻는 R≈0.2와 R≈0.7, 그 중간인 R≈0.45의 정확한 제어점 위치는 원본 float 연산 결과를 보존한 부록에서 확인해야 한다.

### 5.2 한 스플라인 제어점의 구성

각 multipoint spline은 입력 축 하나와 오름차순 제어점 목록이다. 각 점은 `(location, value, derivative)`를 가진다. `value`는 상수일 수도, 다른 입력 축의 스플라인일 수도 있다. derivative는 **부모 spline의 입력 축에 대한 접선**이다. 하위 spline의 derivative와 합쳐서 하나의 값으로 보면 안 된다.

```text
C spline
  C=-0.1 -> E spline
              E=-0.85 -> R spline -> 상수 잎들
              E=-0.70 -> R spline -> 상수 잎들
              ... (이 설명 그림의 생략분은 부록 A에서 모두 전개)
  C= 0.25 -> 다른 E spline -> ...
```

C가 두 제어점 사이에 있으면 두 E spline을 **현재의 동일한 E/W/R 좌표**로 각각 평가한 뒤 부모 C spline에서 보간한다. 인접 제어점 하나를 택하고 그 트리만 내려가는 이산 결정 트리가 아니다. 같은 원리로 두 R spline을 동시에 평가해 E 방향으로 섞을 수 있다.

### 5.3 Cubic Hermite 보간과 바깥 범위

인접 위치가 `x0,x1`, 현재 입력이 x일 때:

```text
t = (x-x0)/(x1-x0)
y0 = evaluate(child0, 전체 좌표)
y1 = evaluate(child1, 전체 좌표)
a = d0*(x1-x0) - (y1-y0)
b = -d1*(x1-x0) + (y1-y0)
output = lerp(t,y0,y1) + t*(1-t)*lerp(t,a,b)
```

첫 점 왼쪽이나 마지막 점 오른쪽에서는 끝점의 값과 기울기로 선형 외삽한다. 끝점 기울기 0이면 상수 연장이다. 모든 입력을 [-1,1]로 임의 clamp하는 일반 규칙이 아니다.

모든 derivative가 0이라고 선형 보간이 되는 것도 아니다. 이 경우 두 값 사이의 cubic smoothstep 보간이 된다. 상수 0.63과 0.30을 W=-0.01, +0.01에 derivative=0으로 두면 W=0의 값은 0.465지만 양 끝에서는 완만하게 붙는다.

`CubicSpline` 자체는 float 계산이다. DensityFunction이 double을 계산하다 spline 좌표에서 float로 변환되는 경계가 있다. 동일 출력을 목표로 구현한다면 연산 순서·정밀도·기울기 적용 위치를 유지해야 한다.

## 6. Offset·Factor·Jaggedness의 전체 구성

### 6.1 Offset: C → E → R, 일부는 R → R

최상위 C spline의 점은 다음과 같다. 기본 derivative는 모두 0이다. 이 표의 값은 아직 전역 상수 `-0.50375F`를 더하기 전 `S_O`다.

| C 위치 | 값 |
| ---: | --- |
| -1.1 | 0.044 |
| -1.02 | -0.2222 |
| -0.51 | -0.2222 |
| -0.44 | -0.12 |
| -0.18 | -0.12 |
| -0.16 | beach E spline |
| -0.15 | 같은 beach E spline |
| -0.1 | low E spline |
| 0.25 | mid E spline |
| 1.0 | high E spline |

`buildErosionOffsetSpline`의 실제 호출 인자를 모두 펼치면:

| E spline | lowValley | hill | tallHill | mountainFactor | plain | swamp | includeExtremeHills | saddle |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| beach | -0.15 | 0 | 0 | 0.1 | 0 | -0.03 | false | false |
| low | -0.1 | 0.03 | 0.1 | 0.1 | 0.01 | -0.03 | false | false |
| mid | -0.1 | 0.03 | 0.1 | 0.7 | 0.01 | -0.03 | true | true |
| high | -0.05 | 0.03 | 0.1 | 1 | 0.01 | 0.01 | true | true |

각 E spline의 기본·추가 점 구성:

| E | 값인 하위 spline |
| ---: | --- |
| -0.85 | mountain modulation=`lerp(mountainFactor,0.6,1.5)` |
| -0.7 | mountain modulation=`lerp(mountainFactor,0.6,1.0)` |
| -0.4 | mountain modulation=`mountainFactor` |
| -0.35 | widePlateau |
| -0.1 | narrowPlateau |
| 0.2 | plains |
| 0.4 | plainsFarInland (includeExtremeHills=true에서만) |
| 0.45 | extremeHills (같은 조건) |
| 0.55 | extremeHills (같은 조건) |
| 0.58 | plainsFarInland (같은 조건) |
| 0.7 | swamps |

여기의 E derivative도 0이다. 숫자는 E 범주 표와 반드시 같지 않다. “erosion index별 구간”과 “연속 spline 제어점”을 같은 데이터로 합치면 원본과 달라진다.

#### ridgeSpline의 모든 점과 기울기

인자가 `valley,low,mid,high,peaks,minValleySteepness`일 때:

```text
d1 = max(0.5*(low-valley), minValleySteepness)
d2 = 5*(mid-low)
R=-1.0: value=valley, derivative=d1
R=-0.4: value=low,    derivative=min(d1,d2)
R= 0.0: value=mid,    derivative=d2
R= 0.4: value=high,   derivative=2*(high-mid)
R= 1.0: value=peaks,  derivative=0.7*(peaks-high)
```

widePlateau의 값 인자는 `(lowValley-0.15, 0.5*m, 0.5*m, 0.5*m, 0.6*m, 0.5)`, narrowPlateau는 `(lowValley, plain*m, hill*m, 0.5*m, 0.6*m, 0.5)`다. plains와 plainsFarInland는 `(lowValley, plain, plain, hill, tallHill, 0.5)`이며 swamps는 `(-0.02, swamp, swamp, hill, tallHill, 0)`이다. 여기서 m은 mountainFactor다.

extremeHills는 R=-1에 lowValley, R=-0.4에 **plains R spline 자체**, R=0에 `tallHill+0.07`을 둔다. 즉 같은 R 축이 중첩되는 가지도 실제로 있다. 부록의 `axis=...ridges_folded`가 연속 두 번 등장하는 것은 출력 오류가 아니다.

#### mountain ridge 생성 함수의 모든 분기

modulation을 m이라 두면 다음 값으로 산 능선 곡선을 만든다.

```text
s = 1-(1-m)*0.5
i = 0.5*(1-m)
raw(r) = (r+1.17)*0.46082947*s-i
h(r) = r < -0.7 ? max(raw(r),-0.2222) : max(raw(r),0)
z = i/(0.46082947*s)-1.17
```

`-0.65 < z < 1`이면 점은 `-1,-0.75,-0.65,z-0.01,z,1`이다. 값은 각각 h에서 오며 `z-0.01`의 값도 h(z)를 사용한다. 처음 기울기는 `(h(-0.75)-h(-1))/0.25`, 마지막 두 점의 기울기는 `(h(1)-h(z))/(1-z)`, 나머지는 0이다.

그 밖에는 `d=(h(1)-h(-1))/2`를 사용한다. saddle=false이면 (-1,h(-1),d), (1,h(1),d) 두 점이다. saddle=true이면 첫 점을 (-1,max(0.2,h(-1)),0)으로 바꾸고, (0,lerp(0.5,h(-1),h(1)),d)를 추가한 뒤 마지막 점을 둔다. **m=0.1/0.7/1 및 파생 modulation의 실제 값들은 부록 A에서 전부 수치로 전개되어 있다.**

### 6.2 Factor: C → E → W 또는 R → W

Factor는 밀도 기울기에 곱해지는 값이다. 높이를 단순히 몇 배 하는 값이 아니며, 3D 노이즈가 같은 크기로 더해질 때 기울기가 작은 곳은 굴곡·돌출의 상대적 영향이 커질 수 있다.

| C | 값/호출 | shatteredTerrain | Amplified transform |
| ---: | --- | --- | --- |
| -0.19 | 상수 3.95 | 해당 없음 | 적용하지 않음 |
| -0.15 | baseValue 6.25의 E spline | true | 적용하지 않음 |
| -0.1 | baseValue 5.47의 E spline | true | amplified일 때 적용 |
| 0.03 | baseValue 5.08의 E spline | true | amplified일 때 적용 |
| 0.06 | baseValue 4.69의 E spline | false | amplified일 때 적용 |

기본 W spline `B(W)`는 `W=-0.2 → 6.3`, `W=0.2 → baseValue`다. E의 공통 점들은 `-0.6:B`, `-0.5:W[-0.05→6.3,+0.05→2.67]`, `-0.35:B`, `-0.25:B`, `-0.1:W[-0.05→2.67,+0.05→6.3]`, `0.03:B`다.

shattered=true의 추가 가지:

```text
WS(W): W=0 -> baseValue; W=0.1 -> 0.625
RS(R): R=-0.9 -> baseValue; R=-0.69 -> WS(W)
E=0.35 -> baseValue
E=0.45 -> RS(R)
E=0.55 -> RS(R)
E=0.62 -> baseValue
```

shattered=false의 추가 가지:

```text
EH(R): R=-0.7 -> B(W); R=-0.15 -> 1.37
PK(R): R=0.45 -> B(W); R=0.7 -> 1.56
E=0.05 -> PK(R)
E=0.4  -> PK(R)
E=0.45 -> EH(R)
E=0.55 -> EH(R)
E=0.58 -> baseValue
```

Factor의 모든 점은 derivative=0으로 등록된다. E가 양수라고 항상 Factor가 단조롭게 변하는 것이 아니다. 0.45–0.55 주변에 별도 중첩 가지가 있어 윈드스웹트 형태를 위한 변화가 생긴다.

### 6.3 Jaggedness: C → E → R → W

최상위 C는 `-0.11→0`, `0.03→near E spline`, `0.65→far E spline`이다. near의 인자는 `(peakE0=1,peakE1=0.5,highE0=0,highE1=0)`, far는 `(1,1,1,0)`이다.

E spline은 `-1→ridgeE0`, `-0.78→ridgeE1`, `-0.5775→ridgeE1`, `-0.375→0`이다. 각 ridge spline은 R=`PV_float(0.4)`에 0, R=`(PV_float(0.4)+PV_float(0.56666666))/2`에 high 계수의 W spline 또는 0, R=1에 peak 계수의 W spline 또는 0을 둔다.

계수 k의 W spline은 `W=-0.01→0.63*k`, `W=+0.01→0.3*k`다. 모든 derivative는 0이다. 이 때문에 같은 접힌 능선에서도 W의 부호에 따라 잔굴곡 강도가 다르다. `S_J` 자체는 잔굴곡의 최종 높이가 아니라 뒤에서 jagged noise에 곱해질 공간별 강도다.

## 7. 일반·Large Biomes·Amplified의 차이와 숫자 예시

### 7.1 Amplified는 최종 높이에 일괄 배율을 곱하지 않는다

스플라인을 생성할 때 상수 잎에 적용되는 변환:

```text
offset leaf:     v < 0 ? v : 2*v
factor leaf:     1.25 - 6.25/(v+5)
jaggedness leaf: 2*v
```

`CubicSpline.Builder`는 **상수 값을 추가할 때** 변환한다. 중첩 spline을 값으로 추가할 때 그 하위 트리를 다시 변환하지 않으며, derivative도 함께 변환하지 않는다. 따라서 완성한 일반 스플라인의 출력 전체에 같은 함수를 씌우면 원본과 같지 않다. 특히 offset은 양수 잎이 커져도 원래 기울기를 유지한다.

Factor에는 앞 절의 예외가 있다. C=-0.19의 3.95와 C=-0.15의 baseValue=6.25 가지는 Amplified에서도 identity다. 전역 offset 상수와 runtime slide 값도 별개다. Amplified는 위쪽 slide와 아래쪽 target도 바꾼다.

Large Biomes는 같은 제어점·기울기 구조에 더 큰 C/E 노이즈를 연결한다. 부록 A에는 단순히 “일반과 동일”이라고 생략하지 않고 large_biomes 축 이름을 가진 3개도 전부 출력한다.

### 7.2 바다 쪽 Offset 계산

일반 지형에서 C=-0.3은 -0.44와 -0.18 사이이고 두 값은 모두 -0.12, derivative=0이다. 따라서 하위 E/W/R과 무관하게 `S_O=-0.12`다. 구형 청크 blending이 없는 곳에서:

```text
O ≈ -0.50375 - 0.12 = -0.62375
YGradient(y) = 1.5 - (y+64)/128  (범위 안)
depth = YGradient + O
depth=0인 기준 y ≈ 128+128*O = 48.16
```

48.16은 실제 해저 최고 블록을 예언하는 값이 아니다. Factor·jaggedness·base 3D noise·동굴·보간·surface 처리를 빼고 본 기준선이다. 바다 수면은 별도의 sea_level=63 규칙을 사용한다.

### 7.3 산지 Jaggedness의 중첩 계산

일반 지형의 C=0.65, E=-1, W=2/3을 설명용으로 택하면 R≈1이다. far E spline의 E=-1 가지 → peak 계수 1의 W spline으로 내려간다. W가 +0.01보다 크고 끝점 derivative가 0이므로 `S_J≈0.3`이다. W=-2/3이면 같은 R≈1이지만 `S_J≈0.63`이다. 이 값에 jagged noise의 half_negative를 곱해야 실제 depth에 더할 변동량이 나온다.

예를 들어 설명용 jagged noise 값이 -0.4이면 half_negative=-0.2이므로 양수 W 쪽 추가량은 약 -0.06이다. 이는 주어진 시드에서 실제 -0.4가 나온다고 주장하는 예가 아니라 중첩 평가와 부호 처리를 보이기 위한 수치다.

### 7.4 부모 축도 보간한다

C가 0.03과 0.65 사이에 있으면 near/far 두 Jaggedness E spline을 현재 E/W/R에서 각각 평가한다. 이후 C 방향에서 두 결과를 Hermite 보간한다. 하위 트리가 달라도 부모는 완성된 두 스칼라를 섞는다. 이 구조가 지형 경계가 계단처럼 갈라지는 것을 줄인다.

구체적으로 C≈0.34, E=-1, W≈0.48333333이면 R≈0.45다. 이 R은 high ridge의 중간 제어점에 해당한다. near 가지의 high 계수는 0이라 결과≈0, far 가지의 high 계수는 1이고 W가 양수 끝점 밖이므로 결과≈0.3이다. 부모 C 구간의 t≈0.5이고 양끝 derivative=0이므로 최종 `S_J≈0.15`가 된다. W를 음수로 바꾸면 같은 R이지만 far 결과≈0.63, 부모 출력≈0.315로 바뀐다. 이 예시의 소수들은 설명용 근사값이며 float32 경계에서 몇 ulp 차이가 날 수 있다.

## 8. 스플라인에서 실제 밀도로

### 8.1 Offset/Factor/Jaggedness를 등록 함수로 감싸기

`splineWithBlending`은 `flat_cache(cache_2d(lerp(blendAlpha, oldTarget, newValue)))` 관계다. 일반적인 새 영역에서는 old/new blending이 필요 없어 새로운 값이 사용된다. 구형 청크 접합 영역에서는 offset은 blendOffset, factor는 10, jaggedness는 0을 기존 지형 쪽 대상으로 삼는다.

```text
O = blend(oldOffset, -0.50375F + S_O)
F = blend(10, S_F)
J = blend(0, S_J)
depth = y_clamped_gradient(-64,320,1.5,-1.5) + O
jaggedTerm = J * half_negative(noise(jagged, xz_scale=1500, y_scale=0))
g = F * (depth + jaggedTerm)
gradientDensity = 4 * quarter_negative(g)
slopedCheese = gradientDensity + base_3d_noise
```

`half_negative(v)`는 음수일 때만 v/2, `quarter_negative(v)`는 음수일 때만 v/4다. 그러므로 4배까지 포함하면 g>0인 고체 쪽은 4g, g<0인 쪽은 g가 된다. y gradient의 음수 기울기 때문에 높은 곳으로 갈수록 대체로 빈 공간 쪽이 된다.

### 8.2 Base 3D noise

`BlendedNoise`는 저·고 limit 두 합과 선택용 main 합을 결합한다. main은 8옥타브, 각 limit은 16옥타브 루프다. main에서 얻은 `factor=(main/10+1)/2`로 `blendMin/512`와 `blendMax/512`를 clampedLerp하고 마지막에 128로 나눈다. factor가 범위를 벗어나면 필요 없는 limit 계산을 생략하는 경로가 있다.

오버월드의 설정은 `(xzScale=.25, yScale=.125, xzFactor=80, yFactor=160, smearScaleMultiplier=8)`이다. 2D 스플라인이 부드러운 기준 지형을 정하고 이 3D 함수가 높이에 따른 불규칙성을 더하므로, 순수 높이맵보다 오버행·입체 구조가 가능하다. 이 함수만으로 모든 동굴이 만들어지는 것은 아니다.

### 8.3 밀도 부호와 기본 재질

블록 채우기에서 쓰는 값은 대체로 `finalDensity + Beardifier`다. Aquifer는 density>0이면 `null`을 반환해 고체 재질 후보로 넘긴다. density≤0이면 공기/유체 또는 대수층 경계의 고체 후보를 정한다. 다음 OreVeinifier도 결정을 못 하면 generator가 defaultBlock을 사용한다.

따라서 `density≤0 → 언제나 공기`, `density>0 → 언제나 일반 돌`이라는 설명은 둘 다 부정확하다. 물·용암·광맥·구조물 주변 보정이 함께 참여한다.

### 8.4 NoiseRouter의 15개 출력

라우터는 하나의 노이즈를 뜻하지 않는다. 뒤의 생성 단계들이 서로 다른 목적에 쓰는 함수 묶음이다. 같은 등록 함수를 여러 출력이 참조할 수 있다.

| 필드 | 기본 오버월드의 소비자·의미 |
| --- | --- |
| barrier | Aquifer 경계 압력 노이즈 |
| fluid_level_floodedness | 대수층의 포화·건조 판단 |
| fluid_level_spread | 지역 수위 높이 변동 |
| lava | 지역 대수층의 용암 선택 |
| temperature | 기후 공간의 온도 |
| vegetation | 기후 공간의 humidity |
| continents | C: 대륙성 |
| erosion | E: 침식 입력 |
| depth | Y gradient + offset |
| ridges | 접기 전 W |
| preliminary_surface_level | 실제 블록 완성 전에 필요한 기준 표면 탐색 |
| final_density | 동굴·slide·보간·noodle까지 포함한 밀도, Beardifier는 이후 추가 |
| vein_toggle | 광맥 종류·강도 |
| vein_ridged | 광맥의 좁은 형상 조건 |
| vein_gap | 광석을 넣을지 끊어낼지 판단 |

위 표의 이름은 JSON 필드 표기다. Java accessor의 barrierNoise/fluidLevelFloodednessNoise 등과 대응한다. 부록 C에서 일반·대형 바이옴·증폭의 실제 연결 차이를 확인할 수 있다.

## 9. 동굴 밀도 함수 전체 연결

동굴에는 **밀도 그래프에 포함된 noise caves**와 **SURFACE 이후 실행하는 carver**가 있다. 생성 단계와 계산 방식이 다르다.

### 9.1 입구와 3D spaghetti

`entrances`는 큰 입구와 3D spaghetti의 min이다. 3D spaghetti는 rarity로 좌표 배율과 출력 크기를 조절한 두 노이즈의 max에 thickness를 더해 clamp한다. roughness는 이 표면에 작은 변화를 더한다.

```text
roughness = mappedNoise(roughness_modulator, 0,-0.1) * (abs(noise(roughness))-0.4)
bigEntrance = noise(cave_entrance, .75,.5) + .37 + gradientY(-10,30,.3,0)
entrances = min(bigEntrance, roughness + spaghetti3D)
```

rarity3D는 입력 경계 `[-0.5,0,0.5]`에 따라 `[0.75,1,1.5,2]`, rarity2D는 `[-0.75,-0.5,0.5,0.75]`에 따라 `[0.5,0.75,1,2,3]`를 사용한다. 각 rarity r에 대해 `r * noise(p/r)`를 만들고 abs를 적용한다. 동굴의 굵기·빈도 관계를 단순히 한 노이즈 진폭으로만 조절하지 않는다.

### 9.2 2D spaghetti

이름에 2D가 들어가지만 최종 함수는 높이와 연결된다. elevation noise를 2D로 캐시한 값과 -64..320의 Y gradient(8..-40)를 더해 절댓값을 취하고 thickness를 더한 후 세제곱한다. 이것과 rarity 적용 노이즈 + `0.083*thickness` 중 max를 골라 [-1,1]로 clamp한다. 결과적으로 제한된 높이 주변을 따라가는 통로 성격이 나온다.

### 9.3 Cheese 동굴과 pillar

```text
layer = 4 * noise(cave_layer, y_scale=8)^2
cheesePart = clamp(0.27 + noise(cave_cheese, y_scale=2/3), -1,1)
topSolid = clamp(1.5 - 0.64*slopedCheese, 0,0.5)
baseCave = layer + cheesePart + topSolid
subtracted = min(baseCave, entrances, spaghetti2D + roughness)
underground = max(subtracted, cutoffPillars)
```

min이 더 낮은 밀도를 골라 공간을 파는 방향, max가 밀도를 높여 고체를 남기는 방향이라는 점이 핵심이다. pillar는 `2*pillarNoise + rareness`에 thickness의 세제곱을 곱한다. 값이 0.03 미만인 pillar는 매우 작은 밀도로 바꾸어 max에서 영향을 없앤다. pillar를 모든 동굴에 일괄 적용해 공간을 막는 방식이 아니다.

### 9.4 지표 가까이와 깊은 영역의 선택

```text
surfaceWithEntrances = min(slopedCheese, 5*entrances)
if -1000000 <= slopedCheese < 1.5625:
    caves = surfaceWithEntrances
else:
    caves = underground(slopedCheese)
```

이 조건은 실제 Y 몇 층 이하라는 분기가 아니라 slopedCheese 값에 대한 분기다. `1.5625`를 블록 높이나 수면 값으로 착각하지 않는다.

### 9.5 Noodle 동굴

noodle toggle·thickness·두 ridge는 -60..320 범위에서 평가하고 바깥에는 각각 정해진 대체 값을 넣는다. toggle이 음수인 쪽은 상수 64를 반환해 나중 min에서 보통 제한을 만들지 않는다. 켜진 쪽은 `thickness + 1.5*max(abs(ridgeA),abs(ridgeB))`다. ridge 입력 배율은 8/3이다. 최종 min에서 별도의 가느다란 통로를 만든다.

### 9.6 상·하단 slide와 최종 식

일반 오버월드의 윗부분은 Y=240..256에서 top target `-0.078125`로, 아랫부분은 -64..-40에서 bottom target `0.1171875`와 지형 값을 섞는다. Amplified는 윗부분 304..320, bottom target=0.4로 달라진다.

```text
topFactor = gradientY(minY+height-topStart, minY+height-topEnd, 1,0)
withTop = lerp(topFactor, topTarget, caves)
bottomFactor = gradientY(minY+bottomStart, minY+bottomEnd, 0,1)
slid = lerp(bottomFactor, bottomTarget, withTop)
post = squeeze(interpolated(0.64 * blend_density(slid)))
finalDensity = min(post, noodle)
squeeze(v): c=clamp(v,-1,1); return c/2-c^3/24
```

보간을 squeeze 앞에 하느냐 뒤에 하느냐, noodle min을 어디에 두느냐도 결과에 영향을 준다. 모든 함수를 묶어 블록마다 평가하거나 최종 결과만 보간하는 것으로 임의 변경하지 않는다.

## 10. Preliminary surface: 값싼 기준 지표 탐색

`preliminarySurfaceLevel`은 Aquifer와 표면 처리 등에 필요한 예비 지표다. 실제 동굴·장식까지 반영한 완성 높이맵과 다르다. O와 F를 캐시하고 상한 추정치를 만든 뒤, 단순화한 밀도를 cellHeight 간격으로 내려가며 `findTopSurface`로 찾는다.

```text
u = 0.2734375/F - O
upper = clamp(remap(u, 1.5,-1.5, -64,320), -40,320)
simple = clamp(noiseGradientDensity(F, offsetToDepth(O)) - 0.703125, -64,64)
probeDensity = slideOverworld(simple) - 0.390625
preliminary = findTopSurface(probeDensity, upper, lower=-64, step=8)
```

이것을 `depth=0`의 해로 대체하면 offset 외 Factor·slide·임계값의 영향이 사라진다. 또 최종 높이맵을 만들기 전에 주변 예상 표면이 필요한 Aquifer의 의존성을 끊는 역할이 있다.

## 11. Aquifer: 지하수·용암·경계의 고체

### 11.1 전역 수위와 지역 대수층

기본 globalFluidPicker는 `y<min(-54,seaLevel)`이면 용암 상태를, 그 밖에는 seaLevel의 기본 유체 상태를 준다. FluidStatus의 `at(y)`는 y가 수위보다 낮아야 유체, 아니면 공기다. 오버월드에서는 이 전역 규칙에 지역 Aquifer 판단을 추가한다.

양의 지형 밀도는 곧바로 고체 후보로 넘긴다. 음수일 때 전역 용암과 높은 위치의 빠른 경로를 검사한 뒤, 필요한 영역에서 이웃 대수층 중심들을 조사한다.

### 11.2 셀·중심과 가까운 이웃

중심 배치 grid는 XZ 간격 16, Y 간격 12다. 각 grid 안 중심은 positional random으로 x/z 0..9, y 0..8의 변위를 가진다. 실제 검사에서는 주변 후보까지 거리 제곱을 구해 가까운 중심들을 유지한다. 주 압력 판단은 가까운 3개 쌍, 유체 후처리 판단에는 4번째 중심도 쓰인다.

```text
similarity(d1Squared,d2Squared) = 1-(d2Squared-d1Squared)/25
```

두 중심의 거리가 비슷하면 경계 가까이라는 뜻이고 수위·유체 차이의 영향이 강해진다. 각 중심의 FluidStatus와 위치는 별도 캐시에 저장되어 반복 계산을 줄인다.

### 11.3 수위 선택

주변 13개 표면 표본 위치를 살펴 낮은 예비 지표와 지표의 전역 유체 노출 여부를 계산한다. 중심 위쪽이 이미 바다와 연결되어야 하는 상황에서는 전역 수위를 빠르게 반환한다. 그 밖에는 floodedness noise와 지표까지 거리로 fully/partially flooded 상태를 고른다.

- fully flooded이면 globalFluid의 수위를 사용한다.
- partially flooded이면 16×40×16의 별도 수위 grid에서 중심 Y를 만들고 spread noise×10을 3 단위로 양자화한다. 수위는 주변 예비 지표를 넘지 않게 한다.
- 둘 다 아니면 매우 낮은 수위로 사실상 건조 상태를 만든다.

deep dark 건조 판정은 `erosion<-0.225 && depth>0.9`다. 바이옴 최근접 조회 자체를 매번 하는 것과 다르다. 지역 수위가 -10 이하이고 특정 예외가 아니면 64×40×64 좌표의 lava noise 절댓값 >0.3에서 용암으로 바뀔 수 있다. 전역 -54 용암 기준과 지역 용암 판정을 혼동하지 않는다.

### 11.4 압력·벽과 유체 후처리

두 상태가 물과 용암이면 압력 함수는 강한 경계값을 준다. 같은 수위면 압력이 0인 빠른 경로가 있다. 다른 경우 평균 수위, 수위 차, 현재 Y와 경계 내부까지 거리를 이용한 piecewise gradient에 barrier noise를 더한다. 가까운 중심 간 similarity로 가중한 압력과 원래 density의 합이 양수이면 고체 후보를 남긴다.

`shouldScheduleFluidUpdate`는 그 위치의 유체를 후속 갱신 대상으로 표시할지 알려준다. 즉 생성 과정의 유체 블록 결정과 게임이 실행된 뒤 흐르는 유체 시뮬레이션은 연결되지만 별개의 단계다.

## 12. 부가 재질: 대형 광맥

OreVeinifier는 기본 블록 채우기 안에서 실행된다. FEATURES의 일반 광석 feature와 별도다. Aquifer가 이미 공기·유체를 결정한 위치에 무조건 광석을 덮는 방식이 아니다.

| veinToggle 부호 | 종류 | Y 포함 범위 | 광석 / 원석 블록 / 주변 충전재 |
| --- | --- | --- | --- |
| 양수 | COPPER | 0..50 | copper_ore / raw_copper_block / granite |
| 그 외 | IRON | -60..-8 | deepslate_iron_ore / raw_iron_block / tuff |

평가 순서:

1. Y 범위를 벗어나면 결정하지 않는다.
2. 위·아래 범위 경계에서 20블록에 걸쳐 -0.2..0의 edgeRoundoff를 넣는다.
3. `abs(toggle)+edgeRoundoff<0.4`이면 탈락한다.
4. 좌표별 난수 값이 0.7보다 크면 탈락한다.
5. `veinRidged>=0`이면 탈락한다. ridged는 `max(abs(veinA),abs(veinB))-0.08F`다.
6. abs(toggle)을 0.4..0.6에서 richness 0.1..0.3으로 clamp-map한다.
7. 난수<richness이고 gap noise>-0.3이면 광석을 놓는다. 이 경우 추가 0.02 확률로 raw ore block을 고른다.
8. 광맥 모양에는 들어왔지만 광석 조건에 해당하지 않으면 granite/tuff 충전재를 쓴다.

FEATURES의 `OreFeature`는 배치 modifier로 뽑은 시작점에서 광상 모양을 만들고, 설정된 교체 대상 블록·공기 노출 조건 등을 검사한다. 광맥 크기와 분포, 광석 노출은 각 configured/placed feature 설정의 문제다.

## 13. NoiseChunk의 보간과 캐시

오버월드 청크 하나는 XZ 각각 4개 cell, 높이 384/8=48개 cell이다. 모두를 균일하게 샘플하는 보간 함수 하나의 격자라면 경계까지 5×49×5=1,225개 표본이다. 최종 블록 수 16×384×16=98,304개보다 훨씬 작다. 이는 **모든 노이즈의 실제 호출 수**가 1,225라는 뜻이 아니다. 그래프에는 보간하지 않는 함수·조건 분기·별도 재질 계산도 있다.

`NoiseInterpolator`는 인접 X slice 두 장을 유지하고 Y→X→Z 순서로 cell 내부 값을 갱신한다. 다음 X cell로 이동할 때 slice를 교환한다. 최종 density만 대충 큰 격자에서 계산하는 일괄 옵션이 아니라 `interpolated` marker가 붙은 함수 위치에서 동작한다.

| marker/캐시 | 역할 |
| --- | --- |
| flat_cache | 주로 Y에 독립적인 기후·지형 함수를 XZ quart grid에 미리 저장 |
| cache_2d | 같은 XZ에서 Y가 달라져도 재사용 가능한 함수의 값 보관 |
| cache_once | 현재 샘플/배열 평가 식별자에서 동일 함수의 중복 계산 방지 |
| cache_all_in_cell | 현재 cell 내부의 블록별 값을 보관 |
| interpolated | cell 모서리 표본을 cell 내부에서 삼선형 보간 |

NoiseChunk.wrap은 레지스트리의 marker를 해당 청크 상태에 연결된 구현으로 바꾼다. `RandomState`가 기후용 sampler를 만들 때 marker/holder를 펼치는 경로와 구분한다. 캐시를 전역 공유하거나 Y 의존 함수에 cache_2d를 무작정 적용하면 잘못된 결과가 된다.

## 14. 블록 배열에 실제로 기록하는 순서

`fillFromNoise`는 NoiseSettings를 대상 높이 범위에 맞춰 clamp하고 처리할 section을 확보한다. 실제 `doFill`은 NoiseChunk를 준비하고 WORLD_SURFACE_WG와 OCEAN_FLOOR_WG를 가져온다. 대수층과 광맥의 판정은 그 NoiseChunk 안 재질 규칙에 연결된다.

```text
첫 X 보간 slice를 준비
각 cellX에 대해:
    다음 X slice를 계산
    각 cellZ에 대해:
        cellY를 위에서 아래로 순회:
            cell의 8개 모서리와 캐시를 준비
            yInCell을 위에서 아래로 순회:
                Y 보간 값을 갱신
                각 xInCell:
                    X 보간 값을 갱신
                    각 zInCell:
                        Z 보간 값과 현재 전역 좌표 갱신
                        state = NoiseChunk.getInterpolatedState()
                        state가 null이면 defaultBlock(일반 오버월드 stone)
                        air가 아니면 section에 기록하고 두 WG 높이맵 갱신
                        유체이고 Aquifer가 후처리를 요구하면 위치를 표시
    두 X slice를 교환
보간 상태를 종료하고 청크 반환
```

여기서 `getInterpolatedState`라는 이름만 보고 모든 재질 결정이 보간된 값 하나로 끝난다고 해석하지 않는다. 최종 밀도에 구조물 보정을 더한 결과를 Aquifer에 전달하고, 고체 후보에는 대형 광맥 함수를 평가하는 재질 규칙이 연결되어 있다. 재질 규칙이 구체적 state를 처음 반환하면 뒤 규칙을 더 적용하지 않는다.

NOISE 완료 뒤 높이맵이 있다는 것과 청크가 플레이 가능한 FULL 상태라는 것은 다르다. 표면·carver·장식·조명·초기 몹·LevelChunk 전환이 남아 있다. WG 높이맵도 표면이 읽는 중간 결과이며 완성 월드의 모든 최종 높이맵과 같은 자료가 아니다.

## 15. '돌·물만' 구현할 때의 바닐라와의 구분

이 문서는 게임을 수정하지 않고 원본을 설명한다. 다음 표는 구현 범위를 정할 때 구분할 기준이다.

| 항목 | 바닐라 NOISE의 동작 | stone/water/air만 쓰는 단순화 시 의미 |
| --- | --- | --- |
| 용암 | globalFluidPicker와 지역 lava noise 모두 존재 | 유체 종류 정책을 별도로 바꾸어야 하며 원본과 다름 |
| 대형 광맥 | 밀도 이후 고체 후보에 적용 | OreVeinifier를 제외하면 해당 위치는 보통 default stone으로 남음 |
| 노이즈 동굴 | 초기 density graph 안에 이미 포함 | 제외하면 초기 형상 자체가 달라짐 |
| Carver 동굴 | SURFACE 다음 CARVERS 단계 | 이번 초기 지형 범위 밖이라 아직 처리하지 않은 상태 |
| 기반암·일반 심층암 | SURFACE 규칙 | 이번 단계에는 아직 일반 지층 전환으로 넣지 않음 |
| Beardifier | 구조물 시작 자료가 있으면 밀도에 가산 | 구조물 없는 독립 지형 연구에서는 0인 조건을 명시 |
| Blender | 구형 청크 주변에 보정 | 새 월드만 연구한다면 비접합 조건을 명시 |

단순화한다고 seaLevel 아래 모든 빈 공간에 무조건 물을 넣으면 Aquifer의 지역 건조 동굴·별도 수위·경계벽이 사라진다. 사용자가 실제 구현을 요청할 때는 이 차이를 선택해야 한다. 이 문서의 수식·부록은 수정된 단순화 결과가 아니라 원본이다.

## 16. 다음 단계로 넘기는 자료

표면 처리에는 초기 블록, WG 높이맵, biome 조회, RandomState의 표면 노이즈, NoiseChunk의 예비 지표가 필요하다. 스플라인 전체를 표면 규칙마다 재계산하는 인터페이스는 아니다. 깊이·수위·Y·바이옴 조건으로 이미 존재하는 default stone을 다른 재질로 바꾸며, 악지 돌기·빙산처럼 특수 형상 추가도 한다.

자세한 실행 순서와 조건식은 [오버월드 표면 처리 문서](minecraft-overworld-surface.md)에서 이어진다. 초기 지형과 표면의 경계를 코드에서는 `generateNoise`와 `generateSurface`, `fillFromNoise`와 `buildSurface`로 추적하면 된다.

<!-- BEGIN OVERWORLD BASE DATA -->

## 부록 A. 오버월드 지형 스플라인 9개 완전 전개

일반·대형 바이옴·증폭을 모두 포함한다. 반복 가지도 생략하지 않는다. `x`는 입력 축의 제어점 위치, `derivative`는 해당 축에 대한 기울기이며, `root/p번호`는 중첩 경로다. JSON 숫자는 Decimal로 읽어 자릿수를 보존한다.

| 루트 | 중첩 spline 노드 | 제어점 | 상수 잎 |
| --- | ---: | ---: | ---: |
| `overworld/offset` | 53 | 253 | 201 |
| `overworld/factor` | 49 | 134 | 86 |
| `overworld/jaggedness` | 16 | 43 | 28 |
| `overworld_large_biomes/offset` | 53 | 253 | 201 |
| `overworld_large_biomes/factor` | 49 | 134 | 86 |
| `overworld_large_biomes/jaggedness` | 16 | 43 | 28 |
| `overworld_amplified/offset` | 53 | 253 | 201 |
| `overworld_amplified/factor` | 49 | 134 | 86 |
| `overworld_amplified/jaggedness` | 16 | 43 | 28 |
| 합계(반복 포함) | 354 | 1290 | 945 |

### A1. overworld/offset

```text
root: axis=minecraft:overworld/continents
  p0: x=-1.1; derivative=0.0; value=0.044
  p1: x=-1.02; derivative=0.0; value=-0.2222
  p2: x=-0.51; derivative=0.0; value=-0.2222
  p3: x=-0.44; derivative=0.0; value=-0.12
  p4: x=-0.18; derivative=0.0; value=-0.12
  p5: x=-0.16; derivative=0.0; value=Spline
    root/p5: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p5/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p5/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p5/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p5/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p5/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p5/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p5/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p6: x=-0.15; derivative=0.0; value=Spline
    root/p6: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p6/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p6/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p6/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p6/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p6/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p6/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p6/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p7: x=-0.1; derivative=0.0; value=Spline
    root/p7: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p7/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p7/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p7/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p7/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p7/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.01; value=0.001
          p2: x=0.0; derivative=0.01; value=0.003
          p3: x=0.4; derivative=0.094000004; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p7/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.7; derivative=0.0; value=Spline
        root/p7/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
  p8: x=0.25; derivative=0.0; value=Spline
    root/p8: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p8/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.20235021
          p1: x=0.0; derivative=0.5138249; value=0.7161751
          p2: x=1.0; derivative=0.5138249; value=1.23
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p8/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.43317974; value=0.44682026
          p2: x=1.0; derivative=0.43317974; value=0.88
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p8/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.3917051; value=0.30829495
          p2: x=1.0; derivative=0.3917051; value=0.70000005
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p8/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.35
          p2: x=0.0; derivative=0.0; value=0.35
          p3: x=0.4; derivative=0.0; value=0.35
          p4: x=1.0; derivative=0.049000014; value=0.42000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p8/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.07; value=0.0069999998
          p2: x=0.0; derivative=0.07; value=0.021
          p3: x=0.4; derivative=0.658; value=0.35
          p4: x=1.0; derivative=0.049000014; value=0.42000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p8/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.4; derivative=0.0; value=Spline
        root/p8/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p7: x=0.45; derivative=0.0; value=Spline
        root/p8/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p8: x=0.55; derivative=0.0; value=Spline
        root/p8/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p9: x=0.58; derivative=0.0; value=Spline
        root/p8/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p10: x=0.7; derivative=0.0; value=Spline
        root/p8/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
  p9: x=1.0; derivative=0.0; value=Spline
    root/p9: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p9/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.34792626
          p1: x=0.0; derivative=0.5760369; value=0.9239631
          p2: x=1.0; derivative=0.5760369; value=1.5
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p9/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.4608295; value=0.5391705
          p2: x=1.0; derivative=0.4608295; value=1.0
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p9/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.4608295; value=0.5391705
          p2: x=1.0; derivative=0.4608295; value=1.0
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p9/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.2
          p1: x=-0.4; derivative=0.0; value=0.5
          p2: x=0.0; derivative=0.0; value=0.5
          p3: x=0.4; derivative=0.0; value=0.5
          p4: x=1.0; derivative=0.070000015; value=0.6
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p9/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.099999994; value=0.01
          p2: x=0.0; derivative=0.099999994; value=0.03
          p3: x=0.4; derivative=0.94; value=0.5
          p4: x=1.0; derivative=0.070000015; value=0.6
      p5: x=0.2; derivative=0.0; value=Spline
        root/p9/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.4; derivative=0.0; value=Spline
        root/p9/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p7: x=0.45; derivative=0.0; value=Spline
        root/p9/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p8: x=0.55; derivative=0.0; value=Spline
        root/p9/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p9: x=0.58; derivative=0.0; value=Spline
        root/p9/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p10: x=0.7; derivative=0.0; value=Spline
        root/p9/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.015; value=-0.02
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
```

### A2. overworld/factor

```text
root: axis=minecraft:overworld/continents
  p0: x=-0.19; derivative=0.0; value=3.95
  p1: x=-0.15; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p1/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p1/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p1/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p6: x=0.35; derivative=0.0; value=6.25
      p7: x=0.45; derivative=0.0; value=Spline
        root/p1/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p1/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=6.25
  p2: x=-0.1; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p2/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p2/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p2/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p6: x=0.35; derivative=0.0; value=5.47
      p7: x=0.45; derivative=0.0; value=Spline
        root/p2/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.47
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.47
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p2/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.47
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.47
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=5.47
  p3: x=0.03; derivative=0.0; value=Spline
    root/p3: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p3/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p3/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p3/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p3/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p3/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p3/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p6: x=0.35; derivative=0.0; value=5.08
      p7: x=0.45; derivative=0.0; value=Spline
        root/p3/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.08
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.08
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p3/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.08
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.08
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=5.08
  p4: x=0.06; derivative=0.0; value=Spline
    root/p4: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p4/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p4/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p4/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p4/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p4/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p4/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p6: x=0.05; derivative=0.0; value=Spline
        root/p4/p6: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p6/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=0.7; derivative=0.0; value=1.56
      p7: x=0.4; derivative=0.0; value=Spline
        root/p4/p7: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p7/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=0.7; derivative=0.0; value=1.56
      p8: x=0.45; derivative=0.0; value=Spline
        root/p4/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p8/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=-0.15; derivative=0.0; value=1.37
      p9: x=0.55; derivative=0.0; value=Spline
        root/p4/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p9/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=-0.15; derivative=0.0; value=1.37
      p10: x=0.58; derivative=0.0; value=4.69
```

### A3. overworld/jaggedness

```text
root: axis=minecraft:overworld/continents
  p0: x=-0.11; derivative=0.0; value=0.0
  p1: x=0.03; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.315
              p1: x=0.01; derivative=0.0; value=0.15
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.315
              p1: x=0.01; derivative=0.0; value=0.15
      p3: x=-0.375; derivative=0.0; value=0.0
  p2: x=0.65; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=Spline
            root/p2/p0/p1: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p3: x=-0.375; derivative=0.0; value=0.0
```

### A4. overworld_large_biomes/offset

```text
root: axis=minecraft:overworld_large_biomes/continents
  p0: x=-1.1; derivative=0.0; value=0.044
  p1: x=-1.02; derivative=0.0; value=-0.2222
  p2: x=-0.51; derivative=0.0; value=-0.2222
  p3: x=-0.44; derivative=0.0; value=-0.12
  p4: x=-0.18; derivative=0.0; value=-0.12
  p5: x=-0.16; derivative=0.0; value=Spline
    root/p5: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p5/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p5/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p5/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p5/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p5/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p5/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p5/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p6: x=-0.15; derivative=0.0; value=Spline
    root/p6: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p6/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p6/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p6/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p6/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p6/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p6/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p6/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p7: x=-0.1; derivative=0.0; value=Spline
    root/p7: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p7/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=0.69000006
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p7/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=0.6400001
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p7/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=2.9802322E-8
          p4: x=0.6054547; derivative=0.2534563; value=2.9802322E-8
          p5: x=1.0; derivative=0.2534563; value=0.100000024
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p7/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.05
          p2: x=0.0; derivative=0.0; value=0.05
          p3: x=0.4; derivative=0.0; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p7/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.01; value=0.001
          p2: x=0.0; derivative=0.01; value=0.003
          p3: x=0.4; derivative=0.094000004; value=0.05
          p4: x=1.0; derivative=0.007000001; value=0.060000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p7/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.7; derivative=0.0; value=Spline
        root/p7/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
  p8: x=0.25; derivative=0.0; value=Spline
    root/p8: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p8/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.20235021
          p1: x=0.0; derivative=0.5138249; value=0.7161751
          p2: x=1.0; derivative=0.5138249; value=1.23
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p8/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.43317974; value=0.44682026
          p2: x=1.0; derivative=0.43317974; value=0.88
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p8/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.3917051; value=0.30829495
          p2: x=1.0; derivative=0.3917051; value=0.70000005
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p8/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.35
          p2: x=0.0; derivative=0.0; value=0.35
          p3: x=0.4; derivative=0.0; value=0.35
          p4: x=1.0; derivative=0.049000014; value=0.42000002
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p8/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.07; value=0.0069999998
          p2: x=0.0; derivative=0.07; value=0.021
          p3: x=0.4; derivative=0.658; value=0.35
          p4: x=1.0; derivative=0.049000014; value=0.42000002
      p5: x=0.2; derivative=0.0; value=Spline
        root/p8/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.4; derivative=0.0; value=Spline
        root/p8/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p7: x=0.45; derivative=0.0; value=Spline
        root/p8/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p8: x=0.55; derivative=0.0; value=Spline
        root/p8/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p9: x=0.58; derivative=0.0; value=Spline
        root/p8/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p10: x=0.7; derivative=0.0; value=Spline
        root/p8/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
  p9: x=1.0; derivative=0.0; value=Spline
    root/p9: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p9/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.34792626
          p1: x=0.0; derivative=0.5760369; value=0.9239631
          p2: x=1.0; derivative=0.5760369; value=1.5
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p9/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.4608295; value=0.5391705
          p2: x=1.0; derivative=0.4608295; value=1.0
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p9/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.2
          p1: x=0.0; derivative=0.4608295; value=0.5391705
          p2: x=1.0; derivative=0.4608295; value=1.0
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p9/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.2
          p1: x=-0.4; derivative=0.0; value=0.5
          p2: x=0.0; derivative=0.0; value=0.5
          p3: x=0.4; derivative=0.0; value=0.5
          p4: x=1.0; derivative=0.070000015; value=0.6
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p9/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.099999994; value=0.01
          p2: x=0.0; derivative=0.099999994; value=0.03
          p3: x=0.4; derivative=0.94; value=0.5
          p4: x=1.0; derivative=0.070000015; value=0.6
      p5: x=0.2; derivative=0.0; value=Spline
        root/p9/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p6: x=0.4; derivative=0.0; value=Spline
        root/p9/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p7: x=0.45; derivative=0.0; value=Spline
        root/p9/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p8: x=0.55; derivative=0.0; value=Spline
        root/p9/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.01
              p2: x=0.0; derivative=0.0; value=0.01
              p3: x=0.4; derivative=0.04; value=0.03
              p4: x=1.0; derivative=0.049; value=0.1
          p2: x=0.0; derivative=0.0; value=0.17
      p9: x=0.58; derivative=0.0; value=Spline
        root/p9/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
      p10: x=0.7; derivative=0.0; value=Spline
        root/p9/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.015; value=-0.02
          p1: x=-0.4; derivative=0.0; value=0.01
          p2: x=0.0; derivative=0.0; value=0.01
          p3: x=0.4; derivative=0.04; value=0.03
          p4: x=1.0; derivative=0.049; value=0.1
```

### A5. overworld_large_biomes/factor

```text
root: axis=minecraft:overworld_large_biomes/continents
  p0: x=-0.19; derivative=0.0; value=3.95
  p1: x=-0.15; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p1/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p1/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p1/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p6: x=0.35; derivative=0.0; value=6.25
      p7: x=0.45; derivative=0.0; value=Spline
        root/p1/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p1/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=6.25
  p2: x=-0.1; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p2/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p2/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p2/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.47
      p6: x=0.35; derivative=0.0; value=5.47
      p7: x=0.45; derivative=0.0; value=Spline
        root/p2/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.47
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.47
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p2/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.47
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.47
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=5.47
  p3: x=0.03; derivative=0.0; value=Spline
    root/p3: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p3/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p3/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p3/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p3/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p3/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p3/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=5.08
      p6: x=0.35; derivative=0.0; value=5.08
      p7: x=0.45; derivative=0.0; value=Spline
        root/p3/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.08
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.08
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p3/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=5.08
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=5.08
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=5.08
  p4: x=0.06; derivative=0.0; value=Spline
    root/p4: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p4/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p4/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p4/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p4/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p4/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p4/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=4.69
      p6: x=0.05; derivative=0.0; value=Spline
        root/p4/p6: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p6/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=0.7; derivative=0.0; value=1.56
      p7: x=0.4; derivative=0.0; value=Spline
        root/p4/p7: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p7/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=0.7; derivative=0.0; value=1.56
      p8: x=0.45; derivative=0.0; value=Spline
        root/p4/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p8/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=-0.15; derivative=0.0; value=1.37
      p9: x=0.55; derivative=0.0; value=Spline
        root/p4/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p9/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=6.3
              p1: x=0.2; derivative=0.0; value=4.69
          p1: x=-0.15; derivative=0.0; value=1.37
      p10: x=0.58; derivative=0.0; value=4.69
```

### A6. overworld_large_biomes/jaggedness

```text
root: axis=minecraft:overworld_large_biomes/continents
  p0: x=-0.11; derivative=0.0; value=0.0
  p1: x=0.03; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.315
              p1: x=0.01; derivative=0.0; value=0.15
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.315
              p1: x=0.01; derivative=0.0; value=0.15
      p3: x=-0.375; derivative=0.0; value=0.0
  p2: x=0.65; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld_large_biomes/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=Spline
            root/p2/p0/p1: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p3: x=-0.375; derivative=0.0; value=0.0
```

### A7. overworld_amplified/offset

```text
root: axis=minecraft:overworld/continents
  p0: x=-1.1; derivative=0.0; value=0.088
  p1: x=-1.02; derivative=0.0; value=-0.2222
  p2: x=-0.51; derivative=0.0; value=-0.2222
  p3: x=-0.44; derivative=0.0; value=-0.12
  p4: x=-0.18; derivative=0.0; value=-0.12
  p5: x=-0.16; derivative=0.0; value=Spline
    root/p5: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p5/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=1.3800001
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p5/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=1.2800002
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p5/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=5.9604645E-8
          p4: x=0.6054547; derivative=0.2534563; value=5.9604645E-8
          p5: x=1.0; derivative=0.2534563; value=0.20000005
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p5/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.1
          p2: x=0.0; derivative=0.0; value=0.1
          p3: x=0.4; derivative=0.0; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p5/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p5: x=0.2; derivative=0.0; value=Spline
        root/p5/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p5/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p6: x=-0.15; derivative=0.0; value=Spline
    root/p6: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p6/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=1.3800001
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p6/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=1.2800002
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p6/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=5.9604645E-8
          p4: x=0.6054547; derivative=0.2534563; value=5.9604645E-8
          p5: x=1.0; derivative=0.2534563; value=0.20000005
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p6/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.3
          p1: x=-0.4; derivative=0.0; value=0.1
          p2: x=0.0; derivative=0.0; value=0.1
          p3: x=0.4; derivative=0.0; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p6/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.1; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p5: x=0.2; derivative=0.0; value=Spline
        root/p6/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.15
          p1: x=-0.4; derivative=0.0; value=0.0
          p2: x=0.0; derivative=0.0; value=0.0
          p3: x=0.4; derivative=0.0; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
      p6: x=0.7; derivative=0.0; value=Spline
        root/p6/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.06; value=0.0
          p4: x=1.0; derivative=0.0; value=0.0
  p7: x=-0.1; derivative=0.0; value=Spline
    root/p7: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p7/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.38940096; value=-0.08880186
          p1: x=1.0; derivative=0.38940096; value=1.3800001
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p7/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.37788022; value=-0.115760356
          p1: x=1.0; derivative=0.37788022; value=1.2800002
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p7/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.2222
          p1: x=-0.75; derivative=0.0; value=-0.2222
          p2: x=-0.65; derivative=0.0; value=0.0
          p3: x=0.5954547; derivative=0.0; value=5.9604645E-8
          p4: x=0.6054547; derivative=0.2534563; value=5.9604645E-8
          p5: x=1.0; derivative=0.2534563; value=0.20000005
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p7/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.1
          p2: x=0.0; derivative=0.0; value=0.1
          p3: x=0.4; derivative=0.0; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p7/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.01; value=0.002
          p2: x=0.0; derivative=0.01; value=0.006
          p3: x=0.4; derivative=0.094000004; value=0.1
          p4: x=1.0; derivative=0.007000001; value=0.120000005
      p5: x=0.2; derivative=0.0; value=Spline
        root/p7/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p6: x=0.7; derivative=0.0; value=Spline
        root/p7/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
  p8: x=0.25; derivative=0.0; value=Spline
    root/p8: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p8/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.40470043
          p1: x=0.0; derivative=0.5138249; value=1.4323502
          p2: x=1.0; derivative=0.5138249; value=2.46
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p8/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.4
          p1: x=0.0; derivative=0.43317974; value=0.8936405
          p2: x=1.0; derivative=0.43317974; value=1.76
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p8/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.4
          p1: x=0.0; derivative=0.3917051; value=0.6165899
          p2: x=1.0; derivative=0.3917051; value=1.4000001
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p8/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.25
          p1: x=-0.4; derivative=0.0; value=0.7
          p2: x=0.0; derivative=0.0; value=0.7
          p3: x=0.4; derivative=0.0; value=0.7
          p4: x=1.0; derivative=0.049000014; value=0.84000003
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p8/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.07; value=0.0139999995
          p2: x=0.0; derivative=0.07; value=0.042
          p3: x=0.4; derivative=0.658; value=0.7
          p4: x=1.0; derivative=0.049000014; value=0.84000003
      p5: x=0.2; derivative=0.0; value=Spline
        root/p8/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p6: x=0.4; derivative=0.0; value=Spline
        root/p8/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p7: x=0.45; derivative=0.0; value=Spline
        root/p8/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.02
              p2: x=0.0; derivative=0.0; value=0.02
              p3: x=0.4; derivative=0.04; value=0.06
              p4: x=1.0; derivative=0.049; value=0.2
          p2: x=0.0; derivative=0.0; value=0.34
      p8: x=0.55; derivative=0.0; value=Spline
        root/p8/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.1
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p8/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.1
              p1: x=-0.4; derivative=0.0; value=0.02
              p2: x=0.0; derivative=0.0; value=0.02
              p3: x=0.4; derivative=0.04; value=0.06
              p4: x=1.0; derivative=0.049; value=0.2
          p2: x=0.0; derivative=0.0; value=0.34
      p9: x=0.58; derivative=0.0; value=Spline
        root/p8/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.1
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p10: x=0.7; derivative=0.0; value=Spline
        root/p8/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.02
          p1: x=-0.4; derivative=0.0; value=-0.03
          p2: x=0.0; derivative=0.0; value=-0.03
          p3: x=0.4; derivative=0.12; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
  p9: x=1.0; derivative=0.0; value=Spline
    root/p9: axis=minecraft:overworld/erosion
      p0: x=-0.85; derivative=0.0; value=Spline
        root/p9/p0: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.6958525
          p1: x=0.0; derivative=0.5760369; value=1.8479263
          p2: x=1.0; derivative=0.5760369; value=3.0
      p1: x=-0.7; derivative=0.0; value=Spline
        root/p9/p1: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.4
          p1: x=0.0; derivative=0.4608295; value=1.078341
          p2: x=1.0; derivative=0.4608295; value=2.0
      p2: x=-0.4; derivative=0.0; value=Spline
        root/p9/p2: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=0.4
          p1: x=0.0; derivative=0.4608295; value=1.078341
          p2: x=1.0; derivative=0.4608295; value=2.0
      p3: x=-0.35; derivative=0.0; value=Spline
        root/p9/p3: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.2
          p1: x=-0.4; derivative=0.0; value=1.0
          p2: x=0.0; derivative=0.0; value=1.0
          p3: x=0.4; derivative=0.0; value=1.0
          p4: x=1.0; derivative=0.070000015; value=1.2
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p9/p4: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.099999994; value=0.02
          p2: x=0.0; derivative=0.099999994; value=0.06
          p3: x=0.4; derivative=0.94; value=1.0
          p4: x=1.0; derivative=0.070000015; value=1.2
      p5: x=0.2; derivative=0.0; value=Spline
        root/p9/p5: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p6: x=0.4; derivative=0.0; value=Spline
        root/p9/p6: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p7: x=0.45; derivative=0.0; value=Spline
        root/p9/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p7/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.02
              p2: x=0.0; derivative=0.0; value=0.02
              p3: x=0.4; derivative=0.04; value=0.06
              p4: x=1.0; derivative=0.049; value=0.2
          p2: x=0.0; derivative=0.0; value=0.34
      p8: x=0.55; derivative=0.0; value=Spline
        root/p9/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.0; value=-0.05
          p1: x=-0.4; derivative=0.0; value=Spline
            root/p9/p8/p1: axis=minecraft:overworld/ridges_folded
              p0: x=-1.0; derivative=0.5; value=-0.05
              p1: x=-0.4; derivative=0.0; value=0.02
              p2: x=0.0; derivative=0.0; value=0.02
              p3: x=0.4; derivative=0.04; value=0.06
              p4: x=1.0; derivative=0.049; value=0.2
          p2: x=0.0; derivative=0.0; value=0.34
      p9: x=0.58; derivative=0.0; value=Spline
        root/p9/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.5; value=-0.05
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
      p10: x=0.7; derivative=0.0; value=Spline
        root/p9/p10: axis=minecraft:overworld/ridges_folded
          p0: x=-1.0; derivative=0.015; value=-0.02
          p1: x=-0.4; derivative=0.0; value=0.02
          p2: x=0.0; derivative=0.0; value=0.02
          p3: x=0.4; derivative=0.04; value=0.06
          p4: x=1.0; derivative=0.049; value=0.2
```

### A8. overworld_amplified/factor

```text
root: axis=minecraft:overworld/continents
  p0: x=-0.19; derivative=0.0; value=3.95
  p1: x=-0.15; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=6.3
          p1: x=0.05; derivative=0.0; value=2.67
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p1/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p1/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=2.67
          p1: x=0.05; derivative=0.0; value=6.3
      p5: x=0.03; derivative=0.0; value=Spline
        root/p1/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=6.3
          p1: x=0.2; derivative=0.0; value=6.25
      p6: x=0.35; derivative=0.0; value=6.25
      p7: x=0.45; derivative=0.0; value=Spline
        root/p1/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p8: x=0.55; derivative=0.0; value=Spline
        root/p1/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=6.25
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p1/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=6.25
              p1: x=0.1; derivative=0.0; value=0.625
      p9: x=0.62; derivative=0.0; value=6.25
  p2: x=-0.1; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6530563
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.6969027
          p1: x=0.05; derivative=0.0; value=0.4351369
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6530563
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p2/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6530563
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p2/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.4351369
          p1: x=0.05; derivative=0.0; value=0.6969027
      p5: x=0.03; derivative=0.0; value=Spline
        root/p2/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6530563
      p6: x=0.35; derivative=0.0; value=0.6530563
      p7: x=0.45; derivative=0.0; value=Spline
        root/p2/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=0.6530563
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=0.6530563
              p1: x=0.1; derivative=0.0; value=0.13888884
      p8: x=0.55; derivative=0.0; value=Spline
        root/p2/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=0.6530563
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p2/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=0.6530563
              p1: x=0.1; derivative=0.0; value=0.13888884
      p9: x=0.62; derivative=0.0; value=0.6530563
  p3: x=0.03; derivative=0.0; value=Spline
    root/p3: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p3/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6299603
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p3/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.6969027
          p1: x=0.05; derivative=0.0; value=0.4351369
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p3/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6299603
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p3/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6299603
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p3/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.4351369
          p1: x=0.05; derivative=0.0; value=0.6969027
      p5: x=0.03; derivative=0.0; value=Spline
        root/p3/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6299603
      p6: x=0.35; derivative=0.0; value=0.6299603
      p7: x=0.45; derivative=0.0; value=Spline
        root/p3/p7: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=0.6299603
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p7/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=0.6299603
              p1: x=0.1; derivative=0.0; value=0.13888884
      p8: x=0.55; derivative=0.0; value=Spline
        root/p3/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.9; derivative=0.0; value=0.6299603
          p1: x=-0.69; derivative=0.0; value=Spline
            root/p3/p8/p1: axis=minecraft:overworld/ridges
              p0: x=0.0; derivative=0.0; value=0.6299603
              p1: x=0.1; derivative=0.0; value=0.13888884
      p9: x=0.62; derivative=0.0; value=0.6299603
  p4: x=0.06; derivative=0.0; value=Spline
    root/p4: axis=minecraft:overworld/erosion
      p0: x=-0.6; derivative=0.0; value=Spline
        root/p4/p0: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6050052
      p1: x=-0.5; derivative=0.0; value=Spline
        root/p4/p1: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.6969027
          p1: x=0.05; derivative=0.0; value=0.4351369
      p2: x=-0.35; derivative=0.0; value=Spline
        root/p4/p2: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6050052
      p3: x=-0.25; derivative=0.0; value=Spline
        root/p4/p3: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6050052
      p4: x=-0.1; derivative=0.0; value=Spline
        root/p4/p4: axis=minecraft:overworld/ridges
          p0: x=-0.05; derivative=0.0; value=0.4351369
          p1: x=0.05; derivative=0.0; value=0.6969027
      p5: x=0.03; derivative=0.0; value=Spline
        root/p4/p5: axis=minecraft:overworld/ridges
          p0: x=-0.2; derivative=0.0; value=0.6969027
          p1: x=0.2; derivative=0.0; value=0.6050052
      p6: x=0.05; derivative=0.0; value=Spline
        root/p4/p6: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p6/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=0.6969027
              p1: x=0.2; derivative=0.0; value=0.6050052
          p1: x=0.7; derivative=0.0; value=0.2972561
      p7: x=0.4; derivative=0.0; value=Spline
        root/p4/p7: axis=minecraft:overworld/ridges_folded
          p0: x=0.45; derivative=0.0; value=Spline
            root/p4/p7/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=0.6969027
              p1: x=0.2; derivative=0.0; value=0.6050052
          p1: x=0.7; derivative=0.0; value=0.2972561
      p8: x=0.45; derivative=0.0; value=Spline
        root/p4/p8: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p8/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=0.6969027
              p1: x=0.2; derivative=0.0; value=0.6050052
          p1: x=-0.15; derivative=0.0; value=0.2688383
      p9: x=0.55; derivative=0.0; value=Spline
        root/p4/p9: axis=minecraft:overworld/ridges_folded
          p0: x=-0.7; derivative=0.0; value=Spline
            root/p4/p9/p0: axis=minecraft:overworld/ridges
              p0: x=-0.2; derivative=0.0; value=0.6969027
              p1: x=0.2; derivative=0.0; value=0.6050052
          p1: x=-0.15; derivative=0.0; value=0.2688383
      p10: x=0.58; derivative=0.0; value=0.6050052
```

### A9. overworld_amplified/jaggedness

```text
root: axis=minecraft:overworld/continents
  p0: x=-0.11; derivative=0.0; value=0.0
  p1: x=0.03; derivative=0.0; value=Spline
    root/p1: axis=minecraft:overworld/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p1/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=1.26
              p1: x=0.01; derivative=0.0; value=0.6
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p1/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p1/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p1/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=0.63
              p1: x=0.01; derivative=0.0; value=0.3
      p3: x=-0.375; derivative=0.0; value=0.0
  p2: x=0.65; derivative=0.0; value=Spline
    root/p2: axis=minecraft:overworld/erosion
      p0: x=-1.0; derivative=0.0; value=Spline
        root/p2/p0: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=Spline
            root/p2/p0/p1: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=1.26
              p1: x=0.01; derivative=0.0; value=0.6
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p0/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=1.26
              p1: x=0.01; derivative=0.0; value=0.6
      p1: x=-0.78; derivative=0.0; value=Spline
        root/p2/p1: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p1/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=1.26
              p1: x=0.01; derivative=0.0; value=0.6
      p2: x=-0.5775; derivative=0.0; value=Spline
        root/p2/p2: axis=minecraft:overworld/ridges_folded
          p0: x=0.19999999; derivative=0.0; value=0.0
          p1: x=0.44999996; derivative=0.0; value=0.0
          p2: x=1.0; derivative=0.0; value=Spline
            root/p2/p2/p2: axis=minecraft:overworld/ridges
              p0: x=-0.01; derivative=0.0; value=1.26
              p1: x=0.01; derivative=0.0; value=0.6
      p3: x=-0.375; derivative=0.0; value=0.0
```

## 부록 B. 초기 지형이 실제 참조하는 밀도 함수 전체

세 오버월드 noise_router에서 등록 함수 참조를 끝까지 따라간 **31개**다. 이름 참조는 이 부록의 해당 정의로 연결되고 SPLINE은 부록 A의 완전 트리로 연결된다. 캐시·보간·연산 순서·상수 필드를 생략하지 않는다.

### B. overworld/base_3d_noise

```text
type: minecraft:old_blended_noise
smear_scale_multiplier: 8.0
xz_factor: 80.0
xz_scale: 0.25
y_factor: 160.0
y_scale: 0.125
```

### B. overworld/caves/entrances

```text
type: minecraft:cache_once
argument:
  type: minecraft:min
  argument1:
    type: minecraft:add
    argument1:
      type: minecraft:add
      argument1: 0.37
      argument2:
        type: minecraft:noise
        noise: minecraft:cave_entrance
        xz_scale: 0.75
        y_scale: 0.5
    argument2:
      type: minecraft:y_clamped_gradient
      from_value: 0.3
      from_y: -10
      to_value: 0.0
      to_y: 30
  argument2:
    type: minecraft:add
    argument1: minecraft:overworld/caves/spaghetti_roughness_function
    argument2:
      type: minecraft:clamp
      input:
        type: minecraft:add
        argument1:
          type: minecraft:max
          argument1:
            type: minecraft:abs
            argument:
              type: minecraft:interval_select
              functions:
                [0]:
                  type: minecraft:mul
                  argument1: 0.75
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_1
                    xz_scale: 1.3333333333333333
                    y_scale: 1.3333333333333333
                [1]:
                  type: minecraft:mul
                  argument1: 1.0
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_1
                    xz_scale: 1.0
                    y_scale: 1.0
                [2]:
                  type: minecraft:mul
                  argument1: 1.5
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_1
                    xz_scale: 0.6666666666666666
                    y_scale: 0.6666666666666666
                [3]:
                  type: minecraft:mul
                  argument1: 2.0
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_1
                    xz_scale: 0.5
                    y_scale: 0.5
              input:
                type: minecraft:cache_once
                argument:
                  type: minecraft:noise
                  noise: minecraft:spaghetti_3d_rarity
                  xz_scale: 2.0
                  y_scale: 1.0
              thresholds:
                [0]:
                  -0.5
                [1]:
                  0.0
                [2]:
                  0.5
          argument2:
            type: minecraft:abs
            argument:
              type: minecraft:interval_select
              functions:
                [0]:
                  type: minecraft:mul
                  argument1: 0.75
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_2
                    xz_scale: 1.3333333333333333
                    y_scale: 1.3333333333333333
                [1]:
                  type: minecraft:mul
                  argument1: 1.0
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_2
                    xz_scale: 1.0
                    y_scale: 1.0
                [2]:
                  type: minecraft:mul
                  argument1: 1.5
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_2
                    xz_scale: 0.6666666666666666
                    y_scale: 0.6666666666666666
                [3]:
                  type: minecraft:mul
                  argument1: 2.0
                  argument2:
                    type: minecraft:noise
                    noise: minecraft:spaghetti_3d_2
                    xz_scale: 0.5
                    y_scale: 0.5
              input:
                type: minecraft:cache_once
                argument:
                  type: minecraft:noise
                  noise: minecraft:spaghetti_3d_rarity
                  xz_scale: 2.0
                  y_scale: 1.0
              thresholds:
                [0]:
                  -0.5
                [1]:
                  0.0
                [2]:
                  0.5
        argument2:
          type: minecraft:add
          argument1: -0.0765
          argument2:
            type: minecraft:mul
            argument1: -0.011499999999999996
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_3d_thickness
              xz_scale: 1.0
              y_scale: 1.0
      max: 1.0
      min: -1.0
```

### B. overworld/caves/noodle

```text
type: minecraft:range_choice
input:
  type: minecraft:interpolated
  argument:
    type: minecraft:range_choice
    input: minecraft:y
    max_exclusive: 321.0
    min_inclusive: -60.0
    when_in_range:
      type: minecraft:noise
      noise: minecraft:noodle
      xz_scale: 1.0
      y_scale: 1.0
    when_out_of_range: -1.0
max_exclusive: 0.0
min_inclusive: -1000000.0
when_in_range: 64.0
when_out_of_range:
  type: minecraft:add
  argument1:
    type: minecraft:interpolated
    argument:
      type: minecraft:range_choice
      input: minecraft:y
      max_exclusive: 321.0
      min_inclusive: -60.0
      when_in_range:
        type: minecraft:add
        argument1: -0.07500000000000001
        argument2:
          type: minecraft:mul
          argument1: -0.025
          argument2:
            type: minecraft:noise
            noise: minecraft:noodle_thickness
            xz_scale: 1.0
            y_scale: 1.0
      when_out_of_range: 0.0
  argument2:
    type: minecraft:mul
    argument1: 1.5
    argument2:
      type: minecraft:max
      argument1:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 321.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:noodle_ridge_a
              xz_scale: 2.6666666666666665
              y_scale: 2.6666666666666665
            when_out_of_range: 0.0
      argument2:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 321.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:noodle_ridge_b
              xz_scale: 2.6666666666666665
              y_scale: 2.6666666666666665
            when_out_of_range: 0.0
```

### B. overworld/caves/pillars

```text
type: minecraft:cache_once
argument:
  type: minecraft:mul
  argument1:
    type: minecraft:add
    argument1:
      type: minecraft:mul
      argument1: 2.0
      argument2:
        type: minecraft:noise
        noise: minecraft:pillar
        xz_scale: 25.0
        y_scale: 0.3
    argument2:
      type: minecraft:add
      argument1: -1.0
      argument2:
        type: minecraft:mul
        argument1: -1.0
        argument2:
          type: minecraft:noise
          noise: minecraft:pillar_rareness
          xz_scale: 1.0
          y_scale: 1.0
  argument2:
    type: minecraft:cube
    argument:
      type: minecraft:add
      argument1: 0.55
      argument2:
        type: minecraft:mul
        argument1: 0.55
        argument2:
          type: minecraft:noise
          noise: minecraft:pillar_thickness
          xz_scale: 1.0
          y_scale: 1.0
```

### B. overworld/caves/spaghetti_2d

```text
type: minecraft:clamp
input:
  type: minecraft:max
  argument1:
    type: minecraft:add
    argument1:
      type: minecraft:abs
      argument:
        type: minecraft:interval_select
        functions:
          [0]:
            type: minecraft:mul
            argument1: 0.5
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_2d
              xz_scale: 2.0
              y_scale: 2.0
          [1]:
            type: minecraft:mul
            argument1: 0.75
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_2d
              xz_scale: 1.3333333333333333
              y_scale: 1.3333333333333333
          [2]:
            type: minecraft:mul
            argument1: 1.0
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_2d
              xz_scale: 1.0
              y_scale: 1.0
          [3]:
            type: minecraft:mul
            argument1: 2.0
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_2d
              xz_scale: 0.5
              y_scale: 0.5
          [4]:
            type: minecraft:mul
            argument1: 3.0
            argument2:
              type: minecraft:noise
              noise: minecraft:spaghetti_2d
              xz_scale: 0.3333333333333333
              y_scale: 0.3333333333333333
        input:
          type: minecraft:noise
          noise: minecraft:spaghetti_2d_modulator
          xz_scale: 2.0
          y_scale: 1.0
        thresholds:
          [0]:
            -0.75
          [1]:
            -0.5
          [2]:
            0.5
          [3]:
            0.75
    argument2:
      type: minecraft:mul
      argument1: 0.083
      argument2: minecraft:overworld/caves/spaghetti_2d_thickness_modulator
  argument2:
    type: minecraft:cube
    argument:
      type: minecraft:add
      argument1:
        type: minecraft:abs
        argument:
          type: minecraft:add
          argument1:
            type: minecraft:flat_cache
            argument:
              type: minecraft:add
              argument1: 0.0
              argument2:
                type: minecraft:mul
                argument1: 8.0
                argument2:
                  type: minecraft:noise
                  noise: minecraft:spaghetti_2d_elevation
                  xz_scale: 1.0
                  y_scale: 0.0
          argument2:
            type: minecraft:y_clamped_gradient
            from_value: 8.0
            from_y: -64
            to_value: -40.0
            to_y: 320
      argument2: minecraft:overworld/caves/spaghetti_2d_thickness_modulator
max: 1.0
min: -1.0
```

### B. overworld/caves/spaghetti_2d_thickness_modulator

```text
type: minecraft:cache_once
argument:
  type: minecraft:add
  argument1: -0.95
  argument2:
    type: minecraft:mul
    argument1: -0.35000000000000003
    argument2:
      type: minecraft:noise
      noise: minecraft:spaghetti_2d_thickness
      xz_scale: 2.0
      y_scale: 1.0
```

### B. overworld/caves/spaghetti_roughness_function

```text
type: minecraft:cache_once
argument:
  type: minecraft:mul
  argument1:
    type: minecraft:add
    argument1: -0.05
    argument2:
      type: minecraft:mul
      argument1: -0.05
      argument2:
        type: minecraft:noise
        noise: minecraft:spaghetti_roughness_modulator
        xz_scale: 1.0
        y_scale: 1.0
  argument2:
    type: minecraft:add
    argument1: -0.4
    argument2:
      type: minecraft:abs
      argument:
        type: minecraft:noise
        noise: minecraft:spaghetti_roughness
        xz_scale: 1.0
        y_scale: 1.0
```

### B. overworld/continents

```text
type: minecraft:flat_cache
argument:
  type: minecraft:shifted_noise
  noise: minecraft:continentalness
  shift_x: minecraft:shift_x
  shift_y: 0.0
  shift_z: minecraft:shift_z
  xz_scale: 0.25
  y_scale: 0.0
```

### B. overworld/depth

```text
type: minecraft:add
argument1:
  type: minecraft:y_clamped_gradient
  from_value: 1.5
  from_y: -64
  to_value: -1.5
  to_y: 320
argument2: minecraft:overworld/offset
```

### B. overworld/erosion

```text
type: minecraft:flat_cache
argument:
  type: minecraft:shifted_noise
  noise: minecraft:erosion
  shift_x: minecraft:shift_x
  shift_y: 0.0
  shift_z: minecraft:shift_z
  xz_scale: 0.25
  y_scale: 0.0
```

### B. overworld/factor

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 10.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -10.0
        argument2:
          SPLINE[overworld/factor] (full tree in Appendix A)
```

### B. overworld/jaggedness

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 0.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -0.0
        argument2:
          SPLINE[overworld/jaggedness] (full tree in Appendix A)
```

### B. overworld/offset

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_offset
      argument2:
        type: minecraft:add
        argument1: 1.0
        argument2:
          type: minecraft:mul
          argument1: -1.0
          argument2:
            type: minecraft:cache_once
            argument:
              type: minecraft:blend_alpha
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: -0.5037500262260437
        argument2:
          SPLINE[overworld/offset] (full tree in Appendix A)
      argument2:
        type: minecraft:cache_once
        argument:
          type: minecraft:blend_alpha
```

### B. overworld/ridges

```text
type: minecraft:flat_cache
argument:
  type: minecraft:shifted_noise
  noise: minecraft:ridge
  shift_x: minecraft:shift_x
  shift_y: 0.0
  shift_z: minecraft:shift_z
  xz_scale: 0.25
  y_scale: 0.0
```

### B. overworld/ridges_folded

```text
type: minecraft:mul
argument1: -3.0
argument2:
  type: minecraft:add
  argument1: -0.3333333333333333
  argument2:
    type: minecraft:abs
    argument:
      type: minecraft:add
      argument1: -0.6666666666666666
      argument2:
        type: minecraft:abs
        argument: minecraft:overworld/ridges
```

### B. overworld/sloped_cheese

```text
type: minecraft:add
argument1:
  type: minecraft:mul
  argument1: 4.0
  argument2:
    type: minecraft:quarter_negative
    argument:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: minecraft:overworld/depth
        argument2:
          type: minecraft:flat_cache
          argument:
            type: minecraft:mul
            argument1: minecraft:overworld/jaggedness
            argument2:
              type: minecraft:half_negative
              argument:
                type: minecraft:noise
                noise: minecraft:jagged
                xz_scale: 1500.0
                y_scale: 0.0
      argument2: minecraft:overworld/factor
argument2: minecraft:overworld/base_3d_noise
```

### B. overworld_amplified/depth

```text
type: minecraft:add
argument1:
  type: minecraft:y_clamped_gradient
  from_value: 1.5
  from_y: -64
  to_value: -1.5
  to_y: 320
argument2: minecraft:overworld_amplified/offset
```

### B. overworld_amplified/factor

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 10.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -10.0
        argument2:
          SPLINE[overworld_amplified/factor] (full tree in Appendix A)
```

### B. overworld_amplified/jaggedness

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 0.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -0.0
        argument2:
          SPLINE[overworld_amplified/jaggedness] (full tree in Appendix A)
```

### B. overworld_amplified/offset

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_offset
      argument2:
        type: minecraft:add
        argument1: 1.0
        argument2:
          type: minecraft:mul
          argument1: -1.0
          argument2:
            type: minecraft:cache_once
            argument:
              type: minecraft:blend_alpha
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: -0.5037500262260437
        argument2:
          SPLINE[overworld_amplified/offset] (full tree in Appendix A)
      argument2:
        type: minecraft:cache_once
        argument:
          type: minecraft:blend_alpha
```

### B. overworld_amplified/sloped_cheese

```text
type: minecraft:add
argument1:
  type: minecraft:mul
  argument1: 4.0
  argument2:
    type: minecraft:quarter_negative
    argument:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: minecraft:overworld_amplified/depth
        argument2:
          type: minecraft:flat_cache
          argument:
            type: minecraft:mul
            argument1: minecraft:overworld_amplified/jaggedness
            argument2:
              type: minecraft:half_negative
              argument:
                type: minecraft:noise
                noise: minecraft:jagged
                xz_scale: 1500.0
                y_scale: 0.0
      argument2: minecraft:overworld_amplified/factor
argument2: minecraft:overworld/base_3d_noise
```

### B. overworld_large_biomes/continents

```text
type: minecraft:flat_cache
argument:
  type: minecraft:shifted_noise
  noise: minecraft:continentalness_large
  shift_x: minecraft:shift_x
  shift_y: 0.0
  shift_z: minecraft:shift_z
  xz_scale: 0.25
  y_scale: 0.0
```

### B. overworld_large_biomes/depth

```text
type: minecraft:add
argument1:
  type: minecraft:y_clamped_gradient
  from_value: 1.5
  from_y: -64
  to_value: -1.5
  to_y: 320
argument2: minecraft:overworld_large_biomes/offset
```

### B. overworld_large_biomes/erosion

```text
type: minecraft:flat_cache
argument:
  type: minecraft:shifted_noise
  noise: minecraft:erosion_large
  shift_x: minecraft:shift_x
  shift_y: 0.0
  shift_z: minecraft:shift_z
  xz_scale: 0.25
  y_scale: 0.0
```

### B. overworld_large_biomes/factor

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 10.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -10.0
        argument2:
          SPLINE[overworld_large_biomes/factor] (full tree in Appendix A)
```

### B. overworld_large_biomes/jaggedness

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1: 0.0
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_alpha
      argument2:
        type: minecraft:add
        argument1: -0.0
        argument2:
          SPLINE[overworld_large_biomes/jaggedness] (full tree in Appendix A)
```

### B. overworld_large_biomes/offset

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:add
    argument1:
      type: minecraft:mul
      argument1:
        type: minecraft:blend_offset
      argument2:
        type: minecraft:add
        argument1: 1.0
        argument2:
          type: minecraft:mul
          argument1: -1.0
          argument2:
            type: minecraft:cache_once
            argument:
              type: minecraft:blend_alpha
    argument2:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: -0.5037500262260437
        argument2:
          SPLINE[overworld_large_biomes/offset] (full tree in Appendix A)
      argument2:
        type: minecraft:cache_once
        argument:
          type: minecraft:blend_alpha
```

### B. overworld_large_biomes/sloped_cheese

```text
type: minecraft:add
argument1:
  type: minecraft:mul
  argument1: 4.0
  argument2:
    type: minecraft:quarter_negative
    argument:
      type: minecraft:mul
      argument1:
        type: minecraft:add
        argument1: minecraft:overworld_large_biomes/depth
        argument2:
          type: minecraft:flat_cache
          argument:
            type: minecraft:mul
            argument1: minecraft:overworld_large_biomes/jaggedness
            argument2:
              type: minecraft:half_negative
              argument:
                type: minecraft:noise
                noise: minecraft:jagged
                xz_scale: 1500.0
                y_scale: 0.0
      argument2: minecraft:overworld_large_biomes/factor
argument2: minecraft:overworld/base_3d_noise
```

### B. shift_x

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:shift_a
    argument: minecraft:offset
```

### B. shift_z

```text
type: minecraft:flat_cache
argument:
  type: minecraft:cache_2d
  argument:
    type: minecraft:shift_b
    argument: minecraft:offset
```

### B. y

```text
type: minecraft:y_clamped_gradient
from_value: -4064.0
from_y: -4064
to_value: 4062.0
to_y: 4062
```

## 부록 C. 오버월드 3개 프리셋의 초기 생성 설정

surface_rule은 표면 문서에서 다루고 spawn_target은 첫 스폰 탐색용이므로 여기서는 제외한다. 나머지 기본 블록·유체·noise cell·라우터 15필드와 각 활성 플래그는 전부 표시한다.

### C. overworld

```text
aquifers_enabled: true
default_block:
  Name: minecraft:stone
default_fluid:
  Name: minecraft:water
  Properties:
    level: 0
disable_mob_generation: false
legacy_random_source: false
noise:
  height: 384
  min_y: -64
  size_horizontal: 1
  size_vertical: 2
noise_router:
  barrier:
    type: minecraft:noise
    noise: minecraft:aquifer_barrier
    xz_scale: 1.0
    y_scale: 0.5
  continents: minecraft:overworld/continents
  depth: minecraft:overworld/depth
  erosion: minecraft:overworld/erosion
  final_density:
    type: minecraft:min
    argument1:
      type: minecraft:squeeze
      argument:
        type: minecraft:interpolated
        argument:
          type: minecraft:mul
          argument1: 0.64
          argument2:
            type: minecraft:blend_density
            argument:
              type: minecraft:add
              argument1: 0.1171875
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 0.0
                  from_y: -64
                  to_value: 1.0
                  to_y: -40
                argument2:
                  type: minecraft:add
                  argument1: -0.1171875
                  argument2:
                    type: minecraft:add
                    argument1: -0.078125
                    argument2:
                      type: minecraft:mul
                      argument1:
                        type: minecraft:y_clamped_gradient
                        from_value: 1.0
                        from_y: 240
                        to_value: 0.0
                        to_y: 256
                      argument2:
                        type: minecraft:add
                        argument1: 0.078125
                        argument2:
                          type: minecraft:range_choice
                          input:
                            type: minecraft:cache_once
                            argument: minecraft:overworld/sloped_cheese
                          max_exclusive: 1.5625
                          min_inclusive: -1000000.0
                          when_in_range:
                            type: minecraft:min
                            argument1:
                              type: minecraft:cache_once
                              argument: minecraft:overworld/sloped_cheese
                            argument2:
                              type: minecraft:mul
                              argument1: 5.0
                              argument2: minecraft:overworld/caves/entrances
                          when_out_of_range:
                            type: minecraft:max
                            argument1:
                              type: minecraft:min
                              argument1:
                                type: minecraft:min
                                argument1:
                                  type: minecraft:add
                                  argument1:
                                    type: minecraft:mul
                                    argument1: 4.0
                                    argument2:
                                      type: minecraft:square
                                      argument:
                                        type: minecraft:noise
                                        noise: minecraft:cave_layer
                                        xz_scale: 1.0
                                        y_scale: 8.0
                                  argument2:
                                    type: minecraft:add
                                    argument1:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 0.27
                                        argument2:
                                          type: minecraft:noise
                                          noise: minecraft:cave_cheese
                                          xz_scale: 1.0
                                          y_scale: 0.6666666666666666
                                      max: 1.0
                                      min: -1.0
                                    argument2:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 1.5
                                        argument2:
                                          type: minecraft:mul
                                          argument1: -0.64
                                          argument2:
                                            type: minecraft:cache_once
                                            argument: minecraft:overworld/sloped_cheese
                                      max: 0.5
                                      min: 0.0
                                argument2: minecraft:overworld/caves/entrances
                              argument2:
                                type: minecraft:add
                                argument1: minecraft:overworld/caves/spaghetti_2d
                                argument2: minecraft:overworld/caves/spaghetti_roughness_function
                            argument2:
                              type: minecraft:range_choice
                              input: minecraft:overworld/caves/pillars
                              max_exclusive: 0.03
                              min_inclusive: -1000000.0
                              when_in_range: -1000000.0
                              when_out_of_range: minecraft:overworld/caves/pillars
    argument2: minecraft:overworld/caves/noodle
  fluid_level_floodedness:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_floodedness
    xz_scale: 1.0
    y_scale: 0.67
  fluid_level_spread:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_spread
    xz_scale: 1.0
    y_scale: 0.7142857142857143
  lava:
    type: minecraft:noise
    noise: minecraft:aquifer_lava
    xz_scale: 1.0
    y_scale: 1.0
  preliminary_surface_level:
    type: minecraft:find_top_surface
    cell_height: 8
    density:
      type: minecraft:add
      argument1: -0.390625
      argument2:
        type: minecraft:add
        argument1: 0.1171875
        argument2:
          type: minecraft:mul
          argument1:
            type: minecraft:y_clamped_gradient
            from_value: 0.0
            from_y: -64
            to_value: 1.0
            to_y: -40
          argument2:
            type: minecraft:add
            argument1: -0.1171875
            argument2:
              type: minecraft:add
              argument1: -0.078125
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 1.0
                  from_y: 240
                  to_value: 0.0
                  to_y: 256
                argument2:
                  type: minecraft:add
                  argument1: 0.078125
                  argument2:
                    type: minecraft:clamp
                    input:
                      type: minecraft:add
                      argument1: -0.703125
                      argument2:
                        type: minecraft:mul
                        argument1: 4.0
                        argument2:
                          type: minecraft:quarter_negative
                          argument:
                            type: minecraft:mul
                            argument1:
                              type: minecraft:add
                              argument1:
                                type: minecraft:y_clamped_gradient
                                from_value: 1.5
                                from_y: -64
                                to_value: -1.5
                                to_y: 320
                              argument2:
                                type: minecraft:cache_2d
                                argument: minecraft:overworld/offset
                            argument2:
                              type: minecraft:cache_2d
                              argument: minecraft:overworld/factor
                    max: 64.0
                    min: -64.0
    lower_bound: -64
    upper_bound:
      type: minecraft:clamp
      input:
        type: minecraft:add
        argument1: 128.0
        argument2:
          type: minecraft:mul
          argument1: -128.0
          argument2:
            type: minecraft:add
            argument1:
              type: minecraft:mul
              argument1: 0.2734375
              argument2:
                type: minecraft:invert
                argument:
                  type: minecraft:cache_2d
                  argument: minecraft:overworld/factor
            argument2:
              type: minecraft:mul
              argument1: -1.0
              argument2:
                type: minecraft:cache_2d
                argument: minecraft:overworld/offset
      max: 320.0
      min: -40.0
  ridges: minecraft:overworld/ridges
  temperature:
    type: minecraft:shifted_noise
    noise: minecraft:temperature
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vegetation:
    type: minecraft:shifted_noise
    noise: minecraft:vegetation
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vein_gap:
    type: minecraft:noise
    noise: minecraft:ore_gap
    xz_scale: 1.0
    y_scale: 1.0
  vein_ridged:
    type: minecraft:add
    argument1: -0.07999999821186066
    argument2:
      type: minecraft:max
      argument1:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_a
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
      argument2:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_b
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
  vein_toggle:
    type: minecraft:interpolated
    argument:
      type: minecraft:range_choice
      input: minecraft:y
      max_exclusive: 51.0
      min_inclusive: -60.0
      when_in_range:
        type: minecraft:noise
        noise: minecraft:ore_veininess
        xz_scale: 1.5
        y_scale: 1.5
      when_out_of_range: 0.0
ore_veins_enabled: true
sea_level: 63
```

### C. large_biomes

```text
aquifers_enabled: true
default_block:
  Name: minecraft:stone
default_fluid:
  Name: minecraft:water
  Properties:
    level: 0
disable_mob_generation: false
legacy_random_source: false
noise:
  height: 384
  min_y: -64
  size_horizontal: 1
  size_vertical: 2
noise_router:
  barrier:
    type: minecraft:noise
    noise: minecraft:aquifer_barrier
    xz_scale: 1.0
    y_scale: 0.5
  continents: minecraft:overworld_large_biomes/continents
  depth: minecraft:overworld_large_biomes/depth
  erosion: minecraft:overworld_large_biomes/erosion
  final_density:
    type: minecraft:min
    argument1:
      type: minecraft:squeeze
      argument:
        type: minecraft:interpolated
        argument:
          type: minecraft:mul
          argument1: 0.64
          argument2:
            type: minecraft:blend_density
            argument:
              type: minecraft:add
              argument1: 0.1171875
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 0.0
                  from_y: -64
                  to_value: 1.0
                  to_y: -40
                argument2:
                  type: minecraft:add
                  argument1: -0.1171875
                  argument2:
                    type: minecraft:add
                    argument1: -0.078125
                    argument2:
                      type: minecraft:mul
                      argument1:
                        type: minecraft:y_clamped_gradient
                        from_value: 1.0
                        from_y: 240
                        to_value: 0.0
                        to_y: 256
                      argument2:
                        type: minecraft:add
                        argument1: 0.078125
                        argument2:
                          type: minecraft:range_choice
                          input:
                            type: minecraft:cache_once
                            argument: minecraft:overworld_large_biomes/sloped_cheese
                          max_exclusive: 1.5625
                          min_inclusive: -1000000.0
                          when_in_range:
                            type: minecraft:min
                            argument1:
                              type: minecraft:cache_once
                              argument: minecraft:overworld_large_biomes/sloped_cheese
                            argument2:
                              type: minecraft:mul
                              argument1: 5.0
                              argument2: minecraft:overworld/caves/entrances
                          when_out_of_range:
                            type: minecraft:max
                            argument1:
                              type: minecraft:min
                              argument1:
                                type: minecraft:min
                                argument1:
                                  type: minecraft:add
                                  argument1:
                                    type: minecraft:mul
                                    argument1: 4.0
                                    argument2:
                                      type: minecraft:square
                                      argument:
                                        type: minecraft:noise
                                        noise: minecraft:cave_layer
                                        xz_scale: 1.0
                                        y_scale: 8.0
                                  argument2:
                                    type: minecraft:add
                                    argument1:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 0.27
                                        argument2:
                                          type: minecraft:noise
                                          noise: minecraft:cave_cheese
                                          xz_scale: 1.0
                                          y_scale: 0.6666666666666666
                                      max: 1.0
                                      min: -1.0
                                    argument2:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 1.5
                                        argument2:
                                          type: minecraft:mul
                                          argument1: -0.64
                                          argument2:
                                            type: minecraft:cache_once
                                            argument: minecraft:overworld_large_biomes/sloped_cheese
                                      max: 0.5
                                      min: 0.0
                                argument2: minecraft:overworld/caves/entrances
                              argument2:
                                type: minecraft:add
                                argument1: minecraft:overworld/caves/spaghetti_2d
                                argument2: minecraft:overworld/caves/spaghetti_roughness_function
                            argument2:
                              type: minecraft:range_choice
                              input: minecraft:overworld/caves/pillars
                              max_exclusive: 0.03
                              min_inclusive: -1000000.0
                              when_in_range: -1000000.0
                              when_out_of_range: minecraft:overworld/caves/pillars
    argument2: minecraft:overworld/caves/noodle
  fluid_level_floodedness:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_floodedness
    xz_scale: 1.0
    y_scale: 0.67
  fluid_level_spread:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_spread
    xz_scale: 1.0
    y_scale: 0.7142857142857143
  lava:
    type: minecraft:noise
    noise: minecraft:aquifer_lava
    xz_scale: 1.0
    y_scale: 1.0
  preliminary_surface_level:
    type: minecraft:find_top_surface
    cell_height: 8
    density:
      type: minecraft:add
      argument1: -0.390625
      argument2:
        type: minecraft:add
        argument1: 0.1171875
        argument2:
          type: minecraft:mul
          argument1:
            type: minecraft:y_clamped_gradient
            from_value: 0.0
            from_y: -64
            to_value: 1.0
            to_y: -40
          argument2:
            type: minecraft:add
            argument1: -0.1171875
            argument2:
              type: minecraft:add
              argument1: -0.078125
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 1.0
                  from_y: 240
                  to_value: 0.0
                  to_y: 256
                argument2:
                  type: minecraft:add
                  argument1: 0.078125
                  argument2:
                    type: minecraft:clamp
                    input:
                      type: minecraft:add
                      argument1: -0.703125
                      argument2:
                        type: minecraft:mul
                        argument1: 4.0
                        argument2:
                          type: minecraft:quarter_negative
                          argument:
                            type: minecraft:mul
                            argument1:
                              type: minecraft:add
                              argument1:
                                type: minecraft:y_clamped_gradient
                                from_value: 1.5
                                from_y: -64
                                to_value: -1.5
                                to_y: 320
                              argument2:
                                type: minecraft:cache_2d
                                argument: minecraft:overworld_large_biomes/offset
                            argument2:
                              type: minecraft:cache_2d
                              argument: minecraft:overworld_large_biomes/factor
                    max: 64.0
                    min: -64.0
    lower_bound: -64
    upper_bound:
      type: minecraft:clamp
      input:
        type: minecraft:add
        argument1: 128.0
        argument2:
          type: minecraft:mul
          argument1: -128.0
          argument2:
            type: minecraft:add
            argument1:
              type: minecraft:mul
              argument1: 0.2734375
              argument2:
                type: minecraft:invert
                argument:
                  type: minecraft:cache_2d
                  argument: minecraft:overworld_large_biomes/factor
            argument2:
              type: minecraft:mul
              argument1: -1.0
              argument2:
                type: minecraft:cache_2d
                argument: minecraft:overworld_large_biomes/offset
      max: 320.0
      min: -40.0
  ridges: minecraft:overworld/ridges
  temperature:
    type: minecraft:shifted_noise
    noise: minecraft:temperature_large
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vegetation:
    type: minecraft:shifted_noise
    noise: minecraft:vegetation_large
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vein_gap:
    type: minecraft:noise
    noise: minecraft:ore_gap
    xz_scale: 1.0
    y_scale: 1.0
  vein_ridged:
    type: minecraft:add
    argument1: -0.07999999821186066
    argument2:
      type: minecraft:max
      argument1:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_a
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
      argument2:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_b
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
  vein_toggle:
    type: minecraft:interpolated
    argument:
      type: minecraft:range_choice
      input: minecraft:y
      max_exclusive: 51.0
      min_inclusive: -60.0
      when_in_range:
        type: minecraft:noise
        noise: minecraft:ore_veininess
        xz_scale: 1.5
        y_scale: 1.5
      when_out_of_range: 0.0
ore_veins_enabled: true
sea_level: 63
```

### C. amplified

```text
aquifers_enabled: true
default_block:
  Name: minecraft:stone
default_fluid:
  Name: minecraft:water
  Properties:
    level: 0
disable_mob_generation: false
legacy_random_source: false
noise:
  height: 384
  min_y: -64
  size_horizontal: 1
  size_vertical: 2
noise_router:
  barrier:
    type: minecraft:noise
    noise: minecraft:aquifer_barrier
    xz_scale: 1.0
    y_scale: 0.5
  continents: minecraft:overworld/continents
  depth: minecraft:overworld_amplified/depth
  erosion: minecraft:overworld/erosion
  final_density:
    type: minecraft:min
    argument1:
      type: minecraft:squeeze
      argument:
        type: minecraft:interpolated
        argument:
          type: minecraft:mul
          argument1: 0.64
          argument2:
            type: minecraft:blend_density
            argument:
              type: minecraft:add
              argument1: 0.4
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 0.0
                  from_y: -64
                  to_value: 1.0
                  to_y: -40
                argument2:
                  type: minecraft:add
                  argument1: -0.4
                  argument2:
                    type: minecraft:add
                    argument1: -0.078125
                    argument2:
                      type: minecraft:mul
                      argument1:
                        type: minecraft:y_clamped_gradient
                        from_value: 1.0
                        from_y: 304
                        to_value: 0.0
                        to_y: 320
                      argument2:
                        type: minecraft:add
                        argument1: 0.078125
                        argument2:
                          type: minecraft:range_choice
                          input:
                            type: minecraft:cache_once
                            argument: minecraft:overworld_amplified/sloped_cheese
                          max_exclusive: 1.5625
                          min_inclusive: -1000000.0
                          when_in_range:
                            type: minecraft:min
                            argument1:
                              type: minecraft:cache_once
                              argument: minecraft:overworld_amplified/sloped_cheese
                            argument2:
                              type: minecraft:mul
                              argument1: 5.0
                              argument2: minecraft:overworld/caves/entrances
                          when_out_of_range:
                            type: minecraft:max
                            argument1:
                              type: minecraft:min
                              argument1:
                                type: minecraft:min
                                argument1:
                                  type: minecraft:add
                                  argument1:
                                    type: minecraft:mul
                                    argument1: 4.0
                                    argument2:
                                      type: minecraft:square
                                      argument:
                                        type: minecraft:noise
                                        noise: minecraft:cave_layer
                                        xz_scale: 1.0
                                        y_scale: 8.0
                                  argument2:
                                    type: minecraft:add
                                    argument1:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 0.27
                                        argument2:
                                          type: minecraft:noise
                                          noise: minecraft:cave_cheese
                                          xz_scale: 1.0
                                          y_scale: 0.6666666666666666
                                      max: 1.0
                                      min: -1.0
                                    argument2:
                                      type: minecraft:clamp
                                      input:
                                        type: minecraft:add
                                        argument1: 1.5
                                        argument2:
                                          type: minecraft:mul
                                          argument1: -0.64
                                          argument2:
                                            type: minecraft:cache_once
                                            argument: minecraft:overworld_amplified/sloped_cheese
                                      max: 0.5
                                      min: 0.0
                                argument2: minecraft:overworld/caves/entrances
                              argument2:
                                type: minecraft:add
                                argument1: minecraft:overworld/caves/spaghetti_2d
                                argument2: minecraft:overworld/caves/spaghetti_roughness_function
                            argument2:
                              type: minecraft:range_choice
                              input: minecraft:overworld/caves/pillars
                              max_exclusive: 0.03
                              min_inclusive: -1000000.0
                              when_in_range: -1000000.0
                              when_out_of_range: minecraft:overworld/caves/pillars
    argument2: minecraft:overworld/caves/noodle
  fluid_level_floodedness:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_floodedness
    xz_scale: 1.0
    y_scale: 0.67
  fluid_level_spread:
    type: minecraft:noise
    noise: minecraft:aquifer_fluid_level_spread
    xz_scale: 1.0
    y_scale: 0.7142857142857143
  lava:
    type: minecraft:noise
    noise: minecraft:aquifer_lava
    xz_scale: 1.0
    y_scale: 1.0
  preliminary_surface_level:
    type: minecraft:find_top_surface
    cell_height: 8
    density:
      type: minecraft:add
      argument1: -0.390625
      argument2:
        type: minecraft:add
        argument1: 0.4
        argument2:
          type: minecraft:mul
          argument1:
            type: minecraft:y_clamped_gradient
            from_value: 0.0
            from_y: -64
            to_value: 1.0
            to_y: -40
          argument2:
            type: minecraft:add
            argument1: -0.4
            argument2:
              type: minecraft:add
              argument1: -0.078125
              argument2:
                type: minecraft:mul
                argument1:
                  type: minecraft:y_clamped_gradient
                  from_value: 1.0
                  from_y: 304
                  to_value: 0.0
                  to_y: 320
                argument2:
                  type: minecraft:add
                  argument1: 0.078125
                  argument2:
                    type: minecraft:clamp
                    input:
                      type: minecraft:add
                      argument1: -0.703125
                      argument2:
                        type: minecraft:mul
                        argument1: 4.0
                        argument2:
                          type: minecraft:quarter_negative
                          argument:
                            type: minecraft:mul
                            argument1:
                              type: minecraft:add
                              argument1:
                                type: minecraft:y_clamped_gradient
                                from_value: 1.5
                                from_y: -64
                                to_value: -1.5
                                to_y: 320
                              argument2:
                                type: minecraft:cache_2d
                                argument: minecraft:overworld_amplified/offset
                            argument2:
                              type: minecraft:cache_2d
                              argument: minecraft:overworld_amplified/factor
                    max: 64.0
                    min: -64.0
    lower_bound: -64
    upper_bound:
      type: minecraft:clamp
      input:
        type: minecraft:add
        argument1: 128.0
        argument2:
          type: minecraft:mul
          argument1: -128.0
          argument2:
            type: minecraft:add
            argument1:
              type: minecraft:mul
              argument1: 0.2734375
              argument2:
                type: minecraft:invert
                argument:
                  type: minecraft:cache_2d
                  argument: minecraft:overworld_amplified/factor
            argument2:
              type: minecraft:mul
              argument1: -1.0
              argument2:
                type: minecraft:cache_2d
                argument: minecraft:overworld_amplified/offset
      max: 320.0
      min: -40.0
  ridges: minecraft:overworld/ridges
  temperature:
    type: minecraft:shifted_noise
    noise: minecraft:temperature
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vegetation:
    type: minecraft:shifted_noise
    noise: minecraft:vegetation
    shift_x: minecraft:shift_x
    shift_y: 0.0
    shift_z: minecraft:shift_z
    xz_scale: 0.25
    y_scale: 0.0
  vein_gap:
    type: minecraft:noise
    noise: minecraft:ore_gap
    xz_scale: 1.0
    y_scale: 1.0
  vein_ridged:
    type: minecraft:add
    argument1: -0.07999999821186066
    argument2:
      type: minecraft:max
      argument1:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_a
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
      argument2:
        type: minecraft:abs
        argument:
          type: minecraft:interpolated
          argument:
            type: minecraft:range_choice
            input: minecraft:y
            max_exclusive: 51.0
            min_inclusive: -60.0
            when_in_range:
              type: minecraft:noise
              noise: minecraft:ore_vein_b
              xz_scale: 4.0
              y_scale: 4.0
            when_out_of_range: 0.0
  vein_toggle:
    type: minecraft:interpolated
    argument:
      type: minecraft:range_choice
      input: minecraft:y
      max_exclusive: 51.0
      min_inclusive: -60.0
      when_in_range:
        type: minecraft:noise
        noise: minecraft:ore_veininess
        xz_scale: 1.5
        y_scale: 1.5
      when_out_of_range: 0.0
ore_veins_enabled: true
sea_level: 63
```

## 부록 D. 초기 지형 그래프의 NormalNoise 파라미터

노이즈 ID, firstOctave, 진폭 배열이다. 호출별 좌표 배율은 부록 B·C에 있다. BlendedNoise는 이 NormalNoise 레지스트리와 별도다. 표면 전용 노이즈는 표면 문서에 싣는다.

| ID | firstOctave | amplitudes |
| --- | ---: | --- |
| `aquifer_barrier` | -3 | `[1.0]` |
| `aquifer_fluid_level_floodedness` | -7 | `[1.0]` |
| `aquifer_fluid_level_spread` | -5 | `[1.0]` |
| `aquifer_lava` | -1 | `[1.0]` |
| `cave_cheese` | -8 | `[0.5, 1.0, 2.0, 1.0, 2.0, 1.0, 0.0, 2.0, 0.0]` |
| `cave_entrance` | -7 | `[0.4, 0.5, 1.0]` |
| `cave_layer` | -8 | `[1.0]` |
| `continentalness` | -9 | `[1.0, 1.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0]` |
| `continentalness_large` | -11 | `[1.0, 1.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0]` |
| `erosion` | -9 | `[1.0, 1.0, 0.0, 1.0, 1.0]` |
| `erosion_large` | -11 | `[1.0, 1.0, 0.0, 1.0, 1.0]` |
| `jagged` | -16 | `[1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]` |
| `noodle` | -8 | `[1.0]` |
| `noodle_ridge_a` | -7 | `[1.0]` |
| `noodle_ridge_b` | -7 | `[1.0]` |
| `noodle_thickness` | -8 | `[1.0]` |
| `ore_gap` | -5 | `[1.0]` |
| `ore_vein_a` | -7 | `[1.0]` |
| `ore_vein_b` | -7 | `[1.0]` |
| `ore_veininess` | -8 | `[1.0]` |
| `pillar` | -7 | `[1.0, 1.0]` |
| `pillar_rareness` | -8 | `[1.0]` |
| `pillar_thickness` | -8 | `[1.0]` |
| `ridge` | -7 | `[1.0, 2.0, 1.0, 0.0, 0.0, 0.0]` |
| `spaghetti_2d` | -7 | `[1.0]` |
| `spaghetti_2d_elevation` | -8 | `[1.0]` |
| `spaghetti_2d_modulator` | -11 | `[1.0]` |
| `spaghetti_2d_thickness` | -11 | `[1.0]` |
| `spaghetti_3d_1` | -7 | `[1.0]` |
| `spaghetti_3d_2` | -7 | `[1.0]` |
| `spaghetti_3d_rarity` | -11 | `[1.0]` |
| `spaghetti_3d_thickness` | -8 | `[1.0]` |
| `spaghetti_roughness` | -5 | `[1.0]` |
| `spaghetti_roughness_modulator` | -8 | `[1.0]` |
| `temperature` | -10 | `[1.5, 0.0, 1.0, 0.0, 0.0, 0.0]` |
| `temperature_large` | -12 | `[1.5, 0.0, 1.0, 0.0, 0.0, 0.0]` |
| `vegetation` | -8 | `[1.0, 1.0, 0.0, 0.0, 0.0, 0.0]` |
| `vegetation_large` | -10 | `[1.0, 1.0, 0.0, 0.0, 0.0, 0.0]` |

## 부록 E. 출처·완전성

- Minecraft 26.2 client.jar SHA256: `40896ee9f1e2bec3c934daac7e93d41e9e3d9c2f8ae0ca366d52ffbfd1afa290`
- spline 루트9개 / 중첩 노드354개 / 제어점1290개 / 상수잎945개.
- 연결된 density function 31개 / 오버월드 noise settings 3개 / 참조 NormalNoise 38개.
- 원본 경로: `data/minecraft/worldgen/{density_function,noise_settings,noise}/`.
- 재생성: `python tools/document-minecraft-overworld.py`. 두 상세 문서의 부록만 바꾸고 전체 설명서는 수정하지 않는다.

<!-- END OVERWORLD BASE DATA -->
