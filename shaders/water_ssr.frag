#version 460
#extension GL_GOOGLE_include_directive : require
#include "water_common.glsl"
#include "water_trace.glsl"
layout(location = 0) out vec4 colour;
layout(location = 1) out vec2 metadata;
void main() {
#ifdef LOD_WATER
    if(outside_lod_water()) discard;
#endif
    vec2 screen = gl_FragCoord.xy / water.sizes.zw;
    vec3 normal = material == 9u ? face_normal : water_normal(1.0);
    if(water.flags.w>0.5 && dot(face_normal,relative_position)<=0.0) discard;
    // This pass has its own 75%-per-axis water depth, plus opaque occlusion.
    if (gl_FragCoord.z > texture(scene_depth, screen).r) discard;
    // Signed radial distance distinguishes horizontal water faces from side faces.
    float distance_to_water = length(relative_position) * (abs(face_normal.y) > 0.5 ? 1.0 : -1.0);
    ReflectionHit hit=ReflectionHit(vec3(0),0.0,-1.0);
    vec3 value=vec3(0);
    if(water.flags.z>0.5) {
        hit=trace_water_reflection(normal);
        value=hit.colour;
    }
    colour=vec4(value,hit.confidence);
    metadata=vec2(distance_to_water,hit.distance);
}
