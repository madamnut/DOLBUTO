void lod_geometry(out vec3 local_position, out vec3 normal, out uint material) {
    uint axis=(face.x>>8u)&3u, positive=(face.x>>10u)&1u;
    material=(face.x>>11u)&15u;
    uint index=uint(gl_VertexIndex);
    if(positive==0u&&index%3u!=0u)index=index/3u*3u+3u-index%3u;
    const uint order[6]=uint[6](0,1,2,0,2,3);
    const vec2 corners[4]=vec2[4](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,1));
    vec3 unit=vec3(0);unit[axis]=float(positive);
    unit[(axis+1u)%3u]=corners[order[index]].x;unit[(axis+2u)%3u]=corners[order[index]].y;
    local_position=vec3((float(face.x&15u)+unit.x)*pc.offset.w,
                        mix(float(face.y),float(face.z),unit.y)/256.0,
                        (float((face.x>>4u)&15u)+unit.z)*pc.offset.w);
    normal=vec3(0);normal[axis]=positive!=0u?1.0:-1.0;
}
