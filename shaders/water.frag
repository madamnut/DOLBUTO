#version 460
#extension GL_GOOGLE_include_directive : require
#include "water_common.glsl"
#include "water_trace.glsl"
#include "water_highlight.glsl"
vec3 water_background;
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(set = 2, binding = 2) uniform sampler2D reflections;
layout(set = 2, binding = 5) uniform sampler2D reflection_metadata;
layout(location = 0) in vec2 uv;
layout(location = 2) in float shade;
layout(location = 3) flat in uint selected;
layout(location = 4) in vec2 face_uv;
layout(location = 0) out vec4 colour;
bool reflection_sample(ivec2 pixel, out vec4 value, out vec2 metadata) {
    ivec2 size=textureSize(reflections,0);
    if(any(lessThan(pixel,ivec2(0))) || any(greaterThanEqual(pixel,size))) return false;
    metadata=texelFetch(reflection_metadata,pixel,0).rg;
    float orientation=abs(face_normal.y)>0.5 ? 1.0 : -1.0;
    if(metadata.x*orientation<=0.0) return false;
    vec2 uv=(vec2(pixel)+0.5)/vec2(size);
    vec3 position=normalize(unproject(uv,0.0))*abs(metadata.x);
    if(abs(dot(position-relative_position,face_normal))>0.015+abs(metadata.x)*0.000002) return false;
    float depth=textureLod(scene_depth,uv,0.0).r;
    if(depth<1.0 && length(unproject(uv,depth))<abs(metadata.x)-0.04) return false;
    value=texelFetch(reflections,pixel,0);
    return true;
}
vec3 resolve_reflection(vec2 screen, vec3 normal, float footprint) {
    vec3 ray=reflection_direction(relative_position,face_normal,normal);
    vec2 pixel=screen*water.sizes.zw-0.5, fraction=fract(pixel);
    ivec2 start=ivec2(floor(pixel));
    vec3 hits=vec3(0);
    float hit_weight=0.0, sky_weight=0.0, coverage=0.0, uncertain=0.0;
    float confidence=0.0, reference_distance=0.0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        vec4 value; vec2 meta;
        if(!reflection_sample(start+ivec2(x,y),value,meta)) continue;
        float w=(x==0 ? 1.0-fraction.x : fraction.x)*(y==0 ? 1.0-fraction.y : fraction.y);
        coverage+=w;
        if(meta.y>0.0) {
            hits+=value.rgb*w; hit_weight+=w; confidence+=value.a*w;
            reference_distance+=meta.y*w;
        } else {
            sky_weight+=w;
            if(meta.y<-1.5) uncertain+=w;
        }
    }
    // An empty trace is not proof of sky: a subpixel gap can skip the visible
    // terrain entirely. Repair such holes only when matching hits surround them.
    bool enclosed_repair=false;
    if(sky_weight>0.001) {
        float reference=hit_weight>0.001 ? reference_distance/hit_weight : 0.0;
        ivec2 centre=ivec2(floor(pixel+0.5));
        if(reference==0.0) {
            float nearest=1e8;
            for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
                vec4 value; vec2 meta;
                if(!reflection_sample(centre+ivec2(x,y),value,meta) || meta.y<=0.0) continue;
                float distance_squared=dot(vec2(centre+ivec2(x,y))-pixel,vec2(centre+ivec2(x,y))-pixel);
                if(distance_squared<nearest) { nearest=distance_squared; reference=meta.y; }
            }
        }
        vec3 repair=vec3(0); float repair_weight=0.0, repair_confidence=0.0;
        int supports=0; bvec4 surround=bvec4(false);
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            vec4 value; vec2 meta;
            if(!reflection_sample(centre+ivec2(x,y),value,meta) || meta.y<=0.0) continue;
            if(abs(meta.y-reference)>max(2.0,reference*0.05)) continue;
            float w=1.0/(1.0+float(x*x+y*y));
            repair+=value.rgb*w; repair_weight+=w; repair_confidence+=value.a*w; ++supports;
            surround=bvec4(surround.x || x<0,surround.y || x>0,surround.z || y<0,surround.w || y>0);
        }
        enclosed_repair=supports>=4 && all(surround);
        float repaired=enclosed_repair ? sky_weight : (supports>=3 ? uncertain : 0.0);
        if(repaired>0.001) {
            hits+=repair/repair_weight*repaired;
            confidence+=repair_confidence/repair_weight*repaired;
            hit_weight+=repaired;
            sky_weight=max(0.0,sky_weight-repaired);
        }
    }
    // Thin water borders missing from the reduced-resolution buffer are retraced at full size.
    if(coverage<0.9) {
        ReflectionHit hit=trace_water_reflection(normal);
        if(hit.distance>0.0) {
            if(hit.confidence>0.999) return hit.colour;
            return mix(water_background,hit.colour,hit.confidence);
        }
        if(hit_weight<0.001 || (hit.distance>-1.5 && !enclosed_repair)) return water_background;
    }
    if(hit_weight<0.001) return water_background;
    float strength=confidence/max(hit_weight+sky_weight,0.001);
    vec3 terrain=hits/hit_weight;
    if(strength>0.999) return terrain;
    // Re-evaluate the coherent sky direction only at mixed boundaries, never bake
    // sky into each hit before interpolation as that created isolated blue dots.
    return mix(water_background,terrain,clamp(strength,0.0,1.0));
}

