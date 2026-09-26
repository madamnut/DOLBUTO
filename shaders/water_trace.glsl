// Complementary Unbound reflection quality 2: 30 steps, six refinements.
// Vulkan uses Z=0..1; colour is already HDR here instead of packed sqrt RGB.
struct ReflectionHit { vec3 colour; float confidence; float distance; };
ReflectionHit trace_water_reflection(vec3 wave_normal) {
    vec3 incident=normalize(relative_position);
    vec3 geometric=dot(face_normal,-relative_position)<0.0 ? -face_normal : face_normal;
    vec3 normal=normalize(mix(geometric,wave_normal,0.8));
    float fresnel=clamp(1.0+dot(wave_normal,incident),0.0,1.0);
    float distance=length(relative_position);
    vec3 start=relative_position+normal*(distance*0.025*(1.0-fresnel)+0.05);
    vec3 vector=reflection_direction(relative_position,face_normal,wave_normal)*0.5;
    vec3 total=vector, position=relative_position+vector;
    vec3 target=vec3(0); vec2 uv=vec2(0); float depth=1.0, error=1e9;
    int refinements=0;
    float jitter=water_bayer(gl_FragCoord.xy);
    bool valid=false;
    for(int i=0;i<30;++i) {
        vec4 projected=water.matrix*vec4(position,1);
        if(projected.w<=0.0) break;
        uv=projected.xy/projected.w*0.5+0.5;
        // Native images clamp at the viewport; never accept a clamped edge hit.
        if(any(lessThanEqual(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1)))) break;
        depth=textureLod(scene_depth,uv,0.0).r;
        target=unproject(uv,depth);
        error=length(position-target);
        // A fixed cutoff below 1 incorrectly classifies distant LOD terrain as sky.
        valid=depth<1.0;
        if(valid && error*0.33333<length(vector)) {
            if(++refinements>=6) break;
            total-=vector;
            vector*=0.1;
        }
        vector*=2.0;
        total+=vector*(0.95+0.1*jitter);
        position=start+total;
        if(length(total)>water.trace.x) { valid=false; break; }
    }
    float travel=length(start-target);
    // Reject unconverged/clamped/background samples rather than seeding blue speckles.
    if(!valid || refinements<6 || travel>water.trace.x || dot(target-relative_position,geometric)<-0.05)
        return ReflectionHit(vec3(0),0.0,-1.0);
    vec2 cdist=abs(uv-0.5)/vec2(0.6,0.55);
    vec2 edge=pow(cdist,vec2(8));
    vec2 sample_uv=uv;
    sample_uv.y+=(jitter-0.5)*(0.05*(edge.x+edge.y));
    vec3 colour=water_gamma(textureLod(scene_colour,clamp(sample_uv,vec2(0),vec2(1)),0.0).rgb);
    float border=clamp(1.0-pow(max(cdist.x,cdist.y),50.0),0.0,1.0);
    edge.x*=edge.x;
    edge=clamp(1.0-edge,0.0,1.0);
    float confidence=border*pow(edge.x*edge.y,2.0+3.0*dot(colour,vec3(0.299,0.587,0.114)));
    confidence*=clamp(length(target)-distance+3.0,0.0,1.0);
    return ReflectionHit(colour,confidence,travel);
}
