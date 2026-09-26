// Adapted from the supplied Complementary Unbound by EminGT.
// Clear Overworld, SHADER_STYLE=4, default colour controls. See assets/licenses/Complementary.txt.
const float comp_brightness=0.5;
const vec3 comp_water_tint=vec3(0.2470588,0.4627451,0.8941176);
float sq(float x) { return x*x; }
vec3 sq(vec3 x) { return x*x; }
float smooth1(float x) { return x*x*(3.0-2.0*x); }
float comp_noise() {
    return fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715)))+
                 (env.view.z>0.5 ? 1.61803398875*env.temporal.z : 0.0));
}
vec3 comp_light(int kind) {
    float noon=env.cycle.x, inv_noon=1.0-noon, visibility=env.cycle.w;
    vec3 midday=kind==2 ? vec3(0.4,0.75,1.3) : vec3(0.65,0.55,0.375)*2.05;
    vec3 sunset=pow(kind==2 ? vec3(0.62,0.39,0.24) : vec3(0.64,0.45,0.3),vec3(1.5+inv_noon))*(kind==2 ? 6.8 : 5.0);
    vec3 night=kind==2 ? vec3(0.08,0.12,0.23) : (kind==1 ? 0.9*vec3(0.11,0.14,0.20) : 0.9*vec3(0.15,0.14,0.20)*(0.4+comp_brightness*0.4));
    return mix(night,mix(sunset,midday,kind==2 ? noon*noon : noon),visibility*visibility);
}
vec3 comp_ambient() {
    vec3 noon=pow(env.sky.rgb,vec3(0.75))*0.85;
    return mix(0.9*vec3(0.09,0.12,0.17)*(1.55+comp_brightness*0.77),
               mix(noon*vec3(1.21,0.92,0.76)*0.95,noon,env.cycle.x),sq(env.cycle.w));
}
struct CompSky { vec3 up, middle, down, night_up; };
CompSky comp_sky_palette() {
    vec3 root=sqrt(env.sky.rgb);
    vec3 noon_up=pow(root,vec3(2.9))*vec3(0.85,0.92,0.81);
    vec3 noon_middle=pow(root,vec3(1.5))*1.3+noon_up*0.65;
    vec3 noon_down=root*0.9+noon_up*0.25;
    float blend=sq(1.0-env.cycle.x);
    CompSky p;
    p.up=mix(noon_up,env.sky.rgb*vec3(0.72,0.522,0.47),blend);
    p.middle=mix(noon_middle,env.sky.rgb*vec3(1.8,1.3,1.2),blend);
    p.down=mix(noon_down,vec3(1.45,0.86,0.5)*0.5+0.25*env.sky.rgb*vec3(1.8,1.3,1.2),blend);
    p.night_up=pow(0.9*vec3(0.07,0.14,0.24)+env.sky.rgb,vec3(0.9))*0.45;
    return p;
}
vec3 comp_sky(vec3 direction, bool ground) {
    CompSky p=comp_sky_palette();
    float up=direction.y, sun=dot(direction,env.solar.xyz), u=max(up,0.0);
    float s1=sq(max(sun,0.0)), s2=sq(s1), s3=pow(max(-sun,0.0),4.0);
    float night_sqrt=sqrt(sqrt(env.cycle.y)), night_m=sqrt(sqrt(night_sqrt))*0.4;
    vec3 night_middle=sqrt(p.night_up)*0.65;
    vec3 top=mix(p.night_up*(1.5-0.5*night_sqrt+night_m*s3*1.5),p.up,env.cycle.z);
    vec3 middle=mix(night_middle*(3.0-2.0*night_sqrt),p.middle*(1.0+s2*0.3),env.cycle.z);
    vec3 down=mix(night_middle*vec3(0.82,0.82,0.88),p.down,(env.cycle.z+env.cycle.w)*0.5);
    vec3 result=mix(top,middle,pow(sq(1.0-u),1.0-s2*0.4));
    float sunset=smooth1(sq(1.0-abs(up)))*(0.7-night_m+s1*(0.3+night_m))*(1.0-env.cycle.x)*env.cycle.z;
    result=mix(result,vec3(1.45,0.86,0.5)*(1.0+s1*0.3),sunset);
    float g=smooth1(clamp((-up+0.08)/0.35,0.0,1.0));
    result=mix(result,down,mix(vec3(g),vec3(g*g,sqrt(g),sqrt(sqrt(sqrt(g)))),0.75));
    if(ground) result*=smooth1(sq(1.0+min(up,0.0)));
    if((env.cycle.w>0.5 ? sun : -sun)>0.0) {
        float scatter=3.0*(2.0-clamp(sun*1000.0,0.0,1.0));
        float glare=(0.075/(1.0-0.925*pow(abs(sun),scatter))-0.075)*0.7;
        vec3 c=mix(vec3(0.38,0.4,0.5)*0.3,vec3(1.5,0.7,0.3)+vec3(0,0.5,0.5)*env.cycle.x,env.cycle.w);
        result+=c*glare*env.light.w;
    }
    float cave=clamp(1.0-(env.camera_time.y-128.0)/61.9,0.0,1.0-env.limits.w);
    result=mix(result,vec3(0.13,0.13,0.15)*0.85,cave*(1.0-u*u));
    result+=(comp_noise()-0.5)/128.0;
    return pow(max(result,vec3(0)),vec3(2.2));
}
vec3 comp_shadow_pos(vec3 position) {
    vec3 p=(env.shadow_matrix[0]*vec4(position,1)).xyz;
    float bias=1.0-25.6/env.limits.x;
    p.xy/=length(p.xy)*bias+1.0-bias;
    return vec3(p.xy*0.5+0.5,0.5+(p.z-0.5)*0.2);
}
vec3 comp_shadow_sample(vec3 p,float sky) {
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1)))) return vec3(1);
    float full=texture(shadow_all,p), opaque=texture(shadow_opaque,p);
    vec3 tint=vec3(0);
    if(full<1.0 && opaque>0.9999) {
        float multiplier=2.5+5.5*pow(sky,1.5)+2.0*sky*sky;
        float exponent=mix(env.view.y>0.5 ? 1.5 : 2.0,0.5,pow(sky,4.0));
        tint=pow(max(texture(shadow_colour,p.xy).rgb*multiplier,vec3(0)),vec3(exponent));
    }
    return vec3(full)+(1.0-full)*tint;
}
vec3 comp_surface_shadow(vec3 position,vec3 normal,float sky,float ao) {
    if(env.flags.x<0.5) return vec3(pow(sky,8.0));
    float n=max(dot(normal,env.light.xyz),0.0);
    float bias=0.12+0.0008*pow(dot(position,position),0.75);
    // Pull the biased receiver toward its own block centre in dark corners.
    // Fractional camera coordinates avoid loss of block fractions far from zero.
    vec3 local=position+fract(env.camera_time.xyz)-normal*0.001;
    vec3 centre=position+(vec3(0.5)-fract(local));
    float centre_factor=max(ao,smooth1(sky));
    vec3 receiver=mix(position,centre,0.2*(1.0-pow(centre_factor,4.0)));
    vec3 p=comp_shadow_pos(receiver+normal*bias*(2.0-0.95*n));
    float noise=comp_noise();
    float offset=0.00098*1.3875*(1.0+sq(normal.z));
    // Original default quality=2 uses two mirrored pairs, accumulated by TAA.
    const int count=2;
    vec3 result=vec3(0);
    for(int i=0;i<count;++i) {
        float x=noise+float(i), a=fract(x*2.427)*3.1415;
        vec2 delta=vec2(cos(a),sin(a))*1.4*x/float(count)*offset;
        result+=comp_shadow_sample(p+vec3(delta,0),sky);
        result+=comp_shadow_sample(p-vec3(delta,0),sky);
    }
    result/=float(count*2);
    float fade=clamp((env.limits.x*0.9166667-length(position))/max(env.limits.x*0.0833333,1.0),0.0,1.0);
    return mix(vec3(pow(sky,8.0)),result,fade);
}
vec3 comp_surface_light(vec3 position,vec3 normal,float sky,float block,float ao,vec3 shadow,bool directional) {
    float sky2=sky*sky, sky_smooth=smooth1(sky);
    float n=max(dot(normal,env.light.xyz)+0.4,0.0)*0.714;
    shadow*=n*env.light.w;
    vec3 direct=comp_light(0), ambient=comp_ambient();
    float direction=1.0;
    if(directional) {
        float e2=normal.x*normal.x, north=abs(normal.z);
        direction=(0.75+normal.y*0.25)*(1.0-0.1*e2)*(1.0+0.075*north);
        direct*=1.0+e2*0.75;
        ambient=mix(ambient,direct,0.05*north*sky2);
        direct*=1.0+pow(env.cycle.x,20.0)*(north*north*0.8-e2*0.2);
    }
    float light_fog=env.view.y<0.5 ? 1.0+max(96.0-length(position),0.0)*0.002*(1.0-sq(env.cycle.w)) : 1.0;
    vec3 scene=(direct*shadow+ambient*sky_smooth)*light_fog;
    float b=pow(pow(block,8.0)*(3.8-0.6*comp_brightness)+block*(1.8+0.6*comp_brightness),2.25);
    vec3 artificial=b*vec3(0.1775,0.104,0.077);
    if(env.view.y<0.5) {
        float shadow_lum=clamp(dot(shadow,vec3(0.299,0.587,0.114)),0.0,1.0);
        float reduction=(sq(env.cycle.w)*0.4+0.6-0.6*sq(1.0-env.cycle.x))*6.0;
        reduction*=sky2+sky2*2.0*sq(shadow_lum);
        artificial*=pow(b/60.0+0.001,0.09*reduction);
    }
    vec3 minimum=(0.005625+comp_brightness*0.043)*vec3(0.45,0.475,0.6)*(1.0-sky_smooth);
    ao=min(ao+0.08,1.0);
    ao=pow(pow(ao,1.5),1.0+dot(scene,scene)*0.02+max(normal.y,0.0)*(0.15+0.25*sq(env.cycle.x*sq(sky2))));
    ao=ao*0.9+0.1;
    vec3 diffuse=sqrt(max(sq(direction*ao)*(artificial+sq(scene)+minimum),vec3(0)));
    return pow(diffuse,vec3(2.2));
}

