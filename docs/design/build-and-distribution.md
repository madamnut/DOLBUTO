# 빌드와 배포

## 빌드 방식

프로젝트는 CMake/Ninja 기반이며 Visual Studio에서 CMake 프로젝트로 열어 빌드한다.
빌드 자체는 사용자가 직접 수행한다.

## 주요 타깃

- `DOLBUTO`: 게임 실행 파일
- `DOLBUTOShaders`: Vulkan 셰이더 컴파일 타깃
- `DOLBUTOPortable`: 포터블 배포 폴더 생성 타깃

## Third-Party Prebuilt Libraries

Third-party libraries that are not expected to be modified are linked as prebuilt binaries.
Game builds should not compile these third-party sources:

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

Current policy:

- GLFW uses the official Windows x64 prebuilt package.
- FreeType is built once as a shared Release library with optional compression/shaping dependencies disabled.
- RmlUi is built once as a shared Release library with the FreeType font engine, no samples, no Lua, no Lottie, and no SVG plugin.
- OpenAL Soft uses the official Windows x64 binary package.
- If the OpenAL Soft binary package provides `soft_oal.dll`, copy the x64 DLL as `OpenAL32.dll` so the app-local DLL is used.
- If a third-party version, compile option, runtime option, or architecture changes, rebuild that library outside the normal game build and replace only the files under `third_party/prebuilt`.

Reference build options for one-time local prebuild generation:

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

The runtime DLLs copied next to `DOLBUTO.exe` are:

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

## Release 계열 빌드

Release, RelWithDebInfo, MinSizeRel은 배포 형태에 가깝게 동작한다.

- 실행 파일 옆의 `assets` 사용
- 실행 파일 옆의 `config` 사용
- 실행 파일 옆의 `shaders` 사용
- 실행 파일 옆의 `saves/world` 사용
- Windows 서브시스템으로 링크해 콘솔 창을 띄우지 않는다.
- 기존 `main()` 진입점을 유지하기 위해 `mainCRTStartup`을 사용한다.

## 포터블 배포

`DOLBUTOPortable` 타깃은 다음 폴더를 생성한다.

```text
dist/DOLBUTO_0.0.0.1/
  DOLBUTO.exe
  glfw3.dll
  assets/
  config/
  shaders/
  saves/world/regions/
```

이 폴더 전체가 포터블 배포 단위다.

## 아이콘

- 탐색기 실행 파일 아이콘은 `assets/textures/icon/icon.ico`를 Windows 리소스로 연결한다.
- 창 제목/작업 표시줄 아이콘은 실행 중 `assets/textures/icon/icon.png`를 GLFW에 전달한다.
- `assets/textures/icon/make_icon.py`는 `icon.png`에서 `icon.ico`를 생성한다.
- 아이콘 생성은 픽셀 느낌 유지를 위해 `NEAREST` 리사이즈를 사용한다.

관련 문서: [[runtime-paths]]
