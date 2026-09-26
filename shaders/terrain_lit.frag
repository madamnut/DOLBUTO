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

float shadowHash(vec2 pixel)
{
    vec3 value = fract(vec3(pixel.xyx) * 0.1031);
    value += dot(value, value.yzx + 33.33);
    return fract((value.x + value.y) * value.z);
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
    float penumbra = smoothstep(0.00025, 0.0048, depthDelta);
    float poissonRadius = mix(basePoissonRadius, maxPoissonRadius, penumbra);
    vec2 shadowPixel = floor(shadowUv / max(texel, 0.0000001));
    mat2 poissonRotation = rotation2D(shadowHash(shadowPixel) * 6.2831853);

    float receiverDepth = lightNdc.z - bias;
    float compareWidth = max(0.000045, bias * 0.35);
    float lit = smoothstep(-compareWidth, compareWidth, centerDepth - receiverDepth) * 1.5;
    float weight = 1.5;
    const vec2 poissonOffsets[16] = vec2[](
        vec2(-0.942016, -0.399062),
        vec2( 0.945586, -0.768907),
        vec2(-0.094184, -0.929389),
        vec2( 0.344959,  0.293878),
        vec2(-0.915886,  0.457714),
        vec2(-0.815442, -0.879125),
        vec2(-0.382775,  0.276768),
        vec2( 0.974844,  0.756484),
        vec2( 0.443233, -0.975116),
        vec2( 0.537430, -0.473734),
        vec2(-0.264969, -0.418930),
        vec2( 0.791975,  0.190902),
        vec2(-0.241888,  0.997066),
        vec2(-0.814100,  0.914376),
        vec2( 0.199841,  0.786414),
        vec2( 0.143832, -0.141008)
    );
    for (int i = 0; i < 16; ++i)
    {
        vec2 rotatedOffset = poissonRotation * poissonOffsets[i];
        float closestDepth = texture(depthMap, vec3(shadowUv + rotatedOffset * texel * poissonRadius, 0.0)).r;
        float sampleWeight = mix(1.12, 0.82, clamp(length(poissonOffsets[i]), 0.0, 1.0));
        lit += smoothstep(-compareWidth, compareWidth, closestDepth - receiverDepth) * sampleWeight;
        weight += sampleWeight;
    }
    return lit / weight;
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
    float normalOffset = cascadeTexelSize * (0.28 + 0.62 * (1.0 - ndotl));
    vec3 shadowSamplePosition = worldPosition + normal * normalOffset;

    float texel = 1.0 / max(shadowData.params.y, 1.0);
    float receiverBias = clamp(shadowData.params.z + cascadeTexelSize * 0.00082, 0.00012, 0.00055);
    float bias = receiverBias * (1.0 + (1.0 - ndotl) * 1.10);
    float distanceFade = 1.0 - smoothstep(shadowData.cascadeSplits.x * 0.76, shadowData.cascadeSplits.x, cameraDistance);
    float sunHeightSoftness = 1.0 + (1.0 - smoothstep(0.16, 0.64, sunPositionDirection.y)) * 0.55;
    float basePoissonRadius = mix(3.40, 1.55, distanceFade) * sunHeightSoftness;
    float maxPoissonRadius = mix(6.00, 3.00, distanceFade) * sunHeightSoftness;
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
            float causticsStrength = fragWaterTint * daylight * clamp(finalLight, 0.0, 1.0) * 0.24;
            color.rgb += vec3(0.16, 0.28, 0.22) * caustics * causticsStrength;
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
