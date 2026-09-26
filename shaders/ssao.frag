#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D sceneDepth;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 params;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec2 fragScreenUv;
layout(location = 0) out vec4 outColor;

float sampleDepth(vec2 uv)
{
    return texture(sceneDepth, clamp(uv, vec2(0.001), vec2(0.999))).r;
}

float localDepthContrast(vec2 uv, float centerDepth, vec2 texel)
{
    float leftDepth = sampleDepth(uv + vec2(-texel.x, 0.0));
    float rightDepth = sampleDepth(uv + vec2(texel.x, 0.0));
    float downDepth = sampleDepth(uv + vec2(0.0, -texel.y));
    float upDepth = sampleDepth(uv + vec2(0.0, texel.y));
    float horizontal = max(abs(centerDepth - leftDepth), abs(centerDepth - rightDepth));
    float vertical = max(abs(centerDepth - downDepth), abs(centerDepth - upDepth));
    return max(horizontal, vertical);
}

float contactOcclusion(vec2 uv, float centerDepth)
{
    if (centerDepth >= 0.9996)
    {
        return 1.0;
    }

    vec2 texel = max(pushData.params.xy, vec2(0.000001));
    float farDepth = smoothstep(0.992, 0.9994, centerDepth);
    float radius = max(pushData.params.z, 0.5) * mix(1.0, 0.55, farDepth);
    float strength = clamp(pushData.params.w, 0.0, 1.0);
    float depthBias = mix(0.00008, 0.00034, centerDepth);
    float depthRange = mix(0.0028, 0.014, centerDepth);
    float localContrast = localDepthContrast(uv, centerDepth, texel);
    float contactGate = smoothstep(depthBias * 1.5, depthRange * 0.65, localContrast);
    float depthFade = 1.0 - smoothstep(0.9975, 0.99965, centerDepth);
    float screenEdgeFade = smoothstep(0.0, 0.025, uv.x) *
        smoothstep(0.0, 0.025, uv.y) *
        smoothstep(0.0, 0.025, 1.0 - uv.x) *
        smoothstep(0.0, 0.025, 1.0 - uv.y);

    const vec2 offsets[16] = vec2[](
        vec2( 1.000,  0.000),
        vec2(-1.000,  0.000),
        vec2( 0.000,  1.000),
        vec2( 0.000, -1.000),
        vec2( 0.707,  0.707),
        vec2(-0.707,  0.707),
        vec2( 0.707, -0.707),
        vec2(-0.707, -0.707),
        vec2( 1.600,  0.450),
        vec2(-1.600,  0.450),
        vec2( 0.450,  1.600),
        vec2( 0.450, -1.600),
        vec2( 1.200, -1.150),
        vec2(-1.200, -1.150),
        vec2( 1.150,  1.200),
        vec2(-1.150,  1.200)
    );

    float occlusion = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float sampleDepthValue = sampleDepth(uv + offsets[i] * texel * radius);
        float depthDelta = centerDepth - sampleDepthValue;
        float sampleOcclusion = smoothstep(depthBias, depthBias + depthRange, depthDelta);
        float rangeFade = 1.0 - smoothstep(depthRange * 1.25, depthRange * 4.0, abs(depthDelta));
        float silhouetteReject = 1.0 - smoothstep(depthRange * 2.0, depthRange * 7.0, depthDelta);
        float weight = mix(1.0, 0.68, clamp(length(offsets[i]) / 1.7, 0.0, 1.0));
        occlusion += sampleOcclusion * rangeFade * silhouetteReject * weight;
        weightSum += weight;
    }

    float effectiveStrength = strength * mix(0.45, 1.0, contactGate) * depthFade * screenEdgeFade;
    float ao = 1.0 - (occlusion / max(weightSum, 0.0001)) * effectiveStrength;
    return clamp(ao, 0.78, 1.0);
}

void main()
{
    float centerDepth = sampleDepth(fragUv);
    float ao = contactOcclusion(fragUv, centerDepth);
    outColor = vec4(vec3(ao), 1.0);
}
