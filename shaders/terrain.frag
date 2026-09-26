#version 450

layout(binding = 0) uniform sampler2DArray terrainTexture;

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
layout(location = 6) flat in float fragAlphaBlend;
layout(location = 7) flat in float fragSkyLight;
layout(location = 8) flat in float fragBlockLight;
layout(location = 9) flat in float fragWaterTint;
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

float causticsPattern(vec3 worldPosition, float time)
{
    vec2 p = worldPosition.xz * 0.42 + vec2(worldPosition.y * 0.10, -worldPosition.y * 0.07);
    vec2 flowA = p + vec2(time * 0.055, -time * 0.040);
    vec2 flowB = mat2(0.62, 0.78, -0.78, 0.62) * p + vec2(-time * 0.035, time * 0.050);
    float a = abs(sin(flowA.x * 3.8 + sin(flowA.y * 2.2 + time * 0.22)));
    float b = abs(sin(flowB.y * 4.3 + cos(flowB.x * 2.0 - time * 0.18)));
    float lines = smoothstep(0.82, 0.985, a) * smoothstep(0.52, 0.95, b);
    float soft = smoothstep(0.72, 1.0, abs(sin((flowA.x + flowB.y) * 2.4 + time * 0.16)));
    return clamp(lines * 0.75 + soft * 0.25, 0.0, 1.0);
}

void main()
{
    float chunkFade = clamp(pushData.dynamicLightParams.y, 0.0, 1.0);
    bool opaqueTerrain = fragAlphaBlend >= 0.999;
    float cameraDistance = length(fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    float textureLayer = fragTextureLayer;
    float fireBaseLayer = pushData.fluidWaterParams.z;
    float fireFrameCount = pushData.fluidWaterParams.w;
    bool fireAnimated = fireFrameCount > 1.0 && abs(textureLayer - fireBaseLayer) < 0.5;
    if (fireAnimated)
    {
        textureLayer = fireBaseLayer + mod(floor(pushData.cameraPosition.w * 12.0), fireFrameCount);
    }
    vec4 color = textureLod(terrainTexture, vec3(fragUv, textureLayer), mipLevel);
    if (fragAlphaBlend >= 0.999 && color.a < 0.5)
    {
        discard;
    }
    if (fragAlphaBlend < 0.999 && color.a < 0.01)
    {
        discard;
    }
    float skyLight = fragSkyLight * pushData.fluidWaterParams.y;
    float finalLight = lightCurve(max(max(skyLight, fragBlockLight), dynamicLight()));
    color.rgb *= fragAo * finalLight;
    if (fireAnimated)
    {
        color.rgb *= 1.85;
    }
    if (fragWaterTint > 0.0)
    {
        vec3 waterColor = vec3(0.18, 0.55, 0.70);
        float waterMix = clamp(fragWaterTint * max(pushData.fluidWaterParams.x, 0.35), 0.0, 0.75);
        color.rgb = mix(color.rgb, waterColor * finalLight, waterMix);
        if (!fireAnimated)
        {
            vec3 worldPosition = fragWorldPosition + pushData.cameraPosition.xyz;
            float daylight = smoothstep(0.18, 0.88, pushData.fluidWaterParams.y);
            float caustics = causticsPattern(worldPosition, pushData.cameraPosition.w);
            float causticsStrength = fragWaterTint * daylight * clamp(finalLight, 0.0, 1.0) * 0.18;
            color.rgb += vec3(0.14, 0.24, 0.20) * caustics * causticsStrength;
        }
    }
    float hurtFlash = clamp(pushData.dynamicLightParams.z, 0.0, 1.0);
    if (hurtFlash > 0.0)
    {
        color.rgb = mix(color.rgb, vec3(1.0, 0.0, 0.0), hurtFlash);
    }
    float fog = airFogFactor(cameraDistance, pushData.fluidWaterParams.y);
    color.rgb = mix(color.rgb, airFogColor(pushData.fluidWaterParams.y), fog);
    float outputAlpha = opaqueTerrain ? chunkFade : color.a * fragAlphaBlend * chunkFade;
    outColor = vec4(color.rgb, outputAlpha);
    outBloom = fireAnimated ? vec4(color.rgb * 1.45 * (1.0 - fog * 0.65), outputAlpha) : vec4(0.0, 0.0, 0.0, outputAlpha);
}
