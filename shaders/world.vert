#version 460
#ifdef TERRAIN_BATCH
#extension GL_EXT_buffer_reference : require
layout(buffer_reference, std430, buffer_reference_align=4) readonly buffer FaceData { uint values[]; };
struct ChunkData { vec4 offset; vec4 target; FaceData faces; FaceData lights; };
layout(buffer_reference, std430, buffer_reference_align=16) readonly buffer DrawTable { ChunkData chunks[]; };
layout(push_constant) uniform BatchPush { mat4 view_projection; DrawTable table; uint first_draw; } batch;
struct LocalPush { mat4 view_projection; vec4 offset; vec4 target; };
#else
layout(constant_id = 1) const bool shadow_pass = false;
layout(set = 1, binding = 0, std430) readonly buffer Faces { uint faces[]; };
layout(set = 1, binding = 1, std430) readonly buffer Lights { uint lights[]; };
layout(push_constant) uniform Push { mat4 view_projection; vec4 offset; vec4 target; } pc;
#endif
layout(location = 0) out vec2 uv;
layout(location = 1) flat out uint material;
layout(location = 2) out float shade;
layout(location = 3) flat out uint selected;
layout(location = 4) out vec2 face_uv;
layout(location = 5) out vec3 relative_position;
layout(location = 6) flat out vec3 face_normal;
layout(location = 7) out float sky_visibility;
layout(location = 8) out vec3 light_channels;
layout(location = 9) flat out uint water_contact;
const vec2 face_corners[4] = vec2[4](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,1));
float water_corner_y(uint face, uint axis, uint corner) {
    float level=float(((face>>18u)&15u)+1u);
    bool full=(face&(1u<<30u))!=0u;
    float baseTop=full ? 1.0 : level*(0.875/16.0);
    if(axis==1u) {
        if(((face>>14u)&1u)==0u) return 0.0;
        uint code=(face>>(22u+2u*corner))&3u;
        return baseTop+(code==1u ? -0.5 : code==2u ? 0.5 : 0.0)*(0.875/16.0);
    }
    vec2 uvCorner=face_corners[corner];
    uint endpoint=uint(axis==0u ? uvCorner.y : uvCorner.x);
    uint code=(face>>(26u+2u*endpoint))&3u;
    float top=baseTop+(code==1u ? -0.5 : code==2u ? 0.5 : 0.0)*(0.875/16.0);
    float bottom=0.0;
    if((face&(1u<<31u))!=0u) {
        float neighbour=float(((face>>22u)&15u)+1u);
        bottom=neighbour*(0.875/16.0);
        if(!full && code!=0u && level==neighbour+1.0) bottom=top;
    }
    return mix(bottom,top,axis==0u ? uvCorner.x : uvCorner.y);
}
vec3 water_top_corner(uint face,uint corner) {
    return vec3(face_corners[corner].y,water_corner_y(face,1u,corner),face_corners[corner].x);
}
void main() {
#ifdef TERRAIN_BATCH
    ChunkData chunk = batch.table.chunks[batch.first_draw + gl_DrawID];
    LocalPush pc = LocalPush(batch.view_projection, chunk.offset, chunk.target);
    uint face = chunk.faces.values[gl_InstanceIndex];
#else
    uint face = faces[gl_InstanceIndex];
#endif
    material = (face >> 15u) & 7u;
    if (material <= 2u && (face & (1u << 27u)) != 0u) material += 8u;
    bool fluid_face = material == 5u || material == 7u;
    water_contact = fluid_face ? 0u : (face >> 28u) & 1u;
    vec3 position = vec3(face & 15u, (face >> 4) & 15u, (face >> 8) & 15u);
    selected = pc.target.w > 0.0 && all(equal(position,pc.target.xyz)) ? 1u : 0u;
    uint axis = (face >> 12) & 3u;
    uint positive = (face >> 14) & 1u;
    uint u = (axis + 1u) % 3u, v = (axis + 2u) % 3u;
    const uint normal_order[6] = uint[6](0, 1, 2, 0, 2, 3);
    const uint flipped_order[6] = uint[6](0, 1, 3, 1, 2, 3);
    uint index = uint(gl_VertexIndex);
    if (positive == 0u && index % 3u != 0u)
        index = (index / 3u) * 3u + 3u - index % 3u;
    uint corner = !fluid_face && material != 10u && ((face >> 26) & 1u) != 0u ? flipped_order[index] : normal_order[index];
    const vec2 corners[4] = vec2[4](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1));
    position[axis] += float(positive);
    position[u] += corners[corner].x;
    position[v] += corners[corner].y;
    if (fluid_face)
        position.y = float((face >> 4) & 15u) + water_corner_y(face,axis,corner);
    if (material == 10u) {
        float base_y = float((face >> 4u) & 15u);
        uint layers = ((face >> 29u) & 7u) + (((face >> 26u) & 1u) << 3u) + 1u;
        position.y = base_y + (position.y - base_y) * (float(layers) / 16.0);
    }
    face_uv = corners[corner];
    uv = vec2(axis == 0u ? position.z : position.x, axis == 1u ? position.z : -position.y);
    float ao = fluid_face ? 3.0 : float((face >> (18u + 2u * corner)) & 3u);
    float directional = axis == 1u ? (positive != 0u ? 1.0 : 0.48) : (axis == 0u ? 0.78 : 0.65);
#ifdef TERRAIN_BATCH
    uint light = (chunk.lights.values[gl_InstanceIndex] >> (corner * 8u)) & 255u;
#else
    uint light = (lights[gl_InstanceIndex] >> (corner * 8u)) & 255u;
#endif
    float sky = float(light & 15u) / 15.0;
    sky_visibility = sky;
    relative_position = position + pc.offset.xyz;
    face_normal = vec3(0);
    face_normal[axis] = positive != 0u ? 1.0 : -1.0;
    if(fluid_face && axis==1u && positive!=0u) {
        uint triangle=(index/3u)*3u;
        vec3 a=water_top_corner(face,normal_order[triangle]);
        vec3 b=water_top_corner(face,normal_order[triangle+1u]);
        vec3 c=water_top_corner(face,normal_order[triangle+2u]);
        face_normal=normalize(cross(b-a,c-a));
    }
    float block = float(light >> 4u) / 15.0;
    light_channels = vec3(sky, block, 0.45 + 0.55 * ao / 3.0);
    float illumination = max(sky * pc.offset.w, block);
    illumination = 0.035 + 0.965 * illumination * illumination;
    shade = material == 8u ? 1.0 : directional * (0.45 + 0.55 * ao / 3.0) * illumination;
    gl_Position = pc.view_projection * vec4(position + pc.offset.xyz, 1.0);
#ifndef TERRAIN_BATCH
    if (shadow_pass) {
        float distortion=length(gl_Position.xy)*pc.offset.w+1.0-pc.offset.w;
        gl_Position.xy/=distortion;
        gl_Position.z=0.5+(gl_Position.z-0.5)*0.2;
    }
#endif
}
