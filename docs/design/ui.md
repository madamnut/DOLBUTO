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
Lobby, world selection, world creation, and pause panels are centered in the screen.

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
  bottom-center hotbar HUD
  1-9/0 or mouse wheel -> change selected hotbar slot
  E -> Inventory
  ESC -> Pause

Inventory:
  centered inventory panel
  E / ESC -> Game

Pause:
  RESUME
  EXIT
```

Lobby `START` opens the world selection screen.
Lobby `EXIT` closes the program.
The lobby title image is rendered at `1040 x 280` pixels, centered over the `520px` lobby stack by a negative left margin.
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
Inventory keeps chunk loading and game simulation active while blocking only player input.
Inventory keeps the game scene, debug text, and hotbar rendered behind a semi-transparent black overlay, and releases the mouse cursor.
The in-game HUD is a RmlUi document shown during the normal game screen and inventory screen, and currently contains only the bottom-center hotbar shell.
The hotbar and inventory sprites use the same 4x pixel scale so their slot sizes match; the hotbar selection scope is offset by 3 source pixels from the hotbar left and bottom edges.
Hotbar slots are selected left to right with `1` through `9`, then `0`; the current selection scope uses a 17 source-pixel step between slots at 4x scale.
The runtime inventory has 50 item slots.
Slot `0` through `9` are the hotbar and are shown in both the HUD and the bottom row of the inventory screen.
The inventory screen uses ten columns and five rows; the bottom row is slots `0` through `9`, then rows above are `10` through `19`, `20` through `29`, `30` through `39`, and `40` through `49`.
Inventory slot icons are placed from source-pixel coordinates at 4x scale.
The inventory sprite uses a 4 source-pixel outer padding, 16x16 source-pixel item slots, 1 source-pixel spacing between slots, and a 6 source-pixel gap between the hotbar row and the row above it.
Item stack count text uses a 24px font size and is placed inside each 64x64 slot with a 40px top offset, a 20px text box height, and a 48px right-aligned text box inset 2px from the slot's right edge.
Hotbar item slots always render a semi-transparent black background behind the item icon.
Inventory item slots are transparent by default and render a semi-transparent black background only while hovered; item icons and stack counts render above that hover background.
The debug slot overlay renders gray rectangles with red `0` through `49` slot indices when enabled, but is disabled by default and hidden from RmlUi hit testing while disabled.
Inventory interaction uses a transient cursor slot. The cursor slot is not part of the 50 saved inventory slots and is returned to the runtime inventory when the inventory screen closes.
Inventory left click picks up a full stack, places a full cursor stack, merges matching stacks up to the item stack limit, or swaps different stacks.
Inventory right click picks up half a stack when the cursor is empty, places one item from the cursor into an empty slot, or adds one item to a matching non-full stack.
Shift-click moves the clicked stack to the opposite inventory area: hotbar slots move to the main inventory and main inventory slots move to the hotbar.
While the inventory is open, number keys `1` through `9` and `0` swap the hovered slot with the matching hotbar slot when the cursor slot is empty.
Inventory item tooltips use `assets/textures/ui/Tooltip.png` as a RmlUi `ninepatch` decorator.
The tooltip spritesheet uses a 16x16 outer sprite and a 6px border, so the inner stretch rectangle is `6px 6px 4px 4px`.
Tooltips appear only when the inventory is open, the cursor stack is empty, and the mouse hovers an item slot.
Tooltips display the item name plus debug fields for id, key, count, stack size, slot texture, dropped render, dropped texture, held render, and held texture.
The tooltip background image is alpha-multiplied through RCSS `image-color` so the 9patch background is semi-transparent even when the source PNG is opaque.
Tooltip width is computed from the longest displayed text line, with a minimum width of 180px and maximum width of 520px.
Tooltip placement starts at the mouse position plus 16px and flips/clamps horizontally and vertically so it never extends outside the current framebuffer.
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
assets/ui/hud.rml
assets/ui/inventory.rml
assets/ui/pause.rml
assets/ui/style.rcss
```

Player UI sprites are stored under:

```text
assets/textures/ui/player
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
Inventory is treated as a non-game input screen so mouse movement and clicks go to RmlUi instead of the player camera or block interaction.
Keyboard input forwards GLFW modifier state to RmlUi so text inputs can handle selection and editing shortcuts such as Shift+Arrow and Ctrl+C/V.
The renderer provides a small RmlUi system interface backed by GLFW for elapsed time and clipboard access.
RmlUi click events are consumed by Application as menu actions.
The old native menu hit-test is kept only as a fallback when RmlUi is unavailable; it must not run after normal RmlUi menu clicks.
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
