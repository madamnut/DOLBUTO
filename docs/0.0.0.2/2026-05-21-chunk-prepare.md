# 2026-05-21 Chunk Prepare Worker

## Work Log
- Added `ChunkPrepareSystem` as a separate snapshot-prepare worker after `ChunkLoadSystem`.
- Kept `ChunkLoadSystem` focused on save snapshot IO through `SaveSystem::load`.
- Moved saved snapshot to `RuntimeChunk` restoration and derived cache rebuild off the main thread.
- Main-thread load completion now installs `PreparedChunkLoad` and keeps only runtime-sensitive entity normalization, clean revision marking, load-state install, and frontier resume scheduling.

## Notes
- Build is left to the user.

## Player Render Follow-up
- Added static player mesh rendering through `PlayerVertex` plus per-frame node transform storage buffers.
- Added `player_model.vert` for player/viewmodel skin rendering so the older `player.vert` can remain available for particle-style `TerrainVertex` users.
- Player skin and first-person hand now update transform buffers instead of rewriting their vertex buffers every frame.
