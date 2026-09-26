#ifndef ENVIRONMENT_GLSL
#define ENVIRONMENT_GLSL
#ifndef ENV_SET
#define ENV_SET 2
#endif
layout(set=ENV_SET,binding=0,std140) uniform Environment {
    mat4 inverse_camera;
    mat4 shadow_matrix[2];
    vec4 camera_time;
    vec4 light;
    vec4 solar;
    vec4 sky;
    vec4 flags;
    vec4 effects;
    vec4 cloud;
    vec4 limits;
    vec4 hydro;
    vec4 water_origin;
    mat4 previous_camera;
    vec4 temporal;
    vec4 cycle;
    vec4 view;
} env;
layout(set=ENV_SET,binding=1) uniform sampler2DShadow shadow_all;
layout(set=ENV_SET,binding=2) uniform sampler2DShadow shadow_opaque;
layout(set=ENV_SET,binding=3) uniform sampler2D shadow_colour;
layout(set=ENV_SET,binding=4) uniform sampler2D scene_factor;
layout(set=ENV_SET,binding=5) uniform sampler2D shadow_shaft_colour;
layout(set=ENV_SET,binding=6) uniform sampler2D shadow_depth_raw;
layout(set=ENV_SET,binding=7) uniform sampler2D reference_noise;
layout(set=ENV_SET,binding=8) uniform sampler2D reference_cloud_water;

