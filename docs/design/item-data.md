# Item Data

This document defines the current item data and block drop format.
Runtime item loading, block drop spawning, and key-triggered dropped-item pickup are implemented.
Inventory behavior, item saving, and held-item rendering are not implemented yet.

## Definition File

Item definitions use one JSON file:

```text
assets/data/items.json
```

Item textures are stored in:

```text
assets/textures/item/*.png
```

## Item Identity

Items are separate from blocks.
A block named `plant` and an item named `Plant` are different data entries.
Breaking or interacting with a block may create item drops, but the block is not automatically the item itself.

Each item has three identity-related fields:

- `id`: numeric primary key used by runtime systems, save data, inventory, and resolved drop results
- `key`: stable string key used by data authoring, debug tools, console commands, and lookup helpers
- `name`: display name used by UI and logs; it can change later

`name` is not used as a lookup key.

## JSON Format

```json
{
  "id": 1,
  "key": "rock_chunk",
  "name": "Rock Chunk",
  "stackSize": 99,
  "texture": "rock_chunk",
  "render": {
    "dropped": "sprite",
    "held": "sprite"
  }
}
```

Field meanings:

- `id`: unsigned numeric item id
- `key`: stable `snake_case` item key
- `name`: player-facing display name
- `stackSize`: maximum stack count
- `texture`: item texture name without extension
- `render.dropped`: render type when the item exists as a dropped world item
- `render.held`: render type when the item is held by the player

`id = 0` is reserved for `none`.
Real items start at `id = 1` and are assigned sequentially unless there is a concrete reason to leave a gap.
The current initial render type for every item is `sprite`.
Dropped `sprite` items currently render through a dedicated item pipeline as thin horizontal world-space 3D sprites.
The current mesh uses top/bottom sprite faces and generated edge faces from the sprite alpha boundary.
The current dropped sprite footprint is `0.68 x 0.68` blocks.
After landing, dropped items stop moving and rest on the terrain surface without bobbing.
Side faces are generated only where an opaque sprite pixel touches a transparent neighbor or the texture border.
Neighboring side edges in the same direction are merged into spans before rendering, so the dropped item keeps the sprite silhouette without creating one side quad per boundary pixel.
Side-face UVs sample from the opaque pixel center instead of the exact alpha boundary to avoid transparent-edge filtering artifacts.

## Naming Rules

Use these naming rules for item keys and texture names:

- `key`: lowercase `snake_case`
- `texture`: lowercase `snake_case`
- texture file: `{texture}.png`
- `name`: readable display text with spaces and capitalization

Example:

```text
key      rock_chunk
name     Rock Chunk
texture  rock_chunk
file     assets/textures/item/rock_chunk.png
```

The initial source sprites use lowercase `snake_case` filenames.

```text
bark_strip.png
bough.png
branch.png
dirt_pile.png
grass_scrap.png
leaf.png
plant_fiber.png
plant_twine.png
plant.png
rock_chunk.png
sand_pile.png
seed.png
```

## Initial Item Draft

```json
[
  { "id": 0, "key": "none", "name": "None", "stackSize": 0, "texture": "none", "render": { "dropped": "sprite", "held": "sprite" } },

  { "id": 1, "key": "rock_chunk", "name": "Rock Chunk", "stackSize": 99, "texture": "rock_chunk", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 2, "key": "dirt_pile", "name": "Dirt Pile", "stackSize": 99, "texture": "dirt_pile", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 3, "key": "sand_pile", "name": "Sand Pile", "stackSize": 99, "texture": "sand_pile", "render": { "dropped": "sprite", "held": "sprite" } },

  { "id": 4, "key": "plant", "name": "Plant", "stackSize": 99, "texture": "plant", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 5, "key": "plant_fiber", "name": "Plant Fiber", "stackSize": 99, "texture": "plant_fiber", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 6, "key": "plant_twine", "name": "Plant Twine", "stackSize": 99, "texture": "plant_twine", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 7, "key": "seed", "name": "Seed", "stackSize": 99, "texture": "seed", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 8, "key": "grass_scrap", "name": "Grass Scrap", "stackSize": 99, "texture": "grass_scrap", "render": { "dropped": "sprite", "held": "sprite" } },

  { "id": 9, "key": "branch", "name": "Branch", "stackSize": 99, "texture": "branch", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 10, "key": "bough", "name": "Bough", "stackSize": 99, "texture": "bough", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 11, "key": "bark_strip", "name": "Bark Strip", "stackSize": 99, "texture": "bark_strip", "render": { "dropped": "sprite", "held": "sprite" } },
  { "id": 12, "key": "leaf", "name": "Leaf", "stackSize": 99, "texture": "leaf", "render": { "dropped": "sprite", "held": "sprite" } }
]
```

## Runtime Lookup

The loader should build these indexes:

```cpp
std::vector<ItemData> itemsById;
std::unordered_map<std::string, uint16_t> itemIdByKey;
```

Loader validation:

- duplicate `id` is an error
- duplicate `key` is an error
- missing `id = 0`, `key = "none"` is an error
- empty `key`, `name`, or `texture` is an error
- `key` and `texture` should be lowercase `snake_case`
- `stackSize` must be nonzero for real items
- `render.dropped` and `render.held` must be valid item render types

## Block Drops

Drop tables are stored inside block definitions in `assets/data/blocks.json`.
They are not stored in a separate `drop_tables.json` file.

Block drop JSON uses item `key` for authoring readability.
The loader resolves each key to an item id after loading `items.json`.
Runtime systems use the resolved item id.

```json
{
  "id": 10000,
  "name": "plant",
  "renderType": "cross",
  "drops": [
    { "item": "plant", "min": 1, "max": 1, "chance": 1.0 },
    { "item": "plant_fiber", "min": 1, "max": 2, "chance": 0.35 },
    { "item": "seed", "min": 1, "max": 1, "chance": 0.05 }
  ]
}
```

Drop entry fields:

- `item`: item `key`
- `min`: minimum dropped count
- `max`: maximum dropped count
- `chance`: probability from `0.0` to `1.0`

Blocks with no item drops still define an empty `drops` array.

When a block is broken, each drop entry rolls `chance` first.
If it succeeds, the final count is a uniform integer random value from `min` to `max`, inclusive.
Spawned dropped items are runtime-only entities for now and are cleared when the game scene unloads.
Pressing `F` while looking at a dropped item within the normal 8-block interaction range marks it for pickup.
The item accelerates toward the player collider center at half player height and disappears when its dropped-item bounds touch the player collider.
This currently removes the runtime drop only; it does not insert the item into inventory yet.

## Initial Drop Draft

The exact balancing values are draft values.

```json
{
  "rock": [
    { "item": "rock_chunk", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "dirt": [
    { "item": "dirt_pile", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "sand": [
    { "item": "sand_pile", "min": 1, "max": 2, "chance": 1.0 }
  ],
  "sandstone": [],
  "mud": [],
  "clay": [],
  "trunk": [],
  "grass": [
    { "item": "dirt_pile", "min": 1, "max": 1, "chance": 1.0 },
    { "item": "grass_scrap", "min": 1, "max": 1, "chance": 0.25 },
    { "item": "seed", "min": 1, "max": 1, "chance": 0.05 }
  ],
  "plant": [
    { "item": "plant", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "stone": [
    { "item": "rock_chunk", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "branch": [
    { "item": "branch", "min": 1, "max": 1, "chance": 1.0 }
  ],
  "leaves": [
    { "item": "leaf", "min": 1, "max": 1, "chance": 1.0 }
  ]
}
```

Related documents: [[block-data]], [[save-load]], [[ui]]
