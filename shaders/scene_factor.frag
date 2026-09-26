#version 460
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=0) uniform sampler2D previous_factor;
layout(set=0,binding=1) uniform sampler2D depth;
layout(set=0,binding=2) uniform sampler2D caster_colour;
layout(set=0,binding=3) uniform sampler2D caster_depth;
layout(location=0) out vec4 colour;
void main() {
    float value=texture(previous_factor,vec2(0.5)).r;
    if(env.temporal.w>0.5 && env.flags.x>0.5) {
        float heights=0.0; int count=0;
        for(int y=0;y<5;++y) for(int x=0;x<5;++x) {
            vec2 uv=0.3+0.4/5.0*(vec2(x,y)+vec2(0.25,0.45));
            float d=texelFetch(caster_depth,ivec2(uv*textureSize(caster_depth,0)),0).r;
            float h=texture(caster_colour,uv).a;
            if(d<0.55 && h>0.0) { heights+=max(h-0.25,0.0)/0.05; ++count; }
        }
        int sky=0;
        // OpenGL y=.9 corresponds to Vulkan y=.1 (upper part of the view).
        for(int x=0;x<5;++x) sky+=int(texture(depth,vec2(0.1+float(x)*0.2,0.1)).r>=1.0);
        float mean=count>0 ? heights/float(count) : 0.0;
        value=clamp(value+(sky>=4 ? -3.0 : mean>6.0 ? 1.0 : -2.0)/255.0,0.0,1.0);
    }
    colour=vec4(value,0,0,1);
}
