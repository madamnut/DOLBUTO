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
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloom;

void main()
{
    vec4 color = texture(spriteTexture, fragUv) * pushData.color;
    float bloomScale = pushData.tone.w > 0.5 ? max(pushData.tone.x, 0.0) : 2.5;
    outColor = color;
    outBloom = vec4(color.rgb * bloomScale, color.a);
}
