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

Debug text uses the renderer's native FreeType text path.
Player-facing menu UI uses RmlUi documents under `assets/ui`.

Current screens:

```text
Lobby:
  DOLBUTO
  START
  EXIT

World Select:
  SELECT WORLD
  saved world list
  NEW WORLD
  EXIT

World Create:
  NEW WORLD
  new world name
  new world seed
  CREATE
  EXIT

Game:
  ESC -> Pause

Pause:
  RESUME
  EXIT
```

Lobby `START` opens the world selection screen.
Lobby `EXIT` closes the program.
World Select opens an existing world by double-clicking a world row or navigates to the new world creation screen.
The world list is a taller scrollable list with a visible draggable scrollbar, mouse wheel scrolling, row hover feedback, and double-click activation.
World Select `NEW WORLD` opens the new world creation screen.
World Select `EXIT` returns to the lobby.
World Create `CREATE` creates the new world and enters the game.
World Create `EXIT` returns to world selection.
Pause `RESUME` returns to the game.
Pause `EXIT` unloads the game scene and returns to the lobby.

When the lobby or pause overlay is active, player movement, camera rotation, block selection, block editing, and the crosshair are disabled.
Lobby and world selection do not request or process chunk loading.
Lobby and world selection do not render the game scene; sky, terrain, fluid, player, selection, climate overlay, crosshair, and debug text are skipped while either screen is active.
Pause keeps chunk loading active while blocking player input.
Pause keeps the game scene rendered behind the pause overlay, but the upper-left debug text is hidden.
The first game frame after leaving the lobby forces the initial terrain load even when the player remains in the default center chunk group.

The lobby and game scene lifetimes are separated.
The renderer remains alive while the lobby is shown so the swapchain, menu textures, and font atlas stay available.
The game scene owns terrain and save workers, loaded runtime chunks, terrain meshes, terrain job queues, and terrain worker progress.
Starting the game starts the game scene workers and resets the terrain load request flag.
Returning from pause to the lobby saves player state, stops terrain workers, enqueues all runtime chunks for saving, drains the save worker, waits for the device to become idle, destroys loaded terrain render data, clears runtime chunk state, and resets the terrain load request flag.
Starting the game from the lobby begins terrain loading again on the first game frame.

RmlUi menu documents:

```text
assets/ui/lobby.rml
assets/ui/world_select.rml
assets/ui/world_create.rml
assets/ui/pause.rml
assets/ui/style.rcss
```

The world selection document has a saved-world list and bottom actions.
Saved-world rows display creation time and last played time.
The world creation document has name and seed inputs.
World list entries are populated from per-world save directories under `saves`.

## Runtime Integration

The first RmlUi integration is implemented inside `Renderer` so it can reuse the existing Vulkan device, render pass, descriptor set layout, sampler, texture upload path, and runtime asset path helpers.

Renderer owns:

- RmlUi initialization and shutdown
- RmlUi context lifetime
- Vulkan `Rml::RenderInterface` implementation
- UI geometry upload buffers
- dedicated RmlUi graphics pipeline
- UI texture loading through the existing texture upload path
- menu document visibility by screen mode

Application forwards GLFW mouse, text, and basic key input to the RmlUi context while not in the game screen.
RmlUi click events are consumed by Application as menu actions.
Menu documents currently use explicit absolute positioning for major layout blocks because that is more predictable in the current RmlUi integration than browser-style stacked layout with automatic margins.
Relative image paths from RmlUi documents are resolved against `assets/ui`, so references such as `../textures/ui/Title.png` load through the normal asset tree.
If RmlUi provides an already-joined but invalid absolute texture path, the renderer remaps any `/textures/...` suffix back under `assetDirectory()/textures`.
The lobby and world menu background is owned by RmlUi and uses an image decorator with repeat mode instead of the native menu sprite fallback.
The pause menu keeps the game scene visible behind a semi-transparent black RmlUi overlay.

The RmlUi backend sources under `third_party/RmlUi/Backends` are reference code.
Runtime code should copy or adapt needed ideas into the engine side instead of depending directly on sample backend targets.
The provided Vulkan backend owns its own Vulkan instance and swapchain, so it should not be wired directly into the current renderer.

## Notes

The native menu overlay remains as a fallback path if RmlUi initialization fails.
Per-world save directories are selected through the RmlUi world list and new-world creation flow.

## Related Documents

- [[rendering]]
- [[runtime-paths]]
- [[build-and-distribution]]
