#define ENV_SET 3
#include "environment.glsl"
layout(set = 2, binding = 0) uniform sampler2D scene_colour;
layout(set = 2, binding = 1) uniform sampler2D scene_depth;
layout(set = 2, binding = 3, std140) uniform Water {
    mat4 matrix;
    mat4 inverse_matrix;
    vec4 camera_time;
    vec4 sky;
    vec4 flags;
    vec4 sizes;
    vec4 extras;
    vec4 trace;
} water;
layout(location = 1) flat in uint material;
layout(location = 5) in vec3 relative_position;
layout(location = 6) flat in vec3 face_normal;
layout(location = 7) in float sky_visibility;
layout(location = 8) in vec3 light_channels;
#ifdef LOD_WATER
layout(push_constant) uniform Push { mat4 matrix; vec4 offset; vec4 meta; } pc;
layout(location=10) in vec3 local_position;
#include "lod_coverage.glsl"
bool outside_lod_water() {
    return lod_covered(local_position,face_normal,pc.meta.xy) || length(relative_position.xz)>pc.meta.z;
}
#endif
vec3 unproject(vec2 screen, float depth) {
    vec4 point = water.inverse_matrix * vec4(screen * 2.0 - 1.0, depth, 1.0);
    return point.xyz / point.w;
}

// Complementary Unbound WATER_STYLE=3, default bump and wind controls.
// Frequencies share the slowest wave's integer world period (524 repeats).
vec3 water_world(vec3 p) { return p+water.camera_time.xyz; }
vec2 water_position(vec3 p) { vec3 w=water_world(p); return (w.xz+2.0*w.y)*(524.0/131072.0)*8.0; }
vec2 water_wind() { return vec2(0.0,-water.camera_time.w*1.10*0.018*2.5); }
mat3 water_basis(vec3 geometric) {
    vec3 tangent=abs(geometric.x)>0.5 ? vec3(0,0,1) : vec3(1,0,0);
    tangent=normalize(tangent-geometric*dot(tangent,geometric));
    return mat3(tangent,normalize(cross(tangent,geometric)),geometric);
}
struct WaterWave { vec3 normal; vec2 medium, small; float fresnel; };
WaterWave water_wave(vec3 position,vec3 geometric,float footprint) {
    if(dot(geometric,-position)<0.0) geometric=-geometric;
    vec3 incident=normalize(position);
    float fresnel=clamp(1.0+dot(geometric,incident),0.0,1.0);
    WaterWave wave=WaterWave(geometric,vec2(0),vec2(0),fresnel);
    if(water.flags.y<0.5) return wave;
    mat3 basis=water_basis(geometric);
    vec3 view=transpose(basis)*incident;
    vec2 p=water_position(position)*2.5, wind=water_wind();
    vec2 parallax=-0.01*view.xy/min(view.z,-0.001);
    for(int i=0;i<4;++i) {
        p+=parallax*textureLod(reference_cloud_water,p-wind,0.0).a;
        p+=parallax*textureLod(reference_cloud_water,p*0.25-0.5*wind,0.0).a;
    }
    float detail=1.0-smoothstep(0.25,2.0,footprint);
    wave.medium=textureLod(reference_cloud_water,p+wind,0.0).rg-0.5;
    wave.small=textureLod(reference_cloud_water,p*4.0-2.0*wind,0.0).rg-0.5;
    vec2 big=textureLod(reference_cloud_water,p*0.25-0.5*wind,0.0).rg-0.5;
    big+=textureLod(reference_cloud_water,p*0.05-0.05*wind,0.0).rg-0.5;
    vec2 bump=wave.medium*1.70+wave.small*0.75+big*2.0;
    float sky=min(light_channels.x*1.07,1.0);
    bump*=6.0*(1.0-0.7*fresnel)*(1.25*0.8)*(0.03*sky+0.01);
    // The original texture's mip chain is not bundled; suppress subpixel waves
    // when their shortest wavelength can no longer be resolved.
    bump*=detail;
    wave.medium*=detail; wave.small*=detail;
    wave.normal=normalize(basis*vec3(bump,sqrt(max(1.0-dot(bump,bump),0.001))));
    vec3 reflected=reflect(incident,wave.normal);
    float repair=pow(1.0-max(0.0,dot(geometric,reflected)),8.0)*0.5;
    wave.normal=normalize(mix(wave.normal,geometric,repair));
    return wave;
}
vec3 water_normal_at(vec3 position,vec3 geometric,float footprint) { return water_wave(position,geometric,footprint).normal; }
float water_footprint(vec2 scale) {
    return max(length(dFdx(relative_position))*scale.x,length(dFdy(relative_position))*scale.y);
}
vec3 water_normal(float scale) { return water_normal_at(relative_position,face_normal,water_footprint(vec2(scale))); }
vec3 reflection_direction(vec3 position,vec3 geometric,vec3 wave_normal) {
    if(dot(geometric,-position)<0.0) geometric=-geometric;
    return normalize(reflect(normalize(position),normalize(mix(geometric,wave_normal,0.8))));
}
vec3 water_gamma(vec3 value) { return pow(max(value,vec3(0)),vec3(1.0/2.2)); }
vec3 water_linear(vec3 value) { return pow(max(value,vec3(0)),vec3(2.2)); }

float water_bayer(vec2 p) {
    float value=0.0, weight=1.0;
    for(int i=0;i<6;++i) {
        vec2 c=0.5*floor(p);
        value+=weight*fract(1.5*fract(c.y)+c.x);
        p*=0.5; weight*=0.25;
    }
    return fract(value+(env.view.z>0.5 ? 1.61803398875*env.temporal.z : 0.0));
}
