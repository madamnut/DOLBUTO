layout(set=1,binding=0,std430) readonly buffer Coverage { ivec4 centre; vec4 palette[11]; uint mask[]; } coverage;
bool lod_covered(vec3 local_position, vec3 normal, vec2 origin) {
    ivec2 column=ivec2(origin)+ivec2(floor((local_position.xz-normal.xz*.001)/16.0));
    ivec2 delta=((column-coverage.centre.xy+ivec2(12288))%8192)-ivec2(4096)+ivec2(64);
    return all(greaterThanEqual(delta,ivec2(0)))&&all(lessThan(delta,ivec2(129)))&&coverage.mask[delta.x+delta.y*129]!=0u;
}
