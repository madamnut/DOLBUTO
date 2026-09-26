// Adapted from Complementary Unbound mainClouds/unboundClouds/cloudColors.
// Clear Overworld, default size. See assets/licenses/Complementary.txt.
#ifndef CLOUDS_GLSL
#define CLOUDS_GLSL
float cloud_noise_reference(vec3 p) {
    // Reference Noise3D: two samples of the original noise.png red channel.
    float z=fract(p.z)*128.0, slice=floor(z);
    vec2 offset=vec2(23.0,29.0)/128.0;
    float a=textureLod(reference_noise,p.xy+offset*slice,0.0).r;
    float b=textureLod(reference_noise,p.xy+offset*(slice+1.0),0.0).r;
    return mix(a,b,fract(z));
}
float cloud_noise_reference(vec3 world,vec3 camera,bool reflection,float stretch) {
    int quality=int(env.cloud.z);
    // The original .00012/.00006 become 16/8 integer repeats over our world.
    float narrowness=(quality==1 ? 8.0 : 16.0)/131072.0;
    vec3 p=world*narrowness;
    float wind=env.cloud.w*(quality==1 ? 0.5 : 1.0);
    int count=quality==1 ? 2 : 4;
    float persistence=quality==1 ? 0.6 : 0.5;
    float multiplier=quality==1 ? 0.95 : (quality==2 || reflection ? 1.14 : 1.0);
    if(reflection) multiplier*=1.2;
    float noise=0.0, weight=1.0, total=0.0;
    for(int i=0;i<count;++i) {
        float sample_value=quality==1
            ? textureLod(reference_noise,p.xz-vec2(0,wind),0.0).b
            : cloud_noise_reference(p-vec3(0,0,wind));
        noise+=sample_value*weight;
        total+=weight;
        p*=3.0; wind*=0.5; weight*=persistence;
    }
    noise=sq(noise/total);
    multiplier*=0.8+0.1*clamp((camera.y-world.y)/(stretch*2.0),0.0,1.0);
    // Existing 50% setting corresponds to original CLOUD_UNBOUND_AMOUNT=1.
    noise*=multiplier*(env.cloud.y*2.0);
    float threshold=clamp(abs(env.cloud.x-world.y)/stretch,0.001,0.999);
    return noise-(pow(threshold,8.0)*0.2+0.25);
}
float cloud_shadow(vec3 position) {
    if(env.effects.x<0.5 || env.cloud.y<=0.0) return 1.0;
    vec3 world=position+env.camera_time.xyz;
    vec2 p=(world.xz+world.y*0.25)*(26.0/131072.0);
    // Reference shadow drift is one third of the cloud-noise phase.
    p.x+=env.cloud.w/3.0;
    const vec2 offsets[8]=vec2[8](vec2(0,1),vec2(.7071,.7071),vec2(1,0),vec2(.7071,-.7071),
        vec2(0,-1),vec2(-.7071,-.7071),vec2(-1,0),vec2(-.7071,.7071));
    float sum=0.0;
    for(int i=0;i<8;++i) sum+=textureLod(reference_noise,p+0.005*offsets[i],0.0).b;
    float visibility=smooth1(sq(min(sum*0.2,1.0)));
    return mix(1.0,visibility,min(env.cloud.y*2.0,1.0));
}
bool cloud_enclosed(vec3 relative,float distance) {
    if(env.flags.x<0.5 || env.limits.w>=0.999 || distance<=env.limits.x*0.9166667) return false;
    vec3 p=comp_shadow_pos(relative);
    if(length(p.xy*2.0-1.0)>=1.0 || p.z<=0.0 || p.z>=1.0) return false;
    return texture(shadow_all,p)<=0.0;
}
// Result is premultiplied display-space RGB, adapted to our existing compositor.
// Direct views and depth-edge fallback use the same quality. Reflections use
// the original non-DEFERRED1 64-block stride and 11-block half-height.
vec4 cloud_ray_from(vec3 origin,vec3 direction,float maximum,bool reflection,out float first) {
    first=1e8;
    if(env.flags.y<0.5 || env.cloud.y<=0.0 || maximum<=0.0) return vec4(0);
    int quality=int(env.cloud.z);
    float stretch=(reflection || quality==1) ? 11.0 : (quality==2 ? 16.0 : 18.0);
    float stride=reflection ? 64.0 : (quality==2 ? 32.0 : 16.0);
    vec3 camera=env.camera_time.xyz+origin;
    float low=env.cloud.x-stretch-camera.y, high=env.cloud.x+stretch-camera.y;
    float near_t=0.0,far_t=maximum;
    if(abs(direction.y)<1e-6) {
        if(low>0.0 || high<0.0) return vec4(0);
    } else {
        float a=low/direction.y,b=high/direction.y;
        near_t=max(0.0,min(a,b)); far_t=min(far_t,max(a,b));
    }
    // Horizontal distance, as in the reference. Explicit geometry/water depth
    // remains a strict upper bound, also when looking down from above clouds.
    far_t=min(far_t,4000.0/max(length(direction.xz),1e-6));
    if(far_t<=near_t) return vec4(0);
    float jitter=comp_noise();
    int count=min(int((far_t-near_t)/stride+jitter+1.0),256);
    vec3 add=direction*stride;
    vec3 trace=camera+near_t*direction+add*jitter;
    trace.y-=add.y;
    float facing=max(dot(direction,env.cycle.w>0.5 ? env.solar.xyz : -env.solar.xyz),0.0);
    float scattering=sq(facing)*abs(env.cycle.w-0.5)*5.0;
    float silver=pow(facing,100.0)*env.cycle.w;
    vec3 ambient=comp_ambient()*(sq(env.cycle.w)*(0.55+0.17*env.cycle.x)+0.35);
    vec3 light=comp_light(reflection ? 0 : 1)*1.3;
    float sky_fade=reflection || maximum>=8192.0 ? 1.0 : 0.0;
    vec3 sky=pow(max(env.flags.z>0.5 ? comp_sky(direction,env.view.y<0.5) : sky_colour(direction),vec3(0)),vec3(1.0/2.2));
    float sky_mult1=1.0-0.2*(1.0-sky_fade)*max(sq(env.cycle.w),env.cycle.y);
    float sky_mult2=1.0-0.33333*sky_fade;
    vec4 cloud=vec4(0);
    for(int i=0;i<count;++i) {
        trace+=add;
        if(abs(trace.y-env.cloud.x)>stretch) break;
        vec3 delta=trace-camera;
        float distance=length(delta),horizontal=length(delta.xz);
        if(horizontal>4000.0 || distance>maximum) break;
        float noise=cloud_noise_reference(trace,camera,reflection,stretch);
        if(noise<=0.00001 || cloud_enclosed(trace-env.camera_time.xyz,distance)) continue;
        if(first>1e7) first=distance;
        // Original low-quality first-hit height perturbation.
        if(quality==1 && !reflection && cloud.a==0.0)
            trace.y+=4.0*(textureLod(reference_noise,trace.xz*(128.0/131072.0),0.0).r-0.5);
        float opacity=min(noise*8.0,1.0);
        float shading=1.0-(env.cloud.x+stretch-trace.y)/(stretch*2.0);
        shading*=1.0+0.2*scattering*(1.0-opacity)+silver;
        vec3 sample_colour=ambient*(0.4+0.6*shading)+light*shading;
        float ratio=(4000.0-horizontal)/4000.0;
        float distance_factor=clamp(ratio,0.0,0.8)*1.25;
        float fog=pow(clamp(ratio,0.0,1.0),3.0);
        sample_colour=mix(sky,sample_colour*sky_mult1,fog*sky_mult2*0.72);
        // Complementary uses additive opacity with colour weighted by the
        // preceding opacity; do not replace it with Beer-Lambert integration.
        cloud.rgb=mix(cloud.rgb,sample_colour,1.0-min(cloud.a,1.0));
        cloud.a+=opacity*pow(distance_factor,0.5+10.0*pow(facing,90.0));
        if(cloud.a>0.9) { cloud.a=1.0; break; }
    }
    cloud.rgb*=cloud.a;
    return cloud;
}
vec4 cloud_ray(vec3 direction,float maximum,out float first) {
    return cloud_ray_from(vec3(0),direction,maximum,false,first);
}
vec3 reflected_sky(vec3 direction,vec3 origin) {
    float first;
    vec4 cloud=cloud_ray_from(origin,direction,1e8,true,first);
    return pow(max(pow(max(celestial_sky(direction,false),vec3(0)),vec3(1.0/2.2))*(1.0-cloud.a)+cloud.rgb,vec3(0)),vec3(2.2));
}
#endif
