#version 460
#extension GL_GOOGLE_include_directive : require
#include "environment.glsl"
layout(location = 5) in vec3 relative_position;
layout(location = 6) flat in vec3 face_normal;
layout(location = 8) in vec3 light_channels;
layout(location = 0) out vec4 colour;
void main() {
    if (env.effects.w > 0.5) { colour = vec4(1); return; }
    colour = vec4(surface_light(relative_position, face_normal, light_channels.x, light_channels.y, 1.0), 1.0);
}
