// Complementary Unbound SUN_MOON_STYLE 2 and stars, adapted without image assets.
// See assets/licenses/Complementary.txt. Full moon until a calendar is defined.
#ifndef CELESTIAL_GLSL
#define CELESTIAL_GLSL
float celestial_hash(vec2 p) {
    // Integer hashing avoids the large sin/hash precision noise of sky angles.
    uvec2 v=uvec2(ivec2(p));
    uint h=v.x*1597334677u ^ v.y*3812015801u;
    h^=h>>16; h*=2246822519u; h^=h>>13; h*=3266489917u; h^=h>>16;
    return float(h&0x00ffffffu)/16777215.0;
}
float moon_noise(vec2 p) {
    vec2 cell=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(celestial_hash(cell),celestial_hash(cell+vec2(1,0)),f.x),
               mix(celestial_hash(cell+vec2(0,1)),celestial_hash(cell+vec2(1,1)),f.x),f.y);
}
vec3 celestial_stars(vec3 direction, float sun_dot) {
    float night=1.0-smoothstep(-0.12,0.02,env.solar.y);
    if(env.water_origin.w<0.5 || direction.y<=0.0 || night<=0.0) return vec3(0);
    // Reference hemispherical projection and 1024*.2 distribution scale.
    // World directions keep the pattern still under translation/world wrapping.
    vec2 p=direction.xz/(direction.y+length(direction.xz)*0.5)*204.8;
    vec2 cell=floor(p), local=fract(p);
    float pixel=max(length(dFdx(p)),length(dFdy(p)));
    float filter_width=clamp(pixel*0.7,0.015,0.5);
    float stars=0.0;
    // Neighbours preserve the footprint when a star straddles a cell boundary.
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec2 offset=vec2(x,y), id=cell+offset;
        float a=celestial_hash(id), b=celestial_hash(id+vec2(71,193));
        float c=celestial_hash(id+vec2(233,29));
        float brightness=max(a*b*c-0.7,0.0);
        if(brightness<=0.0) continue;
        vec2 centre=vec2(celestial_hash(id+vec2(17,31)),celestial_hash(id+vec2(91,7)));
        float radius=mix(0.12,0.27,clamp(brightness/0.3,0.0,1.0));
        float profile=1.0-smoothstep(max(radius-filter_width,0.0),radius+filter_width,
                                    length(offset+centre-local));
        // Fade unresolved stars instead of letting a wide pixel turn into a giant dot.
        stars+=brightness*brightness*profile/(1.0+pixel*pixel);
    }
    float moon_clear=1.0-clamp(pow(min(abs(sun_dot)*1.002,1.0),100.0),0.0,1.0);
    if(env.water_origin.z<0.5) moon_clear=1.0;
    return vec3(0.38,0.4,0.5)*40.0*stars*min(direction.y*3.0,1.0)*night*moon_clear;
}
vec3 celestial_sky(vec3 direction, bool ground) {
    vec3 background=env.flags.z>0.5 ? comp_sky(direction,ground) : sky_colour(direction);
    if(env.water_origin.z<0.5 && env.water_origin.w<0.5) return background;
    vec3 colour=pow(max(background,vec3(0)),vec3(1.0/2.2));
    float sun_dot=dot(direction,env.solar.xyz);
    float cave=clamp(1.0-(env.camera_time.y-128.0)/61.9,0.0,1.0-env.limits.w);
    colour+=celestial_stars(direction,sun_dot)*(1.0-cave);
    if(env.water_origin.z>0.5 && abs(sun_dot)>0.9975) {
        bool sun=sun_dot>0.0;
        vec3 axis=sun ? env.solar.xyz : -env.solar.xyz;
        float t=clamp(400.0*(abs(sun_dot)-0.9975),0.0,1.0);
        float disc=t*(2.0-t); // Reference sqrt1, not sqrt().
        float horizon=smooth1(sq(clamp((axis.y+0.1)*10.0,0.0,1.0)));
        // Clip the below-horizon portion of a rising/setting disc smoothly.
        horizon*=smoothstep(-0.015,0.005,direction.y);
        if(sun) {
            disc=disc*disc*horizon*(1.0-0.65*cave);
            colour=mix(colour,vec3(0.9,0.5,0.3)*25.0,disc);
        } else {
            disc=max(disc-0.25,0.0)*1.33333*horizon*(1.0-0.5*cave);
            // The orbital pole stays perpendicular to the sun/moon axis.
            // Moon-local coordinates prevent the surface from swimming with the camera.
            vec3 pole=vec3(0.0,-0.6427876097,0.7660444431);
            vec3 right=normalize(cross(pole,axis));
            vec3 up=cross(axis,right);
            vec2 uv=vec2(dot(direction,right),dot(direction,up))/0.070666;
            float noise=moon_noise(uv*3.0+0.617)+0.7*moon_noise(uv*7.5+0.617)
                         +0.5*moon_noise(uv*15.0+0.617);
            noise=max(noise-0.75,0.0)*1.7;
            float night=env.cycle.y*(2.0-env.cycle.y);
            vec3 moon=vec3(0.38,0.4,0.5)*max(1.2-(0.2+0.2*night)*noise,0.1)*4.0;
            colour=mix(colour,moon,clamp(disc,0.0,1.0));
        }
    }
    return pow(max(colour,vec3(0)),vec3(2.2));
}
#endif
