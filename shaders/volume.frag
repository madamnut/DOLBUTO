#version 450
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=1) uniform sampler2D scene_depth;
layout(location=0) out vec4 colour;
layout(location=1) out vec2 metadata;
void main() {
    vec2 full=vec2(textureSize(scene_depth,0));
    vec2 half_size=ceil(full*0.5);
    vec2 uv=gl_FragCoord.xy/half_size;
    float d=texture(scene_depth,uv).r;
    vec3 direction=camera_ray(uv);
    float distance=d<0.999999 ? length(scene_position(uv,d)) : 8192.0;
    float first;
    colour=cloud_ray(direction,distance,first);
    metadata=vec2(distance,first);
}
