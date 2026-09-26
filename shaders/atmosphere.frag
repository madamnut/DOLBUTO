#version 450
#extension GL_GOOGLE_include_directive : require
#define ENV_SET 1
#include "environment.glsl"
layout(set=0,binding=0) uniform sampler2D scene_colour;
layout(set=0,binding=1) uniform sampler2D scene_depth;
layout(set=0,binding=2) uniform sampler2D volume_colour;
layout(set=0,binding=3) uniform sampler2D volume_metadata;
layout(set=0,binding=4) uniform sampler2D opaque_depth;
layout(location=0) out vec4 colour;
layout(push_constant) uniform CompositePhase { vec4 value; } phase;
// Original composite1 refraction, adapted to explicit opaque/water depths.
// Shared world period prevents discontinuities at the X/Z wrap.
float water_depth_gap(vec2 uv) {
    float front=textureLod(scene_depth,uv,0.0).r, back=textureLod(opaque_depth,uv,0.0).r;
    if(front>=back || front>=1.0) return 0.0;
    vec3 direction=camera_ray(vec2(0.5));
    return max(dot(scene_position(uv,back)-scene_position(uv,front),direction),0.0);
}
vec2 water_refraction(vec2 uv) {
    if(env.view.w<0.5) return uv;
    float gap=water_depth_gap(uv);
    if(gap<=0.0) return uv;
    vec3 position=scene_position(uv,textureLod(scene_depth,uv,0.0).r);
    vec3 world=position+env.camera_time.xyz;
    vec2 noise_uv=world.xz*(2621.0/131072.0)+world.y*0.01+env.camera_time.w*0.01;
    vec2 noise=textureLod(reference_noise,noise_uv,0.0).rb-0.5;
    vec3 forward=camera_ray(vec2(0.5)), top=camera_ray(vec2(0.5,0.0));
    float fov_scale=dot(forward,top)/max(length(cross(forward,top)),0.001);
    noise*=2.0*fov_scale/(3.0+length(position))*0.02*clamp(gap,0.0,1.0);
    vec2 candidate=uv+noise;
    if(any(lessThanEqual(candidate,vec2(0)))||any(greaterThanEqual(candidate,vec2(1)))) return uv;
    float next_gap=water_depth_gap(candidate);
    if(next_gap<=0.0) return uv;
    candidate=uv+noise*clamp(next_gap,0.0,1.0);
    // A final check protects subpixel shorelines and geometry in front of water.
    return water_depth_gap(candidate)>0.0 ? candidate : uv;
}
void main() {
    vec2 uv=gl_FragCoord.xy/vec2(textureSize(scene_colour,0));
    // Phase 0/2 is before water; phase 1/3 sees the composed surface.
    if(phase.value.x>2.5 || (phase.value.x>0.5 && phase.value.x<1.5)) uv=water_refraction(uv);
    vec3 base=texture(scene_colour,uv).rgb;
    if(env.effects.w>0.5) { colour=vec4(base,1); return; }
    float depth=texture(scene_depth,uv).r;
    vec3 direction=camera_ray(uv);
    vec3 position=scene_position(uv,depth);
    float distance=depth<0.999999 ? length(position) : 8192.0;
    float opaque_z=texture(opaque_depth,uv).r;
    float opaque_distance=opaque_z<0.999999 ? length(scene_position(uv,opaque_z)) : 8192.0;
    if(phase.value.x > 2.5) {
        if(env.hydro.y>0.5) base=underwater_colour(base,distance);
        colour=vec4(base+comp_shafts(direction,opaque_distance,distance),1);
        return;
    }
    if(phase.value.x < 0.5 || phase.value.x > 1.5) {
        if(depth>=0.999999) base=celestial_sky(direction,false);
        else base=fog_colour(base,position,env.limits.w);
        if(phase.value.x < 0.5) { colour=vec4(base,1); return; }
    }
    if(env.flags.y<0.5 && env.effects.y<0.5) { colour=vec4(base,1); return; }
    vec2 size=vec2(textureSize(volume_colour,0));
    vec2 pixel=uv*size-0.5, fraction=fract(pixel);
    ivec2 origin=ivec2(floor(pixel));
    vec4 volume=vec4(0); float weight=0.0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=clamp(origin+ivec2(x,y),ivec2(0),ivec2(size)-1);
        vec2 meta=texelFetch(volume_metadata,p,0).rg;
        float w=(x==0 ? 1.0-fraction.x : fraction.x)*(y==0 ? 1.0-fraction.y : fraction.y);
        // Do not stretch a sky cloud/shaft sample across a terrain silhouette.
        w*=1.0-smoothstep(2.0, max(4.0,distance*0.04),abs(meta.x-distance));
        if(meta.y<1e7 && meta.y>distance+2.0) w=0.0;
        volume+=texelFetch(volume_colour,p,0)*w; weight+=w;
    }
    if(weight>0.25) volume/=weight;
    else { float first; volume=cloud_ray(direction,distance,first); }
    // Complementary composites clouds in display-like space, then raises the
    // scene to 2.2 before adding the linear volumetric-light contribution.
    base=pow(max(pow(max(base,vec3(0)),vec3(1.0/2.2))*(1.0-volume.a)+volume.rgb,vec3(0)),vec3(2.2));
    if(env.view.y<0.5) base+=comp_shafts(direction,opaque_distance,distance)*(1.0-volume.a);
    colour=vec4(base,1);
}
