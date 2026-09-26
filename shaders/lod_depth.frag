#version 460
#extension GL_GOOGLE_include_directive : require
#include "lod_mask.glsl"
void main() {
    if(covered() || length(relative_position.xz)>pc.meta.z) discard;
}
