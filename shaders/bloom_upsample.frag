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

vec3 sampleSource(vec2 offset)
{
    return texture(sourceTexture, clamp(fragUv + offset, vec2(0.0), vec2(1.0))).rgb;
}

void main()
{
    vec2 texel = max(pushData.params.xy, vec2(0.0));
    float radius = max(pushData.params.z, 0.0);
    vec2 delta = texel * radius;

    vec3 color = sampleSource(vec2(0.0)) * 0.20;
    color += sampleSource(vec2( delta.x,  0.0)) * 0.10;
    color += sampleSource(vec2(-delta.x,  0.0)) * 0.10;
    color += sampleSource(vec2( 0.0,  delta.y)) * 0.10;
    color += sampleSource(vec2( 0.0, -delta.y)) * 0.10;
    color += sampleSource(vec2( delta.x,  delta.y)) * 0.10;
    color += sampleSource(vec2(-delta.x,  delta.y)) * 0.10;
    color += sampleSource(vec2( delta.x, -delta.y)) * 0.10;
    color += sampleSource(vec2(-delta.x, -delta.y)) * 0.10;

    outColor = vec4(color, 1.0);
}
