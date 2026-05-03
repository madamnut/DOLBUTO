#version 450

layout(binding = 0) uniform sampler2DArray terrainTexture;

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragAo;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) flat in float fragTextureLayer;
layout(location = 4) flat in float fragMipDistanceScale;
layout(location = 0) out vec4 outColor;

void main()
{
    float cameraDistance = distance(pushData.cameraPosition.xyz, fragWorldPosition);
    float mipLevel = fragMipDistanceScale > 0.0 ? clamp(floor(cameraDistance / (64.0 * fragMipDistanceScale)), 0.0, 5.0) : 0.0;
    vec4 color = textureLod(terrainTexture, vec3(fragUv, fragTextureLayer), mipLevel);
    if (color.a < 0.5)
    {
        discard;
    }
    color.rgb *= fragAo;
    outColor = color;
}
