# MCP-Reborn 로컬 참고 환경 (AI 컨텍스트)

사용자는 최신 MCP-Reborn을 ref에 놓고 실제 소스까지 읽을 수 있게 준비하는 제안에 `실시`로 승인했다. 게임 생성 수식 변경은 범위 밖이다. 자동 테스트/CTest/CU/합성 입력은 사용하지 않는다.

## 출처와 버전

- 저장소: https://github.com/Hexeption/MCP-Reborn
- 2026-09-26 조회 당시 upstream 기본 브랜치: `26.2`.
- 고정 커밋: `727d72ffc66bcdf1a8c16ee92b120db2eaa46e26` (depth 1 clone).
- Minecraft `26.2`, MCP config `20260616.103818`, official mappings `26.2`.
- 대상: `ref/MCP-Reborn/`. 원본 MCP-License 및 README 보존. 생성된 Minecraft 소스는 공개하지 않는다는 upstream 안내를 따른다. 참고 폴더 전체를 루트 .gitignore에서 제외했으며 현재 CMake/package 경로는 ref를 복사하지 않는다.
- 최신이라는 표현은 이 저장소의 최신 기본 브랜치를 뜻한다. 향후 버전 자동 업데이트 없음.

## 도구 환경

- upstream Gradle wrapper `8.14.4`는 기존 `C:/Program Files/Eclipse Adoptium/jdk-21.0.10.7-hotspot`으로 실행.
- Minecraft 도구 체인은 로컬 `.tools/jdk-25.0.4.1+1` (Adoptium Temurin Windows x64 JDK 25, 공식 API 다운로드와 제공 SHA256 검증).
- 다운로드 메타데이터: `.cache/downloads/temurin25-metadata.json`.
- 격리 Gradle 캐시: `.cache/mcp-gradle`. 이 캐시의 gradle.properties에서 `org.gradle.java.installations.paths`로 JDK25 경로 지정. 전역 JAVA_HOME/PATH/시스템 설치를 변경하지 않았다.
- 처음 `-Dorg.gradle.java.installations.paths`로 전달했을 때 Gradle이 JDK25를 찾지 못했다. 캐시 gradle.properties에 지정한 뒤 재실행했다.

재개 명령 (승인된 쓰기 작업에서만):

```powershell
$env:JAVA_HOME='C:/Program Files/Eclipse Adoptium/jdk-21.0.10.7-hotspot'
$env:GRADLE_USER_HOME='C:/Users/PC/Desktop/YD_Unity/sandbox/.cache/mcp-gradle'
Set-Location 'C:/Users/PC/Desktop/YD_Unity/sandbox/ref/MCP-Reborn'
./gradlew.bat --no-daemon --console=plain setup
```

## 검증 상태

`setup` 성공 (4분6초, 6 tasks: 4 executed/2 up-to-date). `src/main/java/`에 Java 파일 7,055개 생성. 최종 로그 `ref/MCP-Reborn/setup-local.log`. Gradle9 호환성 관련 deprecation 안내는 upstream 도구의 경고이며 현재 wrapper8.14.4 작업은 성공했다. Minecraft 실행/재컴파일/자동테스트는 수행하지 않았다.

지형 분석 시작점 (모두 ref/MCP-Reborn/src/main/java 아래):

- `net/minecraft/data/worldgen/TerrainProvider.java`: overworldOffset / overworldFactor / overworldJaggedness 스플라인.
- `net/minecraft/world/level/levelgen/NoiseRouterData.java`: 노이즈·스플라인·밀도 조합과 overworld 경로.
- `net/minecraft/world/level/levelgen/DensityFunctions.java`: 밀도 연산 구현.
- `net/minecraft/world/level/levelgen/NoiseBasedChunkGenerator.java`: 청크 밀도와 블록 생성.
- `net/minecraft/world/level/levelgen/Aquifer.java`: 대수층.
- `net/minecraft/data/worldgen/SurfaceRuleData.java`, `net/minecraft/world/level/levelgen/SurfaceRules.java`: 표면 규칙과 평가.
- `net/minecraft/world/level/biome/Climate.java`, `MultiNoiseBiomeSource.java`: 바이옴 선택.

핵심 파일 존재·메서드 이름·소스 읽기를 확인했다. 이번 작업은 소스 준비이며 Minecraft 전체와 우리 생성기의 정밀 비교/이식은 하지 않았다.

게임 Release 증분 빌드 성공. 패키징 시 실행 중인 편집기/CRT가 잠겨 있었으므로 원본과 대상 SHA256 동일을 확인한 파일만 교체를 생략하고 나머지 패키징 단계를 실행해 완료했다. tools/package.ps1 자체는 변경하지 않았다. 사용자 편집기를 종료하거나 입력을 보내지 않았다.

사용자 설정 SHA256 전후 동일:
- settings.json: `4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1`
- worldgen.json: `E16C2EED246E6B85ECA968FCF5E2C98A7328CB309A1C60BDF03862DBB7DDB7DB`
