#version 460
#extension GL_GOOGLE_include_directive : require
#include "environment.glsl"
layout(constant_id = 0) const bool lod_debug = false;
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(location = 0) in vec2 uv;
layout(location = 1) flat in uint material;
layout(location = 2) in float shade;
layout(location = 3) flat in uint selected;
layout(location = 4) in vec2 face_uv;
layout(location = 5) in vec3 relative_position;
layout(location = 6) flat in vec3 face_normal;
layout(location = 8) in vec3 light_channels;
layout(location = 9) flat in uint water_contact;
layout(location = 0) out vec4 colour;
vec3 lava_sample(vec2 coordinate) {
    vec2 pixel = clamp(fract(coordinate) * 32.0, vec2(0.5), vec2(31.5));
    return textureLod(atlas, (pixel + vec2(7.0 * 32.0, 0)) / vec2(textureSize(atlas, 0)), 0.0).rgb;
}
vec3 lava_colour() {
    vec3 world = relative_position + env.camera_time.xyz;
    vec2 plane = abs(face_normal.y) > 0.5 ? world.xz
               : abs(face_normal.x) > 0.5 ? vec2(world.z, -world.y) : vec2(world.x, -world.y);
    // Keep the image unrotated; move its pattern 30 degrees at 0.018 tiles per second.
    const vec2 direction = vec2(0.8660254038, 0.5);
    vec3 albedo = lava_sample(plane - direction * (0.018 * env.camera_time.w));
    float luminance = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    // Preserve the texture's dark regions while letting its hot regions feed HDR bloom.
    return pow(albedo, vec3(2.2)) * (1.1 + 3.0 * luminance);
}
void main() {
    if (lod_debug) {
        colour = vec4(vec3(0.55), 1.0);
        return;
    }
    if (material == 7u) {
        colour = vec4(lava_colour(), 1.0);
        return;
    }
    // Clamp within each block's own atlas tile.
    vec2 pixel = clamp(fract(uv) * 32.0, vec2(0.5), vec2(31.5));
    vec2 atlas_uv = (pixel + vec2(float(material) * 32.0, 0.0)) / vec2(textureSize(atlas, 0));
    vec4 texel = texture(atlas, atlas_uv);
    // Compute derivatives before material/light branches so silhouettes do not
    // use undefined derivatives in a divergent caustic branch.
    float footprint=caustic_footprint(relative_position);
    vec3 lighting = material == 8u ? vec3(1) : surface_light(relative_position, face_normal, light_channels.x, light_channels.y, light_channels.z, water_contact != 0u, footprint);
    colour = vec4(material == 8u ? vec3(8.0) : pow(texel.rgb,vec3(2.2)) * lighting, material == 5u ? texel.a * 0.72 : 1.0);
    if (material == 5u) colour.rgb = fog_colour(colour.rgb, relative_position, light_channels.x);
    vec2 edge = min(face_uv,1.0-face_uv);
    vec2 width = max(fwidth(face_uv)*1.5,vec2(0.012));
    if (selected != 0u && any(lessThan(edge,width)))
        colour = vec4(0.96,1.0,0.86,1.0);
}
