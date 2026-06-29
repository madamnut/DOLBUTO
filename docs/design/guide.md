# Guide

Guide is the non-reward progression map used to help players learn game systems.

## Input

- `Tab` opens and closes the Guide while in game.
- `Esc` closes the Guide and returns to game.
- Opening the Guide releases the mouse cursor and blocks normal player input.
- World simulation and world rendering continue behind the Guide, matching the inventory overlay behavior.
- Dragging with the left mouse button pans the guide map.
- Mouse wheel zooms the guide map.
- The guide map opens at `1.0` scale, which is the maximum zoom state. Mouse wheel can zoom out to show more of the line.

## Assets

Guide UI frame textures are stored with player UI sprites.

```text
assets/textures/ui/player/guideBG.png
assets/textures/ui/player/guideFG.png
assets/textures/ui/player/guideslot.png
```

Guide entry icons are stored separately from UI frame sprites.

```text
assets/textures/ui/guideicons/
```

The Guide slot sprite is `20 x 20` pixels. The center `16 x 16` pixel area is reserved for the guide icon.

The `guideFG` area acts as the Guide viewport. Guide nodes and links are rendered into that viewport and clipped at the viewport edge, so partially visible nodes remain partially visible instead of disappearing as whole entries.

## Visibility

- Completed guide entries are visible.
- Available but incomplete entries are visible in normal color.
- Locked entries are visible only when they are one direct step beyond the currently available line.
- Deeper locked descendants are hidden.
- Locked entries tint the slot and icon gray.
- Completed entries tint the slot and icon darker.

## Initial Line

- `open_guide`
- `open_inventory`
- `pickup_small_stone`
- `make_stone_blade`
- `make_stone_scraper`
- `make_stone_point`
- `pickup_stone`
- `make_stone_chopper`
- `make_stone_maul`
- `make_stone_pestle`
- `pickup_large_stone`
- `make_stone_anvil`
- `make_stone_mortar`

`open_guide` and `open_inventory` complete from UI input. The stone pickup and make steps complete when the matching item key is present in the player inventory or offhand slot.

The initial stone-age line uses a staggered layout. Pickup entries stay in the `x = 2` column, while derived entries alternate between the `x = 3` and `x = 3.8` columns so `80px` guide slots do not overlap at maximum zoom.

## Completion Notifications

Completing a Guide entry creates a bottom-right HUD notification.

Notification frame textures are stored with player UI sprites.

```text
assets/textures/ui/player/notificationBG.png
assets/textures/ui/player/notificationFG.png
```

The source sprites are scaled by `4x`. `notificationBG.png` renders as a `360 x 152` frame, and `notificationFG.png` renders as a `320 x 112` text area centered inside it.

Notifications enter from the right edge, stack upward, and the bottom notification is the only one that starts its `2` second hold timer. After that timer completes, the bottom notification exits downward. Notifications above it follow the exiting notification downward, and new notifications enter from the right at the current top of the active stack.

## Save Data

Completed guide keys are appended to `player.dat` after the player inventory and offhand item state.
