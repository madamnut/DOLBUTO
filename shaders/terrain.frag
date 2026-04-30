#version 450

layout(binding = 0) uniform sampler2D terrainTexture;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(terrainTexture, fragUv);
}
