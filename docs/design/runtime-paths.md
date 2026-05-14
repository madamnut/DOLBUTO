# 런타임 경로

## 목적

Debug와 Release의 리소스 탐색 기준을 분리하기 위해 런타임 경로 시스템을 둔다.
경로 처리는 `src/platform/RuntimePaths.*`에서 담당한다.

## Debug 경로

Debug에서는 CMake 컴파일 정의로 전달된 개발 폴더를 직접 사용한다.

```text
assets      -> 프로젝트 루트/assets
config      -> 프로젝트 루트/config
saves/world -> 프로젝트 루트/saves/world
shaders     -> 빌드 폴더/shaders
screenshots -> 프로젝트 루트/screenshots
```

이 방식은 개발 중 원본 에셋과 설정을 바로 수정하기 위한 것이다.

## Release 경로

Release에서는 실행 파일 위치를 기준으로 리소스를 찾는다.

```text
assets      -> exe 옆/assets
config      -> exe 옆/config
saves/world -> exe 옆/saves/world
shaders     -> exe 옆/shaders
screenshots -> exe 옆/screenshots
```

이 방식은 Release 출력 폴더를 그대로 옮겨도 실행 가능하게 만들기 위한 것이다.

## 사용처

- `main.cpp`: 창 아이콘
- `GameClient`: 설정 파일, 월드/플레이어 상태 파일
- `Renderer`: 에셋, 셰이더, 월드 저장/로드, 스크린샷
- CMake Release 후처리: exe 옆에 리소스 복사

## 주의점

경로를 직접 문자열로 박아넣지 말고, 새 리소스 경로가 필요하면 `RuntimePaths` 함수를 통해 접근한다.

관련 문서: [[build-and-distribution]]

## 로그

런타임 로그는 `RuntimePaths::logDirectory()`를 사용한다.

Debug 빌드는 프로젝트 루트의 `logs` 디렉터리에 로그를 쓴다.
Release 빌드는 실행 파일 옆의 `logs` 디렉터리에 로그를 쓴다.

```text
logs/Latest.txt
logs/DOLBUTO_YYYYMMDD_HHMMSS_mmm.txt
```

`Latest.txt`는 실행할 때마다 덮어쓴다. 타임스탬프 파일은 기록용으로 유지하며, 같은 이름이 이미 있으면 `_1`, `_2` 접미사를 사용한다.
