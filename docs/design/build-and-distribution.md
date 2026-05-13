# 빌드와 배포

## 빌드 방식

프로젝트는 CMake/Ninja 기반이며 Visual Studio에서 CMake 프로젝트로 열어 빌드한다.
빌드 자체는 사용자가 직접 수행한다.
현재 사용하는 구성은 `Debug`와 `Release`만 둔다.

## 주요 타깃

- `DOLBUTO`: 게임 실행 파일
- `DOLBUTOShaders`: Vulkan 셰이더 컴파일 타깃

## 서드파티 사전 빌드 라이브러리

수정할 예정이 없는 서드파티 라이브러리는 사전 빌드 바이너리로 링크한다.
게임 빌드에서 이 서드파티 소스를 다시 컴파일하지 않는다.

```text
third_party/prebuilt/
  glfw/
    include/
    lib/glfw3dll.lib
    bin/glfw3.dll
    licenses/LICENSE.md
  freetype/
    include/freetype2/
    lib/freetype.lib
    bin/freetype.dll
    licenses/LICENSE.TXT
  rmlui/
    include/RmlUi/
    lib/rmlui.lib
    bin/rmlui.dll
    licenses/LICENSE.txt
  openal-soft/
    include/AL/
    lib/OpenAL32.lib
    bin/OpenAL32.dll
    licenses/COPYING
```

현재 정책:

- GLFW는 공식 Windows x64 사전 빌드 패키지를 사용한다.
- FreeType은 선택적 압축/셰이핑 의존성을 끄고 공유 Release 라이브러리로 한 번 빌드한다.
- RmlUi는 FreeType 폰트 엔진을 사용하고 samples, Lua, Lottie, SVG plugin을 끈 공유 Release 라이브러리로 한 번 빌드한다.
- OpenAL Soft는 공식 Windows x64 바이너리 패키지를 사용한다.
- OpenAL Soft 바이너리 패키지가 `soft_oal.dll`을 제공하면, 앱 로컬 DLL이 사용되도록 x64 DLL을 `OpenAL32.dll` 이름으로 복사한다.
- 서드파티 버전, 컴파일 옵션, 런타임 옵션, 아키텍처가 바뀌면 일반 게임 빌드 밖에서 해당 라이브러리를 다시 빌드하고 `third_party/prebuilt` 아래 파일만 교체한다.

일회성 로컬 사전 빌드 생성용 참고 빌드 옵션:

```text
FreeType:
  BUILD_SHARED_LIBS=ON
  CMAKE_BUILD_TYPE=Release
  FT_DISABLE_ZLIB=ON
  FT_DISABLE_BZIP2=ON
  FT_DISABLE_PNG=ON
  FT_DISABLE_HARFBUZZ=ON
  FT_DISABLE_BROTLI=ON
  FT_ENABLE_ERROR_STRINGS=OFF

RmlUi:
  BUILD_SHARED_LIBS=ON
  CMAKE_BUILD_TYPE=Release
  RMLUI_SAMPLES=OFF
  RMLUI_LUA_BINDINGS=OFF
  RMLUI_LOTTIE_PLUGIN=OFF
  RMLUI_SVG_PLUGIN=OFF
  RMLUI_FONT_ENGINE=freetype
  RMLUI_PRECOMPILED_HEADERS=OFF
  RMLUI_COMPILER_OPTIONS=OFF
```

`DOLBUTO.exe` 옆에 복사되는 런타임 DLL:

```text
glfw3.dll
freetype.dll
rmlui.dll
OpenAL32.dll
```

## Debug 빌드

Debug 빌드는 개발 편의성을 우선한다.

- 프로젝트 루트의 `assets` 사용
- 프로젝트 루트의 `config` 사용
- 프로젝트 루트의 `saves/world` 사용
- 빌드 폴더의 컴파일된 `shaders` 사용
- 콘솔 창 유지

Debug는 로그 확인과 디버깅을 위한 구성으로 본다.

## Release 빌드

Release 빌드는 배포 형태에 가깝게 동작한다.

- 실행 파일 옆의 `assets` 사용
- 실행 파일 옆의 `config` 사용
- 실행 파일 옆의 `shaders` 사용
- 실행 파일 옆의 `saves/world` 사용
- Windows 서브시스템으로 링크해 콘솔 창을 띄우지 않는다.
- 기존 `main()` 진입점을 유지하기 위해 `mainCRTStartup`을 사용한다.

## 포터블 배포

별도의 포터블 생성 타깃은 사용하지 않는다.
Release 출력 폴더 자체를 폴더째 이동 가능한 포터블 실행 단위로 본다.

```text
out/build/.../Release/
  DOLBUTO.exe
  glfw3.dll
  freetype.dll
  rmlui.dll
  OpenAL32.dll
  assets/
  config/
  shaders/
  saves/
```

배포할 때는 Release 출력 폴더 이름을 원하는 배포명으로 바꾸거나 압축한다.

## 아이콘

- 탐색기 실행 파일 아이콘은 `assets/textures/icon/icon.ico`를 Windows 리소스로 연결한다.
- 창 제목/작업 표시줄 아이콘은 실행 중 `assets/textures/icon/icon.png`를 GLFW에 전달한다.
- `assets/textures/icon/make_icon.py`는 `icon.png`에서 `icon.ico`를 생성한다.
- 아이콘 생성은 픽셀 느낌 유지를 위해 `NEAREST` 리사이즈를 사용한다.

관련 문서: [[runtime-paths]]
