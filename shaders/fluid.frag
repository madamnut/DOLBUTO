#version 450

layout(binding = 0) uniform sampler2DArray fluidTexture;

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
    vec4 fluidWaterParams;
    vec4 dynamicLightParams;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragAo;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) flat in float fragTextureLayer;
layout(location = 4) flat in float fragMipDistanceScale;
layout(location = 5) flat in vec3 fragNormal;
layout(location = 7) flat in float fragSkyLight;
layout(location = 8) flat in float fragBlockLight;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloom;

float lightCurve(float normalizedLight)
{
    float x = clamp(normalizedLight, 0.0, 1.0);
    return x * x * (0.667482 + 0.332518 * x);
}

float dynamicLight()
{
    float emission = clamp(pushData.dynamicLightParams.x, 0.0, 15.0);
    if (emission <= 0.0)
    {
        return 0.0;
    }
    return clamp((emission - length(fragWorldPosition)) / 15.0, 0.0, 1.0);
}

vec3 airFogColor(float skyBrightness)
{
    float day = smoothstep(0.10, 0.85, skyBrightness);
    vec3 nightFog = vec3(0.014, 0.018, 0.032);
    vec3 dayFog = vec3(0.58, 0.70, 0.82);
    vec3 color = mix(nightFog, dayFog, day);
    float twilight = smoothstep(0.10, 0.35, skyBrightness) * (1.0 - smoothstep(0.55, 0.95, skyBrightness));
    return mix(color, vec3(0.58, 0.42, 0.34), twilight * 0.26);
}

float airFogFactor(float distanceFromCamera, float skyBrightness)
{
    float day = smoothstep(0.10, 0.85, skyBrightness);
    float start = mix(82.0, 140.0, day);
    float end = mix(260.0, 430.0, day);
    float ramp = smoothstep(start, end, distanceFromCamera);
    float density = 1.0 - exp(-max(distanceFromCamera - start, 0.0) * mix(0.0022, 0.00115, day));
    return clamp(max(ramp * 0.72, density), 0.0, mix(0.78, 0.58, day));
}

vec3 waterFogColor(float skyBrightness)
{
    float day = smoothstep(0.10, 0.85, skyBrightness);
    vec3 nightWater = vec3(0.018, 0.052, 0.085);
    vec3 dayWater = vec3(0.075, 0.255, 0.350);
    vec3 color = mix(nightWater, dayWater, day);
    float twilight = smoothstep(0.08, 0.35, skyBrightness) * (1.0 - smoothstep(0.55, 0.95, skyBrightness));
    return mix(color, vec3(0.115, 0.205, 0.270), twilight * 0.30);
}

vec3 waterSurfaceColor(float skyBrightness, float fresnel, float distanceFromCamera)
{
    float day = smoothstep(0.10, 0.85, skyBrightness);
    vec3 shallowDay = vec3(0.105, 0.470, 0.560);
    vec3 deepDay = vec3(0.028, 0.160, 0.245);
    vec3 shallowNight = vec3(0.025, 0.085, 0.120);
    vec3 deepNight = vec3(0.010, 0.035, 0.060);
    float depthLike = smoothstep(16.0, 140.0, distanceFromCamera);
    vec3 shallow = mix(shallowNight, shallowDay, day);
    vec3 deep = mix(deepNight, deepDay, day);
    vec3 water = mix(shallow, deep, depthLike);
    vec3 reflectedSky = mix(airFogColor(skyBrightness), vec3(0.70, 0.84, 0.94), day * 0.55);
    return mix(water, reflectedSky, fresnel * mix(0.26, 0.52, day));
}

float waterGlitter(vec3 worldPosition, vec3 viewDir, float fresnel, float skyBrightness, float time)
{
    float day = smoothstep(0.18, 0.88, skyBrightness);
    vec2 p = worldPosition.xz * 0.72;
    float waveA = sin(p.x * 2.8 + p.y * 1.4 + time * 0.60);
    float waveB = sin(p.x * -1.7 + p.y * 3.3 - time * 0.47);
    float waveC = sin((p.x + p.y) * 4.1 + time * 0.35);
    float spark = smoothstep(0.78, 0.985, (waveA + waveB + waveC) * 0.1667 + 0.5);
    float grazing = smoothstep(0.20, 0.95, fresnel);
    float towardSurface = smoothstep(-0.18, 0.22, viewDir.y);
    return spark * grazing * towardSurface * day;
}

void main()
{
    float chunkFade = clamp(pushData.dynamicLightParams.y, 0.0, 1.0);
    float cameraDistance = length(fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    vec4 sampled = textureLod(fluidTexture, vec3(fragUv, fragTextureLayer), mipLevel);
    if (sampled.a < 0.05)
    {
        discard;
    }

    float skyLight = fragSkyLight * pushData.fluidWaterParams.y;
    float finalLight = lightCurve(max(max(skyLight, fragBlockLight), dynamicLight()));
    vec3 normal = dot(fragNormal, fragNormal) > 0.0001 ? normalize(fragNormal) : vec3(0.0, 1.0, 0.0);
    vec3 viewDir = normalize(-fragWorldPosition);
    float ndotv = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 4.0);
    fresnel = clamp(fresnel * 1.25 + 0.035, 0.0, 1.0);

    vec3 worldPosition = fragWorldPosition + pushData.cameraPosition.xyz;
    vec3 surface = waterSurfaceColor(pushData.fluidWaterParams.y, fresnel, cameraDistance);
    vec3 textureDetail = mix(vec3(1.0), sampled.rgb * 1.55, 0.16);
    float waterLight = mix(0.58, 1.0, finalLight);
    float glitter = waterGlitter(worldPosition, viewDir, fresnel, pushData.fluidWaterParams.y, pushData.cameraPosition.w);
    vec3 color = surface * textureDetail * waterLight * mix(0.90, 1.12, fresnel);
    color += vec3(0.62, 0.82, 0.78) * glitter * 0.18;
    color = mix(color, waterFogColor(pushData.fluidWaterParams.y), smoothstep(70.0, 230.0, cameraDistance) * 0.34);
    float fog = airFogFactor(cameraDistance, pushData.fluidWaterParams.y);
    color = mix(color, airFogColor(pushData.fluidWaterParams.y), fog * 0.55);
    float alphaByView = mix(0.52, 1.10, fresnel);
    float alphaByDistance = mix(0.84, 1.12, smoothstep(18.0, 120.0, cameraDistance));
    float outputAlpha = sampled.a * pushData.fluidWaterParams.x * alphaByView * alphaByDistance * chunkFade;
    outColor = vec4(color, clamp(outputAlpha, 0.0, 0.92));
    outBloom = vec4(0.0, 0.0, 0.0, outputAlpha);
}
