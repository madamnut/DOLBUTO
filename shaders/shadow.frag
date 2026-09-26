#version 460
#extension GL_GOOGLE_include_directive : require
#include "environment.glsl"
layout(location=1) flat in uint material;
layout(location=5) in vec3 relative_position;
layout(location=0) out vec4 colour;
layout(location=1) out vec4 shaft_colour;
void main() {
    colour=vec4(0);
    shaft_colour=vec4(0,0,0,0.25+max(0.0,relative_position.y*0.05));
    if(material==9u) {
        // Static blue transmission; ice does not inherit water's animated caustics.
        colour.rgb=vec3(0.32,0.43,0.55);
        shaft_colour.rgb=colour.rgb;
    }
    if(material==5u) {
        vec2 world=relative_position.xz+env.camera_time.xz;
        // Keep the noise period commensurate with the 131072-block world.
        vec2 wind=vec2(0,env.camera_time.w*1.10*0.035);
        vec2 p1=world*(10486.0/131072.0)+wind, p2=world*(7864.0/131072.0)-wind;
        float caustic=dot(texture(reference_cloud_water,p1+vec2(0.001,0)).rg-texture(reference_cloud_water,p1-vec2(0.001,0)).rg,vec2(14));
        caustic+=dot(texture(reference_cloud_water,p2+vec2(0,0.001)).rg-texture(reference_cloud_water,p2-vec2(0,0.001)).rg,vec2(14));
        float pattern=env.hydro.x>0.5 ? clamp(caustic*0.8+0.35,0.0,1.0)*0.65+0.35 : 0.75;
        colour.rgb=pow(pattern*comp_water_tint*vec3(0.6,0.8,1.1),vec3(0.75))*0.5;
        vec2 water_wind=vec2(env.camera_time.w*0.01,0);
        float noise=texture(reference_noise,world*(1573.0/131072.0)-water_wind).g+
                    texture(reference_noise,world*(6554.0/131072.0)+water_wind).g;
        float factor=max(2.5-0.025*length(relative_position.xz),0.8333)*1.3;
        noise=pow(noise*0.5,factor)*factor*1.3;
        shaft_colour.rgb=normalize(comp_water_tint*(2.0-comp_water_tint))*vec3(0.24,0.22,0.26)*noise*(1.0+env.cycle.w);
        shaft_colour.a=0.25+max(0.0,(relative_position.y+3.5)*0.05);
    }
}
