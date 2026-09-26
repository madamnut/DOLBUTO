# Minecraft 원본 비교 페이지 (AI 컨텍스트)

## 승인과 범위

사용자는 원본 노이즈/스플라인을 볼 수 있는 읽기 전용 페이지, 시드/범위 조절, 우리 월드 나란히 비교 제안에 `실시`로 승인했다. 게임의 지형생성기/설정/schema는 변경하지 않는다. 원본 Double Perlin/시드 처리의 재현과 현재 일반 오버월드의 offset/factor/jaggedness가 대상이다. 기본3D밀도/동굴/대수층/블록표면/바이옴은 이번 범위 밖이다.

## 구조와 수식

- `src/editor/minecraft_reference.*`: editor만 링크하는 독립 C++ 수치 구현. world_core 또는 sandbox 게임 실행파일에 연결하지 않는다. 해당 소스 `/fp:strict`로 원본 부동소수점 계산 순서를 보존한다. SIMD FastNoise 게임 경로를 교체하지 않는다.
- Minecraft26.2 일반 오버월드의 Xoroshiro128++ / Stafford13 seed 확장 / MD5 이름 기반128비트 seed / positional fork / octave_ 이름 seed / ImprovedPerlin3D / DoublePerlin 구현. MD5는 기존 Windows BCrypt 사용.
- 두 옥타브 묶음,두번째입력배율1.0181268882175227,옥타브감쇠/활성옥타브범위기반정규화. seed는JSON문자열로signed64비트전체보존. 원본좌표는XZ±30,000,000에서블록단위floor,131072wrap금지.
- 원본 `offset` noise 키는 Shift 전용이다. SHIFT_X = noise(x*.25,0,z*.25)*4, SHIFT_Z = noise(z*.25,x*.25,0)*4. 신호입력은(x*.25+SHIFT_X,0,z*.25+SHIFT_Z).
- Continentalness(-9;1,1,2,2,2,1,1,1,1),Erosion(-9;1,1,0,1,1),Ridge(-7;1,2,1,0,0,0),Temperature(-10;1.5,0,1,0,0,0),Vegetation(-8;1,1,0,0,0,0),Shift(-3;1,1,1,0). Jagged(-16;16개1)는XZ1500배/Y0,Shift없음. PV는원본double밀도연산의접기공식.
- `minecraft_splines.inc`는일반TerrainProvider의수치제어점/접선만담는다. C/E/W/PV축을유지한중첩Hermite평가로우리SplineGrid의C/E smoothstep보간을재사용하지않는다. Java구현파일을복사/배포하지않는다. 원본MCP폴더는계속Git/배포제외.
- 지도/곡선의offset은TerrainProvider원시출력이며NoiseRouterData의 -0.50375F bias 제외. 프로브 응답에보정후값도별도제공. 우리offset은height_scale/sea_level적용전, factor는squash곱하기/클램프전, jaggedness는클램프/노이즈/배율/토글적용전스플라인값이다. 기존게임용지도/API는수정하지않는다.
- 실제게임청크의flatCache/격자보간/옛청크blend를재현하지않는다. 직접좌표에서신호평가/원시곡선출력이며블록월드동일성을주장하지않는다.

## UI/API

- `assets/editor/minecraft.html/.js/.css`: 독립페이지. 기존편집기의왼쪽버튼으로이름있는새탭열기,원래탭의작업유지. token fragment/sessionStorage인증동일. source창의 `referenceSnapshot()`으로structuredClone된초안만읽는다. 부모창이없으면/api/state확정규칙fallback. 사본수동갱신,시드독립입력과우리시드맞추기. 어느쪽설정파일도저장하지않는다.
-10종지도,양쪽동일범위/흑백색상범위,기본128최대512표본,공통자동대비옵션,휠확대/드래그후갱신/직접범위/원점주변/우리전체범위,클릭프로브와해당C/E/W에서곡선열기. 우리좌표는131072wrap,원본은비순환. 온도위도/강수량vs습도의차이를표시.
- 곡선은3종동시에원본실선/우리점선. 가로축C/E/W선택과나머지고정입력,[-1.2,1.2]257표본. W변화시PV도연동. 곡선은시드독립.
- POST `/api/minecraft/preview` (float32 w/h + 원본 + 선택우리), `/probe` (double JSON + 양쪽원시곡선값), `/curve` (float곡선점). 인증후전용분기이며save경로없음. seed별원본sampler캐시1개. 우리지도는행별SIMD배치로기존TerrainGenerator사용. 계산순차화/최신요청번호로오래된응답버림.
- reference페이지는MC게임/JDK/ref자료의런타임의존성없음. 일반오버월드만제공. 본문의공식버전/노이즈표는읽기전용.

## 출처와 수치 대조

MCP-Reborn26.2 commit727d72ffc66bcdf1a8c16ee92b120db2eaa46e26 및 해당공식Minecraft26.2 client.jar. 로컬Java ReferenceProbe는공식binary의 NormalNoise/XoroshiroRandomSource/TerrainProvider/CubicSpline을직접호출한다. 역컴파일파일을재컴파일한대조가아니다. ReferenceProbe는이번명시적검증용으로ref/MCP-Reborn/local-reference에격리되어배포하지않는다. 수치제어점JSON과원본값CSV대신JSON을생성한다.

- `build/release/minecraft-reference-inspection/inspect.py/results.json`: 격리복사실행파일과사용자worldgen사본으로브라우저없는HTTP진단. 자동테스트/CTest/게임시작검사등록없음.
- 6시드(0/1337/-1/signed64최소·최대/9007199254740993)×8좌표(원점/±1/지역/순환경계/±30M 등),10필드. 원본double최대차1.11e-16(온도),나머지0. 원시스플라인프로브0.
- 25개C/E조합×257W값(총6,425입력),원본3곡선최대차offset5.79e-8/factor2.37e-7/jaggedness2.92e-8. Java JSON float의짧은10진표현과C++확장10진표현의차이포함. float수준수치대응이며모든시드/좌표의비트동일성보장은아님.
- 10종64×32나란히지도응답/모서리프로브대응,3축×3곡선양쪽응답,잘못된seed/좌표/곡선/해상도400확인. 정적5파일200. 격리worldgen바이트보존/초안생성없음.
- JS구문/정적DOM참조(누락·중복0)/포맷/Release빌드·패키징성공. 배포실행파일/편집기6파일원본해시일치,사용자settings/worldgen기존해시보존(verification최신절). 실제브라우저외관·입력조작은CU금지로검증하지않았다.
