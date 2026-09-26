#version 460
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=0) uniform sampler2D current_colour;
layout(set=0,binding=1) uniform sampler2D current_depth;
layout(set=0,binding=2) uniform sampler2D history;
layout(location=0) out vec4 colour;
// Complementary's Catmull-Rom history reconstruction (c=.7), compact five-tap form.
vec3 history_filter(vec2 uv) {
    vec2 size=vec2(textureSize(history,0)), sample_pos=uv*size;
    vec2 centre=floor(sample_pos-0.5)+0.5, f=sample_pos-centre;
    vec2 w0=f*(-0.7+f*(1.4-0.7*f));
    vec2 w1=1.0+f*f*(-2.3+1.3*f);
    vec2 w2=f*(0.7+f*(1.6-1.3*f));
    vec2 w3=f*f*(-0.7+0.7*f);
    vec2 w12=w1+w2, p12=(centre+w2/w12)/size;
    vec2 p0=(centre-1.0)/size, p3=(centre+2.0)/size;
    vec3 c=texture(history,vec2(p12.x,p0.y)).rgb*w12.x*w0.y
          +texture(history,vec2(p0.x,p12.y)).rgb*w0.x*w12.y
          +texture(history,p12).rgb*w12.x*w12.y
          +texture(history,vec2(p3.x,p12.y)).rgb*w3.x*w12.y
          +texture(history,vec2(p12.x,p3.y)).rgb*w12.x*w3.y;
    return c/(w12.x*(w0.y+w12.y+w3.y)+(w0.x+w3.x)*w12.y);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy), size=textureSize(current_colour,0);
    vec2 uv=gl_FragCoord.xy/vec2(size);
    vec3 current=texelFetch(current_colour,pixel,0).rgb;
    float depth=texelFetch(current_depth,pixel,0).r;
    // R16 alpha stores distance; 8192 remains representable for the far sky.
    vec3 pos=scene_position(uv,depth);
    float distance=depth<0.999999 ? length(pos) : 8192.0;
    colour=vec4(current,distance);
    if(env.temporal.x<0.5 || env.view.z<0.5) return;
    vec4 previous=env.previous_camera*vec4(pos,1);
    if(previous.w<=0.0) return;
    vec2 puv=previous.xy/previous.w*0.5+0.5;
    if(any(lessThan(puv,vec2(0)))||any(greaterThan(puv,vec2(1)))) return;
    vec3 lo=current, hi=current;
    float edge=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        ivec2 p=clamp(pixel+ivec2(x,y),ivec2(0),size-1);
        vec3 c=texelFetch(current_colour,p,0).rgb;
        lo=min(lo,c); hi=max(hi,c);
        float z=texelFetch(current_depth,p,0).r;
        float d=z<0.999999 ? length(scene_position((vec2(p)+0.5)/vec2(size),z)) : 8192.0;
        edge=max(edge,abs(d-distance)/max(env.view.x,1.0));
    }
    vec3 old=history_filter(puv), centre=(hi+lo)*0.5, extent=(hi-lo)*0.5+vec3(0.0001);
    vec3 delta=old-centre;
    float clipping=max(1.0,max(abs(delta.x/extent.x),max(abs(delta.y/extent.y),abs(delta.z/extent.z))));
    old=centre+delta/clipping;
    vec2 velocity=(uv-puv)*vec2(size);
    float blend=max(exp(-dot(velocity,velocity)*10.0)*0.2+0.7-min(env.temporal.y,0.05)*(edge>0.09 ? 6.0 : 0.0),0.35);
    float old_distance=texture(history,puv).a;
    if(abs(old_distance-distance)>max(0.75+env.temporal.y*2.0,distance*0.04)) blend=0.0;
    if(any(isnan(old))||any(isinf(old))) blend=0.0;
    colour.rgb=blend>0.0 ? mix(current,old,blend) : current;
}