vec3 comp_surface_light(vec3 position,vec3 normal,float sky,float block,float ao,vec3 shadow) {
    return comp_surface_light(position,normal,sky,block,ao,shadow,true);
}

vec3 comp_shafts(vec3 direction,float distance,float front_distance) {
    if(env.effects.y<0.5 || env.flags.x<0.5) return vec3(0);
    float far_plane=env.view.x;
    float scene=env.view.y>0.5 ? 1.0 : texture(scene_factor,vec2(0.5)).r;
    vec3 light=comp_light(2), reducer=vec3(1);
    float mult=1.0;
    if(env.cycle.w<0.5) {
        scene=0.0;
        mult*=0.3+0.5*max(far_plane-distance,0.0)/far_plane;
        light=normalize(pow(light,vec3(1.0-max(1.0-1.5*env.cycle.y,0.0))))*(0.0766+0.0766*comp_brightness);
    } else reducer=1.0/sqrt(light);
    float u=smooth1(mix(sq(1.0-max(direction.y,0.0)),1.0,0.5*scene));
    u=pow(u,min(distance/far_plane,1.0)*(3.0-2.0*scene));
    float vl_time=clamp((abs(env.solar.y)-0.05)/0.15,0.0,1.0);
    mult*=u*max((dot(direction,env.light.xyz)+1.0)*0.5,0.0)*vl_time;
    mult*=mix(sq(1.0-env.cycle.x)*0.875+0.125,1.0,scene);
    int count=int(env.limits.z)*(scene<0.5 ? 1 : 2);
    if(env.view.z<0.5) count*=2;
    vec3 forward=camera_ray(vec2(0.5));
    float view_z=max(dot(direction,forward),0.01);
    float view_factor=1.0-0.7*sq(1.0-view_z*view_z);
    float maximum=mix(max(far_plane,96.0)*0.55,80.0,scene)*view_factor;
    float stride=maximum/float(count+1);
    float stop=min(distance*view_z,maximum), jitter=comp_noise();
    vec3 total=vec3(0);
    for(int i=0;i<count;++i) {
        float t=(float(i)+jitter)*stride+1.0;
        if(t>stop) break;
        vec3 pos=direction*(t/view_z), p=comp_shadow_pos(pos);
        vec3 sample_light=vec3(1);
        if(length(p.xy*2.0-1.0)<1.0 && p.z>0.0 && p.z<1.0) {
            ivec2 pixel=clamp(ivec2(p.xy*vec2(textureSize(shadow_depth_raw,0))),ivec2(0),textureSize(shadow_depth_raw,0)-1);
            float visibility=clamp((texelFetch(shadow_depth_raw,pixel,0).r-p.z)*65536.0,0.0,1.0);
            sample_light=vec3(visibility);
            if(visibility<1.0 && texture(shadow_opaque,p)>0.9999)
                sample_light+=sq(texture(shadow_shaft_colour,p.xy).rgb*4.0)*reducer*(1.0-visibility);
        }
        if(t/view_z>front_distance) sample_light*=sq(vec3(0.80,0.87,0.97)*0.71);
        float weight=mix(t/maximum*3.0,env.view.y>0.5 ? 0.85 : 1.0,scene)/float(count);
        if(t<5.0) weight*=smooth1(t/5.0);
        total+=sample_light*weight;
    }
    light=pow(light,vec3(0.5+(0.5+0.25*env.cycle.w)*(1.0-env.cycle.x)));
    light*=1.0+env.cycle.w*sq(1.0-env.cycle.x);
    if(env.view.y>0.5) light*=sq(vec3(0.80,0.87,0.97)*0.71);
    return max(total*light*mult,vec3(0));
}
