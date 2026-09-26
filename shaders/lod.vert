#version 460
#extension GL_GOOGLE_include_directive : require
layout(constant_id=1) const bool shadow_pass=false;
layout(location=0) in uvec4 face;
layout(push_constant) uniform Push { mat4 matrix; vec4 offset; vec4 meta; } pc;
layout(location=0) out vec3 relative_position;
layout(location=1) out vec3 local_position;
layout(location=2) flat out vec3 normal;
layout(location=3) flat out uint material;
layout(location=4) flat out float skylight;
#include "lod_geometry.glsl"
void main() {
    lod_geometry(local_position, normal, material);
    skylight=float(face.w)/15.0;
    relative_position=local_position+pc.offset.xyz;
    gl_Position=pc.matrix*vec4(relative_position,1);
    if(shadow_pass){gl_Position.xy/=length(gl_Position.xy)*pc.meta.z+1.0-pc.meta.z;gl_Position.z=.5+(gl_Position.z-.5)*.2;}
}
