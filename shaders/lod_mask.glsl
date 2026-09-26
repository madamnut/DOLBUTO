layout(push_constant) uniform Push { mat4 matrix; vec4 offset; vec4 meta; } pc;
#include "lod_coverage.glsl"
layout(location=0) in vec3 relative_position;
layout(location=1) in vec3 local_position;
layout(location=2) flat in vec3 normal;
layout(location=3) flat in uint material;
layout(location=4) flat in float skylight;
bool covered() { return lod_covered(local_position, normal, pc.meta.xy); }