vec3 to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 from_linear(vec3 c) {
    c = max(c, vec3(0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 camera_ray(vec2 uv) {
    vec4 p = env.inverse_camera * vec4(uv * 2.0 - 1.0, 1, 1);
    return normalize(p.xyz / p.w);
}
vec3 scene_position(vec2 uv, float depth) {
    vec4 p = env.inverse_camera * vec4(uv * 2.0 - 1.0, depth, 1);
    return p.xyz / p.w;
}
#include "complementary.glsl"
float caustic_hash(ivec3 p, int period) {
    p = (p % period + period) % period;
    uvec3 v = uvec3(p);
    uint h = v.x * 1597334677u ^ v.y * 3812015801u ^ v.z * 2798796415u;
    h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
    return float(h & 0x00ffffffu) / 16777215.0;
}
float caustic_noise(vec3 p, int period) {
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p); f = f*f*f*(f*(f*6.0-15.0)+10.0);
    return mix(mix(mix(caustic_hash(cell,period),caustic_hash(cell+ivec3(1,0,0),period),f.x),
                   mix(caustic_hash(cell+ivec3(0,1,0),period),caustic_hash(cell+ivec3(1,1,0),period),f.x),f.y),
               mix(mix(caustic_hash(cell+ivec3(0,0,1),period),caustic_hash(cell+ivec3(1,0,1),period),f.x),
                   mix(caustic_hash(cell+ivec3(0,1,1),period),caustic_hash(cell+ivec3(1,1,1),period),f.x),f.y),f.z);
}
// Periodic hash used by the no-shadow caustic fallback.
vec3 caustic_feature(ivec3 cell, int period) {
    cell=(cell%period+period)%period;
    uint h=uint(cell.x)*1597334677u ^ uint(cell.y)*3812015801u ^ uint(cell.z)*2798796415u;
    uvec3 v=uvec3(h,h^0x68bc21ebu,h^0x02e5be93u);
    v^=v>>16; v*=2246822519u; v^=v>>13; v*=3266489917u; v^=v>>16;
    return vec3(v & 0x00ffffffu)/16777215.0;
}
vec3 sky_colour(vec3 direction) {
    return env.flags.z<0.5 ? to_linear(env.sky.rgb) : comp_sky(direction,false);
}
#include "celestial.glsl"
#include "clouds.glsl"
float comp_altitude(float y) { return sq(1.0-clamp(y-192.1,0.0,60.0)/60.0); }
vec3 fog_colour(vec3 colour, vec3 position, float sky_visibility) {
    if(env.flags.w<0.5 || env.view.y>0.5) return colour;
    float distance=length(position);
    if(distance<0.01) return colour;
    colour=pow(max(colour,vec3(0)),vec3(1.0/2.2));
    float border=1.0-exp(-3.0*pow(max(length(position.xz),abs(position.y))/env.view.x,16.0));
    float cave=clamp(1.0-(env.camera_time.y-128.0)/61.9,0.0,1.0-sky_visibility);
    colour=mix(colour,vec3(0.13,0.13,0.15)*0.85,cave*(0.9-0.9*exp(-distance*0.015)));
    float amount=(1.0-exp(-sq(distance*0.001)*distance*min(192.0/env.view.x,1.0)))*0.7;
    float altitude=comp_altitude(position.y+env.camera_time.y);
    amount*=(altitude*0.9+0.1)*(1.0-0.75*comp_altitude(env.camera_time.y));
    amount*=(0.2+0.8*sqrt(sqrt(sky_visibility)))*(1.0-cave);
    CompSky palette=comp_sky_palette();
    float blend=pow(1.0-env.cycle.y,4.0-dot(position/distance,env.solar.xyz)-2.5*sq(env.cycle.w));
    float night=2.5-0.625*pow(altitude,4.0);
    vec3 fog=mix(palette.night_up*(night-blend*night),palette.down*(0.9+0.3*env.cycle.x),blend);
    colour=mix(colour,max(fog,vec3(0)),clamp(amount,0.0,1.0));
    colour=mix(colour,pow(comp_sky(position/distance,false),vec3(1.0/2.2)),border);
    return pow(max(colour,vec3(0)),vec3(2.2));
}
// Periodic, gently moving cellular ridges. Unlike crossed sine bands this has
// no repeated diagonal square/ring motif. The pattern lives on the receiver,
// so refraction samples the same illuminated terrain from either side of water.
float caustic_pattern(vec2 p, float footprint) {
    if(footprint>=0.7) return 0.0;
    vec2 warp=vec2(caustic_noise(vec3(p/16.0,5),128),caustic_noise(vec3(p/16.0,19),128))-0.5;
    p+=warp*1.4;
    ivec2 cell=ivec2(floor(p)); vec2 f=fract(p);
    float first=10.0, second=10.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        ivec2 offset=ivec2(x,y);
        vec3 random=caustic_feature(ivec3(cell+offset,3),2048);
        vec2 point=0.5+0.22*sin(vec2(env.hydro.w,-env.hydro.w)+random.xy*6.28318530718);
        vec2 delta=vec2(offset)+point-f;
        float d=dot(delta,delta);
        second=min(second,max(first,d)); first=min(first,d);
    }
    float ridge=sqrt(second)-sqrt(first);
    float width=max(0.025,footprint*0.6);
    float lines=1.0-smoothstep(0.055-width,0.055+width,ridge);
    return lines*(1.0-smoothstep(0.15,0.7,footprint));
}
float caustic_footprint(vec3 position) {
    vec2 projected=(position.xz+env.light.xz*position.y/max(env.light.y,0.25))*0.5;
    return max(length(dFdx(projected)),length(dFdy(projected)));
}
float caustic_gain(vec3 position, float footprint) {
    float depth=env.hydro.z-position.y;
    if(env.hydro.x<0.5 || depth<=0.0 || depth>=48.0 || env.solar.y<=0.04) return 0.0;
    vec2 p=(position.xz+env.water_origin.xy-env.light.xz*depth/max(env.light.y,0.25))*0.5;
    return caustic_pattern(p,footprint)*1.15*exp(-depth*0.055)*smoothstep(0.04,0.15,env.solar.y);
}
vec3 underwater_colour(vec3 colour, float distance) {
    float amount=1.0-exp(-sq(distance/48.0));
    vec3 fog=pow(vec3(0.01960784,0.01960784,0.2),vec3(0.33,0.21,0.26))*(0.2+0.1*comp_brightness);
    return pow(mix(pow(max(colour,vec3(0)),vec3(1.0/2.2)),fog,amount)*vec3(0.80,0.87,0.97)*0.85,vec3(2.2));
}
vec3 surface_light(vec3 position, vec3 normal, float sky, float block, float ao, bool wet, float footprint) {
    vec3 visibility=comp_surface_shadow(position,normal,sky,ao)*cloud_shadow(position);
    if(wet && env.flags.x<0.5) visibility*=1.0+caustic_gain(position,footprint);
    return comp_surface_light(position,normal,sky,block,ao,visibility);
}
vec3 surface_light(vec3 position, vec3 normal, float sky, float block, float ao) {
    return surface_light(position,normal,sky,block,ao,false,0.0);
}
#endif
