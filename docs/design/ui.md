# UI

## Current Direction

DOLBUTO uses RmlUi for external-file UI layouts.

RmlUi is included as source under:

```text
third_party/RmlUi
```

The included version is `6.1`.

FreeType is included as source under:

```text
third_party/freetype-2.14.2
```

The included FreeType version is `2.14.2`.

## Build Integration

RmlUi is added through the root `CMakeLists.txt` with `add_subdirectory`.

Current build options:

- `BUILD_SHARED_LIBS = OFF`
- `RMLUI_SAMPLES = OFF`
- `RMLUI_LUA_BINDINGS = OFF`
- `RMLUI_LOTTIE_PLUGIN = OFF`
- `RMLUI_SVG_PLUGIN = OFF`
- `RMLUI_FONT_ENGINE = freetype`
- `RMLUI_PRECOMPILED_HEADERS = OFF`
- `RMLUI_COMPILER_OPTIONS = OFF`

The game target links `Freetype::Freetype` and `RmlUi::Core`.

## Current Runtime State

The current lobby and pause screens are implemented as a minimal native renderer overlay.
This keeps the screen-state flow usable before the RmlUi Vulkan render interface is adapted to the engine renderer.
Debug text and the native lobby/pause button text use FreeType to bake `assets/fonts/VCR_OSD_MONO.ttf` into the renderer font atlas.

Current screens:

```text
Lobby:
  DOLBUTO
  START
  EXIT

Game:
  ESC -> Pause

Pause:
  RESUME
  EXIT
```

Lobby `START` enters the game.
Lobby `EXIT` closes the program.
Pause `RESUME` returns to the game.
Pause `EXIT` unloads the game scene and returns to the lobby.

When the lobby or pause overlay is active, player movement, camera rotation, block selection, block editing, and the crosshair are disabled.
Lobby does not request or process chunk loading.
Pause keeps chunk loading active while blocking player input.
The first game frame after leaving the lobby forces the initial terrain load even when the player remains in the default center chunk group.

The lobby and game scene lifetimes are separated.
The renderer remains alive while the lobby is shown so the swapchain, menu textures, and font atlas stay available.
The game scene owns terrain and save workers, loaded runtime chunks, terrain meshes, terrain job queues, and terrain worker progress.
Starting the game starts the game scene workers and resets the terrain load request flag.
Returning from pause to the lobby saves player state, stops terrain workers, enqueues all runtime chunks for saving, drains the save worker, waits for the device to become idle, destroys loaded terrain render data, clears runtime chunk state, and resets the terrain load request flag.
Starting the game from the lobby begins terrain loading again on the first game frame.

The lobby background tiles `assets/textures/block/rock.png` across the full screen by using repeated sprite UVs.
The lobby title uses `assets/textures/ui/Title.png`.
Lobby background and title sprites use vertically flipped UVs to match the texture orientation expected by the sprite shader path.

## Runtime Plan

Lobby and menu UI files should live under:

```text
assets/ui
```

The first UI integration should add a dedicated `src/ui` module that owns:

- RmlUi initialization and shutdown
- RmlUi context lifetime
- GLFW input forwarding
- Vulkan render interface implementation
- UI resource loading from runtime asset paths

The RmlUi backend sources under `third_party/RmlUi/Backends` are reference code.
Runtime code should copy or adapt needed ideas into the engine side instead of depending directly on sample backend targets.
The provided Vulkan backend owns its own Vulkan instance and swapchain, so it should not be wired directly into the current renderer.

## Notes

RmlUi has FreeType enabled at build time, but RmlUi documents are not rendered yet.
The current visible menu text still uses the renderer's native text path.

## Related Documents

- [[rendering]]
- [[runtime-paths]]
- [[build-and-distribution]]
