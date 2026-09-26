#version 450

layout(binding = 0) uniform sampler2D ssaoTexture;
layout(binding = 1) uniform sampler2D sceneDepth;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 params;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

float sampleDepth(vec2 uv)
{
    return texture(sceneDepth, clamp(uv, vec2(0.001), vec2(0.999))).r;
}

float sampleAo(vec2 uv)
{
    return texture(ssaoTexture, clamp(uv, vec2(0.001), vec2(0.999))).r;
}

void main()
{
    vec2 texel = max(pushData.params.xy, vec2(0.000001));
    float radius = max(pushData.params.z, 0.5);
    float sharpness = max(pushData.params.w, 0.0001);
    float centerDepth = sampleDepth(fragUv);
    float centerAo = sampleAo(fragUv);

    if (centerDepth >= 0.9996)
    {
        outColor = vec4(1.0);
        return;
    }

    const vec2 offsets[9] = vec2[](
        vec2( 0.0,  0.0),
        vec2( 1.0,  0.0),
        vec2(-1.0,  0.0),
        vec2( 0.0,  1.0),
        vec2( 0.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0, -1.0)
    );

    float total = centerAo * 1.65;
    float weightSum = 1.65;
    float depthRange = mix(0.0025, 0.014, centerDepth);
    for (int i = 1; i < 9; ++i)
    {
        vec2 sampleUv = fragUv + offsets[i] * texel * radius;
        float depthValue = sampleDepth(sampleUv);
        float depthWeight = exp(-abs(centerDepth - depthValue) * sharpness / max(depthRange, 0.00001));
        float spatialWeight = i < 5 ? 1.0 : 0.62;
        float weight = depthWeight * spatialWeight;
        total += sampleAo(sampleUv) * weight;
        weightSum += weight;
    }

    float ao = clamp(total / max(weightSum, 0.0001), 0.78, 1.0);
    outColor = vec4(vec3(ao), 1.0);
}
