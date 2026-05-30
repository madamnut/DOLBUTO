#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAo;
layout(location = 3) in float inTextureLayer;
layout(location = 4) in float inMipDistanceScale;
layout(location = 5) in uint inPackedLight;
layout(location = 6) in float inAlphaBlend;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;
layout(location = 6) flat out float fragAlphaBlend;
layout(location = 7) flat out float fragSkyLight;
layout(location = 8) flat out float fragBlockLight;

void main()
{
    vec3 relativePosition = inPosition - pushData.cameraPosition.xyz;
    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = inUv;
    fragAo = inAo;
    fragWorldPosition = relativePosition;
    fragTextureLayer = inTextureLayer;
    fragMipDistanceScale = inMipDistanceScale;
    fragAlphaBlend = inAlphaBlend;
    fragSkyLight = float((inPackedLight >> 4u) & 0xFu) / 15.0;
    fragBlockLight = float(inPackedLight & 0xFu) / 15.0;
}
