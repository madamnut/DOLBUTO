#version 450

layout(set = 0, binding = 0) uniform sampler2DArray terrainTexture;

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
    vec4 fluidWaterParams;
    vec4 dynamicLightParams;
} pushData;

layout(set = 2, binding = 0) uniform ShadowData
{
    mat4 lightViewProjection[1];
    mat4 previousLightViewProjection[1];
    vec4 cascadeSplits;
    vec4 sunPositionDirection;
    vec4 params;
    vec4 cascadeTexelSizes;
    vec4 previousCascadeTexelSizes;
    vec4 historyParams;
} shadowData;

layout(set = 2, binding = 1) uniform sampler2DArray shadowMap;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragAo;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) flat in float fragTextureLayer;
layout(location = 4) flat in float fragMipDistanceScale;
layout(location = 5) flat in vec3 fragNormal;
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

float interleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

mat2 rotation2D(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, s, -s, c);
}

const float ShadowDistortionBias = 0.8666667;

vec3 distortedShadowNdc(vec4 lightClip)
{
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    float distanceFromCenter = length(lightNdc.xy);
    float distortion = max(distanceFromCenter * ShadowDistortionBias + (1.0 - ShadowDistortionBias), 0.0001);
    lightNdc.xy /= distortion;
    return lightNdc;
}

float shadowVisibility(
    vec3 worldPosition,
    mat4 lightViewProjection,
    sampler2DArray depthMap,
    float bias,
    float basePoissonRadius,
    float maxPoissonRadius,
    float texel)
{
    vec4 lightClip = lightViewProjection * vec4(worldPosition, 1.0);
    vec3 lightNdc = distortedShadowNdc(lightClip);
    vec2 shadowUv = lightNdc.xy * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || lightNdc.z < 0.0 || lightNdc.z > 1.0)
    {
        return 1.0;
    }

    float centerDepth = texture(depthMap, vec3(shadowUv, 0.0)).r;
    float depthDelta = max(lightNdc.z - bias - centerDepth, 0.0);
    float penumbra = smoothstep(0.00035, 0.0035, depthDelta);
    float poissonRadius = mix(basePoissonRadius, maxPoissonRadius, penumbra);
    mat2 poissonRotation = rotation2D(interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853);

    float lit = 0.0;
    const vec2 poissonOffsets[12] = vec2[](
        vec2(-0.326, -0.406),
        vec2(-0.840, -0.074),
        vec2(-0.696, 0.457),
        vec2(-0.203, 0.621),
        vec2(0.962, -0.195),
        vec2(0.473, -0.480),
        vec2(0.519, 0.767),
        vec2(0.185, -0.893),
        vec2(0.507, 0.064),
        vec2(0.896, 0.412),
        vec2(-0.322, -0.933),
        vec2(-0.792, -0.598)
    );
    for (int i = 0; i < 12; ++i)
    {
        vec2 rotatedOffset = poissonRotation * poissonOffsets[i];
        float closestDepth = texture(depthMap, vec3(shadowUv + rotatedOffset * texel * poissonRadius, 0.0)).r;
        lit += lightNdc.z - bias <= closestDepth ? 1.0 : 0.0;
    }
    return lit / 12.0;
}

float shadowFactor()
{
    if (pushData.dynamicLightParams.w < 0.5 || shadowData.params.x < 0.5)
    {
        return 1.0;
    }

    float cameraDistance = length(fragWorldPosition);
    if (cameraDistance > shadowData.cascadeSplits.x)
    {
        return 1.0;
    }

    vec3 normal = dot(fragNormal, fragNormal) > 0.0001 ? normalize(fragNormal) : vec3(0.0, 1.0, 0.0);
    vec3 sunPositionDirection = normalize(shadowData.sunPositionDirection.xyz);
    float ndotl = clamp(dot(normal, sunPositionDirection), 0.0, 1.0);
    float cascadeTexelSize = max(shadowData.cascadeTexelSizes.x, 0.0001);
    vec3 worldPosition = fragWorldPosition + pushData.cameraPosition.xyz;
    float normalOffset = cascadeTexelSize * (0.20 + 0.50 * (1.0 - ndotl));
    vec3 shadowSamplePosition = worldPosition + normal * normalOffset;

    float texel = 1.0 / max(shadowData.params.y, 1.0);
    float receiverBias = clamp(shadowData.params.z + cascadeTexelSize * 0.00065, 0.00008, 0.00045);
    float bias = receiverBias * (1.0 + (1.0 - ndotl) * 0.85);
    float distanceFade = 1.0 - smoothstep(shadowData.cascadeSplits.x * 0.82, shadowData.cascadeSplits.x, cameraDistance);
    float basePoissonRadius = mix(2.75, 1.20, distanceFade);
    float maxPoissonRadius = mix(5.50, 2.25, distanceFade);
    float visibility = shadowVisibility(shadowSamplePosition, shadowData.lightViewProjection[0], shadowMap, bias, basePoissonRadius, maxPoissonRadius, texel);
    float strength = clamp(shadowData.params.w, 0.0, 0.9);
    return mix(1.0, mix(1.0 - strength, 1.0, visibility), distanceFade);
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
    if (!fireAnimated)
    {
        skyLight *= shadowFactor();
    }
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
