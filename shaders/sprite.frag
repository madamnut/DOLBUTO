#version 450

layout(binding = 0) uniform sampler2D spriteTexture;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 color;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec2 fragScreenUv;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 sampled = texture(spriteTexture, fragUv);
    if (pushData.color.a < 0.0)
    {
        float effect = clamp(pushData.color.g, 0.0, 1.0);
        if (effect > 0.98)
        {
            effect = 1.0;
        }
        float luminance = dot(sampled.rgb, vec3(0.299, 0.587, 0.114));
        vec3 rgb = mix(sampled.rgb, vec3(luminance), effect);
        if (pushData.color.a > -1.5)
        {
            float edgeDistance = length(fragScreenUv - vec2(0.5)) / 0.70710678;
            float tunnelInner = mix(1.15, 0.30, effect);
            float tunnelOuter = tunnelInner + mix(0.35, 0.26, effect);
            float tunnel = smoothstep(tunnelInner, tunnelOuter, edgeDistance);
            rgb = mix(rgb, vec3(0.0), tunnel);
        }
        outColor = vec4(rgb * max(pushData.color.r, 0.0), sampled.a * max(pushData.color.b, 0.0));
        return;
    }

    outColor = sampled * pushData.color;
}