void ice_surface() {
    vec2 screen=gl_FragCoord.xy/water.sizes.xy;
    vec2 pixel=clamp(fract(uv)*32.0,vec2(0.5),vec2(31.5));
    vec3 albedo=texture(atlas,(pixel+vec2(9.0*32.0,0))/vec2(textureSize(atlas,0))).rgb;
    // Complementary translucentIPBR material 32004 (regular/frosted ice).
    // PNG alpha is deliberately opaque for inventory; world alpha is a material property.
    const float alpha=190.0/255.0;
    float smoothness=albedo.g*albedo.g*albedo.g;
    float highlight=sq(min(sq(albedo.g)*1.5,1.0))*3.5;
    vec3 normal=dot(face_normal,-relative_position)<0.0 ? -face_normal : face_normal;
    vec3 incident=normalize(relative_position);
    float fresnel=clamp(1.0+dot(normal,incident),0.0,1.0);
    float sky=min(light_channels.x*1.07,1.0);
    vec3 shadow=comp_surface_shadow(relative_position,normal,sky,light_channels.z)*cloud_shadow(relative_position);
    vec3 diffuse=water_gamma(comp_surface_light(relative_position,normal,sky,light_channels.y,light_channels.z,shadow));
    float specular=GGX(normal,incident,env.light.xyz,max(dot(normal,env.light.xyz),0.0),smoothness,normal);
    vec3 visible_shadow=shadow*env.light.w;
    vec3 highlight_colour=normalize(pow(comp_light(0),vec3(0.37)))*(0.3+1.5*sq(env.cycle.w));
    vec3 glint=specular*highlight*highlight_colour*visible_shadow;
    vec3 surface=albedo*diffuse+glint;
    vec3 ray=normalize(reflect(incident,normal));
    float first;
    vec4 clouds=cloud_ray_from(2.0*normal*dot(relative_position,normal),ray,1e8,true,first);
    vec3 reflected=water_gamma(celestial_sky(ray,true))*(1.0-clouds.a)+clouds.rgb+glint;
    float sky_factor=max(sq(max(sky-0.7,0.0)*3.33333),dot(visible_shadow,visible_shadow)*0.333333);
    water_background=mix(surface*0.5,reflected,clamp(sky_factor,0.0,1.0));
    surface=mix(surface,resolve_reflection(screen,normal,1.0),(pow(fresnel,3.0)*0.85+0.15)*0.7);
    surface=water_gamma(fog_colour(water_linear(surface),relative_position,sky_visibility));
    vec3 background=water_gamma(textureLod(scene_colour,screen,0.0).rgb);
    colour=vec4(water_linear(mix(background,surface,alpha)),1);
    vec2 edge=min(face_uv,1.0-face_uv);
    vec2 width=max(fwidth(face_uv)*1.5,vec2(0.012));
    if(selected!=0u && any(lessThan(edge,width))) colour=vec4(0.96,1.0,0.86,1);
}
void main() {
#ifdef LOD_WATER
    if(outside_lod_water()) discard;
#endif
    if(material==9u) { ice_surface(); return; }
    bool underwater=water.flags.w>0.5;
    if(underwater && dot(face_normal,relative_position)<=0.0) discard;
    vec2 screen=gl_FragCoord.xy/water.sizes.xy;
    if(water.extras.z<0.5) { colour=vec4(textureLod(scene_colour,screen,0.0).rgb,1); return; }
    float footprint=water_footprint(water.sizes.xy/water.sizes.zw);
    WaterWave wave=water_wave(relative_position,face_normal,footprint);
    vec3 geometric=dot(face_normal,-relative_position)<0.0 ? -face_normal : face_normal;
    vec3 incident=normalize(relative_position);
    float sky=min(light_channels.x*1.07,1.0);
    float fresnel4=pow(wave.fresnel,4.0);
    vec3 tint=comp_water_tint;
    tint.g=max(tint.g,0.39);
    vec3 albedo=0.375*tint*(2.0-tint)*vec3(1,0.85,0.8);
    float alpha=0.98, reflect_mult=1.0;
    float depth=textureLod(scene_depth,screen,0.0).r;
    vec3 bottom=unproject(screen,depth);
    if(!underwater) {
        float noise=(textureLod(reference_noise,(water_position(relative_position)+water_wind())*0.25,0.0).g-0.5)*0.25;
        albedo=pow(albedo,vec3(1.0+noise));
        float thickness=depth>=1.0 ? 8192.0 : max(length(bottom)-length(relative_position),0.0);
        float fog=water.flags.x>0.5 ? 1.0-exp(-thickness*0.075) : 0.0;
        alpha*=0.25+0.75*fog;
        albedo*=2.5-(1.0-pow(1.0-fog,4.0))-0.5*sky;
        if(water.extras.y>0.5 && face_normal.y>0.99 && depth<1.0) {
            float noise=textureLod(reference_noise,water_position(relative_position)*4.0+water_wind()*0.5,0.0).g;
            float threshold=max(noise*noise*1.6,1e-4);
            float foam=sq(clamp((threshold+bottom.y-relative_position.y)/threshold,0.0,1.0));
            foam*=(0.4+0.25*light_channels.x)*clamp((fract(water_world(relative_position).y)-0.7)*10.0,0.0,1.0);
            albedo=mix(albedo,vec3(0.9,0.95,1.05),foam);
            alpha=mix(alpha,1.0,foam);
            reflect_mult=1.0-foam;
        }
    } else {
        vec2 pixel=clamp(fract(uv)*32.0,vec2(0.5),vec2(31.5));
        alpha=texture(atlas,(pixel+vec2(5.0*32.0,0))/vec2(textureSize(atlas,0))).a;
        reflect_mult=0.5;
    }
    reflect_mult*=0.5+0.5*max(face_normal.y,0.0);
    alpha=mix(alpha,1.0,fresnel4);
    vec3 shadow=comp_surface_shadow(relative_position,geometric,sky,1.0)*cloud_shadow(relative_position);
    vec3 diffuse=water_gamma(comp_surface_light(relative_position,wave.normal,sky,light_channels.y,1.0,shadow,!underwater));
    vec2 light_p=min(1.70*0.75*1.25*0.65,1.0)*(wave.medium+0.5*wave.small);
    vec3 light_normal=normalize(water_basis(geometric)*vec3(light_p,1));
    float highlight=max(dot(light_normal,env.light.xyz),0.0)/max(dot(geometric,env.light.xyz),0.17);
    // Derivative-based mip estimate for the original highlight smoothing.
    float mip=max(log2(max(footprint*32.0,1.0)),0.0);
    highlight=mix(pow(highlight*1.1,4.0),1.0,min(sqrt(mip)*0.45,1.0))*0.24;
    float specular=GGX(wave.normal,incident,env.light.xyz,max(dot(wave.normal,env.light.xyz),0.0),1.0,geometric);
    vec3 highlight_colour=normalize(pow(comp_light(0),vec3(0.37)))*(0.3+1.5*sq(env.cycle.w));
    vec3 visible_shadow=shadow*env.light.w;
    vec3 glint=specular*highlight*highlight_colour*(underwater ? pow(max(visible_shadow,vec3(0)),vec3(0.25))*0.35 : visible_shadow);
    vec3 surface=albedo*diffuse+glint;
    if(water.flags.z>0.5) {
        vec3 sky_ray=normalize(reflect(incident,wave.normal));
        vec3 reflected_normal=normalize(mix(geometric,wave.normal,0.8));
        vec3 cloud_origin=2.0*reflected_normal*dot(relative_position,reflected_normal);
        float first;
        vec4 clouds=cloud_ray_from(cloud_origin,sky_ray,1e8,true,first);
        vec3 background_sky=celestial_sky(sky_ray,!underwater);
        vec3 reflected=water_gamma(background_sky)*(1.0-clouds.a)+clouds.rgb;
        reflected+=specular*highlight*highlight_colour*visible_shadow;
        float sky_factor=max(sq(max(sky-0.7,0.0)*3.33333),dot(visible_shadow,visible_shadow)*0.333333);
        water_background=mix(surface*0.5,reflected,clamp(sky_factor,0.0,1.0));
        float weight=(pow(wave.fresnel,3.0)*0.85+0.15)*reflect_mult;
        surface=mix(surface,resolve_reflection(screen,wave.normal,footprint),weight);
    }
    surface=water_gamma(fog_colour(water_linear(surface),relative_position,sky_visibility));
    // Match the reference's display-space translucent blend, then return HDR.
    vec3 background=water_gamma(textureLod(scene_colour,screen,0.0).rgb);
    colour=vec4(water_linear(mix(background,surface,alpha)),1);
}
