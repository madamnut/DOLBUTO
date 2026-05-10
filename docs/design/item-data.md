# Item Data

This document defines the current item data and block drop format.
Runtime item loading, block drop spawning, key-triggered dropped-item pickup, and pickup insertion into the runtime player inventory are implemented.
Inventory saving is implemented in `player.dat`.
Held-item rendering is not implemented yet.

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
  "slotTexture": "rock_chunk",
  "droppedRender": {
    "type": "extruded_sprite",
    "texture": "rock_chunk"
  },
  "heldRender": {
    "type": "extruded_sprite",
    "texture": "rock_chunk"
  },
  "tags": [],
  "actions": {}
}
```

Field meanings:

- `id`: unsigned numeric item id
- `key`: stable `snake_case` item key
- `name`: player-facing display name
- `stackSize`: maximum stack count
- `slotTexture`: inventory and hotbar slot texture name without extension
- `droppedRender.type`: render type used when the item is dropped in the world
- `droppedRender.texture`: texture name used by the dropped-world item render state
- `heldRender.type`: render type used when the item is held by the player
- `heldRender.texture`: texture name used by the held-player item render state
- `tags`: item classification tags for later systems
- `actions`: item action parameters for later systems

`id = 0` is reserved for `none`.
Real items start at `id = 1` and are assigned sequentially unless there is a concrete reason to leave a gap.
The current initial render type for every item is `extruded_sprite`.
Dropped `extruded_sprite` items currently render through a dedicated item pipeline as thin horizontal world-space 3D sprite-derived meshes.
Held item rendering is not implemented yet, but its render data is kept separate in item JSON and runtime data.
The current mesh uses top/bottom sprite faces and generated edge faces from the sprite alpha boundary.
The current dropped sprite footprint is `0.68 x 0.68` blocks.
Dropped-item runtime position is the item's center-bottom contact point.
Drop spawning starts around the broken block center, which is `x, y + 0.5, z` because block coordinates represent the block center-bottom point.
Dropped-item spawn offset, initial velocity, airborne rotation, and spin use runtime random values, so the throw direction is not deterministic.
Dropped-item physics runs at 20 ticks per second and rendering interpolates between the previous and current physics positions.
While falling, dropped items use the same gravity value and vertical velocity formula as player ground movement: `velocityY -= gravity * dt`.
Horizontal drag, floor collision, coarse side collision, and X/Y/Z render rotation remain item-specific.
After landing, dropped items stop moving, reset X/Z rotation so they lie flat, keep a random Y rotation, and rest on the terrain surface without bobbing.
If the supporting block is removed later, the grounded item becomes airborne again and falls on the next item physics tick.
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
  { "id": 0, "key": "none", "name": "None", "stackSize": 0, "slotTexture": "none", "droppedRender": { "type": "extruded_sprite", "texture": "none" }, "heldRender": { "type": "extruded_sprite", "texture": "none" }, "tags": [], "actions": {} },

  { "id": 1, "key": "rock_chunk", "name": "Rock Chunk", "stackSize": 99, "slotTexture": "rock_chunk", "droppedRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "heldRender": { "type": "extruded_sprite", "texture": "rock_chunk" }, "tags": [], "actions": {} },
  { "id": 2, "key": "dirt_pile", "name": "Dirt Pile", "stackSize": 99, "slotTexture": "dirt_pile", "droppedRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "dirt_pile" }, "tags": [], "actions": {} },
  { "id": 3, "key": "sand_pile", "name": "Sand Pile", "stackSize": 99, "slotTexture": "sand_pile", "droppedRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "heldRender": { "type": "extruded_sprite", "texture": "sand_pile" }, "tags": [], "actions": {} },

  { "id": 4, "key": "plant", "name": "Plant", "stackSize": 99, "slotTexture": "plant", "droppedRender": { "type": "extruded_sprite", "texture": "plant" }, "heldRender": { "type": "extruded_sprite", "texture": "plant" }, "tags": [], "actions": {} },
  { "id": 5, "key": "plant_fiber", "name": "Plant Fiber", "stackSize": 99, "slotTexture": "plant_fiber", "droppedRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_fiber" }, "tags": [], "actions": {} },
  { "id": 6, "key": "plant_twine", "name": "Plant Twine", "stackSize": 99, "slotTexture": "plant_twine", "droppedRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "heldRender": { "type": "extruded_sprite", "texture": "plant_twine" }, "tags": [], "actions": {} },
  { "id": 7, "key": "seed", "name": "Seed", "stackSize": 99, "slotTexture": "seed", "droppedRender": { "type": "extruded_sprite", "texture": "seed" }, "heldRender": { "type": "extruded_sprite", "texture": "seed" }, "tags": [], "actions": {} },
  { "id": 8, "key": "grass_scrap", "name": "Grass Scrap", "stackSize": 99, "slotTexture": "grass_scrap", "droppedRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "heldRender": { "type": "extruded_sprite", "texture": "grass_scrap" }, "tags": [], "actions": {} },

  { "id": 9, "key": "branch", "name": "Branch", "stackSize": 99, "slotTexture": "branch", "droppedRender": { "type": "extruded_sprite", "texture": "branch" }, "heldRender": { "type": "extruded_sprite", "texture": "branch" }, "tags": [], "actions": {} },
  { "id": 10, "key": "bough", "name": "Bough", "stackSize": 99, "slotTexture": "bough", "droppedRender": { "type": "extruded_sprite", "texture": "bough" }, "heldRender": { "type": "extruded_sprite", "texture": "bough" }, "tags": [], "actions": {} },
  { "id": 11, "key": "bark_strip", "name": "Bark Strip", "stackSize": 99, "slotTexture": "bark_strip", "droppedRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "heldRender": { "type": "extruded_sprite", "texture": "bark_strip" }, "tags": [], "actions": {} },
  { "id": 12, "key": "leaf", "name": "Leaf", "stackSize": 99, "slotTexture": "leaf", "droppedRender": { "type": "extruded_sprite", "texture": "leaf" }, "heldRender": { "type": "extruded_sprite", "texture": "leaf" }, "tags": [], "actions": {} }
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
- empty `key`, `name`, `slotTexture`, `droppedRender.texture`, or `heldRender.texture` is an error
- `key`, `slotTexture`, `droppedRender.texture`, and `heldRender.texture` should be lowercase `snake_case`
- `stackSize` must be nonzero for real items
- `droppedRender.type` and `heldRender.type` must be valid item render types

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
Spawned dropped items are chunk-owned `WorldEntity` entries with `type = DroppedItem`.
Pressing `F` while looking at a dropped item within the normal 8-block interaction range marks it for pickup.
The item accelerates toward the player collider center at half player height.
When its dropped-item bounds touch the player collider, it is inserted into the runtime player inventory if space is available.
If insertion fails, the dropped item remains in the world with the remaining count.
Dropped item entities are saved in the owning chunk payload with `entityId`, local position, velocity, grounded flag, `itemId`, and `count`.
Pickup-in-progress state and render-only rotation/spin are not saved.

## Runtime Inventory

The current runtime player inventory has 50 slots.
Slot indices `0` through `9` are the hotbar slots.
The inventory screen displays all 50 slots as ten columns and five rows.
The bottom row is slots `0` through `9`; rows above it are `10` through `19`, `20` through `29`, `30` through `39`, and `40` through `49`.

Pickup insertion currently:

- merges into existing matching stacks across all 50 slots
- fills matching stacks in slot index order `0` through `49`
- fills empty slots in slot index order `0` through `49`
- saves the 50 runtime slots in `saves/<world-name>/player.dat`

Inventory UI manipulation uses a transient cursor `ItemStack`.
The cursor stack is not saved and is returned to the runtime inventory when the inventory screen closes.
The inventory tooltip displays current runtime item data from `ItemStack` and `ItemDefinition`; per-instance overrides are not implemented yet.

The slot-position debug overlay code remains available, but the overlay is disabled by default.

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
