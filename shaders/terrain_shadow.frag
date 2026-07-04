#version 450

layout(set = 0, binding = 0) uniform sampler2DArray terrainTexture;

layout(location = 0) in vec2 fragUv;
layout(location = 1) flat in float fragTextureLayer;

void main()
{
    vec4 color = texture(terrainTexture, vec3(fragUv, fragTextureLayer));
    if (color.a < 0.5)
    {
        discard;
    }
}
