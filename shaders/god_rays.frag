#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
layout(set = 0, binding = 1) uniform ShadowData
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
layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;

layout(push_constant) uniform GodRayPush
{
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 sunPositionDirection;
    vec4 params;
    vec4 depthParams;
} pushData;

layout(location = 0) in vec2 fragNdc;
layout(location = 1) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

float saturate(float value)
{
    return clamp(value, 0.0, 1.0);
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

float linearDepth(float depth)
{
    float nearPlane = pushData.depthParams.x;
    float farPlane = pushData.depthParams.y;
    float a = farPlane / (farPlane - nearPlane);
    float b = -(nearPlane * farPlane) / (farPlane - nearPlane);
    return b / (depth - a);
}

bool insideShadowDistance(float distanceFromCamera)
{
    return distanceFromCamera <= shadowData.cascadeSplits.x;
}

float shadowVisibility(vec3 worldPosition)
{
    vec4 lightClip = shadowData.lightViewProjection[0] * vec4(worldPosition, 1.0);
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 shadowUv = lightNdc.xy * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || lightNdc.z < 0.0 || lightNdc.z > 1.0)
    {
        return 1.0;
    }

    float texel = 1.0 / max(shadowData.params.y, 1.0);
    float cascadeTexelSize = max(shadowData.cascadeTexelSizes.x, 0.0001);
    float bias = clamp(shadowData.params.z + cascadeTexelSize * 0.00045, 0.00008, 0.00040);
    float radius = 2.25;
    mat2 poissonRotation = rotation2D(interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853);

    const vec2 poissonOffsets[8] = vec2[](
        vec2(-0.326, -0.406),
        vec2(-0.840, -0.074),
        vec2(-0.203, 0.621),
        vec2(0.962, -0.195),
        vec2(0.473, -0.480),
        vec2(0.519, 0.767),
        vec2(0.185, -0.893),
        vec2(0.896, 0.412)
    );

    float lit = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        vec2 offset = poissonRotation * poissonOffsets[i] * texel * radius;
        float closestDepth = texture(shadowMap, vec3(shadowUv + offset, 0.0)).r;
        lit += lightNdc.z - bias <= closestDepth ? 1.0 : 0.0;
    }
    return lit * 0.125;
}

vec3 reconstructWorldPosition(float viewZ)
{
    float viewX = fragNdc.x * viewZ * pushData.params.x * pushData.params.y;
    float viewY = -fragNdc.y * viewZ * pushData.params.x;
    return pushData.cameraPosition.xyz +
        pushData.cameraRight.xyz * viewX +
        pushData.cameraUp.xyz * viewY +
        pushData.cameraForward.xyz * viewZ;
}

void main()
{
    float sunVisible = pushData.sunPositionDirection.w;
    float intensity = pushData.params.w * sunVisible;
    if (intensity <= 0.0 || shadowData.params.x < 0.5)
    {
        discard;
    }

    float rawDepth = texture(sceneDepth, fragUv).r;
    float sceneViewZ = rawDepth >= 0.9999 ? pushData.params.z : linearDepth(rawDepth);
    float viewFactor = clamp(1.0 - 0.35 * dot(fragNdc, fragNdc), 0.55, 1.0);
    float maxViewZ = clamp(min(sceneViewZ, pushData.params.z * viewFactor), pushData.depthParams.x, pushData.params.z);
    float startViewZ = max(pushData.depthParams.x * 4.0, 0.75);
    if (maxViewZ <= startViewZ + 0.1)
    {
        discard;
    }

    const int sampleCount = 20;
    float stepLength = (maxViewZ - startViewZ) / float(sampleCount);
    float jitter = interleavedGradientNoise(gl_FragCoord.xy);
    vec3 sunPositionDirection = normalize(pushData.sunPositionDirection.xyz);
    float density = 0.0;
    float previousLit = 1.0;

    for (int i = 0; i < sampleCount; ++i)
    {
        float sampleViewZ = startViewZ + (float(i) + jitter) * stepLength;
        vec3 worldPosition = reconstructWorldPosition(sampleViewZ);
        vec3 toSample = worldPosition - pushData.cameraPosition.xyz;
        float sampleDistance = length(toSample);

        if (!insideShadowDistance(sampleDistance))
        {
            break;
        }

        vec3 rayDirection = toSample / max(sampleDistance, 0.0001);
        float lit = shadowVisibility(worldPosition);
        float transitionBoost = max(lit - previousLit * 0.45, 0.0);
        float percent = sampleViewZ / max(maxViewZ, 0.0001);
        float nearFade = smoothstep(startViewZ, startViewZ + 4.0, sampleViewZ);
        float distanceFade = exp(-sampleDistance * 0.008);
        float viewLight = pow(saturate(dot(rayDirection, sunPositionDirection) * 0.5 + 0.5), 3.0);
        float sampleWeight = nearFade * distanceFade * mix(1.0, 0.35, percent);
        density += (lit * 0.18 + transitionBoost * 0.65) * sampleWeight * (0.18 + viewLight * 0.82);
        previousLit = lit;
    }

    density /= float(sampleCount);
    float shaft = max(density - 0.018, 0.0);
    shaft = pow(shaft, 1.10);
    vec3 color = min(vec3(1.0, 0.82, 0.48) * shaft * intensity, vec3(0.18, 0.15, 0.10));
    outColor = vec4(color, 0.0);
}
