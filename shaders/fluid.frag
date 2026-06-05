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
    float cameraDistance = length(fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    vec4 color = textureLod(fluidTexture, vec3(fragUv, fragTextureLayer), mipLevel);
    if (color.a < 0.05)
    {
        discard;
    }

    float skyLight = fragSkyLight * pushData.fluidWaterParams.y;
    float finalLight = lightCurve(max(max(skyLight, fragBlockLight), dynamicLight()));
    color.rgb *= fragAo * finalLight;
    outColor = vec4(color.rgb, color.a * pushData.fluidWaterParams.x);
}
