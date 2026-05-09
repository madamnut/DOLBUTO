# Prebuilt Third-Party Libraries

`DOLBUTO` links these libraries as prebuilt binaries so normal game builds do not rebuild them.

Expected layout:

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

Build policy:

- GLFW: official Windows x64 prebuilt binary package.
- FreeType: shared Release build, optional compression/shaping dependencies disabled.
- RmlUi: shared Release build, FreeType font engine, samples/tests/plugins disabled.
- OpenAL Soft: official Windows x64 binary package.

For OpenAL Soft, the official Windows package may provide the implementation DLL as `soft_oal.dll`.
Copy the x64 DLL next to the game as `OpenAL32.dll`, matching the normal Windows OpenAL lookup name.

The game CMake configuration fails if any required prebuilt file is missing.
