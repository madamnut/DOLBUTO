#version 450

layout(binding = 0) uniform sampler2D spriteTexture;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 color;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(spriteTexture, fragUv) * pushData.color;
}
