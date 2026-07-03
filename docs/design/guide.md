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

- `open_guide`: Where Am I?
- `open_inventory`: Now What?
- `gather_stones`: DOLBUTO!
- `make_stone_blade`: First Edge
- `make_stone_scraper`: Scrape By
- `make_stone_point`: Point Taken
- `make_stone_chopper`: Rough Cut
- `make_stone_maul`: Blunt Solution
- `make_stone_pestle`: Crush and Grind
- `make_stone_anvil`: Hard Place
- `make_stone_mortar`: Hollowed Purpose
- `gather_plant`: Touch Grass
- `make_plant_fiber`: Scrape the Surface
- `make_plant_twine`: String Theory
- `make_long_plant_twine`: Long Story
- `make_short_plant_twine`: Cut Short
- `gather_log`: Timber!
- `make_stripped_log`: Bare Wood
- `gather_bark_strip`: Woof!
- `make_primal_workbench`: Table Manners

`open_guide` and `open_inventory` complete from UI input. Item-based guide steps complete only when an item count increases while that guide step is available. Locked steps ignore earlier item pickups, so unlocking a step does not retroactively complete it from items the player already had.

Multi-item guide steps keep their own internal progress. `gather_stones` completes after the available `DOLBUTO!` step has separately recorded `small_stone`, `stone`, and `large_stone` acquisitions. The plant fiber step has two parents: the plant pickup step and the scraper step.

`Long Story` extends from `String Theory`, because long plant twine is hand-crafted from plant twine. `Cut Short` extends from both `String Theory` and `First Edge`, because short plant twine requires cutting plant twine with a blade.

The wood branch starts from `Rough Cut`, because obtaining a log requires a chopper. `Bare Wood` and `Woof!` both require `Timber!` and `Scrape By`, because stripping a log requires both a log and a scraper. `Table Manners` requires `Bare Wood` and `First Edge`, because the primal workbench is carved from a stripped log.

The initial stone-age line uses a staggered layout. The combined stone gathering step branches into stone tools, while the plant branch starts from inventory and rejoins the scraper step before plant fiber. The wood branch extends from the chopper and then rejoins the blade and scraper tools for bark stripping and workbench carving.

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

Completed guide keys are written to `player.dat` after the player inventory and offhand item state.

Incomplete item-based guide progress is stored after completed guide keys as per-guide obtained item keys. The save format does not preserve legacy observed-item history.
