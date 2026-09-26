#version 450
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=0) uniform sampler2D source;
layout(set=0,binding=1) uniform sampler2D depth;
layout(push_constant) uniform Parameters { vec4 value; } pc;
layout(location=0) out vec4 colour;
void main() {
    vec2 size=vec2(textureSize(source,0));
    vec2 output_size=pc.value.yz;
    vec2 uv=gl_FragCoord.xy/output_size;
    if(pc.value.x<1.5) {
        vec3 result=vec3(0);
        for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
            vec2 p=uv+(vec2(x,y)-0.5)/size;
            vec3 c=texture(source,p).rgb;
            if(pc.value.x<0.5) {
                float z=texture(depth,p).r;
                float d=z<0.999999 ? length(scene_position(p,z)) : 8192.0;
                float cave=clamp(1.0-(env.camera_time.y-128.0)/61.9,0.0,1.0-env.limits.w);
                float mult=env.view.y>0.5 ? 14.0 : 3.0*(1.0-env.cycle.z)*env.limits.w+cave*14.0;
                float fog=pow(1.0-exp(-d*(env.view.y>0.5 ? 0.06 : 0.02)),4.0);
                c*=1.0+fog*mult*env.effects.z*8.33333;
            }
            result+=c;
        }
        colour=vec4(result*0.25,1); return;
    }
    // Full HDR bloom, without a brightness threshold.
    const float weight[7]=float[7](1,6,15,20,15,6,1);
    vec3 sum=vec3(0);
    for(int y=-3;y<=3;++y) for(int x=-3;x<=3;++x)
        sum+=texture(source,uv+vec2(x,y)/size).rgb*weight[x+3]*weight[y+3];
    colour=vec4(sum/4096.0,1);
}
