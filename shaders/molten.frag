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

void main()
{
    float cameraDistance = length(fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    vec4 color = textureLod(fluidTexture, vec3(fragUv, fragTextureLayer), mipLevel);
    if (color.a < 0.05)
    {
        discard;
    }

    float skyLight = fragSkyLight * pushData.fluidWaterParams.y;
    float finalLight = lightCurve(max(max(skyLight, fragBlockLight), dynamicLight()));
    vec3 litColor = color.rgb * max(finalLight, 0.35) * fragAo;
    vec3 emissive = color.rgb * 1.35;
    float fog = airFogFactor(cameraDistance, pushData.fluidWaterParams.y);
    vec3 foggedColor = mix(litColor + color.rgb * 0.65, airFogColor(pushData.fluidWaterParams.y), fog);
    outColor = vec4(foggedColor, color.a);
    outBloom = vec4(emissive * (1.0 - fog * 0.65), color.a);
}
