#version 450

layout(binding = 0) uniform sampler2D spriteTexture;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 color;
    vec4 tone;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec2 fragScreenUv;
layout(location = 0) out vec4 outColor;

vec3 acesFitted(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3(0.0), vec3(1.0));
}

vec3 applyToneMapping(vec3 color)
{
    if (pushData.tone.w <= 0.5)
    {
        return color;
    }

    color *= max(pushData.tone.x, 0.0);
    color = acesFitted(max(color, vec3(0.0)));

    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luminance), color, max(pushData.tone.z, 0.0));
    color = (color - vec3(0.5)) * max(pushData.tone.y, 0.0) + vec3(0.5);
    return clamp(color, vec3(0.0), vec3(1.0));
}

vec2 underwaterDistortedUv(vec2 uv)
{
    float time = pushData.tone.x;
    float strength = max(pushData.tone.y, 0.0);
    vec2 centered = fragScreenUv - vec2(0.5);
    float edgeFade = smoothstep(0.0, 0.18, min(min(fragScreenUv.x, 1.0 - fragScreenUv.x), min(fragScreenUv.y, 1.0 - fragScreenUv.y)));
    vec2 wave = vec2(
        sin(centered.y * 38.0 + centered.x * 13.0 + time * 1.35),
        sin(centered.x * 31.0 - centered.y * 17.0 + time * 1.08)
    );
    wave += vec2(
        sin((centered.x + centered.y) * 21.0 + time * 0.63),
        cos((centered.x - centered.y) * 24.0 + time * 0.72)
    ) * 0.45;
    return clamp(uv + wave * strength * edgeFade, vec2(0.001), vec2(0.999));
}

float underwaterShaftMask()
{
    vec2 sunUv = pushData.tone.xy;
    float strength = max(pushData.tone.z, 0.0);
    float time = pushData.tone.w;
    vec2 fromSun = fragScreenUv - sunUv;
    float distanceFromSun = length(fromSun);
    vec2 direction = normalize(fromSun + vec2(0.0001));
    float angular = atan(direction.y, direction.x);
    float bands = sin(angular * 18.0 + time * 0.10) * 0.45 +
        sin(angular * 31.0 - time * 0.07) * 0.32 +
        sin((fragScreenUv.x * direction.y - fragScreenUv.y * direction.x) * 26.0 + time * 0.18) * 0.23;
    float shafts = smoothstep(0.18, 0.86, bands * 0.5 + 0.5);
    float centerFade = smoothstep(0.05, 0.32, distanceFromSun);
    float farFade = 1.0 - smoothstep(1.05, 1.75, distanceFromSun);
    float waterFalloff = smoothstep(-0.05, 0.35, fragScreenUv.y);
    return shafts * centerFade * farFade * waterFalloff * strength;
}

void main()
{
    if (pushData.color.a < -3.5)
    {
        float shaft = underwaterShaftMask();
        outColor = vec4(pushData.color.rgb * shaft, shaft);
        return;
    }

    vec2 sampleUv = fragUv;
    if (pushData.color.a < -2.5)
    {
        sampleUv = underwaterDistortedUv(sampleUv);
    }
    vec4 sampled = texture(spriteTexture, sampleUv);
    if (pushData.color.a < 0.0)
    {
        float effect = clamp(pushData.color.g, 0.0, 1.0);
        if (effect > 0.98)
        {
            effect = 1.0;
        }
        vec3 rgb = applyToneMapping(sampled.rgb);
        float luminance = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = mix(rgb, vec3(luminance), effect);
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
