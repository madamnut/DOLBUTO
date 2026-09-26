#version 460
#extension GL_GOOGLE_include_directive : require
layout(location=0) in uvec4 face;
layout(push_constant) uniform Push { mat4 matrix; vec4 offset; vec4 meta; } pc;
layout(location=0) out vec2 uv;
layout(location=1) flat out uint material;
layout(location=2) out float shade;
layout(location=3) flat out uint selected;
layout(location=4) out vec2 face_uv;
layout(location=5) out vec3 relative_position;
layout(location=6) flat out vec3 face_normal;
layout(location=7) out float sky_visibility;
layout(location=8) out vec3 light_channels;
layout(location=10) out vec3 local_position;
#include "lod_geometry.glsl"
void main() {
    lod_geometry(local_position,face_normal,material);
    relative_position=local_position+pc.offset.xyz;
    uv=local_position.xz; face_uv=vec2(.5); selected=0u; shade=1.0;
    sky_visibility=float(face.w)/15.0;
    light_channels=vec3(sky_visibility,0,1);
    gl_Position=pc.matrix*vec4(relative_position,1);
}
