#version 450

layout(binding = 0) uniform sampler2DArray fluidTexture;
layout(set = 2, binding = 0) uniform sampler2D fluidNormalTexture;
layout(set = 3, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 4, binding = 0) uniform sampler2D sceneDepthTexture;

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
    vec4 fluidWaterParams;
    vec4 fluidWaterNormalParams;
    vec4 fluidWaterNormalSpeed;
    vec4 ssrParams;
    vec4 ssrMarchParams;
    vec4 ssrFallbackColor;
    vec4 ssrDepthParams;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragAo;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) flat in float fragTextureLayer;
layout(location = 4) flat in float fragMipDistanceScale;
layout(location = 5) flat in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

vec2 sampleWaterNormalOffset(vec2 uv)
{
    return texture(fluidNormalTexture, uv).rg - vec2(0.5);
}

vec3 projectToUvDepth(vec3 worldPosition)
{
    vec4 clip = pushData.mvp * vec4(worldPosition, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return vec3(ndc.xy * 0.5 + 0.5, ndc.z);
}

vec2 sceneTextureUv(vec2 uv)
{
    return uv;
}

float linearizeProjectionDepth(float depth)
{
    float nearPlane = pushData.ssrDepthParams.x;
    float farPlane = pushData.ssrDepthParams.y;
    float projectionA = farPlane / (farPlane - nearPlane);
    float projectionB = -(nearPlane * farPlane) / (farPlane - nearPlane);
    return projectionB / (depth - projectionA);
}

float edgeFade(vec2 uv)
{
    float edgeDistance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    return pushData.ssrParams.w > 0.0 ? clamp(edgeDistance / pushData.ssrParams.w, 0.0, 1.0) : 1.0;
}

float sampleSceneLinearDepth(vec2 uv)
{
    float sceneProjectionDepth = textureLod(sceneDepthTexture, sceneTextureUv(uv), 0.0).r;
    return linearizeProjectionDepth(sceneProjectionDepth);
}

vec3 sampleSsr(vec3 startPosition, vec3 reflectionDir, vec3 fallbackColor, out float hitWeight)
{
    hitWeight = 0.0;
    if (pushData.ssrParams.x < 0.5)
    {
        return fallbackColor;
    }

    int maxSteps = int(pushData.ssrMarchParams.x);
    float stepSize = max(pushData.ssrMarchParams.y, 0.001);
    float thickness = pushData.ssrMarchParams.z;
    float maxDistance = pushData.ssrParams.z;
    vec3 startUvDepth = projectToUvDepth(startPosition);
    if (startUvDepth.x <= 0.0 || startUvDepth.x >= 1.0 || startUvDepth.y <= 0.0 || startUvDepth.y >= 1.0 || startUvDepth.z <= 0.0 || startUvDepth.z >= 1.0)
    {
        return fallbackColor;
    }

    const int maxRefinements = 6;
    const float stepGrowth = 1.6;
    const float refineScale = 0.25;
    float previousDistance = 0.0;
    float currentDistance = stepSize;
    float currentStep = stepSize;
    int refinementCount = 0;
    for (int i = 0; i < maxSteps && currentDistance <= maxDistance; ++i)
    {
        vec3 rayPosition = startPosition + reflectionDir * currentDistance;
        vec3 uvDepth = projectToUvDepth(rayPosition);
        if (uvDepth.x <= 0.0 || uvDepth.x >= 1.0 || uvDepth.y <= 0.0 || uvDepth.y >= 1.0 || uvDepth.z <= 0.0 || uvDepth.z >= 1.0)
        {
            break;
        }

        float rayLinearDepth = linearizeProjectionDepth(uvDepth.z);
        float sceneLinearDepth = sampleSceneLinearDepth(uvDepth.xy);
        float depthDelta = rayLinearDepth - sceneLinearDepth;
        if (depthDelta >= 0.0 && depthDelta <= thickness)
        {
            vec2 sampleUv = sceneTextureUv(uvDepth.xy);
            hitWeight = edgeFade(uvDepth.xy);
            return texture(sceneColorTexture, sampleUv).rgb;
        }
        if (depthDelta > thickness && refinementCount < maxRefinements)
        {
            currentDistance = previousDistance;
            currentStep *= refineScale;
            ++refinementCount;
            continue;
        }

        previousDistance = currentDistance;
        currentDistance += currentStep;
        if (refinementCount == 0)
        {
            currentStep *= stepGrowth;
        }
    }

    return fallbackColor;
}

void main()
{
    float cameraDistance = distance(pushData.cameraPosition.xyz, fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    vec4 color = textureLod(fluidTexture, vec3(fragUv, fragTextureLayer), mipLevel);
    if (color.a < 0.05)
    {
        discard;
    }

    vec3 viewDir = normalize(pushData.cameraPosition.xyz - fragWorldPosition);
    vec3 baseNormal = normalize(fragNormal);
    float time = pushData.cameraPosition.w;
    vec2 waterUv = 0.032 * (fragWorldPosition.xz + fragWorldPosition.y * 2.0);
    waterUv *= pushData.fluidWaterNormalParams.x;
    waterUv *= 2.5;
    vec2 wind = vec2(0.0, -time * pushData.fluidWaterNormalSpeed.x * 0.018) * 2.5;
    vec2 normalMed = sampleWaterNormalOffset(waterUv + wind);
    vec2 normalSmall = sampleWaterNormalOffset(waterUv * 4.0 - wind * 2.0);
    vec2 normalBig = sampleWaterNormalOffset(waterUv * 0.25 - wind * 0.5);
    normalBig += sampleWaterNormalOffset(waterUv * 0.05 - wind * 0.05);
    float baseFresnel = clamp(1.0 - abs(dot(viewDir, baseNormal)), 0.0, 1.0);
    vec2 normalOffset = normalMed * 1.7 + normalSmall * 0.75 + normalBig * 2.0;
    normalOffset *= 6.0 * pushData.fluidWaterNormalParams.z * (1.0 - 0.7 * baseFresnel);
    vec3 tangent = abs(baseNormal.y) > 0.5 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), baseNormal));
    vec3 bitangent = normalize(cross(baseNormal, tangent));
    vec3 normal = normalize(baseNormal +
        tangent * normalOffset.x +
        bitangent * normalOffset.y);
    vec3 currentUvDepth = projectToUvDepth(fragWorldPosition);
    float sceneDepth = sampleSceneLinearDepth(currentUvDepth.xy);
    float currentLinearDepth = linearizeProjectionDepth(currentUvDepth.z);
    if (currentLinearDepth > sceneDepth + 0.05)
    {
        discard;
    }

    float fresnel = pow(1.0 - abs(dot(viewDir, normal)), pushData.fluidWaterParams.z);
    float alpha = mix(pushData.fluidWaterParams.x, pushData.fluidWaterParams.y, fresnel);
    vec3 reflectionDir = normalize(reflect(-viewDir, normal));
    vec3 fallbackReflection = pushData.ssrFallbackColor.rgb;
    float ssrWeight = 0.0;
    vec3 reflectionColor = sampleSsr(fragWorldPosition + normal * 0.02, reflectionDir, fallbackReflection, ssrWeight);
    float reflectionWeight = mix(pushData.ssrMarchParams.w, pushData.ssrParams.y * ssrWeight, ssrWeight) * fresnel;

    color.rgb *= fragAo;
    color.rgb = mix(color.rgb, reflectionColor, reflectionWeight);
    outColor = vec4(color.rgb, color.a * alpha);
}
