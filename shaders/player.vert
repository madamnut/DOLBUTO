#version 460
layout(constant_id = 1) const bool shadow_pass = false;
layout(push_constant) uniform Push { mat4 view_projection; vec4 offset; vec4 dimensions; } pc;
layout(location = 0) out float shade;
layout(location = 1) flat out uint material;
layout(location = 5) out vec3 relative_position;
layout(location = 6) flat out vec3 face_normal;
layout(location = 8) out vec3 light_channels;
void main() {
    material = 0u;
    uint face = uint(gl_InstanceIndex);
    uint axis = face / 2u, positive = face % 2u;
    uint u = (axis + 1u) % 3u, v = (axis + 2u) % 3u;
    const uint order[6] = uint[6](0, 1, 2, 0, 2, 3);
    const vec2 corners[4] = vec2[4](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1));
    uint index = uint(gl_VertexIndex);
    if (positive == 0u && index % 3u != 0u)
        index = (index / 3u) * 3u + 3u - index % 3u;
    vec3 position = vec3(0);
    position[axis] = float(positive);
    position[u] = corners[order[index]].x;
    position[v] = corners[order[index]].y;
    shade = axis == 1u ? (positive != 0u ? 1.0 : 0.65) : (axis == 0u ? 0.9 : 0.8);
    shade *= pc.offset.w;
    relative_position = position * pc.dimensions.xyz + pc.offset.xyz;
    face_normal = vec3(0); face_normal[axis] = positive != 0u ? 1.0 : -1.0;
    light_channels = vec3(pc.offset.w, pc.dimensions.w, 1.0);
    gl_Position = pc.view_projection * vec4(position * pc.dimensions.xyz + pc.offset.xyz, 1.0);
    if (shadow_pass) {
        float distortion=length(gl_Position.xy)*pc.offset.w+1.0-pc.offset.w;
        gl_Position.xy/=distortion;
        gl_Position.z=0.5+(gl_Position.z-0.5)*0.2;
    }
}
