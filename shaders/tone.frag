#version 450
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=0) uniform sampler2D scene_colour;
layout(set=0,binding=1) uniform sampler2D bloom2;
layout(set=0,binding=2) uniform sampler2D bloom3;
layout(set=0,binding=3) uniform sampler2D bloom4;
layout(set=0,binding=4) uniform sampler2D bloom5;
layout(set=0,binding=5) uniform sampler2D bloom6;
layout(set=0,binding=6) uniform sampler2D bloom7;
layout(set=0,binding=7) uniform sampler2D bloom8;
layout(location=0) out vec4 colour;
float luminance(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
void main() {
    vec2 uv=gl_FragCoord.xy/vec2(textureSize(scene_colour,0));
    vec3 c=texture(scene_colour,uv).rgb;
    if(env.effects.w>0.5) { colour=vec4(c,1); return; }
    if(env.effects.z>0.0) {
        // Original seven bloom tiles, including weights applied before pow4 decoding.
        vec3 blur=(texture(bloom2,uv).rgb+texture(bloom3,uv).rgb+texture(bloom4,uv).rgb+
            (texture(bloom5,uv).rgb+texture(bloom6,uv).rgb)*0.4096+
            texture(bloom7,uv).rgb*0.1296+texture(bloom8,uv).rgb*0.0256)*0.14;
        c=mix(c,blur,env.effects.z);
    }
    // DoCompTonemap: exposure=1, contrast=1.05, white path=1, dark desaturation=.25.
    c=max(c,vec3(0));
    float initial=luminance(c), a=1.05;
    float hi=pow(8.0,a), mid=pow(0.25,a);
    float b=(-mid+hi*0.25)/((hi-mid)*0.25);
    float d=(hi*mid-hi*0.25*mid)/((hi-mid)*0.25);
    vec3 power=pow(c,vec3(a)), mapped=power/(power*b+d);
    vec3 result=from_linear(mapped);
    float lift=1.0-smoothstep(0.0,0.1,luminance(result));
    result=mix(result,pow(mapped,vec3(1.0/2.2)),lift*0.75);
    result=mix(result,vec3(1),smoothstep(0.0,16.0,initial));
    result=mix(result,vec3(luminance(result)),(1.0-smoothstep(0.0,0.1,luminance(result)))*0.25);
    result=clamp(result,0.0,1.0);
    // DoBSLColorSaturation at saturation=1, vibrance=1.
    float grey=(result.r+result.g+result.b)/3.0;
    result=result*1.07-grey*0.07;
    colour=vec4(clamp(result,0.0,1.0),1);
}
