#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(set = 1, binding = 0, std430) readonly buffer PlayerNodeTransformBuffer
{
    mat4 nodeTransforms[];
} playerNodeTransformBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAo;
layout(location = 3) in float inTextureLayer;
layout(location = 4) in float inMipDistanceScale;
layout(location = 5) in uint inNodeIndex;
layout(location = 6) in uint inPackedLight;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;
layout(location = 5) flat out vec3 fragNormal;
layout(location = 6) flat out float fragAlphaBlend;
layout(location = 7) flat out float fragSkyLight;
layout(location = 8) flat out float fragBlockLight;
layout(location = 9) flat out float fragWaterTint;

void main()
{
    vec3 worldPosition = (playerNodeTransformBuffer.nodeTransforms[inNodeIndex] * vec4(inPosition, 1.0)).xyz;
    vec3 relativePosition = worldPosition - pushData.cameraPosition.xyz;
    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = inUv;
    fragAo = inAo;
    fragWorldPosition = relativePosition;
    fragTextureLayer = inTextureLayer;
    fragMipDistanceScale = inMipDistanceScale;
    vec3 worldNormal = (playerNodeTransformBuffer.nodeTransforms[inNodeIndex] * vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    fragNormal = dot(worldNormal, worldNormal) > 0.0001 ? normalize(worldNormal) : vec3(0.0, 1.0, 0.0);
    fragAlphaBlend = 1.0;
    fragSkyLight = float((inPackedLight >> 4u) & 0xFu) / 15.0;
    fragBlockLight = float(inPackedLight & 0xFu) / 15.0;
    fragWaterTint = 0.0;
}
