#version 460
#extension GL_GOOGLE_include_directive : require
#include "lod_mask.glsl"
layout(location=0) out vec4 colour;
layout(location=1) out vec4 shaft_colour;
void main(){
    if(covered()||(pc.meta.w>.5 ? material==5u : material!=5u))discard;
    colour=material==5u?vec4(.12,.24,.32,1):material==9u?vec4(.32,.43,.55,1):vec4(0);
    shaft_colour=vec4(colour.rgb,.25+max(0.0,relative_position.y*.05));
}
