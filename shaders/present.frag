#version 460
layout(set=0,binding=0) uniform sampler2D resolved;
layout(location=0) out vec4 colour;
void main() { colour=vec4(texelFetch(resolved,ivec2(gl_FragCoord.xy),0).rgb,1); }
