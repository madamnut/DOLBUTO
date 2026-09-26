// Frozen pre-optimization implementation. Renaming isolates it from the game's implementation.
#define local_column_light reference_local_column_light
#define connect_column_light reference_connect_column_light
#define update_column_lights reference_update_column_lights
#define face_light reference_face_light
#define MeshLightWorker ReferenceMeshLightWorker
#define LightWorker ReferenceLightWorker
#include "baseline.inc"
