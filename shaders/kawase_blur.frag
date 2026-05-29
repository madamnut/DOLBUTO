#version 450

layout(binding = 0) uniform sampler2D sourceTexture;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 params;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

vec4 sampleSource(vec2 uv)
{
    return texture(sourceTexture, clamp(uv, vec2(0.0), vec2(1.0)));
}

void main()
{
    vec2 texel = max(pushData.params.xy, vec2(0.0));
    float offset = max(pushData.params.z, 0.0);
    vec2 delta = texel * offset;

    vec4 color = sampleSource(fragUv + vec2( delta.x,  delta.y));
    color += sampleSource(fragUv + vec2(-delta.x,  delta.y));
    color += sampleSource(fragUv + vec2( delta.x, -delta.y));
    color += sampleSource(fragUv + vec2(-delta.x, -delta.y));
    outColor = color * 0.25;
}
