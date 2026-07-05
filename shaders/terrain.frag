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
        color.rgb *= 2.0;
    }
    if (fragWaterTint > 0.0)
    {
        vec3 waterColor = vec3(0.18, 0.55, 0.70);
        float waterMix = clamp(fragWaterTint * max(pushData.fluidWaterParams.x, 0.35), 0.0, 0.75);
        color.rgb = mix(color.rgb, waterColor * finalLight, waterMix);
    }
    float hurtFlash = clamp(pushData.dynamicLightParams.z, 0.0, 1.0);
    if (hurtFlash > 0.0)
    {
        color.rgb = mix(color.rgb, vec3(1.0, 0.0, 0.0), hurtFlash);
    }
    float outputAlpha = opaqueTerrain ? chunkFade : color.a * fragAlphaBlend * chunkFade;
    outColor = vec4(color.rgb, outputAlpha);
    outBloom = fireAnimated ? vec4(color.rgb * 2.0, outputAlpha) : vec4(0.0, 0.0, 0.0, outputAlpha);
}
