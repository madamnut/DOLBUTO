#version 460
#extension GL_GOOGLE_include_directive : require
#include "environment.glsl"
#include "lod_mask.glsl"
layout(constant_id=0) const bool lod_debug=false;
layout(location=0) out vec4 colour;
void main() {
    if(covered()||length(relative_position.xz)>pc.meta.z)discard;
    if(lod_debug){
        int level=clamp(int(round(log2(pc.offset.w))),0,9);
        vec3 rgb=coverage.palette[level+1].rgb;
        // Only the X/Z perimeter of this actual tile, never block/triangle edges.
        vec2 border=min(local_position.xz,vec2(16.0*pc.offset.w)-local_position.xz);
        vec2 derivative=fwidth(local_position.xz);
        vec2 pixels=vec2(1e8);
        if(derivative.x>0.00001) pixels.x=border.x/derivative.x;
        if(derivative.y>0.00001) pixels.y=border.y/derivative.y;
        float edge=1.0-smoothstep(0.5,1.5,min(pixels.x,pixels.y));
        colour=vec4(mix(rgb,vec3(.06),edge),1);
        return;
    }
    vec3 albedo=coverage.palette[material].rgb;
    vec3 light=surface_light(relative_position,normal,skylight,material==7u||material==8u?1.0:0.0,1.0,false,1.0);
    vec3 rgb=albedo*light;
    if(material==8u)rgb=vec3(8);
    if(material==7u)rgb=albedo*3.0;
    // Water reaches this shader only when its enhanced material is disabled.
    if(material==5u) rgb=fog_colour(rgb,relative_position,skylight);
    colour=vec4(rgb,1);
}
